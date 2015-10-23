/* { dg-do compile { target {{ i?86-*-* x86_64-*-* } && lp64 } } } */
/* { dg-options "-O2 -fno-omit-frame-pointer -fshrink-wrap-frame-pointer -fno-schedule-insns2" } */
/* { dg-final { scan-assembler-not "push" } } */
/* { dg-final { scan-assembler-not "pop" } } */

/* Check -fshrink-wrap-frame-pointer can enable omitting leaf frame pointer
   if -mno-omit-leaf-frame-pointer is not specified explicitly.  */

int a[1000], b[1000];

void foo() {
  int r1, r2, r3, r4;

  r1 = a[1];
  r2 = a[2];
  r3 = a[3];
  r4 = a[4];
  asm volatile ("" : : : "memory");
  b[1] = r1;
  b[2] = r2;
  b[3] = r3;
  b[4] = r4;
}
