// Test how self-assignment detection handles constant-folding happening
// when parsing the RHS or the initializer.
// { dg-do compile }
// { dg-options "-Wself-assign -Wself-assign-non-pod" }

class Foo {
 private:
  int a_;

 public:
  Foo() : a_(a_+0) {} // should not warn

  void setA(int a) {
    a_ = a_ + 0; // should not warn
  }

  void operator=(Foo& rhs) {
    this->a_ = rhs.a_;
  }
};

struct Bar {
  int b_;
  float c_;
};

int g = g * 1; // should not warn

int func()
{
  Bar *bar1, bar2;
  Foo foo;
  int x = x - 0;        // should not warn
  static int y = y / 1; // should not warn
  float *f;
  Bar bar_array[5];

  *f = *f / 1;             // should not warn
  bar1->b_ = bar1->b_ * 1; // should not warn
  bar2.c_ = bar2.c_ - 0;   // should not warn
  foo.setA(5);
  bar_array[3].c_ = bar_array[3].c_ * 1;     // should not warn
  bar_array[x+g].b_ = bar_array[x+g].b_ / 1; // should not warn
  x += 0;
  y -= 0;
  foo = foo;           // { dg-warning "assigned to itself" }
  foo.operator=(foo);  // should not warn
}
