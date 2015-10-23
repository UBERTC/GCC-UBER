/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-options "-mpatch-functions-for-instrumentation -mno-patch-functions-main-always" } */

/* Even complicated functions shouldn't get patched if they have the
   never_patch_for_instrumentation attribute. */

/* { dg-final { scan-assembler-not ".byte\t0xeb,0x09(.*).byte\t0x90" } } */
/* { dg-final { scan-assembler-not "ret(.*).byte\t0x90(.*).byte\t0x90" } } */

__attribute__ ((never_patch_for_instrumentation))
int foo () {
  volatile unsigned x = 0;
  volatile unsigned y = 1;
  x += y;
  x *= y;
  while (++x)
    foo ();
  return y;
}


int main ()
{
  int x = 0;
  return 0;
}
