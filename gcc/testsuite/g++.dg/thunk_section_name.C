/* { dg-require-named-sections "" } */
/* { dg-do compile } */
/* { dg-options "-O2 -fno-reorder-blocks-and-partition -ffunction-sections" } */

class base_class_1
{
public:
  virtual void vfn () {}
};

class base_class_2
{
public:
  virtual void vfn () {}
};

class need_thunk_class : public base_class_1, public base_class_2
{
public:
  virtual void vfn () {} 
};

int main (int argc, char *argv[])
{
  base_class_1 *c = new need_thunk_class ();
  c->vfn();
  return 0;
}

/* { dg-final { scan-assembler "\.text\._ZThn\[4|8\]_N16need_thunk_class3vfnEv" } } */
