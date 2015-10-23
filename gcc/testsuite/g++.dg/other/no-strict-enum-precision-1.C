/* { dg-do run } */
/* { dg-options "-fno-strict-enums" } */

extern "C" void abort (void);

enum zero_one
{ zero = 0, one = 1 };

int *
allocate_bool (zero_one e)
{
  int *v = 0;
  switch (e)
    {
    case zero:
      v = new int (0);
    case one:
      v = new int (1);
    }
  return v;
}

int
main ()
{
  if (allocate_bool (static_cast < zero_one > (999)))
    {
      /* Error: should not have matched any case label.  */
      abort ();
    }
  else
    {
      return 0;
    }
}
