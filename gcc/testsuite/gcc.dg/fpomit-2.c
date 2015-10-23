/* { dg-do compile { target {{ i?86-*-* x86_64-*-* } && lp64 } } } */
/* { dg-options "-O2 -fno-omit-frame-pointer -fshrink-wrap-frame-pointer -mno-omit-leaf-frame-pointer -fno-schedule-insns2" } */
/* { dg-final { scan-assembler-not "mov\[^\n\r\]*bp" } } */

/* Check fp is the last register to use.  */

int a[1000], b[1000];

void foo() {
  int r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14;

  r1 = a[1];
  r2 = a[2];
  r3 = a[3];
  r4 = a[4];
  r5 = a[5];
  r6 = a[6];
  r7 = a[7];
  r8 = a[8];
  r9 = a[9];
  r10 = a[10];
  r11 = a[11];
  r12 = a[12];
  r13 = a[13];
  r14 = a[14];
  asm volatile ("" : : : "memory");
  b[1] = r1;
  b[2] = r2;
  b[3] = r3;
  b[4] = r4;
  b[5] = r5;
  b[6] = r6;
  b[7] = r7;
  b[8] = r8;
  b[9] = r9;
  b[10] = r10;
  b[11] = r11;
  b[12] = r12;
  b[13] = r13;
  b[14] = r14;
}
