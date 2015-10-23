// Test -Wself-assign and -Wself-assign-non-pod.
// { dg-do compile }
// { dg-options "-Wself-assign -Wself-assign-non-pod" }

template<typename T>
class Foo {
 private:
  T a_;
 public:
  Foo() : a_(a_) {}        // { dg-warning "assigned to itself" }
  void Set() { a_ = a_; }  // { dg-warning "assigned to itself" }
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
  a = a;  // { dg-warning "assigned to itself" }
}

main()
{
  Foo<Bar> foo;
  Bar *bar1, bar2;
  func<int>();
  foo = foo;          // { dg-warning "assigned to itself" }
  bar2 = bar2;        // { dg-warning "assigned to itself" }
  bar1 = bar1;        // { dg-warning "assigned to itself" }
  bar2.b_ = bar2.b_;  // { dg-warning "assigned to itself" }
}
