extern int bar(void);
extern int blah(void);
int foo (void)
{
  int i, s = 0;

  for (i = 0; i < 2; i++)
   {
     s += bar();  /* Inlined */
     s += blah(); /* Inlined */
   }
   return s;
}

int goo (void)
{
 return 10;
}

/* { dg-final-use { scan-tree-dump-not "bar" "optimized"} } */
/* { dg-final-use { scan-tree-dump-not "blah" "optimized"} } */
/* { dg-final-use { cleanup-tree-dump "optimized" } } */
