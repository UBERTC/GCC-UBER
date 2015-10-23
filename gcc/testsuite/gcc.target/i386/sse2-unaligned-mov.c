/* { dg-do compile } */
/* { dg-options "-mtune=corei7 -O2" } */

#include <emmintrin.h>

double a[1000];

__m128d foo1() {
  __m128d res;
  res = _mm_load_sd(&a[1]);
  res = _mm_loadh_pd(res, &a[2]);
  return res;
}

void foo2(__m128d res) {
  _mm_store_sd(&a[1], res);
  _mm_storeh_pd(&a[2], res);
}

/* { dg-final { scan-assembler-times "movup" 2 } } */
