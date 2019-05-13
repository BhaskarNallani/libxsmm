/******************************************************************************
** Copyright (c) 2015-2019, Intel Corporation                                **
** All rights reserved.                                                      **
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions        **
** are met:                                                                  **
** 1. Redistributions of source code must retain the above copyright         **
**    notice, this list of conditions and the following disclaimer.          **
** 2. Redistributions in binary form must reproduce the above copyright      **
**    notice, this list of conditions and the following disclaimer in the    **
**    documentation and/or other materials provided with the distribution.   **
** 3. Neither the name of the copyright holder nor the names of its          **
**    contributors may be used to endorse or promote products derived        **
**    from this software without specific prior written permission.          **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       **
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         **
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     **
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT      **
** HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    **
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED  **
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR    **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
******************************************************************************/
/* Alexander Heinecke (Intel Corp.)
******************************************************************************/
#include <libxsmm.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>


int g_reps = 0;


LIBXSMM_INLINE void print_help(void) {
  printf("\n\n");
  printf("1. Usage (dense*dense=dense, correctness and performance):\n");
  printf("    M\n");
  printf("    N\n");
  printf("    K\n");
  printf("    LDA\n");
  printf("    LDB\n");
  printf("    LDC\n");
  printf("    alpha: 1\n");
  printf("    beta: 0 or 1\n");
  printf("    0: unaligned A, otherwise aligned\n");
  printf("    0: unaligned C, otherwise aligned\n");
  printf("    0: A normal, 1: A trans\n");
  printf("    0: B normal, 1: B trans\n");
  printf("    PREFETCH: nopf (none), pfsigonly, BL2viaC, AL2, curAL2, AL2jpst, AL2_BL2viaC, curAL2_BL2viaC, AL2jpst_BL2viaC, AL1_BL1_CL1\n");
  printf("    PRECISION: SP, DP, I16I32, I16F32, BF16F32, BF16\n");
  printf("    #repetitions\n");
  printf("\n\n");
  printf("2. Usage (dense*dense=dense, performance only option available):\n");
  printf("    filename with space-sperated sizes (M N K LDA LDB LDC)\n");
  printf("    alpha: 1\n");
  printf("    beta: 0 or 1\n");
  printf("    0: unaligned A, otherwise aligned\n");
  printf("    0: unaligned C, otherwise aligned\n");
  printf("    0: A normal, 1: A trans\n");
  printf("    0: B normal, 1: B trans\n");
  printf("    PRECISION: SP, DP, I16I32, I16F32, BF16F32\n");
  printf("    #repetitions\n");
  printf("    0: no check, otherwise: run check\n");
  printf("\n\n");
}


LIBXSMM_INLINE
double run_jit_double( const libxsmm_gemm_descriptor* i_xgemm_desc,
                       const double*                  i_a,
                       const double*                  i_b,
                       double*                        o_c,
                       const unsigned int             i_print_jit_info) {
  /* define function pointer */
  libxsmm_xmmfunction l_test_jit;
  libxsmm_timer_tickint l_start;
  libxsmm_mmkernel_info l_info;
  double l_jittime, l_runtime;
  int l_t;

  if (0 == i_xgemm_desc) {
    fprintf(stderr, "JIT: unsupported descriptor arguments or data type!\n");
    return EXIT_FAILURE;
  }

  l_start = libxsmm_timer_tick();
  l_test_jit = libxsmm_xmmdispatch(i_xgemm_desc);
  l_jittime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if (l_test_jit.xmm == 0) {
    printf("JIT failed, please run with LIBXSMM_VERBOSE=-1 and/or with debug mode LIBXSMM library!\n");
    exit(EXIT_FAILURE);
  }

  /* receive kernel information */
  libxsmm_get_mmkernel_info(l_test_jit, &l_info, NULL/*code_size*/);

  l_start = libxsmm_timer_tick();
  if ( l_info.prefetch == LIBXSMM_GEMM_PREFETCH_NONE ) {
    for (l_t = 0; l_t < g_reps; l_t++) {
      l_test_jit.dmm(i_a, i_b, o_c);
    }
  } else {
    for (l_t = 0; l_t < g_reps; l_t++) {
      l_test_jit.dmm(i_a, i_b, o_c, i_a, i_b, o_c);
    }
  }
  l_runtime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if ( i_print_jit_info == 0 ) {
    printf("function pointer address: %llx\n", (unsigned long long)l_test_jit.xmm);
    printf("%fs for creating jit\n", l_jittime);
  }

  return l_runtime;
}


LIBXSMM_INLINE
double run_jit_float( const libxsmm_gemm_descriptor* i_xgemm_desc,
                      const float*                   i_a,
                      const float*                   i_b,
                      float*                         o_c,
                      const unsigned int             i_print_jit_info ) {
  /* define function pointer */
  libxsmm_xmmfunction l_test_jit;
  libxsmm_timer_tickint l_start;
  libxsmm_mmkernel_info l_info;
  double l_jittime, l_runtime;
  int l_t;

  if (0 == i_xgemm_desc) {
    fprintf(stderr, "JIT: unsupported descriptor arguments or data type!\n");
    return EXIT_FAILURE;
  }

  l_start = libxsmm_timer_tick();
  l_test_jit = libxsmm_xmmdispatch(i_xgemm_desc);
  l_jittime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if (l_test_jit.xmm == 0) {
    printf("JIT failed, please run with LIBXSMM_VERBOSE=-1 and/or with debug mode LIBXSMM library!\n");
    exit(EXIT_FAILURE);
  }

  /* receive kernel information */
  libxsmm_get_mmkernel_info(l_test_jit, &l_info, NULL/*code_size*/);

  l_start = libxsmm_timer_tick();
  if ( l_info.prefetch == LIBXSMM_GEMM_PREFETCH_NONE ) {
    for (l_t = 0; l_t < g_reps; l_t++) {
      l_test_jit.smm(i_a, i_b, o_c);
    }
  } else {
    for (l_t = 0; l_t < g_reps; l_t++) {
      l_test_jit.smm(i_a, i_b, o_c, i_a, i_b, o_c);
    }
  }
  l_runtime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if ( i_print_jit_info == 0 ) {
    printf("function pointer address: %llx\n", (unsigned long long)l_test_jit.xmm);
    printf("%fs for creating jit\n", l_jittime);
  }

  return l_runtime;
}


