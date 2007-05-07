/*==================================================================
| 
| Name: ParticleSystem.cpp
|
|===================================================================
|
==================================================================*/

#include "ParticleSystem.h"
#include <assert.h>

CParticleSystem::CParticleSystem() 
{
}

CParticleSystem::~CParticleSystem() 
{
}

void CParticleSystem::Render() 
{
	CParticleEmitter * curEmitter = NULL;

	std::vector<CParticleEmitter *>::iterator itor = m_Emitters.begin();
	while (itor != m_Emitters.end())
	{	
		curEmitter = (*itor);
		curEmitter->Render();

		itor++;
	}
}

void CParticleSystem::Update()
{
	CParticleEmitter * curEmitter = NULL;

	std::vector<CParticleEmitter *>::iterator itor = m_Emitters.begin();
	while (itor != m_Emitters.end())
	{	
		curEmitter = (*itor);
		curEmitter->Update();

		if (curEmitter->IsFinished()) {
			m_Emitters.erase(itor);
			delete curEmitter;
		}
		++itor;
	}
}

void CParticleSystem::Frame()
{
	Render();
	Update();
}

CParticleEmitter * CParticleSystem::CreateNewEmitter()
{
	CParticleEmitter *newEmitter = new CParticleEmitter;
	assert(newEmitter);

	m_Emitters.push_back(newEmitter);

	return newEmitter;
}
