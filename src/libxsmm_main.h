/******************************************************************************
** Copyright (c) 2014-2019, Intel Corporation                                **
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
/* Hans Pabst (Intel Corp.), Rajkishore Barik (Intel Corp. )
******************************************************************************/
#ifndef LIBXSMM_MAIN_H
#define LIBXSMM_MAIN_H

#include <libxsmm.h>

/** Allow external definition to enable testing corner cases (exhausted registry space). */
#if !defined(LIBXSMM_CAPACITY_REGISTRY) /* must be POT */
# define LIBXSMM_CAPACITY_REGISTRY 131072
#endif
#if !defined(LIBXSMM_CAPACITY_CACHE) /* must be POT */
# define LIBXSMM_CAPACITY_CACHE 16
#endif

#if !defined(LIBXSMM_MAX_NTHREADS)
# define LIBXSMM_MAX_NTHREADS 1024
#endif
#if !defined(LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS)
# define LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS LIBXSMM_MAX_NTHREADS
#endif
#if !defined(LIBXSMM_MALLOC_SCRATCH_LIMIT)
# define LIBXSMM_MALLOC_SCRATCH_LIMIT (4ULL << 30) /* 4 GB */
#endif
#if !defined(LIBXSMM_MALLOC_SCRATCH_MMAP) && 0
# define LIBXSMM_MALLOC_SCRATCH_MMAP
#endif
#if !defined(LIBXSMM_MALLOC_SCRATCH_SCALE)
# if defined(LIBXSMM_MALLOC_SCRATCH_MMAP)
#   define LIBXSMM_MALLOC_SCRATCH_SCALE 1.3
# else
#   define LIBXSMM_MALLOC_SCRATCH_SCALE 1.0
# endif
#endif
#if !defined(LIBXSMM_MALLOC_SCRATCH_INTERNAL_SITE)
# define LIBXSMM_MALLOC_SCRATCH_INTERNAL_SITE ((uintptr_t)-1)
#endif
#if !defined(LIBXSMM_MALLOC_SCRATCH_INTERNAL)
# define LIBXSMM_MALLOC_SCRATCH_INTERNAL ((const char*)(LIBXSMM_MALLOC_SCRATCH_INTERNAL_SITE))
#endif

#if !defined(LIBXSMM_VERBOSITY_HIGH)
# define LIBXSMM_VERBOSITY_HIGH 3 /* secondary warning or info-verbosity */
#endif
#if !defined(LIBXSMM_VERBOSITY_WARN)
# define LIBXSMM_VERBOSITY_WARN ((LIBXSMM_VERBOSITY_HIGH) - LIBXSMM_MIN(1, LIBXSMM_VERBOSITY_HIGH))
#endif

#if !defined(LIBXSMM_LOCK)
# define LIBXSMM_LOCK LIBXSMM_LOCK_DEFAULT
#endif

#if !defined(LIBXSMM_EXT_MIN_NTASKS)
# define LIBXSMM_MIN_NTASKS(NT) 1
#endif
#if !defined(LIBXSMM_OVERHEAD)
# define LIBXSMM_OVERHEAD(NT) 0
#endif
#if !defined(LIBXSMM_NOOP_ARGS)
# define LIBXSMM_NOOP_ARGS(...)
#endif
#if !defined(LIBXSMM_NOOP)
# define LIBXSMM_NOOP
#endif

/** Check if M, N, K, or LDx fits into the descriptor. */
#if (0 != LIBXSMM_ILP64)
# define LIBXSMM_GEMM_NO_BYPASS_DIMS(M, N, K) ( \
    ((unsigned int)(-1)) >= ((unsigned int)(M)) && \
    ((unsigned int)(-1)) >= ((unsigned int)(N)) && \
    ((unsigned int)(-1)) >= ((unsigned int)(K)))
#else /* always fits */
# define LIBXSMM_GEMM_NO_BYPASS_DIMS(M, N, K) 1
#endif

#if defined(LIBXSMM_ASSERT) /* assert available */
# define LIBXSMM_GEMM_DESCRIPTOR_DIM_CHECK(M, N, K) LIBXSMM_ASSERT(LIBXSMM_GEMM_NO_BYPASS_DIMS(M, N, K))
#else
# define LIBXSMM_GEMM_DESCRIPTOR_DIM_CHECK(M, N, K)
#endif

#if defined(LIBXSMM_UNPACKED)
# define LIBXSMM_DESCRIPTOR_CLEAR(BLOB) memset(BLOB, 0, LIBXSMM_DESCRIPTOR_MAXSIZE)
#else
# define LIBXSMM_DESCRIPTOR_CLEAR(BLOB) LIBXSMM_ASSERT(BLOB)
#endif

/** Low-level/internal GEMM descriptor initialization. */
#define LIBXSMM_GEMM_DESCRIPTOR(DESCRIPTOR, DATA_TYPE, FLAGS, M, N, K, LDA, LDB, LDC, ALPHA, BETA, PREFETCH) \
  LIBXSMM_GEMM_DESCRIPTOR_DIM_CHECK(LDA, LDB, LDC); \
  LIBXSMM_GEMM_DESCRIPTOR_DIM_CHECK(M, N, K); \
  LIBXSMM_DESCRIPTOR_CLEAR(&(DESCRIPTOR)); \
  (DESCRIPTOR).datatype = (unsigned char)(DATA_TYPE); \
  (DESCRIPTOR).flags = (unsigned short)((FLAGS) \
    /*| (LIBXSMM_NEQ(0, ALPHA) ? 0 : LIBXSMM_GEMM_FLAG_ALPHA_0)*/ \
    | (LIBXSMM_NEQ(0, BETA) ? 0 : LIBXSMM_GEMM_FLAG_BETA_0)); \
  (DESCRIPTOR).m   = (unsigned int)(M);   (DESCRIPTOR).n   = (unsigned int)(N);   (DESCRIPTOR).k   = (unsigned int)(K); \
  (DESCRIPTOR).lda = (unsigned int)(LDA); (DESCRIPTOR).ldb = (unsigned int)(LDB); (DESCRIPTOR).ldc = (unsigned int)(LDC); \
  (DESCRIPTOR).prefetch = (unsigned char)(PREFETCH)

