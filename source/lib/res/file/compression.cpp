/**
 * =========================================================================
 * File        : compression.cpp
 * Project     : 0 A.D.
 * Description : interface for compressing/decompressing data streams.
 *             : currently implements "deflate" (RFC1951).
 * =========================================================================
 */

// license: GPL; see lib/license.txt

#include "precompiled.h"
#include "compression.h"

#include <deque>

#include "lib/res/mem.h"
#include "lib/allocators.h"
#include "lib/timer.h"
#include "file_io.h"	// IO_EOF


AT_STARTUP(\
	error_setDescription(ERR::COMPRESSION_UNKNOWN_METHOD, "Unknown/unsupported compression method");\
)


// provision for removing all ZLib code (all inflate calls will fail).
// used for checking DLL dependency; might also simulate corrupt Zip files.
//#define NO_ZLIB

#ifndef NO_ZLIB
# include "lib/external_libraries/zlib.h"
#else
// several switch statements are going to have all cases removed.
// squelch the corresponding warning.
# pragma warning(disable: 4065)
#endif

TIMER_ADD_CLIENT(tc_zip_inflate);
TIMER_ADD_CLIENT(tc_zip_memcpy);


/*
what is value added of zlib layer?
it gets the zlib interface cororrect - e.g. checking for return vals.
code can then just use our more simple interfacde

inf_*: in-memory inflate routines (zlib wrapper)
decompresses blocks from file_io callback.
*/

static LibError LibError_from_zlib(int zlib_err, bool warn_if_failed = true)
{
	LibError err = ERR::FAIL;
	// (NB: don't just remove the cases - that raises a warning)
#ifndef NO_ZLIB
	switch(zlib_err)
	{
	case Z_OK:
		return INFO::OK;
	case Z_STREAM_END:
		err = ERR::IO_EOF; break;
	case Z_MEM_ERROR:
		err = ERR::NO_MEM; break;
	case Z_DATA_ERROR:
		err = ERR::CORRUPTED; break;
	case Z_STREAM_ERROR:
		err = ERR::INVALID_PARAM; break;
	default:
		err = ERR::FAIL; break;
	}
#endif

	if(warn_if_failed)
		DEBUG_WARN_ERR(err);
	return err;
}



// must be dynamically allocated - need one for every open AFile,
// and z_stream is large.
class Compressor
{
public:
	Compressor(ContextType type_)
	{
		type = type_;
	}

	virtual ~Compressor()
	{
		mem_free(out_mem);

		// free all remaining input buffers that we copied (rare)
		for(size_t i = 0; i < pending_bufs.size(); i++)
			free(pending_bufs[i].mem_to_free);
	}


	virtual LibError init() = 0;

	virtual LibError reset()
	{
		next_out = 0;
		avail_out = 0;
		return INFO::OK;
	}

	virtual LibError alloc_output(size_t in_size) = 0;

	// consume as much of the given input buffer as possible. the data is
	// decompressed/compressed into the previously established output buffer.
	// reports how many bytes were consumed and produced; either or both
	// can be 0 if input size is small or not enough room in output buffer.
	// caller is responsible for saving any leftover input data,
	// which is why we pass back in_consumed.
	virtual LibError consume(const void* in, size_t in_size, size_t& in_consumed, size_t& out_produced) = 0;

	virtual LibError finish(void** out, size_t* out_size) = 0;

	virtual void release() = 0;


	void set_output(void* out, size_t out_size)
	{
		// must only be set once, because finish() returns the
		// output buffer set as: next_out - total_out.
		debug_assert(next_out == 0 && avail_out == 0);
		next_out = out;
		avail_out = out_size;
	}
	void* get_output()
	{
		debug_assert(next_out != 0);
		return next_out;
	}
	ssize_t feed(const void* in, size_t in_size)
	{
		size_t out_total = 0;	// returned unless error occurs
		LibError err;

		// note: calling with in_size = 0 is allowed and just means
		// we don't queue a new buffer. this can happen when compressing
		// newly decompressed data if nothing was output (due to
		// small compressed input buffer).
		if(in_size != 0)
			pending_bufs.push_back(Buf(in, in_size, 0));

		// work off all queued input buffers until output buffer is filled.
		while(!pending_bufs.empty())
		{
			Buf& buf = pending_bufs.front();

			size_t in_consumed, out_consumed;
			err = consume(buf.cdata, buf.csize, in_consumed, out_consumed);
			if(err < 0)
				return err;

			out_total += out_consumed;
			debug_assert(in_consumed <= buf.csize);
			// all input consumed - dequeue input buffer
			if(in_consumed == buf.csize)
			{
				free(buf.mem_to_free);	// no-op unless we allocated it
				pending_bufs.pop_front();
			}
			// limited by output space - mark input buffer as partially used
			else
			{
				buf.cdata += in_consumed;
				buf.csize -= in_consumed;

				// buffer was allocated by caller and may be freed behind our
				// backs after returning (which we must because output buffer
				// is full). allocate a copy of the remaining input data.
				if(!buf.mem_to_free)
				{
					void* cdata_copy = malloc(buf.csize);
					if(!cdata_copy)
						 WARN_RETURN(ERR::NO_MEM);
					cpu_memcpy(cdata_copy, buf.cdata, buf.csize);
					buf.cdata = (const u8*)cdata_copy;
				}

				return (ssize_t)out_total;
			}
		}

		return (ssize_t)out_total;
	}


protected:
	ContextType type;
	CompressionMethod method;

