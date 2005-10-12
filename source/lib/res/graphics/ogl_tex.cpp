// OpenGL texture API
//
// Copyright (c) 2003-2005 Jan Wassenberg
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// Contact info:
//   Jan.Wassenberg@stud.uni-karlsruhe.de
//   http://www.stud.uni-karlsruhe.de/~urkt/

#include "precompiled.h"

#include "sysdep/gfx.h"

#include "lib.h"
#include "../res.h"
#include "ogl.h"
#include "tex.h"
#include "ogl_tex.h"


//----------------------------------------------------------------------------
// OpenGL helper routines
//----------------------------------------------------------------------------

static bool filter_valid(GLint filter)
{
	switch(filter)
	{
	case GL_NEAREST:
	case GL_LINEAR:
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_LINEAR_MIPMAP_NEAREST:
	case GL_NEAREST_MIPMAP_LINEAR:
	case GL_LINEAR_MIPMAP_LINEAR:
		return true;
	default:
		return false;
	}
	UNREACHABLE;
}


static bool wrap_valid(GLint wrap)
{
	switch(wrap)
	{
	case GL_CLAMP:
	case GL_CLAMP_TO_EDGE:
	case GL_CLAMP_TO_BORDER:
	case GL_REPEAT:
	case GL_MIRRORED_REPEAT:
		return true;
	default:
		return false;
	}
	UNREACHABLE;
}


static bool filter_uses_mipmaps(GLint filter)
{
	switch(filter)
	{
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_LINEAR_MIPMAP_NEAREST:
	case GL_NEAREST_MIPMAP_LINEAR:
	case GL_LINEAR_MIPMAP_LINEAR:
		return true;
	default:
		return false;
	}
	UNREACHABLE;
}


static bool fmt_is_s3tc(GLenum fmt)
{
	switch(fmt)
	{
	case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		return true;
	default:
		return false;
	}
	UNREACHABLE;
}


// determine OpenGL texture format, given <bpp> and Tex <flags>.
static GLint choose_fmt(uint bpp, uint flags)
{
	const bool alpha = (flags & TEX_ALPHA) != 0;
	const bool bgr   = (flags & TEX_BGR  ) != 0;
	const bool grey  = (flags & TEX_GREY ) != 0;
	const uint dxt   = flags & TEX_DXT;

	// S3TC
	if(dxt != 0)
	{
		switch(dxt)
		{
		case 1:
			return alpha? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
		case 3:
			return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
		case 5:
			return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
		default:
			debug_warn("choose_fmt: invalid DXT value");
			return 0;
		}
	}

	// uncompressed
	switch(bpp)
	{
	case 8:
		debug_assert(grey);
		return GL_LUMINANCE;
	case 16:
		return GL_LUMINANCE_ALPHA;
	case 24:
		debug_assert(!alpha);
		return bgr? GL_BGR : GL_RGB;
	case 32:
		debug_assert(alpha);
		return bgr? GL_BGRA : GL_RGBA;
	default:
		debug_warn("choose_fmt: invalid bpp");
		return 0;
	}

	UNREACHABLE;
}


//----------------------------------------------------------------------------
// quality mechanism
//----------------------------------------------------------------------------

static GLint default_filter = GL_LINEAR;	// one of the GL *minify* filters
static uint default_q_flags = OGL_TEX_FULL_QUALITY;	// OglTexQualityFlags

static bool q_flags_valid(uint q_flags)
{
	const uint bits = OGL_TEX_FULL_QUALITY|OGL_TEX_HALF_BPP|OGL_TEX_HALF_RES;
	// unrecognized bits are set - invalid
	if((q_flags & ~bits) != 0)
		return false;
	// "full quality" but other reduction bits are set - invalid
	if(q_flags & OGL_TEX_FULL_QUALITY && q_flags & ~OGL_TEX_FULL_QUALITY)
		return false;
	return true;
}


// change default settings - these affect performance vs. quality.
// may be overridden for individual textures via parameter to
// ogl_tex_upload or ogl_tex_set_filter, respectively.
// 
// pass 0 to keep the current setting; defaults and legal values are:
// - q_flags: OGL_TEX_FULL_QUALITY; combination of OglTexQualityFlags 
// - filter: GL_LINEAR; any valid OpenGL minification filter
void ogl_tex_set_defaults(uint q_flags, GLint filter)
{
	if(q_flags)
	{
		debug_assert(q_flags_valid(q_flags));
		default_q_flags = q_flags;
	}

	if(filter)
	{
		debug_assert(filter_valid(filter));
		default_filter = filter;
	}
}


