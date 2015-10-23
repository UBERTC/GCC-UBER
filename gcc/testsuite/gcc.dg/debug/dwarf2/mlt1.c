/* Test that -g1 includes line tables and inlined subroutine entries,
   and excludes types and variables.  */
/* Origin: Cary Coutant  <ccoutant@google.com> */
/* { dg-do compile } */
/* { dg-options "-O2 -gdwarf-2 -dA -g1" } */
/* { dg-final { scan-assembler "DW_AT_stmt_list" } } */
/* { dg-final { scan-assembler "DW_TAG_subprogram" } } */
/* { dg-final { scan-assembler "DW_TAG_inlined_subroutine" } } */
/* { dg-final { scan-assembler-not "DW_TAG_variable" } } */
/* { dg-final { scan-assembler-not "DW_TAG_formal_parameter" } } */
/* { dg-final { scan-assembler-not "DW_TAG_base_type" } } */

static inline __attribute__((always_inline)) int
a(int i, int j)
{
  return (i << 5) + j;
}

int
b(int i, int j)
{
  return (i >> 5) + (j << 27);
}

int
c(int i, int j)
{
  int r = a(i, j);
  r = b(r, i);
  r = b(r, j);
  return r;
}
