/* { dg-options "-O2 -fno-inline" } */
#include "comdat_fixup.h"
int foo2(int x)
{
  Foo f;
  return f.foo(x);
}
