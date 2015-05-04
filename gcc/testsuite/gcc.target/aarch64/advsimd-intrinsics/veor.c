#define INSN_NAME veor
#define TEST_MSG "VEOR/VEORQ"

#include "binary_op.inc"

/* Expected results.  */
VECT_VAR_DECL(expected,int,8,8) [] = { 0xf2, 0xf3, 0xf0, 0xf1,
				       0xf6, 0xf7, 0xf4, 0xf5 };
VECT_VAR_DECL(expected,int,16,4) [] = { 0xc, 0xd, 0xe, 0xf };
VECT_VAR_DECL(expected,int,32,2) [] = { 0xfffffff3, 0xfffffff2 };
VECT_VAR_DECL(expected,int,64,1) [] = { 0xffffffffffffff94 };
VECT_VAR_DECL(expected,uint,8,8) [] = { 0xe4, 0xe5, 0xe6, 0xe7,
					0xe0, 0xe1, 0xe2, 0xe3 };
VECT_VAR_DECL(expected,uint,16,4) [] = { 0xffee, 0xffef, 0xffec, 0xffed };
VECT_VAR_DECL(expected,uint,32,2) [] = { 0xffffffd8, 0xffffffd9 };
VECT_VAR_DECL(expected,uint,64,1) [] = { 0xfffffffffffffff2 };
VECT_VAR_DECL(expected,int,8,16) [] = { 0x6, 0x7, 0x4, 0x5,
					0x2, 0x3, 0x0, 0x1,
					0xe, 0xf, 0xc, 0xd,
					0xa, 0xb, 0x8, 0x9 };
VECT_VAR_DECL(expected,int,16,8) [] = { 0x1c, 0x1d, 0x1e, 0x1f,
					0x18, 0x19, 0x1a, 0x1b };
VECT_VAR_DECL(expected,int,32,4) [] = { 0x12, 0x13, 0x10, 0x11 };
VECT_VAR_DECL(expected,int,64,2) [] = { 0xffffffffffffffe8,
					0xffffffffffffffe9 };
VECT_VAR_DECL(expected,uint,8,16) [] = { 0xfc, 0xfd, 0xfe, 0xff,
					 0xf8, 0xf9, 0xfa, 0xfb,
					 0xf4, 0xf5, 0xf6, 0xf7,
					 0xf0, 0xf1, 0xf2, 0xf3 };
VECT_VAR_DECL(expected,uint,16,8) [] = { 0xfff3, 0xfff2, 0xfff1, 0xfff0,
					 0xfff7, 0xfff6, 0xfff5, 0xfff4 };
VECT_VAR_DECL(expected,uint,32,4) [] = { 0xffffffc7, 0xffffffc6,
					 0xffffffc5, 0xffffffc4 };
VECT_VAR_DECL(expected,uint,64,2) [] = { 0xfffffffffffffff3,
					 0xfffffffffffffff2 };
