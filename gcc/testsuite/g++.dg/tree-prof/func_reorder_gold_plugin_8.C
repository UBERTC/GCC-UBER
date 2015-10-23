/* Check if unlikely_cutoff works as expected.  Function foo is unlikely because of the cutoff.  */
/* { dg-require-section-exclude "" } */
/* { dg-require-linker-function-reordering-plugin "" } */
/* { dg-options "-O2 -freorder-functions=callgraph -ffunction-sections -Wl,-plugin-opt,file=linker.dump -Wl,-plugin-opt,edge_cutoff=p100,-plugin-opt,unlikely_cutoff=1" } */

int __attribute__ ((noinline,section(".text.hot._Z3foov")))
foo ()
{
  return 0;
}

int main()
{
  return foo ();
}

/* { dg-final-use { scan-file-not linker.dump "Callgraph group" } }  */
/* { dg-final-use { scan-file linker.dump "=== Unlikely sections start ===\n.*\.text\.hot\._Z3foov.* entry count = 1 computed = 1 max count = 1 split = 0\n.*=== Unlikely sections end ===" } }  */
/* { dg-final-use { remove-build-file "linker.dump" } }  */
