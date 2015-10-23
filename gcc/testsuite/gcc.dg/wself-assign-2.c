/* Test how self-assignment detection handles constant-folding happening */
/*   when parsing the RHS or the initializer.  */
/* { dg-do compile } */
/* { dg-options "-Wself-assign" } */

struct Bar {
  int b_;
  float c_;
};

int g;

int main()
{
  struct Bar *bar;
  int x = x - 0; /* should not warn */
  static int y;
  struct Bar b_array[5];

  b_array[x+g].b_ = b_array[x+g].b_ * 1; /* should no warn */
  g = g + 0; /* should not warn */
  y = y / 1; /* should not warn */
  bar->b_ = bar->b_ - 0; /* should not warn  */
}
