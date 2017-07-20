/* { dg-do compile { target { powerpc64*-*-* && lp64 } } } */
/* { dg-skip-if "do not override -mcpu" { powerpc*-*-* } { "-mcpu=*" } { "-mcpu=power9" } } */
/* { dg-require-effective-target powerpc_p9vector_ok } */
/* { dg-options "-mcpu=power9 -O2" } */

#include <altivec.h>

/* Optimize x = vec_insert (vec_extract (v2, N), v1, M) for SFmode.  */

vector float
foo (vector float v1, vector float v2)
{
  return vec_insert (vec_extract (v2, 4), v1, 0);
}

/* { dg-final { scan-assembler     {\mxxinsertw\M}   } } */
/* { dg-final { scan-assembler     {\mxxextractuw\M} } } */
/* { dg-final { scan-assembler-not {\mlvewx\M}       } } */
/* { dg-final { scan-assembler-not {\mlvx\M}         } } */
/* { dg-final { scan-assembler-not {\mvperm\M}       } } */
/* { dg-final { scan-assembler-not {\mvpermr\M}      } } */
/* { dg-final { scan-assembler-not {\mstfs\M}        } } */
/* { dg-final { scan-assembler-not {\mstxssp\M}      } } */
/* { dg-final { scan-assembler-not {\mstxsspx\M}     } } */