// choose an internal format for <fmt> based on the given q_flags.
static GLint choose_int_fmt(GLenum fmt, uint q_flags)
{
	// true => 4 bits per component; otherwise, 8
	const bool half_bpp = (q_flags & OGL_TEX_HALF_BPP) != 0;

	// early-out for S3TC textures: they don't need an internal format
	// (because upload is via glCompressedTexImage2DARB), but we must avoid
	// triggering the default case below. we might as well return a
	// meaningful value (i.e. int_fmt = fmt).
	if(fmt_is_s3tc(fmt))
		return fmt;

	switch(fmt)
	{
	// 8bpp
	case GL_LUMINANCE:
		return half_bpp? GL_LUMINANCE4 : GL_LUMINANCE8;
	case GL_INTENSITY:
		return half_bpp? GL_INTENSITY4 : GL_INTENSITY8;
	case GL_ALPHA:
		return half_bpp? GL_ALPHA4 : GL_ALPHA8;

	// 16bpp
	case GL_LUMINANCE_ALPHA:
		return half_bpp? GL_LUMINANCE4_ALPHA4 : GL_LUMINANCE8_ALPHA8;

	// 24bpp
	case GL_RGB:
	case GL_BGR:	// note: BGR can't be used as internal format
		return half_bpp? GL_RGB4 : GL_RGB8;

	// 32bpp
	case GL_RGBA:
	case GL_BGRA:	// note: BGRA can't be used as internal format
		return half_bpp? GL_RGBA4 : GL_RGBA8;

	default:
		debug_warn("choose_int_fmt doesn't cover the given fmt! please add it.");
		// fall back to a reasonable default
		return half_bpp? GL_RGB4 : GL_RGB8;
	}

	UNREACHABLE;
}


//----------------------------------------------------------------------------
// texture state to allow seamless reload
//----------------------------------------------------------------------------

// see "Texture Parameters" in docs.

// all GL state tied to the texture that must be reapplied after reload.
// (this mustn't get too big, as it's stored in the already sizeable OglTex)
struct OglTexState
{
	// glTexParameter
	// note: there are more options, but they do not look to
	//       be important and will not be applied after a reload!
	//       in particular, LOD_BIAS isn't needed because that is set for
	//       the entire texturing unit via glTexEnv.
	// .. texture filter
	//    note: this is the minification filter value; magnification filter
	//          is GL_NEAREST if it's GL_NEAREST, otherwise GL_LINEAR.
	//          we don't store mag_filter explicitly because it
	//          doesn't appear useful - either apps can tolerate LINEAR, or
	//          mipmaps aren't called for and filter could be NEAREST anyway).
	GLint filter;
	// .. wrap mode
	//    note: to simplify things, we assume that apps will never want to
	//          set S/T modes independently. it that becomes necessary,
	//          it's easy to add.
	GLint wrap;
};


// fill the given state object with default values.
static void state_set_to_defaults(OglTexState* ots)
{
	ots->filter = default_filter;
	ots->wrap = GL_REPEAT;
}


// send all state to OpenGL (actually the currently bound texture).
// called from ogl_tex_upload.
static void state_latch(OglTexState* ots)
{
	// filter
	const GLint filter = ots->filter;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	const GLint mag_filter = (filter == GL_NEAREST)? GL_NEAREST : GL_LINEAR;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);

	// wrap
	const GLint wrap = ots->wrap;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	// .. only CLAMP and REPEAT are guaranteed to be available.
	//    if we're using one of the others, we squelch the error that
	//    may have resulted if this GL implementation is old.
	if(wrap != GL_CLAMP && wrap != GL_REPEAT)
		oglSquelchError(GL_INVALID_ENUM);
}


//----------------------------------------------------------------------------
// texture resource object
//----------------------------------------------------------------------------

