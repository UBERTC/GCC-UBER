/* { dg-do compile { target i?86-*-* x86_64-*-* } } */
/* { dg-options "-O2 -fno-omit-frame-pointer -fshrink-wrap-frame-pointer -fno-schedule-insns2 -fdisable-rtl-ce1 -fdisable-rtl-ce2 -fdisable-rtl-ce3 -ffixed-ax -dA" } */
/* { dg-final { scan-assembler-times "BLOCK 2.*push\[^\n\r\]*bp.*mov\[^\n\r\]*sp\[^\n\r\]*bp.*BLOCK 3" 1 } } */

/* Check loop1 should not set fp free, and lra should not use fp too.  */
int a[1000], b[1000], cond1, cond2, cond3;
unsigned M, N;
extern void goo();
int *p;

void foo() {
  int r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15;
  unsigned i, j;

  r15 = a[15];
  for (i = 0; i <= M; i++) {
    goo();
    goo();
    goo();
    goo();
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
    asm volatile ("" : : : "memory");
    b[14] = a[14];
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
  }
  b[15] = r15;
}
