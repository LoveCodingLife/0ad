/**
 * =========================================================================
 * File        : Sprite.h
 * Project     : 0 A.D.
 * Description : Billboarding sprite class - always faces the camera. It
 *               does this by getting the current model view matrix state.
 * =========================================================================
 */

// Usage: Instantiate, then be sure to pass a loaded
// (using ogl_tex_load()) texture before calling Render().

#ifndef INCLUDED_SPRITE
#define INCLUDED_SPRITE

//--------------------------------------------------------
//  Includes / Compiler directives
//--------------------------------------------------------

#include "maths/Vector3D.h"
#include "Texture.h"

//--------------------------------------------------------
//  Declarations
//--------------------------------------------------------

class CSprite 
{
public:
	CSprite();
	~CSprite();

	void Render();

	int SetTexture(CTexture *texture);

	void SetSize(float width, float height);

	float GetWidth();
	void  SetWidth(float width);

	float GetHeight();
	void  SetHeight(float height);

	CVector3D GetTranslation();
	void SetTranslation(CVector3D pos);
	void SetTranslation(float x, float y, float z);

	CVector3D GetScale();
	void SetScale(CVector3D scale);
	void SetScale(float x, float y, float z);

	void SetColour(float * colour);
	void SetColour(float r, float g, float b, float a = 1.0f);
private:
	void BeginBillboard();
	void EndBillboard();

	CTexture	*m_texture;

	CVector3D	m_coords[4];

	float	    m_width;
	float		m_height;

	CVector3D	m_translation;
	CVector3D	m_scale;

	float m_colour[4];
};

#endif // INCLUDED_SPRITE
