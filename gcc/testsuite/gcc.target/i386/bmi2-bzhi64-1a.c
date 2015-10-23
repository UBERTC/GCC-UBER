/* { dg-do compile { target { ! ia32 } } } */
/* { dg-options "-mbmi2 -O2 -dp --param max-default-completely-peeled-insns=0" } */

#include "bmi2-bzhi64-1.c"

/* { dg-final { scan-assembler-times "bmi2_bzhi_di3" 1 } } */
