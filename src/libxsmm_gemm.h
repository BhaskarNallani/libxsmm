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
/* Hans Pabst (Intel Corp.)
******************************************************************************/
#ifndef LIBXSMM_GEMM_H
#define LIBXSMM_GEMM_H

#include "libxsmm_main.h"

#if defined(LIBXSMM_OFFLOAD_TARGET)
# pragma offload_attribute(push,target(LIBXSMM_OFFLOAD_TARGET))
#endif
#if !defined(LIBXSMM_BLAS_WRAP_DYNAMIC) && defined(LIBXSMM_BUILD) && \
  (!defined(__BLAS) || (0 != __BLAS)) && (defined(__GNUC__) || defined(_CRAYC)) && \
  !(defined(__APPLE__) && defined(__MACH__) && LIBXSMM_VERSION3(6, 1, 0) >= \
    LIBXSMM_VERSION3(__clang_major__, __clang_minor__, __clang_patchlevel__)) && \
  !defined(_WIN32) && !defined(__CYGWIN__)
# define LIBXSMM_BLAS_WRAP_DYNAMIC
#endif
#if defined(LIBXSMM_BLAS_WRAP_DYNAMIC)
# include <dlfcn.h>
#endif
#include <limits.h>
#include <stdio.h>
#if defined(LIBXSMM_OFFLOAD_TARGET)
# pragma offload_attribute(pop)
#endif

#if !defined(LIBXSMM_GEMM_CHECK) && !defined(NDEBUG)
# define LIBXSMM_GEMM_CHECK
#endif
#if !defined(LIBXSMM_GEMM_LOCK)
# define LIBXSMM_GEMM_LOCK LIBXSMM_LOCK_DEFAULT
#endif
#if !defined(LIBXSMM_GEMM_TASKSCALE)
# define LIBXSMM_GEMM_TASKSCALE 2
#endif
#if !defined(LIBXSMM_GEMM_MMBATCH_SCALE)
# define LIBXSMM_GEMM_MMBATCH_SCALE 1.5
#endif
#if !defined(LIBXSMM_GEMM_MMBATCH_VERBOSITY)
# define LIBXSMM_GEMM_MMBATCH_VERBOSITY ((LIBXSMM_VERBOSITY_HIGH) + 1)
#endif
#if !defined(LIBXSMM_GEMM_NPARGROUPS)
# define LIBXSMM_GEMM_NPARGROUPS 128
#endif

#if !defined(LIBXSMM_WRAP) && defined(LIBXSMM_BUILD) && \
    (defined(LIBXSMM_CONFIG_WRAP) && 0 != (LIBXSMM_CONFIG_WRAP)) && \
    (defined(LIBXSMM_BLAS_WRAP_DYNAMIC) || !defined(NDEBUG) || defined(_WIN32)) /* debug */
# define LIBXSMM_WRAP LIBXSMM_CONFIG_WRAP
#endif

/** Undefine (disarm) MKL's DIRECT_CALL macros. */
#if (defined(MKL_DIRECT_CALL_SEQ) || defined(MKL_DIRECT_CALL))
# if defined(sgemm_)
#   undef sgemm_
# endif
# if defined(dgemm_)
#   undef dgemm_
# endif
#endif

#if defined(LIBXSMM_BUILD)
# define LIBXSMM_BLAS_WRAPPER_STATIC(TYPE, KIND, ORIGINAL) if (NULL == (ORIGINAL)) { \
    ORIGINAL = LIBXSMM_FSYMBOL(LIBXSMM_CONCATENATE(__real_, LIBXSMM_TPREFIX(TYPE, KIND))); \
  }
#elif (!defined(__BLAS) || (0 != __BLAS))
# define LIBXSMM_BLAS_WRAPPER_STATIC(TYPE, KIND, ORIGINAL) if (NULL == (ORIGINAL)) { \
    ORIGINAL = LIBXSMM_BLAS_SYMBOL(TYPE, KIND); \
  }
