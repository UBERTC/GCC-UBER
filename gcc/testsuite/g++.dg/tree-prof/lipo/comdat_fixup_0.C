/* { dg-options "-O2 -fno-inline -fprofile-generate-buildinfo=$srcdir/g++.dg/tree-prof/lipo/buildinfo.txt" } */
#include <stdio.h>

extern int foo1(int x);
extern int foo2(int x);
int main()
{
  printf ("Result = %d\n", foo1(1) + foo2(1));
}
