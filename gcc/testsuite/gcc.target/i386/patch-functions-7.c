/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-options "-mpatch-functions-for-instrumentation -mno-patch-functions-main-always" } */

/* 'main' shouldn't be patched with the option -mno-patch-functions-main-always.
   Check there aren't any nop-bytes at beginning and end of main.  */

/* { dg-final { scan-assembler-not ".byte\t0xeb,0x09(.*).byte\t0x90" } } */
/* { dg-final { scan-assembler-not "ret(.*).byte\t0x90(.*).byte\t0x90" } } */

int main()
{
  int x = 0;
  return 0;
}