#else
# define LIBXSMM_BLAS_WRAPPER_STATIC(TYPE, KIND, ORIGINAL)
#endif

#if defined(LIBXSMM_BLAS_WRAP_DYNAMIC)
# define LIBXSMM_BLAS_WRAPPER_DYNAMIC(TYPE, KIND, ORIGINAL, NEXT) { \
    union { const void* pfin; \
      LIBXSMM_BLAS_FNTYPE(TYPE, KIND) (*chain)(void); /* chain */ \
      LIBXSMM_BLAS_FNTYPE(TYPE, KIND) pfout; \
    } libxsmm_blas_wrapper_dynamic_ /*= { 0 }*/; \
    dlerror(); /* clear an eventual error status */ \
    libxsmm_blas_wrapper_dynamic_.chain = NEXT; \
    libxsmm_blas_wrapper_dynamic_.pfin = ((NULL == libxsmm_blas_wrapper_dynamic_.pfin) ? \
      dlsym(RTLD_NEXT, "libxsmm_original_" LIBXSMM_STRINGIFY(LIBXSMM_TPREFIX(TYPE, KIND))) : NULL); \
    if (NULL == libxsmm_blas_wrapper_dynamic_.pfout || NULL != dlerror() || NULL == libxsmm_blas_wrapper_dynamic_.chain()) { \
      libxsmm_blas_wrapper_dynamic_.pfin = dlsym(RTLD_NEXT, LIBXSMM_STRINGIFY(LIBXSMM_BLAS_SYMBOL(TYPE, KIND))); \
      /*LIBXSMM_ATOMIC_STORE(&(ORIGINAL), libxsmm_blas_wrapper_dynamic_.pfout, LIBXSMM_ATOMIC_RELAXED);*/ \
      ORIGINAL = (NULL == dlerror() ? libxsmm_blas_wrapper_dynamic_.pfout : NULL); \
    } \
  }
#else
# define LIBXSMM_BLAS_WRAPPER_DYNAMIC(TYPE, KIND, ORIGINAL, NEXT)
#endif

#define LIBXSMM_BLAS_WRAPPER(TYPE, KIND, ORIGINAL, NEXT) if (NULL == (ORIGINAL)) { \
  LIBXSMM_BLAS_WRAPPER_DYNAMIC(TYPE, KIND, ORIGINAL, NEXT); \
  LIBXSMM_BLAS_WRAPPER_STATIC(TYPE, KIND, ORIGINAL); \
}


/** Provides GEMM functions available via BLAS; NOT thread-safe. */
LIBXSMM_API_INTERN void libxsmm_gemm_init(int archid);

/** Finalizes the GEMM facility; NOT thread-safe. */
LIBXSMM_API_INTERN void libxsmm_gemm_finalize(void);

LIBXSMM_API_INTERN int libxsmm_gemm_prefetch2uid(libxsmm_gemm_prefetch_type prefetch);
LIBXSMM_API_INTERN libxsmm_gemm_prefetch_type libxsmm_gemm_uid2prefetch(int uid);

