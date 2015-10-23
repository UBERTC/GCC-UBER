// { dg-options "-std=c++11 -O2 --param=lipo-sampling-period=1" }
#include "tls.h"

thread_local TLSClass* current_tls_ = NextTLSClass();
void *SetTLSClass(TLSClass *a) {
  current_tls_ = a;
}
TLSClass *GetTLSClass() {
  return current_tls_;
}
