/* { dg-do compile } */
/* { dg-options "-O2 -w" } */

struct xktime {
  int tv;
};
typedef struct xktime xktime_t;

__attribute__((always_inline)) xktime_t xktime_set(void)
{
  return (xktime_t) { .tv = 0 };
}

#pragma GCC optimize ("O0")

extern void foo (xktime_t);

void LCD_WaitTE(void)
{
  foo (xktime_set());
}
