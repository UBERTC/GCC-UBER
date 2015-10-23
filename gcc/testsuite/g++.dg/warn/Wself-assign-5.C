// Test -Wself-assign does not warn on self-assignment of non-POD variables.
// { dg-do compile }
// { dg-options "-Wself-assign" }

template<typename T>
class Foo {
 private:
  T a_;
 public:
  Foo() : a_(a_) {}  // { dg-warning "assigned to itself" }
  void Set() { a_ = a_; }
};

struct Bar {
  int b_;
  int c_;
  void operator=(Bar& rhs) {
    this->b_ = rhs.b_;
    this->c_ = rhs.c_;
  }
};

template <typename T>
void func() {
  T a;
  a = a;
}

main()
{
  Foo<Bar> foo;
  Bar *bar1, bar2;
  func<int>();
  foo = foo;
  bar2 = bar2;
  bar1 = bar1;        // { dg-warning "assigned to itself" }
  bar2.b_ = bar2.b_;  // { dg-warning "assigned to itself" }
}
