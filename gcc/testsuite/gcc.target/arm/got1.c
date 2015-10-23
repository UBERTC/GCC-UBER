/* { dg-options "-Os -fpic" }  */
/* { dg-final { scan-assembler "GOT_PREL" } } */

extern int x;
int foo(int j)
{
  int t = x;
  x = j;
  return t;
}
