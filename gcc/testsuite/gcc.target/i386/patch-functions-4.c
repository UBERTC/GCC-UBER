/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-options "-mpatch-functions-for-instrumentation -mpatch-functions-ignore-loops -mno-patch-functions-main-always" } */

/* Function is too small to be patched when ignoring the loop.
   Check there aren't any nop-bytes at beginning and end of function.  */

/* { dg-final { scan-assembler-not ".byte\t0xeb,0x09(.*).byte\t0x90" } } */
/* { dg-final { scan-assembler-not "ret(.*).byte\t0x90(.*).byte\t0x90" } } */

__attribute__ ((noinline))
void foo()
{
  int x = 0;
  while (++x);
}

int main()
{
  foo();
  return 0;
}
