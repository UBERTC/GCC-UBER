// Check if patching works with function splitting.
// { dg-require-effective-target freorder }
// { dg-options "-O2 -fnon-call-exceptions -freorder-blocks-and-partition " { target { ! x86_64-*-* } } }
// { dg-options "-O2 -fnon-call-exceptions -freorder-blocks-and-partition -mpatch-functions-for-instrumentation -fno-optimize-sibling-calls " { target x86_64-*-* } }

int k;

int
main ()
{
  try
  {
    if (k)
      throw 6;
  }
  catch (...)
  {
  }
}
