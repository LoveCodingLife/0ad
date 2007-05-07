///////////////////////////////////////////////////////////////////////////////
// Name:		SkeletonAnimDef.cpp
///////////////////////////////////////////////////////////////////////////////

#include "pmdexp_types.h"
#include "SkeletonAnimDef.h"
#include "FilePacker.h"


///////////////////////////////////////////////////////////////////////////////////////////
// CSkeletonAnimDef constructor
CSkeletonAnimDef::CSkeletonAnimDef() : m_Keys(0), m_NumKeys(0), m_NumFrames(0), m_FrameTime(0)
{
}

///////////////////////////////////////////////////////////////////////////////////////////
// CSkeletonAnimDef destructor
CSkeletonAnimDef::~CSkeletonAnimDef() 
{
	delete[] m_Keys;
}

///////////////////////////////////////////////////////////////////////////////////////////
// BuildBoneMatrices: build matrices for all bones at the given time (in MS) in this 
// animation
void CSkeletonAnimDef::BuildBoneMatrices(float time,CMatrix3D* matrices) const
{
	float fstartframe=time/m_FrameTime;
	u32 startframe=u32(time/m_FrameTime);
	float deltatime=fstartframe-startframe;

	startframe%=m_NumFrames; 

	u32 endframe=startframe+1;
	endframe%=m_NumFrames; 

	u32 i;
	for (i=0;i<m_NumKeys;i++) {
		const Key& startkey=GetKey(startframe,i);
		const Key& endkey=GetKey(endframe,i);
		
		CVector3D trans=startkey.m_Translation*(1-deltatime)+endkey.m_Translation*deltatime;
		CQuaternion rot;
		rot.Slerp(startkey.m_Rotation,endkey.m_Rotation,deltatime);

		matrices[i].SetIdentity();
		matrices[i].Rotate(rot);
		matrices[i].Translate(trans);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Save: try to save anim to file
void CSkeletonAnimDef::Save(const char* filename,const CSkeletonAnimDef* anim)
{
	CFilePacker packer;

	// pack up all the data
	packer.PackString(anim->m_Name);
	packer.PackRaw(&anim->m_FrameTime,sizeof(anim->m_FrameTime));
	packer.PackRaw(&anim->m_NumKeys,sizeof(anim->m_NumKeys));
	packer.PackRaw(&anim->m_NumFrames,sizeof(anim->m_NumFrames));
	packer.PackRaw(anim->m_Keys,anim->m_NumKeys*anim->m_NumFrames*sizeof(Key));

	// now write it
	packer.Write(filename,FILE_VERSION,"PSSA");
}


