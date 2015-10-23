/* { dg-do run { target {{ i?86-*-* x86_64-*-* } && lp64 } } } */
/* { dg-options "-O2 -fno-omit-frame-pointer -fshrink-wrap-frame-pointer" } */
/* { dg-output "stack_depth = 8" } */

#include <stdio.h>

void* address[1000];
int sizes[1000];
int N;
int main();
void *main_ret_address;

__attribute((noinline)) int 
get_stack_traces (void **result)
{
   void **sp = (void**) __builtin_frame_address (0);
   int n = 0;
   while (sp) {
       if ((*(char **)(sp+1)) == main_ret_address)
          return n;

       void **next_sp = (void**) *sp;
       result[n] = *(sp+1);
       if (next_sp > sp)
          sizes[n] = (char*)next_sp - (char*)sp;
       else
           sizes[n] = 0;
       sp = next_sp;
       n++;
   }
   return n;
}

__attribute__((noinline)) int bar(int level)
{
  int a[100];

  int i;

   if (level == 0)
     {
       N = get_stack_traces (address);
       return 0;
      }

  for (i = 0; i < 100; i++)
    a[i] = i + 10;

  return bar (level - 1) + a[0] + a[99];
}

__attribute__((noinline)) int foo()
{

  int a[100];

  int i;

  for (i = 0; i < 100; i++)
    a[i] = i + 10;


  return bar (5) + a[0] + a[99];
}


int main()
{
  int i, n;

  main_ret_address = __builtin_return_address (0);
  n = foo();

   fprintf (stderr, "return value = %d, stack_depth = %d\n", n, N);

   for (i = 0; i < N; i++)
     {
         fprintf (stderr, "Frame[%d]: addr = %p, stack_size = %d\n", i,
                  address[i], sizes[i]);

     }

   return 0;
}
