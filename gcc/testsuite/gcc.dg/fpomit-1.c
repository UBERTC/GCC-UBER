/* { dg-do compile { target i?86-*-* x86_64-*-* } } */
/* { dg-options "-O2 -fno-omit-frame-pointer -fshrink-wrap-frame-pointer -dA" } */
/* { dg-final { scan-assembler-times "BLOCK 2.*push\[^\n\r\]*bp.*mov\[^\n\r\]*sp\[^\n\r\]*bp.*BLOCK 3" 1 } } */
/* { dg-final { scan-assembler-times "pop\[^\n\r\]*bp" 1 } } */

/* Check there is 1 push, 1 pop and 1 mov-sp-to-bp.  */

unsigned M;
int cond1, cond2;
extern void goo();
extern void hoo();
extern void ioo();

void foo() {
  unsigned i;
  for (i = 0; i <= M; i++) {
    if (cond1) {
      goo();
    } else if (cond2) {
      hoo();
    } else {
      ioo();
    }
  }
}
