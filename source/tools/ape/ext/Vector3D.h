//***********************************************************
//
// Name:		Vector3D.H
//
// Description: Provides an interface for a vector in R3 and
//				allows vector and scalar operations on it
//
//***********************************************************

#ifndef VECTOR3D_H
#define VECTOR3D_H

#include <math.h>
#include "MathUtil.h"
#include "Types.h"

class CVector3D
{
	public:
		float X, Y, Z;

	public:
		CVector3D ();
		CVector3D (float x, float y, float z);

		int operator == (const CVector3D &vector) const ;
		int operator != (const CVector3D &vector) const ;
		int operator ! () const ; 

		float& operator[](int index) { return *((&X)+index); }
		const float& operator[](int index) const { return *((&X)+index); }

		//vector addition
		CVector3D operator + (const CVector3D &vector) const ;
		//vector addition/assignment
		CVector3D &operator += (const CVector3D &vector);

		//vector subtraction
		CVector3D operator - (const CVector3D &vector) const ;
		//vector subtraction/assignment
		CVector3D &operator -= (const CVector3D &vector);
		
		//scalar multiplication
		CVector3D operator * (float value) const ;
		//scalar multiplication/assignment
		CVector3D& operator *= (float value);

		// negation
		CVector3D operator-() const;

	public:
		void Set (float x, float y, float z);
		void Clear ();

		//Dot product
		float Dot (const CVector3D &vector) const;
		//Cross product
		CVector3D Cross (const CVector3D &vector) const;

		//Returns length of the vector
		float GetLength () const;
		void Normalize ();

		//Returns a color which describes the vector
		SColor4ub ConvertToColor (float alpha_factor) const;
};


#endif