	// output buffer - assigned once by set_output
	void* next_out;
	size_t avail_out;

	// output memory allocated by allow_output_impl
	void* out_mem;
	size_t out_mem_size;

	struct Buf
	{
		const u8* cdata;
		size_t csize;
		void* mem_to_free;
		Buf(const void* cdata_, size_t csize_, void* mem_to_free_)
		{
			cdata = (const u8*)cdata_;
			csize = csize_;
			mem_to_free = mem_to_free_;
		}
	};
	// note: a 'list' (deque is more efficient) is necessary.
	// lack of output space can result in leftover input data;
	// since we do not want feed() to always have to check for and
	// use up any previous remnants, we allow queuing them.
	std::deque<Buf> pending_bufs;

	LibError alloc_output_impl(size_t required_out_size)
	{
		size_t alloc_size = required_out_size;

		// .. already had a buffer
		if(out_mem)
		{
			// it was big enough - reuse
			if(out_mem_size >= required_out_size)
				goto have_out_mem;

			// free previous
			// note: mem.cpp doesn't support realloc; don't use Pool etc. because
			// maximum file size may be huge (more address space than we can afford)
			mem_free(out_mem);

			// TODO: make sure difference in required_out_size vs. out_mem_size
			// is big enough - i.e. don't only increment in small chunks.
			// set alloc_size...

			// fall through..
		}

		// .. need to allocate anew
		out_mem = mem_alloc(alloc_size, 32*KiB);
		if(!out_mem)
			WARN_RETURN(ERR::NO_MEM);
		out_mem_size = alloc_size;

have_out_mem:
		// must only be set once, because finish() returns the
		// output buffer set as: next_out - total_out.
		debug_assert(next_out == 0 && avail_out == 0);

		next_out  = out_mem;
		avail_out = out_mem_size;

		return INFO::OK;
	}

};	// class Compressor


#ifndef NO_ZLIB

class ZLibCompressor : public Compressor
{
	z_stream zs;

public:
	// default ctor cannot be generated
	ZLibCompressor(ContextType type)
		: Compressor(type), zs() {}

	virtual LibError init()
	{
		memset(&zs, 0, sizeof(zs));

		int ret;
		if(type == CT_COMPRESSION)
		{
			// note: with Z_BEST_COMPRESSION, 78% percent of
			// archive builder CPU time is spent in ZLib, even though
			// that is interleaved with IO; everything else is negligible.
			// we therefore enable this only in final builds; during
			// development, 1.5% bigger archives are definitely worth much
			// faster build time.
#if CONFIG_FINAL
			const int level      = Z_BEST_COMPRESSION;
#else
			const int level      = Z_BEST_SPEED;
#endif
			const int windowBits = -MAX_WBITS;	// max window size; omit ZLib header
			const int memLevel   = 9;					// max speed; total mem ~= 384KiB
			const int strategy   = Z_DEFAULT_STRATEGY;	// normal data - not RLE
			ret = deflateInit2(&zs, level, Z_DEFLATED, windowBits, memLevel, strategy);
		}
		else
		{
			const int windowBits = -MAX_WBITS;	// max window size; omit ZLib header
			ret = inflateInit2(&zs, windowBits);
		}
		return LibError_from_zlib(ret);
	}

	virtual LibError reset()
	{
		Compressor::reset();
		int ret;
		if(type == CT_COMPRESSION)
			ret = deflateReset(&zs);
		else
			ret = inflateReset(&zs);
		return LibError_from_zlib(ret);
	}


	// out:
	// compression ratios can be enormous (1000x), so we require
	// callers to allocate the output buffer themselves
	// (since they know the actual size).
	// allocate buffer
	// caller can't do it because they don't know what compression ratio
	// will be achieved.
	virtual LibError alloc_output(size_t in_size)
	{
		if(type == CT_COMPRESSION)
		{
			size_t required_size = (size_t)deflateBound(&zs, (uLong)in_size);
			RETURN_ERR(alloc_output_impl(required_size));
			return INFO::OK;
		}
		else
			WARN_RETURN(ERR::LOGIC);
	}


