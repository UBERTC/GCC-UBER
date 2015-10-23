/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-options "-O3 -mpatch-functions-for-instrumentation -mno-patch-functions-main-always" } */

/* Functions which have the always_patch attribute should be patched no matter
   what.  Check that there are nop-bytes at the beginning and end of the
   function.  We add -O3 so that the compiler will try to inline foo (but it
   will be blocked by the attribute).  */

/* { dg-final { scan-assembler ".byte\t0xeb,0x09(.*).byte\t0x90" } } */
/* { dg-final { scan-assembler "ret(.*).byte\t0x90(.*).byte\t0x90" } } */

__attribute__ ((always_patch_for_instrumentation))
static int foo () {
  return 3;
}

int main () {
  volatile int x = foo ();
}