/** Similar to LIBXSMM_GEMM_DESCRIPTOR, but separately taking the input-/output-precision. */
#define LIBXSMM_GEMM_DESCRIPTOR2(DESCRIPTOR, IPREC, OPREC, FLAGS, M, N, K, LDA, LDB, LDC, ALPHA, BETA, PREFETCH) \
  LIBXSMM_GEMM_DESCRIPTOR(DESCRIPTOR, LIBXSMM_GETENUM(IPREC, OPREC), FLAGS, M, N, K, LDA, LDB, LDC, ALPHA, BETA, PREFETCH)

/** Declare and construct a GEMM descriptor. */
#define LIBXSMM_GEMM_DESCRIPTOR_TYPE(DESCRIPTOR, DATA_TYPE, FLAGS, M, N, K, LDA, LDB, LDC, ALPHA, BETA, PREFETCH) \
  libxsmm_gemm_descriptor DESCRIPTOR; LIBXSMM_GEMM_DESCRIPTOR(DESCRIPTOR, DATA_TYPE, \
    FLAGS, M, N, K, LDA, LDB, LDC, ALPHA, BETA, PREFETCH)

/** Similar to LIBXSMM_GEMM_DESCRIPTOR_TYPE, but separately taking the input-/output-precision. */
#define LIBXSMM_GEMM_DESCRIPTOR2_TYPE(DESCRIPTOR, IPREC, OPREC, FLAGS, M, N, K, LDA, LDB, LDC, ALPHA, BETA, PREFETCH) \
  LIBXSMM_GEMM_DESCRIPTOR_TYPE(DESCRIPTOR, LIBXSMM_GETENUM(IPREC, OPREC), FLAGS, M, N, K, LDA, LDB, LDC, ALPHA, BETA, PREFETCH)

#define LIBXSMM_REGDESC_DEFAULT
#define LIBXSMM_REGDESC(START, MODIFIER) \
  START libxsmm_gemm_descriptor MODIFIER gemm; \
  START libxsmm_mcopy_descriptor MODIFIER mcopy; \
  START libxsmm_trans_descriptor MODIFIER trans; \
  START libxsmm_pgemm_descriptor MODIFIER pgemm; \
  START libxsmm_getrf_descriptor MODIFIER getrf; \
  START libxsmm_trmm_descriptor MODIFIER trmm; \
  START libxsmm_trsm_descriptor MODIFIER trsm


/**
* Packed structure, which stores the argument description of GEMM routines.
* The size of the structure is padded to LIBXSMM_DESCRIPTOR_MAXSIZE.
*/
LIBXSMM_EXTERN_C LIBXSMM_PACKED(struct LIBXSMM_RETARGETABLE) libxsmm_gemm_descriptor {
  /** Denotes the data-type. */
  unsigned char datatype;
  /** Set of flags. */
  unsigned short flags;
  /** Extents of the matrix. */
  unsigned int m, n, k;
  /** Leading dimensions. */
  unsigned int lda, ldb, ldc;
  /** Prefetch strategy. */
  unsigned char prefetch;
};

/** Packed structure storing the matcopy argument description. */
LIBXSMM_EXTERN_C LIBXSMM_PACKED(struct LIBXSMM_RETARGETABLE) libxsmm_mcopy_descriptor {
  /** LDx, M, and N. */
  unsigned int m, n, ldi, ldo;
  /** Size of data element. */
  unsigned char typesize;
  /** Level of unrolling. */
  unsigned char unroll_level;
  /** Boolean value (@TODO fix this). */
  unsigned char prefetch;
  /** Set of flags. */
  unsigned char flags;
};

/** Packed structure storing the transpose argument description. */
LIBXSMM_EXTERN_C LIBXSMM_PACKED(struct LIBXSMM_RETARGETABLE) libxsmm_trans_descriptor {
  /** LD, M, and N. */
  unsigned int m, n, ldo;
  /** Size of data element. */
  unsigned char typesize;
};

/** Packed structure storing arguments of packed GEMM. */
LIBXSMM_EXTERN_C LIBXSMM_PACKED(struct LIBXSMM_RETARGETABLE) libxsmm_pgemm_descriptor {
  unsigned int m, n, k, lda, ldb, ldc;
  unsigned char typesize;
  unsigned char layout;
  char transa, transb;
  char alpha_val;
};

/** Packed structure storing arguments of packed GETRF. */
LIBXSMM_EXTERN_C LIBXSMM_PACKED(struct LIBXSMM_RETARGETABLE) libxsmm_getrf_descriptor {
  unsigned int m, n, lda;
  unsigned char typesize;
  unsigned char layout;
};

/** Packed structure storing arguments of packed TRSM. */
LIBXSMM_EXTERN_C LIBXSMM_PACKED(struct LIBXSMM_RETARGETABLE) libxsmm_trmm_descriptor {
  union { double d; float s; } alpha;
  unsigned int m, n, lda, ldb;
  unsigned char typesize;
  unsigned char layout;
  char diag, side, uplo;
  char transa;
};

