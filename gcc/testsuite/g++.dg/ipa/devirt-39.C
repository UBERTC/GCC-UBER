// PR c++/61214
/* { dg-options "-O2 -fdump-tree-optimized -fdevirtualize"  } */

struct Base
{
  virtual ~Base();
  virtual Base* clone() {
    return 0;
  }
};

struct Foo : Base
{
  virtual ~Foo();
  virtual Base* clone() {
    return new Foo();
  }
};

int main()
{
  Base* f = new Foo();
  f->clone();
  return 0;
}

/* { dg-final { scan-assembler-not "_ZN3Foo5cloneEv" } } */
