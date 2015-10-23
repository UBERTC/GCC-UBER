/* { dg-options "-O2 -fdump-tree-optimized -fdump-ipa-profile" } */
#include <stdio.h>
struct A {
  A () {}
  virtual int AA (void) { return 0; }
};

extern A* getB (void);
extern A* getC (void);

int g;

int
main (void)
{
  A* p;
  int i;
  int s = 0;

  p = getB();
  for (i = 0; i < 100; i++)
   {
      s += p->AA();
   }

  for (i = 0; i < 100; i++)
   {
      if (i%10 == 0)
        p = getB();
      else
        p = getC();

      s += p->AA();
   }
   printf ("result = %d\n",s);
}

/* { dg-final-use { scan-ipa-dump-times "Indirect call -> direct call" 2 "profile" } } */
/* { dg-final-use { scan-tree-dump-not "Invalid sum" "optimized" } } */
/* { dg-final-use { cleanup-tree-dump "optimized" } } */
/* { dg-final-use { cleanup-ipa-dump "profile" } } */
