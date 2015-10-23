/* { dg-options "-O2 -fdump-ipa-profile --param=lipo-sampling-period=1" } */

extern void callee (void);
extern void caller (void (*func) (void));

typedef void (*func_t) (void);
func_t func;

int
main ()
{
  func = callee;
  caller (callee);
  func ();
  return 0;
}

/* { dg-final-use { scan-ipa-dump-times "Indirect call -> direct call" 2 "profile" } } */
/* { dg-final-use { cleanup-ipa-dump "profile" } } */
