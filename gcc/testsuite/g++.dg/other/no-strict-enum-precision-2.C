/* { dg-do run } */
/* { dg-options "-O2 -fno-strict-enums" } */

extern "C" void abort (void);

enum X
{
  X1,
  X2
};

int
foo (enum X x)
{
  switch (x)
    {
    case X1:
      return 0;
    case X2:
      return 1;
    }
  return x;
}

int
main (int argc, char *argv[])
{
  int n = argc + 999;
  if (n == foo (static_cast < X > (n)))
    {
      return 0;
    }
  else
    {
      abort ();
    }
}