	virtual LibError consume(const void* in, size_t in_size, size_t& in_consumed, size_t& out_consumed)
	{
		zs.avail_in = (uInt)in_size;
		zs.next_in  = (Byte*)in;
		zs.next_out  = (Byte*)next_out;
		zs.avail_out = (uInt)avail_out;
		const size_t prev_avail_in  = zs.avail_in;
		const size_t prev_avail_out = zs.avail_out;

		int ret;
		if(type == CT_COMPRESSION)
			ret = deflate(&zs, 0);
		else
			ret = inflate(&zs, Z_SYNC_FLUSH);
		// sanity check: if ZLib reports end of stream, all input data
		// must have been consumed.
		if(ret == Z_STREAM_END)
		{
			debug_assert(zs.avail_in == 0);
			ret = Z_OK;
		}

		debug_assert(prev_avail_in >= zs.avail_in && prev_avail_out >= avail_out);
		in_consumed  = prev_avail_in - zs.avail_in;
		out_consumed = prev_avail_out- zs.avail_out;
		next_out  = zs.next_out;
		avail_out = zs.avail_out;

		return LibError_from_zlib(ret);
	}


	virtual LibError finish(void** out, size_t* out_size)
	{
		if(type == CT_COMPRESSION)
		{
			// notify zlib that no more data is forthcoming and have it flush output.
			// our output buffer has enough space due to use of deflateBound;
			// therefore, deflate must return Z_STREAM_END.
			int ret = deflate(&zs, Z_FINISH);
			if(ret != Z_STREAM_END)
				debug_warn("deflate: unexpected Z_FINISH behavior");
		}
		else
		{
			// nothing to do - decompression always flushes immediately
		}

		*out = zs.next_out - zs.total_out;
		*out_size = zs.total_out;
		return INFO::OK;
	}


	virtual void release()
	{
		// can have both input or output data remaining
		// (if not all data in uncompressed stream was needed)

		int ret;
		if(type == CT_COMPRESSION)
			ret = deflateEnd(&zs);
		else
			ret = inflateEnd(&zs);
		(void)LibError_from_zlib(ret, true);	// just warn
	}
};

#else

typedef struct
{
	int dummy;
}
ZLibCompressor;

#endif	// #ifndef NO_ZLIB


//-----------------------------------------------------------------------------

// allocator
static const size_t MAX_COMPRESSOR_SIZE = sizeof(ZLibCompressor);
typedef u8 CompressorMem[MAX_COMPRESSOR_SIZE];
static SingleAllocator<u8[MAX_COMPRESSOR_SIZE]> compressor_allocator;

uintptr_t comp_alloc(ContextType type, CompressionMethod method)
{
	CompressorMem* c_mem = compressor_allocator.alloc();
	if(!c_mem)
		return 0;
	Compressor* c;

	switch(method)
	{
#ifndef NO_ZLIB
	case CM_DEFLATE:
		cassert(sizeof(ZLibCompressor) <= MAX_COMPRESSOR_SIZE);
#include "lib/nommgr.h"	// protect placement new
		c = new(c_mem) ZLibCompressor(type);
#include "lib/mmgr.h"
		break;
#endif
	default:
		compressor_allocator.release(c_mem);
		WARN_ERR(ERR::COMPRESSION_UNKNOWN_METHOD);
		return 0;

	}

	c->init();
	return (uintptr_t)c;
}

LibError comp_reset(uintptr_t c_)
{
	Compressor* c = (Compressor*)c_;
	return c->reset();
}

// subsequent calls to comp_feed will unzip into <out>.
void comp_set_output(uintptr_t c_, void* out, size_t out_size)
{
	Compressor* c = (Compressor*)c_;
	c->set_output(out, out_size);
}

LibError comp_alloc_output(uintptr_t c_, size_t in_size)
{
	Compressor* c = (Compressor*)c_;
	return c->alloc_output(in_size);
}

void* comp_get_output(uintptr_t c_)
{
	Compressor* c = (Compressor*)c_;
	return c->get_output();
}

// unzip into output buffer. returns bytes written
// (may be 0, if not enough data is passed in), or < 0 on error.
ssize_t comp_feed(uintptr_t c_, const void* in, size_t in_size)
{
	Compressor* c = (Compressor*)c_;
	return c->feed(in, in_size);
}

LibError comp_finish(uintptr_t c_, void** out, size_t* out_size)
{
	Compressor* c = (Compressor*)c_;
	return c->finish(out, out_size);
}

void comp_free(uintptr_t c_)
{
	// no-op if context is 0 (i.e. was never allocated)
	if(!c_)
		return;

	Compressor* c = (Compressor*)c_;
	c->release();

	c->~Compressor();
	compressor_allocator.release((CompressorMem*)c);
}
