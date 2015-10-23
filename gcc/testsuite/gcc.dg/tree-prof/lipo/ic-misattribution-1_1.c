/* { dg-options "-O2 -fdump-ipa-profile" } */

extern void other_caller (void);

void
callee (void)
{
  return;
}

void
caller(void (*func) (void))
{
  func ();
}

/* { dg-final-use { scan-ipa-dump "Indiret call -> direct call"  "profile" } } */
/* { dg-final-use { cleanup-ipa-dump "profile" } } */
