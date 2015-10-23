/* { dg-do compile} */
/* { dg-options "-frecord-compilation-info-in-elf -Dtest -dA" } */

void foobar(int);

void
foo (void)
{
  int i;
  for (i = 0; i < 100; i++)
    {
      foobar(i);
    }
}

/* { dg-final { scan-assembler-times "Dtest" 1 } } */
