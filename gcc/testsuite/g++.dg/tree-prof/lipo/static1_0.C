// { dg-options "-std=c++11 -O2" }

// A test case for static var promotion on targets like powerpc, where
// the static initializer data is loaded via indirection through
// TOC. Ensure that the global label for static initializer data is
// unique via *.cmo* suffix.

// Bug: after static var promotion in two different modules, we have
// the following which leads to multiple definition of ._41" error during
// link time.

// Module 1                     Module 2
//      .hidden ._41.cmo.1           .hidden ._41.cmo.3
//      .globl ._41                  .globl ._41
// ._41:                        ._41:
//   ...                          ...


// Instead we should use the appropriate unique names for initializer
// data as in the following.

// Module 1                     Module 2
//      .hidden ._41.cmo.1           .hidden ._41.cmo.3
//      .globl ._41.cmo.1            .globl ._41.cmo.3
// ._41.cmo.1:                  ._41.cmo.3:
// ...                          ...

class A {
 public:
  int f(int x) const;
};

class B {
 public:
  int f(int x) const;
};

int main()
{
  A *a = new A();
  B *b = new B();
  int total = 0;
  for (int i=0; i<3; ++i) {
    total += a->f(1);
    total += b->f(1);
  }
  return (total > 0) ? 0 : 1;
}