LIBXSMM_INLINE
double run_jit_short_int( const libxsmm_gemm_descriptor* i_xgemm_desc,
                          const short*                   i_a,
                          const short*                   i_b,
                          int*                           o_c,
                          const unsigned int             i_print_jit_info ) {
  /* define function pointer */
  libxsmm_xmmfunction l_test_jit;
  libxsmm_timer_tickint l_start;
  libxsmm_mmkernel_info l_info;
  double l_jittime, l_runtime;
  int l_t;

  if (0 == i_xgemm_desc) {
    fprintf(stderr, "JIT: unsupported descriptor arguments or data type!\n");
    return EXIT_FAILURE;
  }

  l_start = libxsmm_timer_tick();
  l_test_jit = libxsmm_xmmdispatch(i_xgemm_desc);
  l_jittime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if (l_test_jit.xmm == 0) {
    printf("JIT failed, please run with LIBXSMM_VERBOSE=-1 and/or with debug mode LIBXSMM library!\n");
    exit(EXIT_FAILURE);
  }

  /* receive kernel information */
  libxsmm_get_mmkernel_info(l_test_jit, &l_info, NULL/*code_size*/);

  l_start = libxsmm_timer_tick();
  if (l_info.prefetch == LIBXSMM_GEMM_PREFETCH_NONE ) {
    for ( l_t = 0; l_t < g_reps; l_t++ ) {
      l_test_jit.wimm(i_a, i_b, o_c, NULL, NULL, NULL);
    }
  } else {
    for ( l_t = 0; l_t < g_reps; l_t++ ) {
      l_test_jit.wimm(i_a, i_b, o_c, i_a, i_b, o_c);
    }
  }
  l_runtime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if ( i_print_jit_info == 0 ) {
    printf("function pointer address: %llx\n", (unsigned long long)l_test_jit.xmm);
    printf("%fs for creating jit\n", l_jittime);
  }

  return l_runtime;
}


LIBXSMM_INLINE
double run_jit_short_float( const libxsmm_gemm_descriptor* i_xgemm_desc,
                            const short*                   i_a,
                            const short*                   i_b,
                            float*                         o_c,
                            float*                         i_scf,
                            const unsigned int             i_print_jit_info ) {
  /* define function pointer */
  libxsmm_xmmfunction l_test_jit;
  libxsmm_timer_tickint l_start;
  libxsmm_mmkernel_info l_info;
  double l_jittime, l_runtime;
  int l_t;

  if (0 == i_xgemm_desc) {
    fprintf(stderr, "JIT: unsupported descriptor arguments or data type!\n");
    return EXIT_FAILURE;
  }

  l_start = libxsmm_timer_tick();
  l_test_jit = libxsmm_xmmdispatch(i_xgemm_desc);
  l_jittime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if (l_test_jit.xmm == 0) {
    printf("JIT failed, please run with LIBXSMM_VERBOSE=-1 and/or with debug mode LIBXSMM library!\n");
    exit(EXIT_FAILURE);
  }

  /* receive kernel information */
  libxsmm_get_mmkernel_info(l_test_jit, &l_info, NULL/*code_size*/);

  l_start = libxsmm_timer_tick();
  if (l_info.prefetch == LIBXSMM_GEMM_PREFETCH_NONE ) {
    for ( l_t = 0; l_t < g_reps; l_t++ ) {
      l_test_jit.wsmm(i_a, i_b, o_c, NULL, NULL, NULL, i_scf);
    }
  } else {
    for ( l_t = 0; l_t < g_reps; l_t++ ) {
      l_test_jit.wsmm(i_a, i_b, o_c, i_a, i_b, o_c, i_scf);
    }
  }
  l_runtime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if ( i_print_jit_info == 0 ) {
    printf("function pointer address: %llx\n", (unsigned long long)l_test_jit.xmm);
    printf("%fs for creating jit\n", l_jittime);
  }

  return l_runtime;
}


LIBXSMM_INLINE
double run_jit_bfloat16_float( const libxsmm_gemm_descriptor* i_xgemm_desc,
                               const libxsmm_bfloat16*        i_a,
                               const libxsmm_bfloat16*        i_b,
                               float*                         o_c,
                               const unsigned int             i_print_jit_info ) {
  /* define function pointer */
  libxsmm_xmmfunction l_test_jit;
  libxsmm_timer_tickint l_start;
  libxsmm_mmkernel_info l_info;
  double l_jittime, l_runtime;
  int l_t;

  if (0 == i_xgemm_desc) {
    fprintf(stderr, "JIT: unsupported descriptor arguments or data type!\n");
    return EXIT_FAILURE;
  }

  l_start = libxsmm_timer_tick();
  l_test_jit = libxsmm_xmmdispatch(i_xgemm_desc);
  l_jittime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if (l_test_jit.xmm == 0) {
    printf("JIT failed, please run with LIBXSMM_VERBOSE=-1 and/or with debug mode LIBXSMM library!\n");
    exit(EXIT_FAILURE);
  }

  /* receive kernel information */
  libxsmm_get_mmkernel_info(l_test_jit, &l_info, NULL/*code_size*/);

  l_start = libxsmm_timer_tick();
  if (l_info.prefetch == LIBXSMM_GEMM_PREFETCH_NONE ) {
    for ( l_t = 0; l_t < g_reps; l_t++ ) {
      l_test_jit.bsmm(i_a, i_b, o_c, NULL, NULL, NULL);
    }
  } else {
    for ( l_t = 0; l_t < g_reps; l_t++ ) {
      l_test_jit.bsmm(i_a, i_b, o_c, i_a, i_b, o_c);
    }
  }
  l_runtime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if ( i_print_jit_info == 0 ) {
    printf("function pointer address: %llx\n", (unsigned long long)l_test_jit.xmm);
    printf("%fs for creating jit\n", l_jittime);
  }

  return l_runtime;
}


LIBXSMM_INLINE
double run_jit_bfloat16( const libxsmm_gemm_descriptor* i_xgemm_desc,
                         const libxsmm_bfloat16*        i_a,
                         const libxsmm_bfloat16*        i_b,
                               libxsmm_bfloat16*        o_c,
                         const unsigned int             i_print_jit_info ) {
  /* define function pointer */
  libxsmm_xmmfunction l_test_jit;
  libxsmm_timer_tickint l_start;
  libxsmm_mmkernel_info l_info;
  double l_jittime, l_runtime;
  int l_t;

  if (0 == i_xgemm_desc) {
    fprintf(stderr, "JIT: unsupported descriptor arguments or data type!\n");
    return EXIT_FAILURE;
  }

  l_start = libxsmm_timer_tick();
  l_test_jit = libxsmm_xmmdispatch(i_xgemm_desc);
  l_jittime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if (l_test_jit.xmm == 0) {
    printf("JIT failed, please run with LIBXSMM_VERBOSE=-1 and/or with debug mode LIBXSMM library!\n");
    exit(EXIT_FAILURE);
  }

  /* receive kernel information */
  libxsmm_get_mmkernel_info(l_test_jit, &l_info, NULL/*code_size*/);

  l_start = libxsmm_timer_tick();
  if (l_info.prefetch == LIBXSMM_GEMM_PREFETCH_NONE ) {
    for ( l_t = 0; l_t < g_reps; l_t++ ) {
      l_test_jit.bmm(i_a, i_b, o_c, NULL, NULL, NULL);
    }
  } else {
    for ( l_t = 0; l_t < g_reps; l_t++ ) {
      l_test_jit.bmm(i_a, i_b, o_c, i_a, i_b, o_c);
    }
  }
  l_runtime = libxsmm_timer_duration(l_start, libxsmm_timer_tick());

  if ( i_print_jit_info == 0 ) {
    printf("function pointer address: %llx\n", (unsigned long long)l_test_jit.xmm);
    printf("%fs for creating jit\n", l_jittime);
  }

  return l_runtime;
}

