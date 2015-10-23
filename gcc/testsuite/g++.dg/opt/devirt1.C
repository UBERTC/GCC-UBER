// { dg-do compile }
// { dg-options "-O2 -fdevirtualize" }
// { dg-final { scan-assembler "xyzzy" } }

struct S { S(); virtual void xyzzy(); };
inline void foo(S *s) { s->xyzzy(); }
void bar() { S s; foo(&s); }
