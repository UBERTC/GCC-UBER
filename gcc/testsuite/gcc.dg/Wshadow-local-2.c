/* { dg-do compile } */
/* { dg-options "-Wshadow-local" } */

struct Bar {
};

struct Bar bar;       /* { dg-bogus "shadowed declaration" } */

int val;              /* { dg-bogus "shadowed declaration" } */

int func1(int x) {    /* { dg-bogus "shadowed declaration" } */
  int val;            /* { dg-bogus "shadows a global" } */
  val = x;
  return val;
}

int func2(int i) {
  int a = 3;          /* { dg-warning "shadowed declaration" } */
  int j;              /* { dg-warning "shadowed declaration" } */

  for (j = 0; j < i; ++j) {
    int a = j;        /* { dg-warning "shadows a previous local" } */
    int j = a + 1;    /* { dg-warning "shadows a previous local" } */
    func1(j);
  }

  return a;
}

int func3() {
  int bar;            /* { dg-bogus "shadows a global" } */
  float func1 = 0.3;  /* { dg-bogus "shadows a global" } */

  if (func1 > 1)
    bar = 2;
  else
    bar = 1;
  return bar;
}

void func4() {
  struct Bar bar;     /* { dg-warning "shadowed declaration" } */
  if (val) {
    int bar;          /* { dg-warning "shadows a previous local" } */
    func1(bar);
  }
}

/* { dg-bogus "shadows a global" ""  { target *-*-* } 42 } */