// ideally we would split OglTex into data and state objects as in
// snd.cpp's SndData / VSrc. this gives us the benefits of caching while 
// still leaving each "instance" (state object, which owns a data reference)
// free to change its state. however, unlike in OpenAL, there is no state
// independent of the data object - all parameters are directly tied to the
// GL texture object. therefore, splitting them up is impossible.
// (we shouldn't even keep the texel data in memory since that's already
// covered by the FS cache).
//
// given that multiple "instances" share the state stored here, we conclude:
// - a refcount is necessary to prevent ogl_tex_upload from freeing
//   <t> as long as other instances are active.
// - concurrent use risks cross-talk (if the 2nd "instance" changes state and
//   the first is reloaded, its state may change to that of the 2nd)
//
// as bad as it sounds, the latter issue isn't a problem: we do not expect
// multiple instances of the same texture where someone changes its filter.
// even if it is reloaded, the differing state is not critical.
// the alternative is even worse: disabling *all* caching/reuse would
// really hurt performance and h_mgr doesn't support only disallowing
// reuse of active objects (this would break the index lookup code, since
// multiple instances may then exist).

struct OglTex
{
	Tex t;

	// allocated by OglTex_reload; indicates the texture is currently uploaded.
	GLuint id;

	// ogl_tex_upload calls choose_fmt to determine these from <t>.
	// however, its caller may override those values via parameters.
	// note: these are stored here to allow retrieving via ogl_tex_get_format;
	// they are only used within ogl_tex_upload.
	GLenum fmt;
	GLint int_fmt;

	OglTexState state;

	// OglTexQualityFlags
	uint q_flags : 8;

	// to which Texture Mapping Unit was this bound?
	uint tmu : 8;

	// flags influencing reload() behavior
	// .. reload() should be a no-op, because we either have the texture in
	//    memory or it's been uploaded to OpenGL. reset in dtor.
	uint is_loaded : 1;
	// .. reload() should automatically re-upload the texture, because
	//    it had been uploaded before the reload.
	uint need_auto_upload : 1;
	// .. reload() doesn't need to load from disk, because <t> already
	//    contains the texture data. note: in this case, actual reloads are
	//    disallowed by ogl_tex_wrap, but we still need this flag to
	//    correctly handle the reload() triggered by h_alloc.
	uint was_wrapped : 1;

	// indicates if this texture is currently uploaded.
	// used by state setters; reset in dtor.
	uint is_currently_uploaded : 1;

	uint tex_valid : 1;
};

H_TYPE_DEFINE(OglTex);

static void OglTex_init(OglTex* ot, va_list args)
{
	Tex* wrapped_tex = va_arg(args, Tex*);
	if(wrapped_tex)
	{
		// note: this only happens once; ogl_tex_wrap makes sure
		// this OglTex cannot be reloaded, so it's safe.
		ot->t = *wrapped_tex;
		ot->was_wrapped = 1;
	}

	state_set_to_defaults(&ot->state);
	ot->q_flags = default_q_flags;
}

static void OglTex_dtor(OglTex* ot)
{
	if(ot->tex_valid)
		(void)tex_free(&ot->t);

	glDeleteTextures(1, &ot->id);
	ot->id = 0;

	ot->is_currently_uploaded = 0;

	// need to clear this so actual reloads (triggered by h_reload)
	// actually reload.
	ot->is_loaded = 0;
}

static int OglTex_reload(OglTex* ot, const char* fn, Handle h)
{
	// make sure the texture has been loaded
	// .. already done (<h> had been freed but not yet unloaded)
	if(ot->is_loaded)
		return 0;
	// .. load from file - but only if we weren't wrapping an existing
	//    Tex object (i.e. copy its values and be done).
	if(!ot->was_wrapped)
		RETURN_ERR(tex_load(fn, &ot->t));
	ot->tex_valid = 1;
	ot->is_loaded = 1;

	glGenTextures(1, &ot->id);

	// if it had already been uploaded before this reload,
	// re-upload it (this also does state_latch).
	if(ot->need_auto_upload)
		(void)ogl_tex_upload(h);

	return 0;
}