#if defined(LIBXSMM_BUILD)
#if defined(LIBXSMM_BUILD_EXT)
LIBXSMM_APIEXT void LIBXSMM_FSYMBOL(__wrap_dgemm_batch)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, double, gemm_batch));
LIBXSMM_APIEXT void LIBXSMM_FSYMBOL(__wrap_sgemm_batch)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, float, gemm_batch));
LIBXSMM_APIEXT void LIBXSMM_FSYMBOL(__wrap_dgemm)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, double, gemm));
LIBXSMM_APIEXT void LIBXSMM_FSYMBOL(__wrap_sgemm)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, float, gemm));
LIBXSMM_APIEXT void LIBXSMM_FSYMBOL(__wrap_dgemv)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, double, gemv));
LIBXSMM_APIEXT void LIBXSMM_FSYMBOL(__wrap_sgemv)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, float, gemv));
LIBXSMM_APIEXT void __wrap_dgemm_batch(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, double, gemm_batch));
LIBXSMM_APIEXT void __wrap_sgemm_batch(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, float, gemm_batch));
#endif
LIBXSMM_API void LIBXSMM_FSYMBOL(__real_dgemm_batch)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, double, gemm_batch));
LIBXSMM_API void LIBXSMM_FSYMBOL(__real_sgemm_batch)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, float, gemm_batch));
LIBXSMM_API void LIBXSMM_FSYMBOL(__real_dgemm)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, double, gemm));
LIBXSMM_API void LIBXSMM_FSYMBOL(__real_sgemm)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, float, gemm));
LIBXSMM_API void LIBXSMM_FSYMBOL(__real_dgemv)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, double, gemv));
LIBXSMM_API void LIBXSMM_FSYMBOL(__real_sgemv)(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, float, gemv));
LIBXSMM_API void __real_dgemm_batch(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, double, gemm_batch));
LIBXSMM_API void __real_sgemm_batch(LIBXSMM_BLAS_SYMBOL_SIGNATURE(const*, *, float, gemm_batch));
#endif

LIBXSMM_BLAS_SYMBOL_FDECL(const*, *, double, gemm_batch);
LIBXSMM_BLAS_SYMBOL_CDECL(const*, *, double, gemm_batch);
LIBXSMM_BLAS_SYMBOL_FDECL(const*, *, float, gemm_batch);
LIBXSMM_BLAS_SYMBOL_CDECL(const*, *, float, gemm_batch);
LIBXSMM_BLAS_SYMBOL_FDECL(const*, *, double, gemm);
LIBXSMM_BLAS_SYMBOL_FDECL(const*, *, float, gemm);
LIBXSMM_BLAS_SYMBOL_FDECL(const*, *, double, gemv);
LIBXSMM_BLAS_SYMBOL_FDECL(const*, *, float, gemv);

LIBXSMM_EXTERN_C struct LIBXSMM_RETARGETABLE libxsmm_gemm_handle {
  libxsmm_code_pointer copy_a, copy_b, copy_i, copy_o;
  libxsmm_xmmfunction kernel[2];
  unsigned int m, n, k, lda, ldb, ldc;
  /* kernel size (tile) */
  unsigned int km, kn, kk;
  /* tile size per task */
  unsigned int dm, dn, dk;
  unsigned int itypesize, otypesize;
  /* number of tasks per direction */
  unsigned int mt, nt, kt;
  int gemm_flags, flags;
};

LIBXSMM_EXTERN_C typedef union LIBXSMM_RETARGETABLE libxsmm_mmbatch_item {
  struct {
    const void *a, *b;
    void *c;
  } value;
  struct {
    libxsmm_gemm_descriptor desc;
    unsigned int count;
    const char* symbol;
  } stat;
  /* TODO: consider padding */
} libxsmm_mmbatch_item;

LIBXSMM_API void libxsmm_gemm_internal_set_batchflag(libxsmm_gemm_descriptor* descriptor, void* c, libxsmm_blasint index_stride,
  libxsmm_blasint batchsize, int multithreaded);

LIBXSMM_API int libxsmm_mmbatch_kernel(libxsmm_xmmfunction kernel, libxsmm_blasint index_base,
  libxsmm_blasint index_stride, const libxsmm_blasint stride_a[], const libxsmm_blasint stride_b[], const libxsmm_blasint stride_c[],
  const void* a, const void* b, void* c, libxsmm_blasint batchsize, /*unsigned*/int tid, /*unsigned*/int ntasks,
  unsigned char itypesize, unsigned char otypesize, int flags);

