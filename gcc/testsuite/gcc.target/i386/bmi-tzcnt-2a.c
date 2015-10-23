/* { dg-do compile } */
/* { dg-options "-O2 -mbmi -fno-inline --param max-default-completely-peeled-insns=0" } */

#include "bmi-tzcnt-2.c"

/* { dg-final { scan-assembler-times "tzcntl" 1 } } */
