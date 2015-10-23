/* Check if turning off use_maxcount works as expected.  This makes the node
   profile weight to be equal to the entry count of any basic block in a
   function rather than the max count.
   foo_100's maxcount > foo_200's max count but 
   foo_100's entry count < foo_200's entry count.  */
/* { dg-require-section-exclude "" } */
/* { dg-require-linker-function-reordering-plugin "" } */
/* { dg-options "-O2 -freorder-functions=callgraph -ffunction-sections -Wl,-plugin-opt,file=linker.dump -Wl,-plugin-opt,edge_cutoff=p100,-plugin-opt,use_maxcount=no" } */


int __attribute__ ((noinline))
bar (int *i)
{
  (*i)--;
  if (*i >= 0)
    return 1;
  return 0;
}

int __attribute__ ((noinline))
foo_100 (int count)
{
  int sum = 0;
  while (count > 0)
    {
      sum += bar(&count);  
    }
  return sum;
}

int __attribute__ ((noinline))
foo_200 (int count)
{
  int sum = 0;
  while (count > 0)
    {
      sum += bar(&count);  
    }
  return sum;
}

int main ()
{
  int sum = 0;
  for (int i = 0; i< 200; i++)
    sum += foo_200 (100);
  for (int i = 0; i< 100; i++)
    sum += foo_100 (400);
  return sum - 60000;
}
/* { dg-final-use { scan-file-not linker.dump "Callgraph group" } }  */
/* { dg-final-use { scan-file linker.dump "\.text\.*\._Z7foo_100i entry count = 100 computed = 100 max count = 40000" } }  */
/* { dg-final-use { scan-file linker.dump "\.text\.*\._Z7foo_200i entry count = 200 computed = 200 max count = 20000" } }  */
/* { dg-final-use { scan-file linker.dump "\.text\.*\._Z7foo_100i.*\n\.text\.*\._Z7foo_200i.*\n\.text\.*\._Z3barPi.*\n" } }  */
/* { dg-final-use { remove-build-file "linker.dump" } }  */