/** Packed structure storing arguments of packed TRSM. */
LIBXSMM_EXTERN_C LIBXSMM_PACKED(struct LIBXSMM_RETARGETABLE) libxsmm_trsm_descriptor {
  union { double d; float s; } alpha;
  unsigned int m, n, lda, ldb;
  unsigned char typesize;
  unsigned char layout;
  char diag, side, uplo;
  char transa;
};

LIBXSMM_EXTERN_C typedef struct LIBXSMM_RETARGETABLE LIBXSMM_MAY_ALIAS libxsmm_csr_soa_descriptor {
  const libxsmm_gemm_descriptor* gemm;
  const unsigned int* row_ptr;
  const unsigned int* column_idx;
  const void* values;
} libxsmm_csr_soa_descriptor;

LIBXSMM_EXTERN_C typedef struct LIBXSMM_RETARGETABLE LIBXSMM_MAY_ALIAS libxsmm_csc_soa_descriptor {
  const libxsmm_gemm_descriptor* gemm;
  const unsigned int* column_ptr;
  const unsigned int* row_idx;
  const void* values;
} libxsmm_csc_soa_descriptor;

LIBXSMM_EXTERN_C typedef struct LIBXSMM_RETARGETABLE LIBXSMM_MAY_ALIAS libxsmm_rm_ac_soa_descriptor {
  const libxsmm_gemm_descriptor* gemm;
} libxsmm_rm_ac_soa_descriptor;

LIBXSMM_EXTERN_C typedef struct LIBXSMM_RETARGETABLE LIBXSMM_MAY_ALIAS libxsmm_rm_bc_soa_descriptor {
  const libxsmm_gemm_descriptor* gemm;
} libxsmm_rm_bc_soa_descriptor;

LIBXSMM_EXTERN_C typedef struct LIBXSMM_RETARGETABLE LIBXSMM_MAY_ALIAS libxsmm_csr_reg_descriptor {
  const libxsmm_gemm_descriptor* gemm;
  const unsigned int* row_ptr;
  const unsigned int* column_idx;
  const void* values;
} libxsmm_csr_reg_descriptor;

/** Function type used for convolutions (single-precision); the actual signature depends on the kind of convolution. */
LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_sconvfunction)(
  const float* input1, const float* input2, float* output,
  const float* ipf1, const float* ipf2, const float* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_bf16convfunction)(
  const libxsmm_bfloat16* input1, const libxsmm_bfloat16* input2, libxsmm_bfloat16* output,
  const libxsmm_bfloat16* ipf1, const libxsmm_bfloat16* ipf2, const libxsmm_bfloat16* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_bf16f32convfunction)(
  const libxsmm_bfloat16* input1, const float* input2, libxsmm_bfloat16* output,
  const libxsmm_bfloat16* ipf1, const float* ipf2, const libxsmm_bfloat16* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_wconvfunction)(
  const short* input1, const short* input2, int* output,
  const short* ipf1, const short* ipf2, const int* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_wsconvfunction)(
  const short* input1, const short* input2, float* output,
  const short* ipf1, const short* ipf2, const float* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_uwsconvfunction)(
  short* input1, float* input2, short* output,
  short* ipf1, float* ipf2, short* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_bdbconvfunction)(
  unsigned char* input1, int* input2, unsigned char* output,
  unsigned char* ipf1, int* ipf2, unsigned char* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_busconvfunction)(
  const unsigned char* input1, const char* input2, short* output,
  const unsigned char* ipf1, const char* ipf2, const short* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_budconvfunction)(
  const unsigned char* input1, const char* input2, int* output,
  const unsigned char* ipf1, const char* ipf2, const int* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_wconvfunction_bwd)(
  int* input1, const short* input2, const short* output,
  const int* ipf1, const short* ipf2, const short* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_busconvfunction_bwd)(
  const unsigned short* input1, const char* input2, const char* output,
  const unsigned short* ipf1, const char* ipf2, const char* opf, ...);

LIBXSMM_EXTERN_C typedef LIBXSMM_RETARGETABLE void (*libxsmm_budconvfunction_bwd)(
  const unsigned int* input1, const char* input2, const char* output,
  const unsigned int* ipf1, const char* ipf2, const char* opf, ...);

/** Function type which is either libxsmm_sconvfunction or libxsmm_wconvfunction (weak-typed). */
LIBXSMM_EXTERN_C typedef union LIBXSMM_RETARGETABLE libxsmm_xconvfunction {
  libxsmm_sconvfunction sconv;
  libxsmm_bf16convfunction bf16conv;
  libxsmm_bf16f32convfunction bf1632conv;
  libxsmm_wsconvfunction wsconv;
  libxsmm_uwsconvfunction uwsconv;
  libxsmm_wconvfunction wconv;
  libxsmm_bdbconvfunction bdbconv;
  libxsmm_busconvfunction busconv;
  libxsmm_budconvfunction budconv;
  libxsmm_wconvfunction_bwd wconvb;
  libxsmm_busconvfunction_bwd busconvb;
  libxsmm_budconvfunction_bwd budconvb;
} libxsmm_xconvfunction;

