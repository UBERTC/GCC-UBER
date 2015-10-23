/* Verify -mpatch-functions-for-instrumentation works.  */
/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-options "-mpatch-functions-for-instrumentation" } */

/* Check nop-bytes at beginning.  */
/* { dg-final { scan-assembler ".byte\t0xeb,0x09(.*).byte\t0x90" } } */
/* Check nop-bytes at end.  */
/* { dg-final { scan-assembler "ret(.*).byte\t0x90(.*).byte\t0x90" } } */

__attribute__ ((noinline))
void foo()
{
  /* Dummy loop.  */
  int x = 0;
  while (++x);
}

int main()
{
  foo();
  return 0;
}
