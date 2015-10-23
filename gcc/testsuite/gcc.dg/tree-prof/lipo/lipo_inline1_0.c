/* { dg-options "-O2 -fdump-tree-optimized-details-blocks -fdump-ipa-inline-details -fopt-info" } */

extern int foo (void);
extern int goo (void);


int bar (void)
{
  return 2;
}

int g;
int main ()
{
   int s = 0, i;

   for (i = 0; i < 1000; i ++)
    {
     s += foo();  /* Should be inlined */
     s += goo();  /* Should be inlined */
    }

   g = s;
   return 0;
}

/* { dg-final-use { scan-tree-dump-not "foo" "optimized"} } */
/* { dg-final-use { scan-tree-dump-not "goo" "optimized"} } */
/* { dg-final-use { cleanup-tree-dump "optimized" } } */