LIBXSMM_EXTERN_C typedef union LIBXSMM_RETARGETABLE libxsmm_code_pointer {
  void (*ptr_fn)(LIBXSMM_VARIADIC);
  const void* ptr_const;
  void* pmm;
  uintptr_t uval;
  intptr_t ival;
  libxsmm_xmmfunction xgemm; /* GEMM: smm, dmm, wimm, wsmm, or void-function */
  libxsmm_xmcopyfunction xmatcopy;
  libxsmm_xtransfunction xtrans;
  libxsmm_xconvfunction xconv;
  libxsmm_pgemm_xfunction xpgemm;
  libxsmm_getrf_xfunction xgetrf;
  libxsmm_trmm_xfunction xtrmm;
  libxsmm_trsm_xfunction xtrsm;
} libxsmm_code_pointer;

/** Structure which describes all tensors in LIBXSMM's DNN module */
LIBXSMM_EXTERN_C struct LIBXSMM_RETARGETABLE libxsmm_dnn_tensor {
  libxsmm_dnn_tensor_datalayout* layout;           /* data-layout descriptor */
  void* data;                                      /* pointer to data */
  unsigned char scf;                               /* fix point scaling factor for this tensor */
};

/* Structure to record segment in stream of code */
LIBXSMM_EXTERN_C typedef struct LIBXSMM_RETARGETABLE segment_t {
  int segment_type;
  int n_convs;
  int aux_index;
} segment_t;

LIBXSMM_EXTERN_C struct LIBXSMM_RETARGETABLE libxsmm_dnn_layer {
  libxsmm_dnn_datatype datatype_in;
  libxsmm_dnn_datatype datatype_out;
  libxsmm_dnn_conv_desc desc;
  libxsmm_dnn_conv_algo algo;
  libxsmm_dnn_tensor_format buffer_format;
  libxsmm_dnn_tensor_format filter_format;
  libxsmm_dnn_conv_fuse_op fuse_ops;
  libxsmm_dnn_conv_option options;
  libxsmm_dnn_internal_format custom_format_type;    /* Specifies internal LIBXSMM format to be used */

  /* These are the batchnorm handles in case of fusion */
  libxsmm_dnn_fusedbatchnorm* pre_bn;
  libxsmm_dnn_fusedbatchnorm* post_bn;

  /* additional size for internal data types */
  int ifhp;
  int ifwp;
  int ofh;
  int ofw;
  int ofhp;
  int ofwp;
  int ifmblock;
  int ifmblock_hp;
  int ifmblock_lp;
  int ofmblock;
  int ofmblock_lp;
  int blocksifm;
  int blocksofm;
  int blocksifm_lp;
  int blocksofm_lp;
  int fwd_ofw_rb;
  int fwd_ofw_rb_2;
  int fwd_ofh_rb;
  int fwd_ofh_rb_2;
  int bwd_ofw_rb;
  int bwd_ofh_rb;
  int upd_ofw_rb;
  int upd_ofh_rb;
  int fm_lp_block; /* additional blocking for low precision datatypes of feature maps */
  int upd_use_thread_fil;
  int upd_use_external_reduce;
  int filter_transposed;
  int nBImg;
  int nbImg;
  int blocksifm_blocking;
  int blocksofm_blocking;
  int blocksimg_blocking;
  int use_accumulation_scratch;
  int use_nts_fwd;
  int use_nts_bwd;
  int use_nts_upd;
  int avoid_acc_load;
  int avoid_acc_load_bwd;
  int pack_input;
  int pack_input_bwd;
  int spread_input_bwd;
  int use_fwd_for_bwd;
  int exploit_duality;
  int qfma_input_pad;
  int resize_input;
  int ifhp_resized;
  int ifwp_resized;
  int use_fastpath;
  int use_hybrid_wu_parallelism;
  int weight_copies;
  int compute_batch_stats_in_kernel_fwd;
  int compute_batch_stats_in_kernel_bwd;
  int compute_eltwise_in_kernel_bwd;
  int perform_relu_in_kernel;
  int compute_max_in_kernel_fwd;
  int compute_max_in_kernel_bwd;
  int fuse_batchstats_fwd;
  int fuse_batchstats_bwd;
  int fuse_eltwise_bwd;
  int fuse_relu_bwd;
  int use_lp_kernel;
  int output_lp_padding;
  int reduce_weights;
  int use_vperm_transposes;
  int avoid_output_trans;
  int avoid_input_trans;
  int enforce_sfma_kernel;
  int n_variants;
  int w_variants;
  int h_variants;
  int loop_order;
  int f32_bf16_cvt_rne;
  int fwd_img_par;
  int use_ofm_parallelization;
  int use_ifm_parallelization;
  int avoid_fmas_in_rim;
  int avoid_init_weights;
  int upd_use_batchreduce;
  int upd_pack_input;
  int upd_img_br_block;
  int upd_loop_order;
  int upd_linearized_tasklist;
  int upd_avoid_rim_fmas;
  int fwd_flags;
  int shuffle_filter_accesses;
  int use_fallback_fwd_loops;
  int use_fallback_bwd_loops;

  libxsmm_xtransfunction tr_kernel;

  /* internal data representation */
  libxsmm_dnn_tensor* reg_input;
  libxsmm_dnn_tensor* reg_output;
  libxsmm_dnn_tensor* reg_filter;
  libxsmm_dnn_tensor* grad_input;
  libxsmm_dnn_tensor* grad_output;
  libxsmm_dnn_tensor* grad_filter;
  libxsmm_dnn_tensor* reg_bias;
  libxsmm_dnn_tensor* grad_bias;
  /* internal data representations for copies of tensors */
  libxsmm_dnn_tensor* reg_input_tr;
  libxsmm_dnn_tensor* reg_filter_tr;
  /* batchnorm stats */
  libxsmm_dnn_tensor* batch_stats;
  /* maxstats used in low-precision kernels */
  libxsmm_dnn_tensor* maxstats_fwd;
  libxsmm_dnn_tensor* maxstats_bwd;
  libxsmm_dnn_tensor* maxstats_upd;

  /* barrier */
  libxsmm_barrier* barrier;

  /* scratch */
  void* scratch1;
  size_t scratch1_size;
  void* scratch2;
  size_t scratch2_size;
  void* scratch3;
  size_t scratch3_size;
  void* scratch4;             /* TLS: used to reduce weights */
  size_t scratch4_size;
  void* scratch5;             /* TLS: copy-buffer (if padding is needed), or [H][W][c-block]-tensor (generic FWD/BWD) */
  size_t max_scratch5_size;
  void* scratch6;             /* TLS: output_scratch (generic WU), or float-accumulation buffer */
  size_t scratch6_size;
  void* scratch7;             /* TLS: filter_scratch (generic WU) */
  size_t scratch7_size;
  size_t minibatch_scratch_size;
  size_t fwdbwd_scratch_size;
  int padding_flag;           /* Flag that dictates if we should apply padding in the input */
  void* scratchIw;            /* Winograd input buffer */
  size_t scratchIw_size;
  void* scratchOw;            /* Winograd output buffer */
  size_t scratchOw_size;
  void* scratchVk;            /* Winograd weight buffer */
  size_t scratchVk_size;

  /* JIT-generated convolution code */
  int use_fwd_generic;
  int use_bwd_generic;
  int use_upd_generic;
  /*
  libxsmm_convolution_forward_descriptor       fwd_desc;
  libxsmm_convolution_forward_descriptor       bwd_desc;
  libxsmm_convolution_weight_update_descriptor wu_desc;
  */
  libxsmm_code_pointer code_fwd[3];
  libxsmm_code_pointer code_bwd[3];
  libxsmm_code_pointer code_upd[2];

  libxsmm_code_pointer matcopy_fwd[4];
  libxsmm_code_pointer matcopy_bwd[4];
  libxsmm_code_pointer matcopy_upd[3];

  /* Data structures and metadata related to per-thread private JITing */
  int trans_ofw_ifm;

  int *n_entries_fwd;
  int **compute_fwd_indices_ptrs;
  int **bn_stats_indices_ptrs;
  int **bn_aux_stats_indices_ptrs;
  int **bn_aux_input_indices_ptrs;
  char **kernel_fwd_variant_ptrs;
  int block_fwd_oj;
  int block_fwd_oi;
  int block_fwd_ifm;
  int block_fwd_ofm;
  int *n_fwd_code_segments;
  segment_t **fwd_code_segments;
  int *ofh_fwd_start;
  int *ofh_fwd_end;

  int *n_entries_bwd;
  int **compute_bwd_indices_ptrs;
  char **kernel_bwd_variant_ptrs;
  int block_bwd_oj;
  int block_bwd_oi;
  int block_bwd_ifm;
  int block_bwd_ofm;
  int *n_bwd_code_segments;
  segment_t **bwd_code_segments;
  int *n_entries_trans_bwd;
  int **transpose_bwd_indices_ptrs;
  int *ofh_bwd_start;
  int *ofh_bwd_end;

  int *n_entries_upd;
  int block_upd_ifm;
  int block_upd_ofm;
  int **compute_upd_indices_ptrs;
  char **kernel_upd_variant_ptrs;
  int *n_upd_code_segments;
  segment_t **upd_code_segments;
  int *n_entries_init_upd;
  int **init_upd_indices_ptrs;
  int *n_entries_copy_upd;
  int **copy_upd_indices_ptrs;
};

