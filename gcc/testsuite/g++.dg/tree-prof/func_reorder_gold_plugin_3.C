/* Check if the edge_cutoffp option to the function reordering plugin works as
   expected.  */
/* { dg-require-section-exclude "" } */
/* { dg-require-linker-function-reordering-plugin "" } */
/* { dg-options "-O2 -freorder-functions=callgraph -ffunction-sections -Wl,-plugin-opt,file=linker.dump -Wl,-plugin-opt,edge_cutoff=p100" } */

int __attribute__ ((noinline))
foo ()
{
  return 1;
}

int main ()
{
  int sum = 0;
  for (int i = 0; i< 1000; i++)
    {
      sum += foo ();
    }
  return sum - 1000;
}

/* { dg-final-use { scan-file linker.dump "Not considering edge with weight 1000 and below" } }  */
/* { dg-final-use { scan-file-not linker.dump "Callgraph group" } }  */
/* { dg-final-use { remove-build-file "linker.dump" } }  */
