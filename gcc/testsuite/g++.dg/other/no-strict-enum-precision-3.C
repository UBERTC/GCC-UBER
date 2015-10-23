/* { dg-do run } */
/* { dg-options "-O2 -fno-strict-enums" } */

extern "C" void abort (void);

enum X
{
  X1,
  X2
};

int
main (int argc, char *argv[])
{
  X x = static_cast < X > (argc + 999);
  if (x == X1)
    abort ();
  if (x == X2)
    abort ();
  return 0;
}
