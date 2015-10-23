// { dg-options "-std=c++11 -O2 --param=lipo-sampling-period=1" }
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include "tls2.h"
TLSClass* NextTLSClass() {
  return new TLSClass();
}
int NextId() {
  static int id = 0;
  return id++;
}
static thread_local TLSClass* current_tls2_ = NextTLSClass();
void *SetTLSClass2(TLSClass *a) {
  current_tls2_ = a;
}
int main() {
  int i = 0;
  if (GetTLSClass()->id != i++)
    abort();
  TLSClass *A = NextTLSClass();
  SetTLSClass(A);
  if (GetTLSClass()->id != i++)
    abort();
  if (current_tls2_->id != i++)
    abort();
  A = NextTLSClass();
  SetTLSClass2(A);
  if (current_tls2_->id != i++)
    abort();
}