LIBXSMM_EXTERN_C struct LIBXSMM_RETARGETABLE libxsmm_dnn_fusedbatchnorm {
  libxsmm_dnn_fusedbatchnorm_desc desc;
  libxsmm_dnn_tensor* reg_input;      /* input tensor */
  libxsmm_dnn_tensor* reg_output;     /* output tensor */
  libxsmm_dnn_tensor* grad_input;     /* grad input tensor */
  libxsmm_dnn_tensor* grad_output;    /* grad output tensor */
  libxsmm_dnn_tensor* reg_add;        /* elementwise tensor */
  libxsmm_dnn_tensor* grad_add;       /* grad elementwise tensor */
  libxsmm_dnn_tensor* reg_beta;       /* beta tensor */
  libxsmm_dnn_tensor* reg_gamma;      /* gamma tensor */
  libxsmm_dnn_tensor* grad_beta;      /* grad beta tensor */
  libxsmm_dnn_tensor* grad_gamma;     /* grad gamma tensor */
  libxsmm_dnn_tensor* expvalue;       /* expected value */
  libxsmm_dnn_tensor* rcpstddev;      /* reciprocal of standard derivation */
  libxsmm_dnn_tensor* variance;       /* variance */
  libxsmm_dnn_tensor* relumask;       /* relumask */
  libxsmm_barrier* barrier;           /* barrier */
  int ifmblock;
  int ifmblock_hp;
  int ofmblock;
  int ofmblock_lp;
  int blocksifm;
  int blocksofm;
  int blocksifm_lp;  /* not used */
  int blocksofm_lp;  /* not used */
  int fm_lp_block;
  size_t scratch_size;
  void* scratch;
};

