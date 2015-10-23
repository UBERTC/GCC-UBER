/* { dg-options "-O2 -fdump-tree-optimized -fdump-ipa-profile" } */

int a1 (void)
{
    return 10;
}

int a2 (void)
{
    return 0;
}

typedef int (*tp) (void);

tp aa [] = {a2, a1, a1, a1, a1};

__attribute__((noinline)) void setp (int (**pp) (void), int i)
{
  if (!i)
    *pp = aa [i];
  else
    *pp = aa [(i & 2) + 1];
}
