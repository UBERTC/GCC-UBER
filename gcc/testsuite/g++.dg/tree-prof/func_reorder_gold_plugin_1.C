/* Verify if call-graph profile sections are created with -freorder-functions=.
   Check of edge profiles and node profiles are present in the profile
   sections.  Check if the segment splitting API is invoked.  */
/* { dg-require-section-exclude "" } */
/* { dg-require-linker-function-reordering-plugin "" } */
/* { dg-options "-O2 -freorder-functions=callgraph -ffunction-sections --save-temps -Wl,-plugin-opt,file=linker.dump -Wl,-plugin-opt,split_segment=yes" } */

int
notcalled ()
{
  return 0;
}

int __attribute__ ((noinline))
foo ()
{
  return 1;
}

int __attribute__ ((noinline))
bar ()
{
  return 0;
}

int main ()
{
  int sum;
  for (int i = 0; i< 1000; i++)
    {
      sum = foo () + bar();
    }
  return sum * bar ();
}

/* { dg-final-use { scan-assembler "\.gnu\.callgraph\.text\.main" } } */
/* { dg-final-use { scan-assembler "\.string \"1000\"" } } */
/* { dg-final-use { scan-assembler "\.string \"Weight 1000 1000\"" } }  */
/* { dg-final-use { scan-assembler "\.string \"Weight 1001 1001\"" } }  */
/* Check if main is next to foo or bar  */
/* { dg-final-use { scan-file linker.dump "Callgraph group : *\.*(_Z3foov main|main _Z3foov|_Z3barv main|main _Z3barv\).*\n" } }  */
/* { dg-final-use { scan-file linker.dump ".text\..*\._Z9notcalledv entry count = 0 computed = 0 max count = 0" } }  */
/* { dg-final-use { scan-file linker.dump "Moving .* section\\(s\\) to new segment" } }  */
/* { dg-final-use { cleanup-saved-temps } }  */
/* { dg-final-use { remove-build-file "linker.dump" } }  */