LIBXSMM_EXTERN_C struct LIBXSMM_RETARGETABLE libxsmm_dnn_fullyconnected {
  libxsmm_dnn_fullyconnected_desc desc;
  libxsmm_dnn_tensor* reg_input;      /* input tensor */
  libxsmm_dnn_tensor* reg_output;     /* output tensor */
  libxsmm_dnn_tensor* grad_input;     /* grad input tensor */
  libxsmm_dnn_tensor* grad_output;    /* grad output tensor */
  libxsmm_dnn_tensor* reg_filter;     /* filter tensor */
  libxsmm_dnn_tensor* grad_filter;    /* grad filter tensor */
  libxsmm_barrier* barrier;           /* barrier */
  int ifmblock;
  int ifmblock_hp;
  int ofmblock;
  int ofmblock_lp;
  int blocksifm;
  int blocksofm;
  int blocksifm_lp;  /* not used */
  int blocksofm_lp;  /* not used */
  int fm_lp_block;
  int bn;
  int bk;
  int bc;
  size_t scratch_size;
  void* scratch;
};

LIBXSMM_EXTERN_C struct LIBXSMM_RETARGETABLE libxsmm_dnn_pooling {
  libxsmm_dnn_pooling_desc desc;
  libxsmm_dnn_tensor* reg_input;      /* input tensor */
  libxsmm_dnn_tensor* reg_output;     /* output tensor */
  libxsmm_dnn_tensor* grad_input;     /* grad input tensor */
  libxsmm_dnn_tensor* grad_output;    /* grad output tensor */
  libxsmm_dnn_tensor* mask;           /* elementwise tensor */
  libxsmm_barrier* barrier;           /* barrier */
  int ifmblock;
  int ifmblock_hp;
  int ofmblock;
  int ofmblock_lp;
  int blocksifm;
  int blocksofm;
  int blocksifm_lp;  /* not used */
  int blocksofm_lp;  /* not used */
  int fm_lp_block;
  int ofh;
  int ofw;
  size_t scratch_size;
  void* scratch;
};

LIBXSMM_EXTERN_C struct LIBXSMM_RETARGETABLE libxsmm_dnn_rnncell {
  libxsmm_dnn_rnncell_desc desc;
  libxsmm_dnn_internal_format custom_format_type; /* required only for comparing layouts */
  libxsmm_blasint T;                              /* sequence length, must be smaller than max sequence length in desc */
  libxsmm_blasint bk;
  libxsmm_blasint bn;
  libxsmm_blasint bc;
  libxsmm_blasint lpb;

  /* external tensors */
  libxsmm_dnn_tensor* xt;
  libxsmm_dnn_tensor* csp;
  libxsmm_dnn_tensor* hp;
  libxsmm_dnn_tensor* w;
  libxsmm_dnn_tensor* wt;
  libxsmm_dnn_tensor* r;
  libxsmm_dnn_tensor* rt;
  libxsmm_dnn_tensor* b;
  libxsmm_dnn_tensor* cst;
  libxsmm_dnn_tensor* ht;
  libxsmm_dnn_tensor* dxt;
  libxsmm_dnn_tensor* dcsp;
  libxsmm_dnn_tensor* dhp;
  libxsmm_dnn_tensor* dw;
  libxsmm_dnn_tensor* dr;
  libxsmm_dnn_tensor* db;
  libxsmm_dnn_tensor* dcs;
  libxsmm_dnn_tensor* dht;
  libxsmm_dnn_tensor* it;
  libxsmm_dnn_tensor* ft;
  libxsmm_dnn_tensor* ot;
  libxsmm_dnn_tensor* cit;
  libxsmm_dnn_tensor* cot;
  float forget_bias;
  /* internal  state */
  void* internal_z;
  /* scratch pointers */
  void* scratch_base;
  void* scratch_wT;
  void* scratch_rT;
  void* scratch_w;
  void* scratch_r;
  void* scratch_xT;
  void* scratch_hT;
  void* scratch_deltat;
  void* scratch_di;
  void* scratch_df;
  void* scratch_do;
  void* scratch_dci;
  void* scratch_diB;
  void* scratch_dfB;
  void* scratch_dpB;
  void* scratch_dciB;
  void* scratch_dx;
  void* scratch_dhp;
  void* scratch_db;
  void* scratch_t1;
  void* scratch_t2;
  void* csp_scratch;
  void* cst_scratch;
  void* ht_scratch;
  void* it_scratch;
  void* ft_scratch;
  void* ot_scratch;
  void* cit_scratch;
  void* cot_scratch;
  /* options */
  int fwd_generic;
  int bwdupd_generic;
  /* barrier */
  libxsmm_barrier* barrier; /* barrier */
};

struct LIBXSMM_RETARGETABLE libxsmm_dfsspmdm {
  int M;
  int N;
  int K;
  int ldb;
  int ldc;
  int N_chunksize;
  double* a_dense;
  libxsmm_dmmfunction kernel;
};

struct LIBXSMM_RETARGETABLE libxsmm_sfsspmdm {
  int M;
  int N;
  int K;
  int ldb;
  int ldc;
  int N_chunksize;
  float* a_dense;
  libxsmm_smmfunction kernel;
};

