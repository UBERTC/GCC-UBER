// Test how operands_equal_p handles a NULL operand.
// { dg-do compile }
// { dg-options "-Wself-assign" }

#include <cstdio>

namespace testing {

class Foo {
  int f;
 public:
  Foo() { printf("Construct Foo\n"); }
};

class Bar {
  int b;
 public:
  Bar(int x) { printf("Construct Bar\n"); }

  void operator=(const Foo& foo) {
    printf("Assign Foo to Bar\n");
  }
};

}

template <class T>
void func(T t) {
  ::testing::Bar(1) = ::testing::Foo(); // used to trigger a segfault
  ::testing::Foo() = ::testing::Foo();
}

main() {
  func(2);
}
