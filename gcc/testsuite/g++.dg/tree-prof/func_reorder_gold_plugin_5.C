/* Check if cutting off callgraph and using sort_name_prefix gets all functions laid out
   according to prefixes.   foo_200 is almost as hot as the other foo's but should
   not be grouped with them as it has a different section prefix and sort_name_prefix is
   turned on.  */
/* { dg-require-section-exclude "" } */
/* { dg-require-linker-function-reordering-plugin "" } */
/* { dg-options "-O2 -freorder-functions=callgraph -ffunction-sections -Wl,-plugin-opt,file=linker.dump,-plugin-opt,edge_cutoff=p100,-plugin-opt,sort_name_prefix=yes" } */

int __attribute__ ((noinline, section(".text.unlikely._Z7foo_200v")))
foo_200 ()
{
  return 1;
}

int __attribute__ ((noinline))
foo_100 ()
{
  return 1;
}

int __attribute__ ((noinline))
foo_300 ()
{
  return 1;
}
int main ()
{
  int sum = 0;
  for (int i = 0; i< 200; i++)
    sum += foo_200 ();
  for (int i = 0; i< 100; i++)
    sum += foo_100 ();
  for (int i = 0; i< 300; i++)
    sum += foo_300 ();
  return sum - 600;
}

/* { dg-final-use { scan-file-not linker.dump "Callgraph group" } }  */
/* { dg-final-use { scan-file linker.dump ".text.unlikely._Z7foo_200v entry count = 200 computed = 200 max count = 200" } }  */
/* { dg-final-use { scan-file linker.dump "\.text\.unlikely\._Z7foo_200v.*\n\.text\.*\._Z7foo_100v.*\n\.text\.*\._Z7foo_300v.*\n" } }  */
/* { dg-final-use { remove-build-file "linker.dump" } }  */
