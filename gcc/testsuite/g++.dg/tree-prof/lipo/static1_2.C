/* { dg-options "-std=c++11 -O2" } */

#include <vector>

class B {
 public:
  int f(int x) const;
};

static const std::vector<int> point2_{43};

int B::f(int x) const {
  return x+1;
}
