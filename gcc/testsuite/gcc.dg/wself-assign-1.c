/* Test the self-assignemnt detection and warning.  */
/* { dg-do compile } */
/* { dg-options "-Wself-assign" } */

struct Bar {
  int b_;
  int c_;
};

int g;

int main()
{
  struct Bar *bar;
  int x = x; /* { dg-warning "assigned to itself" } */
  static int y;
  struct Bar b_array[5];

  b_array[x+g].b_ = b_array[x+g].b_; /* { dg-warning "assigned to itself" } */
  g = g; /* { dg-warning "assigned to itself" } */
  y = y; /* { dg-warning "assigned to itself" } */
  bar->b_ = bar->b_; /* { dg-warning "assigned to itself" } */
  x += 0; /* should not warn */
  y -= 0; /* should not warn */
  x /= x; /* should not warn */
  y *= y; /* should not warn */
}
