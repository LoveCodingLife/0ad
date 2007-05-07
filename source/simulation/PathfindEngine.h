// PathfindEngine.h
// 
// The pathfinding engine singleton.
//
// Usage: g_Pathfinder.RequestPath( HEntity me, float x, float y );
//
// Mark Thompson mot20@cam.ac.uk / mark@wildfiregames.com

#ifndef INCLUDED_PATHFINDENGINE
#define INCLUDED_PATHFINDENGINE

#include "ps/Singleton.h"
#include "EntityHandles.h"
#include "ps/Vector2D.h"
#include "AStarEngine.h"

#define g_Pathfinder CPathfindEngine::GetSingleton()

class CEntityOrder;

enum EPathType
{
	PF_STANDARD,
	PF_ATTACK_MELEE,
};


class CPathfindEngine : public Singleton<CPathfindEngine>
{
public:
	CPathfindEngine();

	void RequestPath( HEntity entity, const CVector2D& destination, 
		CEntityOrder::EOrderSource orderSource );

	void RequestLowLevelPath( HEntity entity, const CVector2D& destination, bool contact, 
		float radius, CEntityOrder::EOrderSource orderSource );

	void RequestContactPath( HEntity entity, CEntityOrder* current, float range );
	bool RequestAvoidPath( HEntity entity, CEntityOrder* current, float avoidRange );
private:
	CAStarEngineLowLevel mLowPathfinder;
};

#endif
