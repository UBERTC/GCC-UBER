/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* -O2 forces a sibling call.  */
/* { dg-options "-O2 -mpatch-functions-for-instrumentation" } */

/* { dg-final { scan-assembler ".byte\t0xeb,0x09(.*).byte\t0x90" } } */

/* Checks correct nop-bytes are generated just before a sibling call.  */
/* { dg-final { scan-assembler ".byte\t0xeb,0x09(.*).byte\t0x90(.*)jmp" } } */

/* Not instrumented as function has no loop and is small.  */
__attribute__ ((noinline))
int foo(int n)
{
  int x = 0;
  return n + 10;
}

__attribute__ ((noinline))
int bar(int n)
{
  /* Dummy loop.  */
  while (--n)
    n = n * 2;
  return foo(n);
}