int main(int argc, char* argv []) {
  char* l_precision = NULL;
  libxsmm_blasint l_lda = 0, l_ldb = 0, l_ldc = 0;
  int l_m = 0, l_n = 0, l_k = 0;
  int l_aligned_a = 0;
  int l_aligned_c = 0;
  int l_trans_a = 0;
  int l_trans_b = 0;
  double l_alpha = 0;
  double l_beta = 0;

  int l_flags = LIBXSMM_GEMM_FLAGS('N', 'N');
  libxsmm_gemm_prefetch_type l_prefetch = LIBXSMM_GEMM_PREFETCH_NONE;
  const libxsmm_gemm_descriptor* l_xgemm_desc = 0;
  libxsmm_descriptor_blob l_xgemm_blob;
  libxsmm_matdiff_info l_diff;
  int l_i = 0, l_j = 0, l_s = 0, l_t = 0;
  double l_runtime_c = 0;
  double l_runtime_libxsmm = 0;
  libxsmm_timer_tickint l_start;
  int l_file_input = 0;
  char* l_file_name = NULL;
  FILE *l_file_handle = NULL;
  int l_run_check = 0;

  /* input data */
  double *l_a_d = 0, *l_b_d = 0, *l_c_d = 0;
  float *l_a_f = 0, *l_b_f = 0, *l_c_f = 0;
  short *l_a_w = 0, *l_b_w = 0;
  libxsmm_bfloat16 *l_a_bf = 0, *l_b_bf = 0, *l_c_bf = 0;
  unsigned char* l_a_b = 0;
  char* l_b_b = 0;
  int* l_c_b = 0;
  float* l_c_w_f = 0;
  int* l_c_w_i = 0;
  /* Gold data */
  double* l_c_gold_d = 0;
  float* l_c_gold_f = 0;
  int* l_c_gold_b = 0;
  float* l_c_gold_w_f = 0;
  libxsmm_bfloat16* l_c_gold_bf = 0;
  unsigned char exp_a = 0, exp_b = 0;
  float l_scf = libxsmm_sexp2(-1.f*((float)exp_a + (float)exp_b));
  /*l_scf = 1000;*/
  int* l_c_gold_w_i = 0;
  double l_total_max_error = 0.0;

  libxsmm_matdiff_clear(&l_diff);

  /* check argument count for a valid range */
  if ( argc != 16 && argc != 11 ) {
    print_help();
    return EXIT_FAILURE;
  }

  if ( argc == 16 ) {
    /* xgemm sizes */
    l_m = atoi(argv[1]);
    l_n = atoi(argv[2]);
    l_k = atoi(argv[3]);
    l_lda = atoi(argv[4]);
    l_ldb = atoi(argv[5]);
    l_ldc = atoi(argv[6]);

    /* some sugar */
    l_alpha = atof(argv[7]);
    l_beta = atof(argv[8]);
    l_aligned_a = atoi(argv[9]);
    l_aligned_c = atoi(argv[10]);
    l_trans_a = atoi(argv[11]);
    l_trans_b = atoi(argv[12]);

    /* arch specific stuff */
    l_precision = argv[14];
    g_reps = atoi(argv[15]);

    /* set value of prefetch flag */
    if (strcmp("nopf", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_NONE;
    }
    else if (strcmp("pfsigonly", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_SIGONLY;
    }
    else if (strcmp("BL2viaC", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_BL2_VIA_C;
    }
    else if (strcmp("curAL2", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_AL2_AHEAD;
    }
    else if (strcmp("curAL2_BL2viaC", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD;
    }
    else if (strcmp("AL2", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_AL2;
    }
    else if (strcmp("AL2_BL2viaC", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C;
    }
    else if (strcmp("AL2jpst", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_AL2_JPST;
    }
    else if (strcmp("AL2jpst_BL2viaC", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_JPST;
    }
    else if (strcmp("AL1_BL1_CL1", argv[13]) == 0) {
      l_prefetch = LIBXSMM_GEMM_PREFETCH_AL1_BL1_CL1;
    }
    else {
      print_help();
      return EXIT_FAILURE;
    }

    l_file_input = 0;
    l_run_check = 1;
  } else {
    l_file_input = 1;
    l_file_name = argv[1];
    l_alpha = atof(argv[2]);
    l_beta = atof(argv[3]);
    l_aligned_a = atoi(argv[4]);
    l_aligned_c = atoi(argv[5]);
    l_trans_a = atoi(argv[6]);
    l_trans_b = atoi(argv[7]);
    l_precision = argv[8];
    g_reps = atoi(argv[9]);
    l_run_check = atoi(argv[10]);
    l_prefetch = LIBXSMM_GEMM_PREFETCH_NONE;
  }

  if ( l_trans_b != 0 ) {
    l_flags |= LIBXSMM_GEMM_FLAG_TRANS_B;
  }
  if ( l_trans_a != 0 ) {
    fprintf(stderr, "trans_a needs to be 0\n");
    return EXIT_FAILURE;
  }

  l_flags |= (0 != l_aligned_a ? LIBXSMM_GEMM_FLAG_ALIGN_A : 0);
  l_flags |= (0 != l_aligned_c ? LIBXSMM_GEMM_FLAG_ALIGN_C : 0);

  /* check alpha */
  if ( LIBXSMM_NEQ(l_alpha, 1.0) ) {
    fprintf(stderr, "JIT: alpha needs to be 1.0!\n");
    exit(EXIT_FAILURE);
  }

  /* check beta */
  if ( LIBXSMM_NEQ(l_beta, 0.0) && LIBXSMM_NEQ(l_beta, 1.0) ) {
    fprintf(stderr, "JIT: beta needs to be 0.0 or 1.0!\n");
    exit(EXIT_FAILURE);
  }

  if ( l_file_input != 0 ) {
    l_file_handle = fopen( l_file_name, "r" );
  } else {
    if ( l_trans_b == 0 ) {
      printf("------------------------------------------------\n");
      printf("RUNNING (%ix%i) X (%ix%i) = (%ix%i), %s\n", l_m, l_k, l_k, l_n, l_m, l_n, l_precision);
      printf("------------------------------------------------\n");
    } else {
      printf("------------------------------------------------\n");
      printf("RUNNING (%ix%i) X (%ix%i)^T = (%ix%i), %s\n", l_m, l_k, l_k, l_n, l_m, l_n, l_precision);
      printf("------------------------------------------------\n");
    }
  }

  if ((strcmp(l_precision, "DP") == 0) && (l_trans_b == 0)) {
    unsigned int l_keep_going = 0;
    do {
      if ( l_file_input != 0 ) {
        char l_line[512];
        if ( fgets( l_line, 512, l_file_handle) == NULL ) {
          l_keep_going = 0;
          break;
        } else {
          l_keep_going = 1;
        }
        if ( 6 != sscanf( l_line, "%i %i %i %i %i %i", &l_m, &l_n, &l_k, &l_lda, &l_ldb, &l_ldc ) ) exit(EXIT_FAILURE);
      }
      l_xgemm_desc = libxsmm_gemm_descriptor_dinit(&l_xgemm_blob, LIBXSMM_GEMM_PRECISION_F64,
        l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags,
        /* translate an eventual LIBXSMM_PREFETCH_AUTO */
        libxsmm_get_gemm_prefetch(l_prefetch));
      l_a_d = (double*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(double), 64);
      l_b_d = (double*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_n * sizeof(double), 64);
      l_c_d = (double*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(double), 64);
      l_c_gold_d = (double*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(double), 64);
      /* touch A */
      for (l_i = 0; l_i < l_lda; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          l_a_d[(l_j * l_lda) + l_i] = libxsmm_rng_f64();
        }
      }
      /* touch B */
      for (l_i = 0; l_i < l_ldb; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_b_d[(l_j * l_ldb) + l_i] = libxsmm_rng_f64();
        }
      }
      /* touch C */
      for (l_i = 0; l_i < l_ldc; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_c_d[(l_j * l_ldc) + l_i] = 0.0;
          l_c_gold_d[(l_j * l_ldc) + l_i] = 0.0;
        }
      }

      l_runtime_libxsmm = run_jit_double( l_xgemm_desc, l_a_d, l_b_d, l_c_d, l_file_input );

      if ( l_run_check == 1 ) {
        l_start = libxsmm_timer_tick();
        for (l_t = 0; l_t < g_reps; l_t++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            for (l_s = 0; l_s < l_k; l_s++) {
              for (l_i = 0; l_i < l_m; l_i++) {
                l_c_gold_d[(l_j * l_ldc) + l_i] += l_a_d[(l_s * l_lda) + l_i] * l_b_d[(l_j * l_ldb) + l_s];
              }
            }
          }
        }
        l_runtime_c = libxsmm_timer_duration(l_start, libxsmm_timer_tick());
        libxsmm_matdiff(&l_diff, LIBXSMM_DATATYPE_F64, l_m, l_n, l_c_gold_d, l_c_d, &l_ldc, &l_ldc);
      }

      if ( l_file_input == 0 ) {
        printf("%fs for C\n", l_runtime_c);
        printf("%f GFLOPS for C\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_c * 1.0e9));
        printf("%fs for libxsmm\n", l_runtime_libxsmm);
        printf("%f GFLOPS for libxsmm\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9));
        printf("max. error: %f\n", l_diff.linf_abs);
      } else {
        if ( l_run_check == 1 ) {
          printf("%i %i %i %i %i %i %f %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9), l_diff.linf_abs );
        } else {
          printf("%i %i %i %i %i %i %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9) );
        }
      }

      if ( (l_total_max_error < l_diff.linf_abs) && (l_run_check == 1) ) {
        l_total_max_error = l_diff.linf_abs;
      }

      libxsmm_free(l_a_d);
      libxsmm_free(l_b_d);
      libxsmm_free(l_c_d);
      libxsmm_free(l_c_gold_d);
    } while ( l_keep_going );
  }
  else if ((strcmp(l_precision, "DP") == 0) && (l_trans_b != 0)) {
    unsigned int l_keep_going = 0;
    do {
      if ( l_file_input != 0 ) {
        char l_line[512];
        if ( fgets( l_line, 512, l_file_handle) == NULL ) {
          l_keep_going = 0;
          break;
        } else {
          l_keep_going = 1;
        }
        if ( 6 != sscanf( l_line, "%i %i %i %i %i %i", &l_m, &l_n, &l_k, &l_lda, &l_ldb, &l_ldc ) ) exit(EXIT_FAILURE);
      }
      l_xgemm_desc = libxsmm_gemm_descriptor_dinit(&l_xgemm_blob, LIBXSMM_GEMM_PRECISION_F64,
        l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags,
        /* translate an eventual LIBXSMM_PREFETCH_AUTO */
        libxsmm_get_gemm_prefetch(l_prefetch));
      l_a_d = (double*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(double), 64);
      l_b_d = (double*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_k * sizeof(double), 64);
      l_c_d = (double*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(double), 64);
      l_c_gold_d = (double*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(double), 64);
      /* touch A */
      for (l_i = 0; l_i < l_lda; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          l_a_d[(l_j * l_lda) + l_i] = libxsmm_rng_f64();
        }
      }
      /* touch B */
      for (l_i = 0; l_i < l_ldb; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          l_b_d[(l_j * l_ldb) + l_i] = libxsmm_rng_f64();
        }
      }
      /* touch C */
      for (l_i = 0; l_i < l_ldc; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_c_d[(l_j * l_ldc) + l_i] = 0.0;
          l_c_gold_d[(l_j * l_ldc) + l_i] = 0.0;
        }
      }

      l_runtime_libxsmm = run_jit_double( l_xgemm_desc, l_a_d, l_b_d, l_c_d, l_file_input );

      if ( l_run_check == 1 ) {
        l_start = libxsmm_timer_tick();
        for (l_t = 0; l_t < g_reps; l_t++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            for (l_s = 0; l_s < l_k; l_s++) {
              for (l_i = 0; l_i < l_m; l_i++) {
                l_c_gold_d[(l_j * l_ldc) + l_i] += l_a_d[(l_s * l_lda) + l_i] * l_b_d[(l_s * l_ldb) + l_j];
              }
            }
          }
        }
        l_runtime_c = libxsmm_timer_duration(l_start, libxsmm_timer_tick());
        libxsmm_matdiff(&l_diff, LIBXSMM_DATATYPE_F64, l_m, l_n, l_c_gold_d, l_c_d, &l_ldc, &l_ldc);
      }

      if ( l_file_input == 0 ) {
        printf("%fs for C\n", l_runtime_c);
        printf("%f GFLOPS for C\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_c * 1.0e9));
        printf("%fs for libxsmm\n", l_runtime_libxsmm);
        printf("%f GFLOPS for libxsmm\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9));
        printf("max. error: %f\n", l_diff.linf_abs);
      } else {
        if ( l_run_check == 1 ) {
          printf("%i %i %i %i %i %i %f %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9), l_diff.linf_abs );
        } else {
          printf("%i %i %i %i %i %i %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9) );
        }
      }

      if ( (l_total_max_error < l_diff.linf_abs) && (l_run_check == 1) ) {
        l_total_max_error = l_diff.linf_abs;
      }

      libxsmm_free(l_a_d);
      libxsmm_free(l_b_d);
      libxsmm_free(l_c_d);
      libxsmm_free(l_c_gold_d);
    } while ( l_keep_going );
  }
  else if ((strcmp(l_precision, "SP") == 0) && (l_trans_b == 0)) {
    unsigned int l_keep_going = 0;
    do {
      if ( l_file_input != 0 ) {
        char l_line[512];
        if ( fgets( l_line, 512, l_file_handle) == NULL ) {
          l_keep_going = 0;
          break;
        } else {
          l_keep_going = 1;
        }
        if ( 6 != sscanf( l_line, "%i %i %i %i %i %i", &l_m, &l_n, &l_k, &l_lda, &l_ldb, &l_ldc ) ) exit(EXIT_FAILURE);
      }
      l_xgemm_desc = libxsmm_gemm_descriptor_dinit(&l_xgemm_blob, LIBXSMM_GEMM_PRECISION_F32,
        l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags, l_prefetch);
      l_a_f = (float*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(float), 64);
      l_b_f = (float*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_n * sizeof(float), 64);
      l_c_f = (float*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(float), 64);
      l_c_gold_f = (float*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(float), 64);
      /* touch A */
      for (l_i = 0; l_i < l_lda; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          l_a_f[(l_j * l_lda) + l_i] = (float)libxsmm_rng_f64();
        }
      }
      /* touch B */
      for (l_i = 0; l_i < l_ldb; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_b_f[(l_j * l_ldb) + l_i] = (float)libxsmm_rng_f64();
        }
      }
      /* touch C */
      for (l_i = 0; l_i < l_ldc; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_c_f[(l_j * l_ldc) + l_i] = 0.f;
          l_c_gold_f[(l_j * l_ldc) + l_i] = 0.f;
        }
      }

      l_runtime_libxsmm = run_jit_float( l_xgemm_desc, l_a_f, l_b_f, l_c_f, l_file_input );

      if ( l_run_check == 1 ) {
        l_start = libxsmm_timer_tick();
        for (l_t = 0; l_t < g_reps; l_t++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            for (l_s = 0; l_s < l_k; l_s++) {
              for (l_i = 0; l_i < l_m; l_i++) {
                l_c_gold_f[(l_j * l_ldc) + l_i] += l_a_f[(l_s * l_lda) + l_i] * l_b_f[(l_j * l_ldb) + l_s];
              }
            }
          }
        }
        l_runtime_c = libxsmm_timer_duration(l_start, libxsmm_timer_tick());
        libxsmm_matdiff(&l_diff, LIBXSMM_DATATYPE_F32, l_m, l_n, l_c_gold_f, l_c_f, &l_ldc, &l_ldc);
      }

      if ( l_file_input == 0 ) {
        printf("%fs for C\n", l_runtime_c);
        printf("%f GFLOPS for C\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_c * 1.0e9));
        printf("%fs for libxsmm\n", l_runtime_libxsmm);
        printf("%f GFLOPS for libxsmm\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9));
        printf("max. error: %f\n", l_diff.linf_abs);
      } else {
        if ( l_run_check == 1 ) {
          printf("%i %i %i %i %i %i %f %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9), l_diff.linf_abs );
        } else {
          printf("%i %i %i %i %i %i %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9) );
        }
      }

      if ( (l_total_max_error < l_diff.linf_abs) && (l_run_check == 1) ) {
        l_total_max_error = l_diff.linf_abs;
      }

      libxsmm_free(l_a_f);
      libxsmm_free(l_b_f);
      libxsmm_free(l_c_f);
      libxsmm_free(l_c_gold_f);
    } while ( l_keep_going );
  }
  else if ((strcmp(l_precision, "SP") == 0) && (l_trans_b != 0)) {
    unsigned int l_keep_going = 0;
    do {
      if ( l_file_input != 0 ) {
        char l_line[512];
        if ( fgets( l_line, 512, l_file_handle) == NULL ) {
          l_keep_going = 0;
          break;
        } else {
          l_keep_going = 1;
        }
        if ( 6 != sscanf( l_line, "%i %i %i %i %i %i", &l_m, &l_n, &l_k, &l_lda, &l_ldb, &l_ldc ) ) exit(EXIT_FAILURE);
      }
      l_xgemm_desc = libxsmm_gemm_descriptor_dinit(&l_xgemm_blob, LIBXSMM_GEMM_PRECISION_F32,
        l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags, l_prefetch);
      l_a_f = (float*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(float), 64);
      l_b_f = (float*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_k * sizeof(float), 64);
      l_c_f = (float*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(float), 64);
      l_c_gold_f = (float*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(float), 64);
      /* touch A */
      for (l_i = 0; l_i < l_lda; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          l_a_f[(l_j * l_lda) + l_i] = (float)libxsmm_rng_f64();
        }
      }
      /* touch B */
      for (l_i = 0; l_i < l_ldb; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          l_b_f[(l_j * l_ldb) + l_i] = (float)libxsmm_rng_f64();
        }
      }
      /* touch C */
      for (l_i = 0; l_i < l_ldc; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_c_f[(l_j * l_ldc) + l_i] = 0.f;
          l_c_gold_f[(l_j * l_ldc) + l_i] = 0.f;
        }
      }

      l_runtime_libxsmm = run_jit_float( l_xgemm_desc, l_a_f, l_b_f, l_c_f, l_file_input );

      if ( l_run_check == 1 ) {
        l_start = libxsmm_timer_tick();
        for (l_t = 0; l_t < g_reps; l_t++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            for (l_s = 0; l_s < l_k; l_s++) {
              for (l_i = 0; l_i < l_m; l_i++) {
                l_c_gold_f[(l_j * l_ldc) + l_i] += l_a_f[(l_s * l_lda) + l_i] * l_b_f[(l_s * l_ldb) + l_j];
              }
            }
          }
        }
        l_runtime_c = libxsmm_timer_duration(l_start, libxsmm_timer_tick());
        libxsmm_matdiff(&l_diff, LIBXSMM_DATATYPE_F32, l_m, l_n, l_c_gold_f, l_c_f, &l_ldc, &l_ldc);
      }

      if ( l_file_input == 0 ) {
        printf("%fs for C\n", l_runtime_c);
        printf("%f GFLOPS for C\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_c * 1.0e9));
        printf("%fs for libxsmm\n", l_runtime_libxsmm);
        printf("%f GFLOPS for libxsmm\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9));
        printf("max. error: %f\n", l_diff.linf_abs);
      } else {
        if ( l_run_check == 1 ) {
          printf("%i %i %i %i %i %i %f %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9), l_diff.linf_abs );
        } else {
          printf("%i %i %i %i %i %i %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9) );
        }
      }

      if ( (l_total_max_error < l_diff.linf_abs) && (l_run_check == 1) ) {
        l_total_max_error = l_diff.linf_abs;
      }

      libxsmm_free(l_a_f);
      libxsmm_free(l_b_f);
      libxsmm_free(l_c_f);
      libxsmm_free(l_c_gold_f);
    } while ( l_keep_going );
  } else if (strcmp(l_precision, "I16I32") == 0) {
    const int l_k_block = 2;
    double l_max_error = 0;
    int l_k2;
    unsigned int l_keep_going = 0;
    do {
      if ( l_file_input != 0 ) {
        char l_line[512];
        if ( fgets( l_line, 512, l_file_handle) == NULL ) {
          l_keep_going = 0;
          break;
        } else {
          l_keep_going = 1;
        }
        if ( 6 != sscanf( l_line, "%i %i %i %i %i %i", &l_m, &l_n, &l_k, &l_lda, &l_ldb, &l_ldc ) ) exit(EXIT_FAILURE);
      }
      l_xgemm_desc = libxsmm_gemm_descriptor_dinit2(&l_xgemm_blob,
        LIBXSMM_GEMM_PRECISION_I16, LIBXSMM_GEMM_PRECISION_I32,
        l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags, l_prefetch);
      l_a_w = (short*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(short), 64);
      l_b_w = (short*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_n * sizeof(short), 64);
      l_c_w_i = (int*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(int), 64);
      l_c_gold_w_i = (int*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(int), 64);

      /* touch A */
      for (l_i = 0; l_i < l_lda; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          l_a_w[(l_j * l_lda) + l_i] = (short)(libxsmm_rng_f64() * 10.0);
        }
      }
      /* touch B */
      for (l_i = 0; l_i < l_ldb; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_b_w[(l_j * l_ldb) + l_i] = (short)(libxsmm_rng_f64() * 10.0);
        }
      }
      /* touch C */
      for (l_i = 0; l_i < l_ldc; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_c_w_i[(l_j * l_ldc) + l_i] = 0;
          l_c_gold_w_i[(l_j * l_ldc) + l_i] = 0;
        }
      }

      l_runtime_libxsmm = run_jit_short_int(l_xgemm_desc, l_a_w, l_b_w, l_c_w_i, l_file_input );

      if ( l_run_check == 1 ) {
        l_start = libxsmm_timer_tick();
        for (l_t = 0; l_t < g_reps; l_t++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            for (l_s = 0; l_s < (l_k / l_k_block); l_s++) {
              for (l_i = 0; l_i < l_m; l_i++) {
                for (l_k2 = 0; l_k2 < l_k_block; l_k2++) {
                  l_c_gold_w_i[(l_j * l_ldc) + l_i] += l_a_w[(l_s * (l_lda*l_k_block)) + (l_i*l_k_block) + l_k2] * l_b_w[(l_j * l_ldb) + (l_s*l_k_block) + l_k2];
                }
              }
            }
          }
        }
        l_runtime_c = libxsmm_timer_duration(l_start, libxsmm_timer_tick());
        l_max_error = 0;
        for (l_i = 0; l_i < l_m; l_i++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            const double l_fabs = fabs((double)l_c_gold_w_i[(l_j * l_ldc) + l_i] - (double)l_c_w_i[(l_j * l_ldc) + l_i]);
            if (l_max_error < l_fabs) l_max_error = l_fabs;
          }
        }
      }

      if ( l_file_input == 0 ) {
        printf("%fs for C\n", l_runtime_c);
        printf("%f GFLOPS for C\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_c * 1.0e9));
        printf("%fs for libxsmm\n", l_runtime_libxsmm);
        printf("%f GFLOPS for libxsmm\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9));
        printf("max. error: %f\n", l_max_error);
      } else {
        if ( l_run_check == 1 ) {
          printf("%i %i %i %i %i %i %f %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9), l_max_error );
        } else {
          printf("%i %i %i %i %i %i %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9) );
        }
      }

      if ( (l_total_max_error < l_max_error) && (l_run_check == 1) ) {
        l_total_max_error = l_max_error;
      }

      libxsmm_free(l_a_w);
      libxsmm_free(l_b_w);
      libxsmm_free(l_c_w_i);
      libxsmm_free(l_c_gold_w_i);
    } while ( l_keep_going );
  } else if (strcmp(l_precision, "I16F32") == 0) {
    const int l_k_block = 2;
    double l_max_error = 0;
    int l_k2;
    unsigned int l_keep_going = 0;
    do {
      if ( l_file_input != 0 ) {
        char l_line[512];
        if ( fgets( l_line, 512, l_file_handle) == NULL ) {
          l_keep_going = 0;
          break;
        } else {
          l_keep_going = 1;
        }
        if ( 6 != sscanf( l_line, "%i %i %i %i %i %i", &l_m, &l_n, &l_k, &l_lda, &l_ldb, &l_ldc ) ) exit(EXIT_FAILURE);
      }
      l_xgemm_desc = libxsmm_gemm_descriptor_dinit2(&l_xgemm_blob,
        LIBXSMM_GEMM_PRECISION_I16, LIBXSMM_GEMM_PRECISION_F32,
        l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags, l_prefetch);
      l_a_w = (short*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(short), 64);
      l_b_w = (short*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_n * sizeof(short), 64);
      l_c_w_f = (float*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(float), 64);
      l_c_gold_w_f = (float*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(float), 64);
      /* touch A */
      for (l_i = 0; l_i < l_lda; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          l_a_w[(l_j * l_lda) + l_i] = (short)(libxsmm_rng_f64() * 10.0);
        }
      }
      /* touch B */
      for (l_i = 0; l_i < l_ldb; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_b_w[(l_j * l_ldb) + l_i] = (short)(libxsmm_rng_f64() * 10.0);
        }
      }
      /* touch C */
      for (l_i = 0; l_i < l_ldc; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_c_w_f[(l_j * l_ldc) + l_i] = 0.0f;
          l_c_gold_w_f[(l_j * l_ldc) + l_i] = 0.0f;
        }
      }

      l_runtime_libxsmm = run_jit_short_float(l_xgemm_desc, l_a_w, l_b_w, l_c_w_f, &l_scf, l_file_input );

      if ( l_run_check == 1 ) {
        l_start = libxsmm_timer_tick();
        for (l_t = 0; l_t < g_reps; l_t++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            for (l_s = 0; l_s < (l_k / l_k_block); l_s++) {
              for (l_i = 0; l_i < l_m; l_i++) {
                for (l_k2 = 0; l_k2 < l_k_block; l_k2++) {
                  const int iprod = (int)l_a_w[(l_s * (l_lda*l_k_block)) + (l_i*l_k_block) + l_k2] * (int)l_b_w[(l_j * l_ldb) + (l_s*l_k_block) + l_k2];
                  const float fprod = (float)iprod;
                  l_c_gold_w_f[(l_j * l_ldc) + l_i] += fprod * l_scf;
                }
              }
            }
          }
        }
        l_runtime_c = libxsmm_timer_duration(l_start, libxsmm_timer_tick());
        l_max_error = 0;
        for (l_i = 0; l_i < l_m; l_i++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            const double l_fabs = fabs((double)l_c_gold_w_f[(l_j * l_ldc) + l_i] - (double)l_c_w_f[(l_j * l_ldc) + l_i]);
            if (l_max_error < l_fabs) l_max_error = l_fabs;
          }
        }
      }

      if ( l_file_input == 0 ) {
        printf("%fs for C\n", l_runtime_c);
        printf("%f GFLOPS for C\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_c * 1.0e9));
        printf("%fs for libxsmm\n", l_runtime_libxsmm);
        printf("%f GFLOPS for libxsmm\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9));
        printf("max. error: %f\n", l_max_error);
      } else {
        if ( l_run_check == 1 ) {
          printf("%i %i %i %i %i %i %f %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9), l_max_error );
        } else {
          printf("%i %i %i %i %i %i %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9) );
        }
      }

      if ( (l_total_max_error < l_max_error) && (l_run_check == 1) ) {
        l_total_max_error = l_max_error;
      }

      libxsmm_free(l_a_w);
      libxsmm_free(l_b_w);
      libxsmm_free(l_c_w_f);
      libxsmm_free(l_c_gold_w_f);
    } while ( l_keep_going );
  } else if (strcmp(l_precision, "BF16F32") == 0) {
    const int l_k_block = 2;
    double l_max_error = 0;
    int l_k2;
    unsigned int l_keep_going = 0;
    do {
      if ( l_file_input != 0 ) {
        char l_line[512];
        if ( fgets( l_line, 512, l_file_handle) == NULL ) {
          l_keep_going = 0;
          break;
        } else {
          l_keep_going = 1;
        }
        if ( 6 != sscanf( l_line, "%i %i %i %i %i %i", &l_m, &l_n, &l_k, &l_lda, &l_ldb, &l_ldc ) ) exit(EXIT_FAILURE);
      }
      l_xgemm_desc = libxsmm_gemm_descriptor_dinit2(&l_xgemm_blob,
        LIBXSMM_GEMM_PRECISION_BF16, LIBXSMM_GEMM_PRECISION_F32,
        l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags, l_prefetch);
      l_a_bf = (libxsmm_bfloat16*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(libxsmm_bfloat16), 64);
      l_b_bf = (libxsmm_bfloat16*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_n * sizeof(libxsmm_bfloat16), 64);
      l_c_w_f = (float*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(float), 64);
      l_c_gold_w_f = (float*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(float), 64);
      /* touch A */
      for (l_i = 0; l_i < l_lda; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          union libxsmm_bfloat16_hp tmp;
          tmp.f = (float)libxsmm_rng_f64();
          l_a_bf[(l_j * l_lda) + l_i] = tmp.i[1];
        }
      }
      /* touch B */
      for (l_i = 0; l_i < l_ldb; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          union libxsmm_bfloat16_hp tmp;
          tmp.f = (float)libxsmm_rng_f64();
          l_b_bf[(l_j * l_ldb) + l_i] = tmp.i[1];
        }
      }
      /* touch C */
      for (l_i = 0; l_i < l_ldc; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          l_c_w_f[(l_j * l_ldc) + l_i] = 0.0f;
          l_c_gold_w_f[(l_j * l_ldc) + l_i] = 0.0f;
        }
      }

      l_runtime_libxsmm = run_jit_bfloat16_float(l_xgemm_desc, l_a_bf, l_b_bf, l_c_w_f, l_file_input );

      if ( l_run_check == 1 ) {
        l_start = libxsmm_timer_tick();
        for (l_t = 0; l_t < g_reps; l_t++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            for (l_s = 0; l_s < (l_k / l_k_block); l_s++) {
              for (l_i = 0; l_i < l_m; l_i++) {
                for (l_k2 = 0; l_k2 < l_k_block; l_k2++) {
                  union libxsmm_bfloat16_hp tmp_a_f;
                  union libxsmm_bfloat16_hp tmp_b_f;
                  tmp_a_f.i[1] = l_a_bf[(l_s * (l_lda*l_k_block)) + (l_i*l_k_block) + l_k2];
                  tmp_a_f.i[0] = 0;
                  tmp_b_f.i[1] = l_b_bf[(l_j * l_ldb) + (l_s*l_k_block) + l_k2];
                  tmp_b_f.i[0] = 0;
                  l_c_gold_w_f[(l_j * l_ldc) + l_i] += (float)(tmp_a_f.f * tmp_b_f.f);
                }
              }
            }
          }
        }
        l_runtime_c = libxsmm_timer_duration(l_start, libxsmm_timer_tick());
        l_max_error = 0;
        for (l_i = 0; l_i < l_m; l_i++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            const double l_fabs = fabs((double)l_c_gold_w_f[(l_j * l_ldc) + l_i] - (double)l_c_w_f[(l_j * l_ldc) + l_i]);
            if (l_max_error < l_fabs) l_max_error = l_fabs;
          }
        }
      }

      if ( l_file_input == 0 ) {
        printf("%fs for C\n", l_runtime_c);
        printf("%f GFLOPS for C\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_c * 1.0e9));
        printf("%fs for libxsmm\n", l_runtime_libxsmm);
        printf("%f GFLOPS for libxsmm\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9));
        printf("max. error: %f\n", l_max_error);
      } else {
        if ( l_run_check == 1 ) {
          printf("%i %i %i %i %i %i %f %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9), l_max_error );
        } else {
          printf("%i %i %i %i %i %i %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9) );
        }
      }

      if ( (l_total_max_error < l_max_error) && (l_run_check == 1) ) {
        l_total_max_error = l_max_error;
      }

      libxsmm_free(l_a_bf);
      libxsmm_free(l_b_bf);
      libxsmm_free(l_c_w_f);
      libxsmm_free(l_c_gold_w_f);
    } while ( l_keep_going );
  } else if (strcmp(l_precision, "BF16") == 0) {
    const int l_k_block = 2;
    double l_max_error = 0;
    int l_k2;
    unsigned int l_keep_going = 0;
    do {
      if ( l_file_input != 0 ) {
        char l_line[512];
        if ( fgets( l_line, 512, l_file_handle) == NULL ) {
          l_keep_going = 0;
          break;
        } else {
          l_keep_going = 1;
        }
        if ( 6 != sscanf( l_line, "%i %i %i %i %i %i", &l_m, &l_n, &l_k, &l_lda, &l_ldb, &l_ldc ) ) exit(EXIT_FAILURE);
      }
      l_xgemm_desc = libxsmm_gemm_descriptor_dinit2(&l_xgemm_blob,
        LIBXSMM_GEMM_PRECISION_BF16, LIBXSMM_GEMM_PRECISION_BF16,
        l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags, l_prefetch);
      l_a_bf = (libxsmm_bfloat16*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(libxsmm_bfloat16), 64);
      l_b_bf = (libxsmm_bfloat16*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_n * sizeof(libxsmm_bfloat16), 64);
      l_c_bf = (libxsmm_bfloat16*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(libxsmm_bfloat16), 64);
      l_c_gold_bf = (libxsmm_bfloat16*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(libxsmm_bfloat16), 64);
      /* touch A */
      for (l_i = 0; l_i < l_lda; l_i++) {
        for (l_j = 0; l_j < l_k; l_j++) {
          union libxsmm_bfloat16_hp tmp;
          tmp.f = (float)libxsmm_rng_f64();
          l_a_bf[(l_j * l_lda) + l_i] = tmp.i[1];
        }
      }
      /* touch B */
      for (l_i = 0; l_i < l_ldb; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          union libxsmm_bfloat16_hp tmp;
          tmp.f = (float)libxsmm_rng_f64();
          l_b_bf[(l_j * l_ldb) + l_i] = tmp.i[1];
        }
      }
      /* touch C */
      for (l_i = 0; l_i < l_ldc; l_i++) {
        for (l_j = 0; l_j < l_n; l_j++) {
          union libxsmm_bfloat16_hp tmp;
          tmp.f = 0.0f;
          l_c_bf[(l_j * l_ldc) + l_i] = tmp.i[1];
          l_c_gold_bf[(l_j * l_ldc) + l_i] = tmp.i[1];
        }
      }

      l_runtime_libxsmm = run_jit_bfloat16(l_xgemm_desc, l_a_bf, l_b_bf, l_c_bf, l_file_input );

      if ( l_run_check == 1 ) {
        l_start = libxsmm_timer_tick();
        for (l_t = 0; l_t < g_reps; l_t++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            for (l_i = 0; l_i < l_m; l_i++) {
              union libxsmm_bfloat16_hp fprod;
              fprod.i[1] = l_c_gold_bf[(l_j * l_ldc) + l_i];
              fprod.i[0] = 0;
              for (l_s = 0; l_s < (l_k / l_k_block); l_s++) {
                for (l_k2 = 0; l_k2 < l_k_block; l_k2++) {
                  union libxsmm_bfloat16_hp tmp_a_f;
                  union libxsmm_bfloat16_hp tmp_b_f;
                  tmp_a_f.i[1] = l_a_bf[(l_s * (l_lda*l_k_block)) + (l_i*l_k_block) + l_k2];
                  tmp_a_f.i[0] = 0;
                  tmp_b_f.i[1] = l_b_bf[(l_j * l_ldb) + (l_s*l_k_block) + l_k2];
                  tmp_b_f.i[0] = 0;
                  fprod.f += (float)(tmp_a_f.f * tmp_b_f.f);
                }
              }
              l_c_gold_bf[(l_j * l_ldc) + l_i] = fprod.i[1];
            }
          }
        }
        l_runtime_c = libxsmm_timer_duration(l_start, libxsmm_timer_tick());
        l_max_error = 0;
        for (l_i = 0; l_i < l_m; l_i++) {
          for (l_j = 0; l_j < l_n; l_j++) {
            union libxsmm_bfloat16_hp tmp_c;
            union libxsmm_bfloat16_hp tmp_gold;
            double l_fabs;

            tmp_c.i[1] = l_c_bf[(l_j * l_ldc) + l_i];
            tmp_c.i[0] = 0;
            tmp_gold.i[1] = l_c_gold_bf[(l_j * l_ldc) + l_i];
            tmp_gold.i[0] = 0;
            l_fabs = fabs((double)tmp_gold.f - (double)tmp_c.f);
            if (l_max_error < l_fabs) l_max_error = l_fabs;
          }
        }
      }

      if ( l_file_input == 0 ) {
        printf("%fs for C\n", l_runtime_c);
        printf("%f GFLOPS for C\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_c * 1.0e9));
        printf("%fs for libxsmm\n", l_runtime_libxsmm);
        printf("%f GFLOPS for libxsmm\n", ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9));
        printf("max. error: %f\n", l_max_error);
      } else {
        if ( l_run_check == 1 ) {
          printf("%i %i %i %i %i %i %f %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9), l_max_error );
        } else {
          printf("%i %i %i %i %i %i %f\n", l_m, l_n, l_k, l_lda, l_ldb, l_ldc, ((double)((double)g_reps * (double)l_m * (double)l_n * (double)l_k) * 2.0) / (l_runtime_libxsmm * 1.0e9) );
        }
      }

      if ( (l_total_max_error < l_max_error) && (l_run_check == 1) ) {
        l_total_max_error = l_max_error;
      }

      libxsmm_free(l_a_bf);
      libxsmm_free(l_b_bf);
      libxsmm_free(l_c_w_f);
      libxsmm_free(l_c_gold_w_f);
    } while ( l_keep_going );
  }
#if 0
  else if (strcmp(l_precision, "I8") == 0) {
    l_xgemm_desc = libxsmm_gemm_descriptor_dinit2(&l_xgemm_blob,
      LIBXSMM_GEMM_PRECISION_I8, LIBXSMM_GEMM_PRECISION_I32,
      l_m, l_n, l_k, l_lda, l_ldb, l_ldc, l_alpha, l_beta, l_flags, l_prefetch);
    l_a_b = (unsigned char*)libxsmm_aligned_malloc((size_t)l_lda * (size_t)l_k * sizeof(unsigned char), 64);
    l_b_b = (char*)libxsmm_aligned_malloc((size_t)l_ldb * (size_t)l_n * sizeof(char), 64);
    l_c_b = (int*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(int), 64);
    l_c_gold_b = (int*)libxsmm_aligned_malloc((size_t)l_ldc * (size_t)l_n * sizeof(int), 64);
    /* touch A */
    for (l_i = 0; l_i < l_lda; l_i++) {
      for (l_j = 0; l_j < (l_k / 2); l_j++) {
        l_a_b[(l_j * l_lda) + l_i] = (unsigned char)(libxsmm_rng_f64() * 10.0);
      }
    }
    /* touch B */
    for (l_i = 0; l_i < l_ldb; l_i++) {
      for (l_j = 0; l_j < l_n; l_j++) {
        l_b_b[(l_j * l_ldb) + l_i] = (char)(libxsmm_rng_f64() * 10.0);
      }
    }
    /* touch C */
    for (l_i = 0; l_i < l_ldc; l_i++) {
      for (l_j = 0; l_j < l_n; l_j++) {
        l_c_b[(l_j * l_ldc) + l_i] = 0;
        l_c_gold_b[(l_j * l_ldc) + l_i] = 0;
      }
    }
  }
#else
  LIBXSMM_UNUSED(l_c_b); LIBXSMM_UNUSED(l_b_b); LIBXSMM_UNUSED(l_c_gold_b); LIBXSMM_UNUSED(l_a_b);
#endif

  if ( l_file_input != 0 ) {
    fclose( l_file_handle );
  } else {
    printf("------------------------------------------------\n");
  }

  /* Print total max error */
  printf("\n\n Total Max Error %f\n\n", l_total_max_error );

  if ( l_total_max_error >= 0.00005 ) {
    return EXIT_FAILURE;
  } else {
    return EXIT_SUCCESS;
  }
}