static int OglTex_validate(const OglTex* ot)
{
	RETURN_ERR(tex_validate(&ot->t));

	// width, height
	// (note: this is done here because tex.cpp doesn't impose any
	// restrictions on dimensions, while OpenGL does).
	GLsizei w = (GLsizei)ot->t.w;
	GLsizei h = (GLsizei)ot->t.h;
	// .. == 0; texture file probably not loaded successfully.
	if(w == 0 || h == 0)
		return -100;
	// .. greater than max supported tex dimension.
	//    no-op if oglInit not yet called
	if(w > (GLsizei)ogl_max_tex_size || h > (GLsizei)ogl_max_tex_size)
		return -101;
	// .. not power-of-2.
	//    note: we can't work around this because both NV_texture_rectangle
	//    and subtexture require work for the client (changing tex coords).
	//    TODO: ARB_texture_non_power_of_two
	if(!is_pow2(w) || !is_pow2(h))
		return -102;

	// texture state
	if(!filter_valid(ot->state.filter))
		return -103;
	if(!wrap_valid(ot->state.wrap))
		return -104;

	// misc
	if(!q_flags_valid(ot->q_flags))
		return -105;
	if(ot->tmu >= 128)	// unexpected that there will ever be this many
		return -106;
	// .. note: don't check ot->fmt and ot->int_fmt - they aren't set
	//    until during ogl_tex_upload.

	return 0;
}


// load and return a handle to the texture given in <fn>.
// for a list of supported formats, see tex.h's tex_load.
Handle ogl_tex_load(const char* fn, uint flags)
{
	Tex* wrapped_tex = 0;	// we're loading from file
	return h_alloc(H_OglTex, fn, flags, wrapped_tex);
}


// make the given Tex object ready for use as an OpenGL texture
// and return a handle to it. this will be as if its contents
// had been loaded by ogl_tex_load.
//
// we need only add bookkeeping information and "wrap" it in
// a resource object (accessed via Handle), hence the name.
//
// <fn> isn't strictly needed but should describe the texture so that
// h_filename will return a meaningful comment for debug purposes.
// note: because we cannot guarantee that callers will pass distinct
// "filenames", caching is disabled for the created object. this avoids
// mistakenly reusing previous objects that share the same comment.
Handle ogl_tex_wrap(Tex* t, const char* fn, uint flags)
{
	// this object may not be backed by a file ("may", because
	// someone could do tex_load and then ogl_tex_wrap).
	// if h_mgr asks for a reload, the dtor will be called but
	// we won't be able to reconstruct it. therefore, disallow reloads.
	// (they are improbable anyway since caller is supposed to pass a
	// 'descriptive comment' instead of filename, but don't rely on that)
	// also disable caching as explained above.
	flags |= RES_DISALLOW_RELOAD|RES_NO_CACHE;
	return h_alloc(H_OglTex, fn, flags, t);
}


// free all resources associated with the texture and make further
// use of it impossible. (subject to refcount)
int ogl_tex_free(Handle& ht)
{
	return h_free(ht, H_OglTex);
}


//----------------------------------------------------------------------------
// state setters (see "Texture Parameters" in docs)

// we require the below functions be called before uploading; this avoids
// potentially redundant glTexParameter calls (we'd otherwise need to always
// set defaults because we don't know if an override is forthcoming).

// raise a debug warning if the texture has already been uploaded
// (except in the few cases where this is allowed; see below).
// this is so that you will notice incorrect usage - only one instance of a
// texture should be active at a time, because otherwise they vie for
// control of one shared OglTexState.
static void warn_if_uploaded(Handle ht, const OglTex* ot)
{
#ifndef NDEBUG
	// we do not require users of this module to remember if they've
	// already uploaded a texture (inconvenient). since they also can't
	// tell if the texture was newly loaded (due to h_alloc interface),
	// we have to squelch this warning in 2 cases:
	// - it's ogl_tex_loaded several times (i.e. refcount > 1) and the
	//   caller (typically a higher-level LoadTexture) is setting filter etc.
	// - caller is using our Handle as a caching mechanism, and calls
	//   ogl_tex_set_* before every use of the texture. note: this
	//   need not fall under the above check, e.g. if freed but cached.
	//   workaround is that ogl_tex_set_* won't call us if the
	//   same state values are being set (harmless anyway).
	int refs = h_get_refcnt(ht);
	if(refs > 1)
		return;	// don't complain

	if(ot->is_currently_uploaded)
		debug_warn("ogl_tex_set_*: texture already uploaded and shouldn't be changed");
#else
	// (prevent warnings; the alternative of wrapping all call sites in
	// #ifndef is worse)
	UNUSED2(ht);
	UNUSED2(ot);
#endif
}