typedef enum libxsmm_build_kind {
  LIBXSMM_BUILD_KIND_GEMM     = LIBXSMM_KERNEL_KIND_MATMUL,
  LIBXSMM_BUILD_KIND_MCOPY    = LIBXSMM_KERNEL_KIND_MCOPY,
  LIBXSMM_BUILD_KIND_TRANS    = LIBXSMM_KERNEL_KIND_TRANS,
  LIBXSMM_BUILD_KIND_PGEMM    = LIBXSMM_KERNEL_KIND_PGEMM,
  LIBXSMM_BUILD_KIND_GETRF    = LIBXSMM_KERNEL_KIND_GETRF,
  LIBXSMM_BUILD_KIND_TRMM     = LIBXSMM_KERNEL_KIND_TRMM,
  LIBXSMM_BUILD_KIND_TRSM     = LIBXSMM_KERNEL_KIND_TRSM,
  LIBXSMM_BUILD_KIND_RMACSOA  = LIBXSMM_KERNEL_KIND_INVALID,
  LIBXSMM_BUILD_KIND_RMBCSOA,
  LIBXSMM_BUILD_KIND_SRSOA,
  LIBXSMM_BUILD_KIND_SCSOA,
  LIBXSMM_BUILD_KIND_SREG,
  LIBXSMM_BUILD_KIND_CFWD,
  LIBXSMM_BUILD_KIND_CUPD
} libxsmm_build_kind;

/** Integral type (libxsmm_kernel_kind, libxsmm_build_kind). */
#if defined(LIBXSMM_UNPACKED)
typedef unsigned int libxsmm_descriptor_kind;
#else
typedef unsigned char libxsmm_descriptor_kind;
#endif

/** All descriptor types, which are valid for code-registration. */
LIBXSMM_EXTERN_C typedef union LIBXSMM_RETARGETABLE libxsmm_descriptor {
  char data[LIBXSMM_DESCRIPTOR_MAXSIZE];
  libxsmm_descriptor_kind kind; /* kind: must be the first member */
  LIBXSMM_REGDESC(LIBXSMM_PACKED(struct) { libxsmm_descriptor_kind /*repeated kind*/ pad; , desc; });
} libxsmm_descriptor;

LIBXSMM_EXTERN_C typedef struct LIBXSMM_RETARGETABLE libxsmm_build_request {
  union {
    const void* ptr; /* raw content */
    LIBXSMM_REGDESC(LIBXSMM_REGDESC_DEFAULT, const*);
    const libxsmm_csr_soa_descriptor* srsoa;
    const libxsmm_csc_soa_descriptor* scsoa;
    const libxsmm_rm_ac_soa_descriptor* rmacsoa;
    const libxsmm_rm_bc_soa_descriptor* rmbcsoa;
    const libxsmm_csr_reg_descriptor* sreg;
    const libxsmm_convolution_forward_descriptor* cfwd;
    const libxsmm_convolution_weight_update_descriptor* cupd;
  } descriptor;
  libxsmm_build_kind kind;
} libxsmm_build_request;

typedef enum libxsmm_malloc_flags {
  LIBXSMM_MALLOC_FLAG_DEFAULT = 0,
  LIBXSMM_MALLOC_FLAG_SCRATCH = 1,
  LIBXSMM_MALLOC_FLAG_PRIVATE = 2,
  LIBXSMM_MALLOC_FLAG_MMAP    = 4,
  LIBXSMM_MALLOC_FLAG_R       = 8,
  LIBXSMM_MALLOC_FLAG_W       = 16,
  LIBXSMM_MALLOC_FLAG_X       = 32,
  LIBXSMM_MALLOC_FLAG_RW  = LIBXSMM_MALLOC_FLAG_R | LIBXSMM_MALLOC_FLAG_W,
  LIBXSMM_MALLOC_FLAG_WX  = LIBXSMM_MALLOC_FLAG_X | LIBXSMM_MALLOC_FLAG_W,
  LIBXSMM_MALLOC_FLAG_RWX = LIBXSMM_MALLOC_FLAG_X | LIBXSMM_MALLOC_FLAG_RW
} libxsmm_malloc_flags;

/** Returns the type-size of data-type (can be also libxsmm_gemm_precision). */
LIBXSMM_API unsigned char libxsmm_typesize(libxsmm_datatype datatype);

/** Returns the type-name of data-type (can be also libxsmm_gemm_precision). */
LIBXSMM_API const char* libxsmm_typename(libxsmm_datatype datatype);

/** Determines the generic value given in double-precision. */
LIBXSMM_API int libxsmm_cast(libxsmm_datatype datatype, double dvalue, void* value);

/** Retrieve internal information about a buffer (default memory domain). */
LIBXSMM_API int libxsmm_get_malloc_xinfo(const void* memory, size_t* size, int* flags, void** extra);

/** Calculates an alignment depending on supposedly allocated size; alignment can be zero ("auto"). */
LIBXSMM_API_INTERN size_t libxsmm_alignment(size_t size, size_t alignment);

/** Same as libxsmm_set_default_allocator, but takes a lock (can be NULL). */
LIBXSMM_API_INTERN int libxsmm_xset_default_allocator(LIBXSMM_LOCK_TYPE(LIBXSMM_LOCK)* lock,
  void* context, libxsmm_malloc_function malloc_fn, libxsmm_free_function free_fn);
/** Same as libxsmm_get_default_allocator, but takes a lock (can be NULL). */
LIBXSMM_API_INTERN int libxsmm_xget_default_allocator(LIBXSMM_LOCK_TYPE(LIBXSMM_LOCK)* lock,
  void** context, libxsmm_malloc_function* malloc_fn, libxsmm_free_function* free_fn);

/** Same as libxsmm_set_scratch_allocator, but takes a lock (can be NULL). */
LIBXSMM_API_INTERN int libxsmm_xset_scratch_allocator(LIBXSMM_LOCK_TYPE(LIBXSMM_LOCK)* lock,
  void* context, libxsmm_malloc_function malloc_fn, libxsmm_free_function free_fn);
