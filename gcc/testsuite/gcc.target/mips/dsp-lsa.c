/* Test MIPS32 DSP instructions - should use LSA instead of LHX */
/* { dg-do compile } */
/* { dg-options "-mgp32 -mdsp (HAS_LSA)" } */
/* { dg-skip-if "code quality test" { *-*-* } { "-O0" } { "" } } */

/* { dg-final { scan-assembler "\tlsa\t" } } */

NOMIPS16 signed short test (signed short *a, int index)
{
  return a[index];
}
