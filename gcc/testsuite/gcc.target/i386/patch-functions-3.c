/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-options "-mpatch-functions-for-instrumentation --param function-patch-min-instructions=0" } */

/* Function should have nop-bytes with -mpatch-function-min-instructions=0.
   Check there are nop-bytes at beginning and end of function.  */

/* { dg-final { scan-assembler ".byte\t0xeb,0x09(.*).byte\t0x90" } } */
/* { dg-final { scan-assembler "ret(.*).byte\t0x90(.*).byte\t0x90" } } */

__attribute__ ((noinline))
void foo()
{
  int x = 0;
}

int main()
{
  foo();
  return 0;
}
