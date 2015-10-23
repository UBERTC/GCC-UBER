/* { dg-message "note: file" "" } */
/* { dg-do compile } */
/* { dg-options "-O2 -fprofile-generate -fprofile-use -fopt-info" } */

unsigned test (unsigned a, unsigned b)
{
  return a / b;
} 
