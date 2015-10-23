/* PR63538 is about not using 64-bit addresses for .lrodata accesses when it
   involves STRING_CSTs.  */
/* { dg-do compile { target x86_64-*-* } } */
/* { dg-options "-O2 -mcmodel=medium -mlarge-data-threshold=0" { target x86_64-*-* } } */

#include <stdio.h>

const char *str = "Hello World";

int main() {
 printf("str = %p %s\n",str, str);
 return 0;
}
/* { dg-final { scan-assembler-not "movl" } } */
