#pragma once

#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long

#define BYTEn(x, n) (*((_BYTE*)&(x)+n))

#define BYTE1(x) BYTEn(x, 1) 
#define BYTE2(x) BYTEn(x, 2)

//typedef unsigned __int8 BYTE;
typedef unsigned __int8 _BYTE;
//typedef unsigned int DWORD;
typedef unsigned int _DWORD;
//typedef int LONG;

/*
typedef struct _RECT {
	LONG left;
	LONG top;
	LONG right;
	LONG bottom;
} RECT, *PRECT;
*/

typedef struct Vector3_float {
	float x;
	float y;
	float z;
} Vector3_float;

typedef struct XwaMatrix3x3 {
	float _11;
	float _12;
	float _13;
	float _21;
	float _22;
	float _23;
	float _31;
	float _32;
	float _33;
} XwaMatrix3x3;