LIBXSMM_API int libxsmm_mmbatch_blas(
  libxsmm_gemm_precision iprec, libxsmm_gemm_precision oprec, const char* transa, const char* transb, libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const void* alpha, const void* a, const libxsmm_blasint* lda, const void* b, const libxsmm_blasint* ldb, const void* beta, void* c, const libxsmm_blasint* ldc,
  libxsmm_blasint index_base, libxsmm_blasint index_stride, const libxsmm_blasint stride_a[], const libxsmm_blasint stride_b[], const libxsmm_blasint stride_c[],
  libxsmm_blasint batchsize);

LIBXSMM_API_INTERN void libxsmm_dmmbatch_blas(const char* transa, const char* transb, libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const double* alpha, const void* a, const libxsmm_blasint* lda, const void* b, const libxsmm_blasint* ldb, const double* beta, void* c, const libxsmm_blasint* ldc,
  libxsmm_blasint index_base, libxsmm_blasint index_stride, const libxsmm_blasint stride_a[], const libxsmm_blasint stride_b[], const libxsmm_blasint stride_c[],
  libxsmm_blasint batchsize);

LIBXSMM_API_INTERN void libxsmm_smmbatch_blas(const char* transa, const char* transb, libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const float* alpha, const void* a, const libxsmm_blasint* lda, const void* b, const libxsmm_blasint* ldb, const float* beta, void* c, const libxsmm_blasint* ldc,
  libxsmm_blasint index_base, libxsmm_blasint index_stride, const libxsmm_blasint stride_a[], const libxsmm_blasint stride_b[], const libxsmm_blasint stride_c[],
  libxsmm_blasint batchsize);

LIBXSMM_EXTERN_C typedef void (*libxsmm_mmbatch_flush_function)(void);

/** auto-batch descriptor (filter). */
LIBXSMM_APIVAR_ALIGNED(libxsmm_gemm_descriptor libxsmm_mmbatch_desc);
/** Records a batch of SMMs or is used for batch-reduce. */
LIBXSMM_APIVAR_ALIGNED(void* libxsmm_mmbatch_array);
/** Lock: libxsmm_mmbatch_begin, libxsmm_mmbatch_end, internal_mmbatch_flush. */
LIBXSMM_APIVAR_ALIGNED(LIBXSMM_LOCK_TYPE(LIBXSMM_GEMM_LOCK) libxsmm_mmbatch_lock);
/** Maximum size of the recorded batch. */
LIBXSMM_APIVAR_ALIGNED(unsigned int libxsmm_mmbatch_size);
/** Maximum number of parallelized batch-groups. */
LIBXSMM_APIVAR_ALIGNED(unsigned int libxsmm_gemm_npargroups);
/** Minimum batchsize per thread/task. */
LIBXSMM_APIVAR_ALIGNED(unsigned int libxsmm_gemm_taskgrain);
/** Determines if OpenMP tasks are used, and scales beyond the number of threads. */
LIBXSMM_APIVAR_ALIGNED(int libxsmm_gemm_taskscale);

/** Determines the default prefetch strategy, which is used in case of LIBXSMM_PREFETCH_AUTO. */
LIBXSMM_APIVAR(libxsmm_gemm_prefetch_type libxsmm_gemm_auto_prefetch_default);
/** Determines the prefetch strategy, which is used in case of LIBXSMM_PREFETCH_AUTO. */
LIBXSMM_APIVAR(libxsmm_gemm_prefetch_type libxsmm_gemm_auto_prefetch);

/**
 * Intercepted GEMM
 * - [>=1 and  odd]: sequential and non-tiled (small problem sizes only)
 * - [>=2 and even]: parallelized and tiled (all problem sizes)
 * - [>=3 and  odd]: GEMV is intercepted; small problem sizes
 * - [>=4 and even]: GEMV is intercepted; all problem sizes
 * - [negative]: assume BLAS with DGEMM_BATCH
 * - [0]: disabled
 */
LIBXSMM_APIVAR_ALIGNED(int libxsmm_gemm_wrap);

#endif /*LIBXSMM_GEMM_H*/

