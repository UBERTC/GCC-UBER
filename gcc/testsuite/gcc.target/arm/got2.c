/* We should not use GOT_PREL relocation to load global address with so
   many global accesses.  */

/* { dg-options "-Os -fpic" }  */
/* { dg-final { scan-assembler-not "GOT_PREL" } } */

extern int x1, x2, x3, x4, x5;
int sum()
{
  return x1 + x2 + x3 + x4 + x5;
}
