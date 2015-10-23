/* Verify -mpatch-functions-for-instrumentation works.  */
/* { dg-do run } */
/* { dg-require-effective-target lp64 } */

/* -O2 forces a sibling call for foo from bar.  */
/* { dg-options "-O2 -mpatch-functions-for-instrumentation --param function-patch-min-instructions=0" } */

__attribute__ ((noinline))
int foo()
{
  /* Dummy loop.  */
  int x = 10;
  int y = 100;
  while (--x)
    ++y;
  return y;
}

__attribute__ ((noinline))
int bar()
{
  return foo();
}

int main()
{
  bar();
  return 0;
}
