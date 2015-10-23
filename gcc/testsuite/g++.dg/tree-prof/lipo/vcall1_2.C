/* { dg-options "-O2 -fdump-tree-optimized -fdump-ipa-profile" } */

struct A {
  A () {}

  virtual int AA (void)
  { return 0; }

};

struct B : public A {
  B () {}

  virtual int AA (void)
  { return 1; }
};

struct C : public B {
  C () {}

  virtual int AA (void)
  { return 2; }

};

C c;

A* getC(void)
{
  return &c;
}
