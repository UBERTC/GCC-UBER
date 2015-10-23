/* { dg-do compile } */
/* { dg-options "-O3 -fdump-ipa-cp -fdevirtualize" } */

struct data {
    data(int);
};

struct top {
    virtual int topf();
};

struct intermediate: top {
    int topf() /* override */ { return 0; }
};

struct child1: top {
    __attribute__((noinline)) void childf()
    {
        data d(topf());
    }
};

struct child2: intermediate {};

void test(top& t)
{
    child1& c = static_cast<child1&>(t);
    c.childf();
    child2 d;
    test(d);
}

/* { dg-final { scan-ipa-dump "Type inconsistent devirtualization" "cp" } } */
/* { dg-final { cleanup-ipa-dump "cp" } } */
