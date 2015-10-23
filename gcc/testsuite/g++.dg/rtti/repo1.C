// PR c++/22204
// { dg-options "-frepo" }
// { dg-require-host-local "" }
// { dg-skip-if "dkms are not final links" { vxworks_kernel } }
// { dg-skip-if "-frepo not supported with --sysroot (as it is not passed to COLLECT_GCC_OPTIONS" { *-*-linux* } { "*" } { "" } }

#include <typeinfo>
template<int>
struct function1
{
  function1()
  {
    typeid(int[100]);
  }
};
function1<1> b;

int main () {}

// { dg-final { cleanup-repo-files } }