/** Same as libxsmm_get_scratch_allocator, but takes a lock (can be NULL). */
LIBXSMM_API_INTERN int libxsmm_xget_scratch_allocator(LIBXSMM_LOCK_TYPE(LIBXSMM_LOCK)* lock,
  void** context, libxsmm_malloc_function* malloc_fn, libxsmm_free_function* free_fn);

/**
 * Attribute memory allocation and protect with only the necessary flags.
 * This procedure is expected to run only one time per buffer, and may
 * relocate the given memory.
 */
LIBXSMM_API_INTERN int libxsmm_malloc_attrib(void** memory, int flags,
  /** If a name is given, an executable buffer will be dumped into a file. */
  const char* name);

/** Allocate memory of the requested size, which is aligned according to the given alignment. */
LIBXSMM_API_INTERN int libxsmm_xmalloc(void** memory, size_t size, size_t alignment, int flags,
  /* The extra information is stored along with the allocated chunk; can be NULL/zero. */
  const void* extra, size_t extra_size);
/** Release memory, which was allocated using libxsmm_[*]malloc. */
LIBXSMM_API_INTERN int libxsmm_xfree(const void* memory);

/** Determines the given value in double-precision based on the given type. */
LIBXSMM_API_INTERN int libxsmm_dvalue(libxsmm_datatype datatype, const void* value, double* dvalue);

/** Services a build request, and (optionally) registers the code (use regindex=LIBXSMM_CAPACITY_REGISTRY for unmanaged code). */
LIBXSMM_API_INTERN int libxsmm_build(const libxsmm_build_request* request, unsigned int regindex, libxsmm_code_pointer* code);

/** Attempts to receive information about JIT-generated code. */
LIBXSMM_API const libxsmm_descriptor* libxsmm_get_kernel_info(libxsmm_code_pointer code, size_t* size);

/** Updates counters of the statistic, which is shown at program termination. */
LIBXSMM_API_INTERN unsigned int libxsmm_update_mmstatistic(libxsmm_gemm_precision precision,
  libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k, unsigned int ntry, unsigned int ncol);

/** Returns the current tick of a (monotonic) platform-specific counter; not necessarily CPU cycles. */
LIBXSMM_API_INTERN libxsmm_timer_tickint libxsmm_timer_tick_rtc(void);

LIBXSMM_API_INTERN void libxsmm_dnn_init(int target_arch);
LIBXSMM_API_INTERN void libxsmm_dnn_finalize(void);

/** Code generation routine for a forward-convolution kernel. Call libxsmm_release_kernel in order to deallocate the JIT'ted code. */
LIBXSMM_API_INTERN libxsmm_sconvfunction libxsmm_create_sconv_forward(const libxsmm_convolution_forward_descriptor* descriptor);

/** Code generation routine for a convolution kernel as specified by descriptor. */
LIBXSMM_API_INTERN libxsmm_sconvfunction libxsmm_create_sconv_update_weights(const libxsmm_convolution_weight_update_descriptor* descriptor);

/** Code generation routine for a forward-convolution kernel. Call libxsmm_release_kernel in order to deallocate the JIT'ted code. */
LIBXSMM_API_INTERN void* libxsmm_create_xconv_forward(const libxsmm_convolution_forward_descriptor* descriptor);

/** Code generation routine for a convolution kernel as specified by descriptor. */
LIBXSMM_API_INTERN void* libxsmm_create_xconv_update_weights(const libxsmm_convolution_weight_update_descriptor* descriptor);

/** Global lock; create an own lock for an independent domain. */
LIBXSMM_APIVAR_ALIGNED(LIBXSMM_LOCK_TYPE(LIBXSMM_LOCK) libxsmm_lock_global);
/** Target architecture (libxsmm_get_target_archid, libxsmm_set_target_archid). */
LIBXSMM_APIVAR_ALIGNED(int libxsmm_target_archid);
/** Determines whether a threaded implementation is synchronized or not. */
LIBXSMM_APIVAR_ALIGNED(int libxsmm_nosync);
/** Number of threads per core. */
LIBXSMM_APIVAR_ALIGNED(int libxsmm_nt);

/** Function used to allocate default memory. */
LIBXSMM_APIVAR(libxsmm_malloc_function libxsmm_default_malloc_fn);
/** Function used to allocate scratch memory. */
LIBXSMM_APIVAR(libxsmm_malloc_function libxsmm_scratch_malloc_fn);
/** Function used to release default memory. */
LIBXSMM_APIVAR(libxsmm_free_function libxsmm_default_free_fn);
/** Function used to release scratch memory. */
LIBXSMM_APIVAR(libxsmm_free_function libxsmm_scratch_free_fn);
/** If non-NULL, this context is used by the context-form of memory allocation. */
LIBXSMM_APIVAR(void* libxsmm_default_allocator_context);
/** If non-NULL, this context is used by the context-form of memory allocation. */
LIBXSMM_APIVAR(void* libxsmm_scratch_allocator_context);
/** Number of discovered threads (per libxsmm_get_tid) */
LIBXSMM_APIVAR(unsigned int libxsmm_threads_count);
/** Number of scratch memory pools used; clamped against internal maximum. */
LIBXSMM_APIVAR(unsigned int libxsmm_scratch_pools);
/** Maximum total size of the scratch memory domain. */
LIBXSMM_APIVAR(size_t libxsmm_scratch_limit);
/** Growth factor used to scale the scratch memory in case of reallocation. */
LIBXSMM_APIVAR(double libxsmm_scratch_scale);
/** Number of seconds per RDTSC-cycle (zero if RDTSC is not used for wall-clock) */
LIBXSMM_APIVAR(double libxsmm_timer_scale);

#endif /*LIBXSMM_MAIN_H*/

