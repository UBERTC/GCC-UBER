/* { dg-options "-O2 -fdump-tree-optimized -fdump-ipa-profile --param=lipo-sampling-period=3" } */

extern void setp (int (**pp) (void), int i);

int
main (void)
{
  int (*p) (void);
  int  i;

  for (i = 0; i < 10; i ++)
    {
	setp (&p, i);
	p ();
    }
  
  return 0;
}

/* { dg-final-use { scan-ipa-dump "Indirect call -> direct call.* a1" "profile"} } */
/* { dg-final-use { scan-tree-dump-not "Invalid sum" "optimized"} } */
/* { dg-final-use { cleanup-tree-dump "optimized" } } */
/* { dg-final-use { cleanup-ipa-dump "profile" } } */