// override default filter (as set above) for this texture.
// must be called before uploading (raises a warning if called afterwards).
// filter is as defined by OpenGL; it is applied for both minification and
// magnification (for rationale and details, see OglTexState)
int ogl_tex_set_filter(Handle ht, GLint filter)
{
	H_DEREF(ht, OglTex, ot);

	if(!filter_valid(filter))
		CHECK_ERR(ERR_INVALID_PARAM);

	if(ot->state.filter != filter)
	{
		warn_if_uploaded(ht, ot);
		ot->state.filter = filter;
	}
	return 0;
}


// override default wrap mode (GL_REPEAT) for this texture.
// must be called before uploading (raises a warning if called afterwards).
// wrap is as defined by OpenGL and applies to both S and T coordinates
// (rationale: see OglTexState).
int ogl_tex_set_wrap(Handle ht, GLint wrap)
{
	H_DEREF(ht, OglTex, ot);

	if(!wrap_valid(wrap))
		CHECK_ERR(ERR_INVALID_PARAM);

	if(ot->state.wrap != wrap)
	{
		warn_if_uploaded(ht, ot);
		ot->state.wrap = wrap;
	}
	return 0;
}


//----------------------------------------------------------------------------
// upload

static int get_mipmaps(Tex* t, GLint filter, uint q_flags, int* plevels_to_skip)
{
	// decisions:
	// .. does this OpenGL implementation support auto mipmap generation?
	static const bool have_auto_mipmap_gen = oglHaveVersion("1.4") || oglHaveExtension("GL_SGIS_generate_mipmap");
	// .. does filter call for uploading mipmaps?
	const bool need_mipmaps = filter_uses_mipmaps(filter);
	// .. does the image data include mipmaps? (stored as separate
	//    images after the regular texels)
	const bool includes_mipmaps = (t->flags & TEX_MIPMAPS) != 0;
	// .. is this texture in S3TC format? (more generally, "compressed")
	const bool is_s3tc = (t->flags & TEX_DXT) != 0;

	*plevels_to_skip = TEX_BASE_LEVEL_ONLY;
	if(!need_mipmaps)
		return 0;

	// image already contains pregenerated mipmaps; we need do nothing.
	// this is the nicest case, because they are fastest to load
	// (no extra processing needed) and typically filtered better than
	// if automatically generated.
	if(includes_mipmaps)
		*plevels_to_skip = 0;	// t contains mipmaps
	// OpenGL supports automatic generation; we need only
	// activate that and upload the base image.
	else if(have_auto_mipmap_gen)
	{
		// note: we assume GL_GENERATE_MIPMAP and GL_GENERATE_MIPMAP_SGIS
		// have the same values - it's heavily implied by the spec
		// governing 'promoted' ARB extensions and just plain makes sense.
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
	}
	// image is S3TC-compressed and the previous 2 alternatives weren't
	// available; we're going to cheat and just disable mipmapping.
	// rationale: having tex_transform add mipmaps would be slow (since
	// all<->all transforms aren't implemented, it'd have to decompress
	// from S3TC first), and DDS images ought to include mipmaps!
	else if(is_s3tc)
		return -1;
	// image is uncompressed and we're on an old OpenGL implementation;
	// we will generate mipmaps in software.
	else
	{
		RETURN_ERR(tex_transform_to(t, t->flags|TEX_MIPMAPS));
		*plevels_to_skip = 0;	// t contains mipmaps
	}

	// t contains mipmaps; we can apply our resolution reduction trick:
	if(*plevels_to_skip == 0)
	{
		// this saves texture memory by skipping some of the lower
		// (high-resolution) mip levels.
		//
		// note: we don't just use GL_TEXTURE_BASE_LEVEL because it would
		// require uploading unused levels, which is wasteful.
		// .. can be expanded to reduce to 1/4, 1/8 by encoding factor in q_flags.
		const uint reduce = (q_flags & OGL_TEX_HALF_RES)? 2 : 1;
		*plevels_to_skip = log2(reduce);
	}

	return 0;
}


struct UploadParams
{
	GLenum fmt;
	GLint int_fmt;
};

static void upload_level(uint level, uint level_w, uint level_h,
	const u8* restrict level_data, size_t UNUSED(level_data_size), void* restrict ctx)
{
	const UploadParams* up = (const UploadParams*)ctx;
	glTexImage2D(GL_TEXTURE_2D, level, up->int_fmt, level_w, level_h, 0,
		up->fmt, GL_UNSIGNED_BYTE, level_data);
}

