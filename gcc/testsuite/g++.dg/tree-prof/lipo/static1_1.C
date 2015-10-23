/* { dg-options "-std=c++11 -O2" } */

#include <vector>

class A {
 public:
  int f(int x) const;
};

static const std::vector<int> point1_{42};

int A::f(int x) const {
  return x+1;
}
