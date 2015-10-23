// { dg-do link}
// { dg-options "-O -fsized-delete" }
// { dg-final { scan-assembler "_ZdlPv\[mj\]" } }
struct A
{
  int a[100];
};

int main(void)
{
  A *a = new A;
  delete a;
  return 0;
}