static void upload_compressed_level(uint level, uint level_w, uint level_h,
	const u8* restrict level_data, size_t level_data_size, void* restrict ctx)
{
	const UploadParams* up = (const UploadParams*)ctx;
	glCompressedTexImage2DARB(GL_TEXTURE_2D, level, up->fmt,
		(GLsizei)level_w, (GLsizei)level_h, 0, (GLsizei)level_data_size, level_data);
}

// upload the texture in the specified (internal) format.
// split out of ogl_tex_upload because it was too big.
//
// pre: <t> is valid for OpenGL use; texture is bound.
static void upload_impl(Tex* t, GLenum fmt, GLint int_fmt, int levels_to_skip)
{
	const GLsizei w  = (GLsizei)t->w;
	const GLsizei h  = (GLsizei)t->h;
	const uint bpp   = t->bpp;
	const u8* data = (const u8*)tex_get_data(t);
	const UploadParams up = { fmt, int_fmt };

	if(t->flags & TEX_DXT)
		tex_util_foreach_mipmap(w, h, bpp, data, levels_to_skip, 4, upload_compressed_level, (void*)&up);
	else
		tex_util_foreach_mipmap(w, h, bpp, data, levels_to_skip, 1, upload_level, (void*)&up);
}


static bool detect_s3tc()
{
	// 1. require extensions to be advertised.
	// note: we don't bother checking for GL_S3_s3tc - it is incompatible
	// and irrelevant (was never widespread).
	bool have_s3tc = oglHaveExtensions(0, "GL_ARB_texture_compression", "GL_EXT_texture_compression_s3tc", 0) == 0;

	// 2. exclude any card/driver combos on which it is known to break.
	if(gfx_card[0] == '\0')
		debug_warn("ogl_tex requires get_gfx_info be called before ogl_tex_upload");
	if(!strcmp(gfx_card, "S3 SuperSavage/IXC 1014"))
	{
		if(strstr(gfx_drv_ver, "ssicdnt.dll (2.60.115)"))
			have_s3tc = false;
	}

	return have_s3tc;
}


// upload the texture to OpenGL.
// if not 0, parameters override the following:
//   fmt_ovr     : OpenGL format (e.g. GL_RGB) decided from bpp / Tex flags;
//   q_flags_ovr : global default "quality vs. performance" flags;
//   int_fmt_ovr : internal format (e.g. GL_RGB8) decided from fmt / q_flags.
//
// side effects:
// - enables texturing on TMU 0 and binds the texture to it;
// - frees the texel data! see ogl_tex_get_data.
int ogl_tex_upload(const Handle ht, GLenum fmt_ovr, uint q_flags_ovr, GLint int_fmt_ovr)
{
	H_DEREF(ht, OglTex, ot);
	Tex* t = &ot->t;
	const char* fn = h_filename(ht);
	if(!fn)
		fn = "(could not determine filename)";
	debug_assert(q_flags_valid(q_flags_ovr));
	// we don't bother verifying *fmt_ovr - there are too many values

	// upload already happened; no work to do.
	// (this also happens if a cached texture is "loaded")
	if(ot->is_currently_uploaded)
		return 0;

	// decompress S3TC if that's not supported by OpenGL.
	static const bool have_s3tc = detect_s3tc();
	if((t->flags & TEX_DXT) && !have_s3tc)
	{
		ONCE(DISPLAY_ERROR(L"Performance warning: your graphics card does not support compressed textures. The game will try to continue anyway, but may be slower than expected. Please try updating your graphics drivers; if that doesn't help, please try upgrading your hardware."));
		(void)tex_transform_to(t, t->flags & ~TEX_DXT);
	}

	// determine fmt and int_fmt, allowing for user override.
	ot->fmt = choose_fmt(t->bpp, t->flags);
	if(fmt_ovr) ot->fmt = fmt_ovr;
	if(q_flags_ovr) ot->q_flags = q_flags_ovr;
	ot->int_fmt = choose_int_fmt(ot->fmt, ot->q_flags);
	if(int_fmt_ovr) ot->int_fmt = int_fmt_ovr;

	// now actually send to OpenGL:
	oglCheck();
	{
		// (note: we know ht is valid due to H_DEREF, but ogl_tex_bind can
		// fail in debug builds if OglTex.id isn't a valid texture name)
		RETURN_ERR(ogl_tex_bind(ht, ot->tmu));
		int levels_to_skip;
		if(get_mipmaps(t, ot->state.filter, ot->q_flags, &levels_to_skip) < 0)
			// error => disable mipmapping
			ot->state.filter = GL_LINEAR;
		// (note: if first time, applies our defaults/previous overrides;
		// otherwise, replays all state changes)
		state_latch(&ot->state);
		upload_impl(t, ot->fmt, ot->int_fmt, levels_to_skip);
	}
	oglCheck();

	ot->need_auto_upload = 1;
	ot->is_currently_uploaded = 1;

	// see rationale for <refs> at declaration of OglTex.
	// note: tex_free is safe even if this OglTex was wrapped -
	//       the Tex contains a mem handle.
	int refs = h_get_refcnt(ht);
	if(refs == 1)
	{
		tex_free(t);
		ot->tex_valid = 0;
	}

	return 0;
}


//----------------------------------------------------------------------------
// getters

// retrieve texture dimensions and bits per pixel.
// all params are optional and filled if non-NULL.
int ogl_tex_get_size(Handle ht, uint* w, uint* h, uint* bpp)
{
	H_DEREF(ht, OglTex, ot);

	if(w)
		*w = ot->t.w;
	if(h)
		*h = ot->t.h;
	if(bpp)
		*bpp = ot->t.bpp;
	return 0;
}


// retrieve Tex.flags and the corresponding OpenGL format.
// the latter is determined during ogl_tex_upload and is 0 before that.
// all params are optional and filled if non-NULL.
int ogl_tex_get_format(Handle ht, uint* flags, GLenum* fmt)
{
	H_DEREF(ht, OglTex, ot);

	if(flags)
		*flags = ot->t.flags;
	if(fmt)
	{
		if(!ot->is_currently_uploaded)
			debug_warn("ogl_tex_get_format: hasn't been defined yet!");
		*fmt = ot->fmt;
	}
	return 0;
}


// retrieve pointer to texel data.
//
// note: this memory is freed after a successful ogl_tex_upload for
// this texture. after that, the pointer we retrieve is NULL but 	ps_dbg.exe!ogl_tex_set_filter(__int64 ht=476741374144, int filter=9729)  Line 490 + 0x4a	C++

// the function doesn't fail (negative return value) by design.
// if you still need to get at the data, add a reference before
// uploading it or read directly from OpenGL (discouraged).
int ogl_tex_get_data(Handle ht, void** p)
{
	H_DEREF(ht, OglTex, ot);

	*p = tex_get_data(&ot->t);
	return 0;
}


//----------------------------------------------------------------------------
// misc API

// bind the texture to the specified unit [number] in preparation for
// using it in rendering. assumes multitexturing is available.
// not necessary before calling ogl_tex_upload!
// side effects:
// - enables (or disables, if <ht> == 0) texturing on the given unit.
//
// note: there are many call sites of glActiveTextureARB, so caching
// those and ignoring redundant sets isn't feasible.
int ogl_tex_bind(const Handle ht, GLenum unit)
{
	int id = 0;

	// special case: avoid dereference and disable texturing directly.
	if(ht == 0)
		goto disable_texturing;

	{
		// (we can't use H_DEREF because it exits immediately)
		OglTex* ot = H_USER_DATA(ht, OglTex);
		if(!ot)
		{
			glBindTexture(GL_TEXTURE_2D, 0);
			CHECK_ERR(ERR_INVALID_HANDLE);
			UNREACHABLE;
		}

#ifndef NDEBUG
		if(!ot->id)
		{
			debug_warn("ogl_tex_bind: OglTex.id is not a valid texture");
			return -1;
		}
#endif

		id = ot->id;
		ot->tmu = unit;
	}

disable_texturing:
	glActiveTextureARB(GL_TEXTURE0+unit);
	if(id)
	{
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, id);
	}
	else
		glDisable(GL_TEXTURE_2D);
	return 0;
}


// apply the specified transforms (as in tex_transform) to the image.
// must be called before uploading (raises a warning if called afterwards).
int ogl_tex_transform(Handle ht, uint transforms)
{
	H_DEREF(ht, OglTex, ot);
	int ret = tex_transform(&ot->t, transforms);
	return ret;
}
