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
/* Hans Pabst, Alexander Heinecke (Intel Corp.)
******************************************************************************/
#include "libxsmm_trace.h"
#include "libxsmm_xcopy.h"
#include "libxsmm_gemm.h"
#include "libxsmm_hash.h"
#include "libxsmm_diff.h"
#include "libxsmm_main.h"
#if defined(LIBXSMM_PERF)
# include "libxsmm_perf.h"
#endif
#include "generator_common.h"
#include <libxsmm_intrinsics_x86.h>

#if defined(LIBXSMM_OFFLOAD_TARGET)
# pragma offload_attribute(push,target(LIBXSMM_OFFLOAD_TARGET))
#endif
/* mute warning about target attribute; KNC/native plus JIT is disabled below! */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if !defined(NDEBUG)
# include <errno.h>
#endif
#if defined(_WIN32)
# include <Windows.h>
#else
# include <sys/types.h>
# include <sys/mman.h>
# include <sys/stat.h>
# include <unistd.h>
# include <fcntl.h>
#endif
#if defined(LIBXSMM_OFFLOAD_TARGET)
# pragma offload_attribute(pop)
#endif

#if !defined(LIBXSMM_CODE_MAXSIZE)
# define LIBXSMM_CODE_MAXSIZE 131072
#endif
#if !defined(LIBXSMM_DIFF_SIZE)
# define LIBXSMM_DIFF_SIZE LIBXSMM_DESCRIPTOR_SIGSIZE
#endif
#if !defined(LIBXSMM_HASH_SIZE)
# define LIBXSMM_HASH_SIZE LIBXSMM_DESCRIPTOR_SIGSIZE
#endif
#if !defined(LIBXSMM_HASH_SEED)
# define LIBXSMM_HASH_SEED 25071975
#endif
#if !defined(LIBXSMM_UNIFY_LOCKS)
# define LIBXSMM_UNIFY_LOCKS
#endif
#if !defined(LIBXSMM_ENABLE_DEREG) && 0
# define LIBXSMM_ENABLE_DEREG
#endif
#if !defined(LIBXSMM_REGLOCK_TRY) && 0
# define LIBXSMM_REGLOCK_TRY
#endif
#if !defined(LIBXSMM_DIFF_INLINE) && 0
# define LIBXSMM_DIFF_INLINE
#endif
#if !defined(LIBXSMM_DESC_INLINE) && 0
# define LIBXSMM_DESC_INLINE
#endif
#if !defined(LIBXSMM_DESC_PAD) && 1
# define LIBXSMM_DESC_PAD
#endif

/* flag fused into the memory address of a code version in case of non-JIT */
#define LIBXSMM_CODE_STATIC (1ULL << (8 * sizeof(void*) - 1))
/* flag fused into the memory address of a code version in case of collision */
#if 1 /* beneficial when registry approaches capacity (collisions) */
# define LIBXSMM_HASH_COLLISION (1ULL << (8 * sizeof(void*) - 2))
#endif

/** Helper macro determining the default prefetch strategy which is used for statically generated kernels. */
#if (0 > LIBXSMM_PREFETCH) /* auto-prefetch (frontend) */ || (defined(_WIN32) || defined(__CYGWIN__))
# define INTERNAL_PREFETCH LIBXSMM_GEMM_PREFETCH_NONE
#else
# define INTERNAL_PREFETCH ((libxsmm_gemm_prefetch_type)LIBXSMM_PREFETCH)
#endif

#if (0 != LIBXSMM_SYNC)
# if !defined(INTERNAL_REGLOCK_MAXN)
#   if defined(_MSC_VER)
#     define INTERNAL_REGLOCK_MAXN 0
#   else
#     define INTERNAL_REGLOCK_MAXN 0
#   endif
# endif
# if (1 < INTERNAL_REGLOCK_MAXN)
#   if !defined(LIBXSMM_CACHE_MAXSIZE) && (8 > INTERNAL_REGLOCK_MAXN)
#     define LIBXSMM_CACHE_MAXSIZE LIBXSMM_CAPACITY_CACHE
#   endif
#   if !defined(LIBXSMM_REGLOCK)
#     define LIBXSMM_REGLOCK LIBXSMM_LOCK_DEFAULT
#   endif
#   if !defined(LIBXSMM_CLEANUP_NTRY)
#     define LIBXSMM_CLEANUP_NTRY 7
#   endif
#   if LIBXSMM_LOCK_TYPE_ISPOD(LIBXSMM_REGLOCK)
LIBXSMM_EXTERN_C typedef union LIBXSMM_RETARGETABLE internal_reglocktype {
  char pad[LIBXSMM_CACHELINE];
  LIBXSMM_LOCK_TYPE(LIBXSMM_REGLOCK) state;
} internal_reglocktype;
#   else
LIBXSMM_EXTERN_C typedef union LIBXSMM_RETARGETABLE internal_reglocktype {
  LIBXSMM_LOCK_TYPE(LIBXSMM_REGLOCK) state;
} internal_reglocktype;
#   endif
LIBXSMM_APIVAR_ARRAY(internal_reglocktype internal_reglock, INTERNAL_REGLOCK_MAXN);
# else /* RW-lock */
#   if !defined(LIBXSMM_CACHE_MAXSIZE)
#     define LIBXSMM_CACHE_MAXSIZE LIBXSMM_CAPACITY_CACHE
#   endif
#   if !defined(LIBXSMM_REGLOCK)
#     if defined(LIBXSMM_UNIFY_LOCKS)
#       define LIBXSMM_REGLOCK LIBXSMM_LOCK
#     elif defined(_MSC_VER)
#       define LIBXSMM_REGLOCK LIBXSMM_LOCK_MUTEX
#     elif 0
#       define LIBXSMM_REGLOCK LIBXSMM_LOCK_RWLOCK
#     else
#       define LIBXSMM_REGLOCK LIBXSMM_LOCK_DEFAULT
#     endif
#   endif
LIBXSMM_APIVAR(LIBXSMM_LOCK_TYPE(LIBXSMM_REGLOCK)* internal_reglock_ptr);
# endif
#endif

#if defined(LIBXSMM_CACHE_MAXSIZE) && (0 < (LIBXSMM_CACHE_MAXSIZE))
# define INTERNAL_FIND_CODE_CACHE_GROW(RESULT_INDEX, CACHE_SIZE) \
    RESULT_INDEX = CACHE_SIZE; CACHE_SIZE = (unsigned char)(0 != (CACHE_SIZE) ? ((CACHE_SIZE) << 1) : 1)
# define INTERNAL_FIND_CODE_CACHE_EVICT(RESULT_INDEX, CACHE_SIZE, CACHE_HIT) \
    RESULT_INDEX = (unsigned char)LIBXSMM_MOD2((CACHE_HIT) + ((CACHE_SIZE) - 1), CACHE_SIZE)
#endif

LIBXSMM_EXTERN_C typedef struct LIBXSMM_RETARGETABLE internal_statistic_type {
  unsigned int ntry, ncol, njit, nsta;
} internal_statistic_type;

/** Determines the try-lock property (1<N: disabled, N=1: enabled [N=0: disabled in case of RW-lock]). */
LIBXSMM_APIVAR(int internal_reglock_count);
LIBXSMM_APIVAR(size_t internal_registry_nbytes);
LIBXSMM_APIVAR(libxsmm_descriptor* internal_registry_keys);
LIBXSMM_APIVAR(libxsmm_code_pointer* internal_registry);
LIBXSMM_APIVAR_ARRAY(internal_statistic_type internal_statistic[2/*DP/SP*/], 4/*sml/med/big/xxx*/);
LIBXSMM_APIVAR(unsigned int internal_statistic_sml);
LIBXSMM_APIVAR(unsigned int internal_statistic_med);
LIBXSMM_APIVAR(unsigned int internal_statistic_mnk);
LIBXSMM_APIVAR(unsigned int internal_statistic_num_mcopy);
LIBXSMM_APIVAR(unsigned int internal_statistic_num_tcopy);
LIBXSMM_APIVAR(unsigned int internal_statistic_num_trsm);
LIBXSMM_APIVAR(unsigned int internal_statistic_num_trmm);
LIBXSMM_APIVAR(int internal_gemm_auto_prefetch_locked);
LIBXSMM_APIVAR(const char* internal_build_state);

#if defined(_WIN32)
# define INTERNAL_DELIMS ";,"
LIBXSMM_APIVAR(HANDLE internal_singleton_handle);
#else
# define INTERNAL_DELIMS ";,:"
LIBXSMM_APIVAR_ARRAY(char internal_singleton_fname, 64);
LIBXSMM_APIVAR(int internal_singleton_handle);
#endif

#if (0 == LIBXSMM_SYNC)
# define INTERNAL_FIND_CODE_LOCK(LOCKINDEX, INDEX, DIFF, CODE) {
# define INTERNAL_FIND_CODE_UNLOCK(LOCKINDEX) }
#else
# if defined(LIBXSMM_REGLOCK_TRY)
#   define INTERNAL_REGLOCK_TRY(DIFF, CODE) \
    if (1 != internal_reglock_count) { /* (re-)try and get (meanwhile) generated code */ \
      LIBXSMM_ASSERT(0 != internal_registry); /* engine is not shut down */ \
      continue; \
    } \
    else { /* exit dispatch and let client fall back */ \
      DIFF = 0; CODE = 0; break; \
    }
# else
#   define INTERNAL_REGLOCK_TRY(DIFF, CODE) \
      LIBXSMM_ASSERT(0 != internal_registry); /* engine is not shut down */ \
      continue
# endif
# if (1 < INTERNAL_REGLOCK_MAXN)
#   define INTERNAL_FIND_CODE_LOCK(LOCKINDEX, INDEX, DIFF, CODE) { \
      const unsigned int LOCKINDEX = (0 != internal_reglock_count ? LIBXSMM_MOD2(INDEX, internal_reglock_count) : 0); \
      if (LIBXSMM_LOCK_ACQUIRED(LIBXSMM_REGLOCK) != LIBXSMM_LOCK_TRYLOCK(LIBXSMM_REGLOCK, &internal_reglock[LOCKINDEX].state)) { \
        INTERNAL_REGLOCK_TRY(DIFF, CODE); \
      }
#   define INTERNAL_FIND_CODE_UNLOCK(LOCKINDEX) LIBXSMM_LOCK_RELEASE(LIBXSMM_REGLOCK, &internal_reglock[LOCKINDEX].state); }
# else /* RW-lock */
#   define INTERNAL_FIND_CODE_LOCK(LOCKINDEX, INDEX, DIFF, CODE) { \
      if (LIBXSMM_LOCK_ACQUIRED(LIBXSMM_REGLOCK) != LIBXSMM_LOCK_TRYLOCK(LIBXSMM_REGLOCK, internal_reglock_ptr)) { \
        INTERNAL_REGLOCK_TRY(DIFF, CODE); \
      }
#   define INTERNAL_FIND_CODE_UNLOCK(LOCKINDEX) LIBXSMM_LOCK_RELEASE(LIBXSMM_REGLOCK, internal_reglock_ptr); }
# endif
#endif


LIBXSMM_API_INTERN unsigned int libxsmm_update_mmstatistic(libxsmm_gemm_precision precision,
  libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k, unsigned int ntry, unsigned int ncol)
{
  const unsigned long long kernel_size = LIBXSMM_MNK_SIZE(m, n, k);
  const int idx = (LIBXSMM_GEMM_PRECISION_F64 == precision ? 0 : 1);
  int bucket = 3/*huge*/;

  if (LIBXSMM_MNK_SIZE(internal_statistic_sml, internal_statistic_sml, internal_statistic_sml) >= kernel_size) {
    bucket = 0;
  }
  else if (LIBXSMM_MNK_SIZE(internal_statistic_med, internal_statistic_med, internal_statistic_med) >= kernel_size) {
    bucket = 1;
  }
  else if (LIBXSMM_MNK_SIZE(internal_statistic_mnk, internal_statistic_mnk, internal_statistic_mnk) >= kernel_size) {
    bucket = 2;
  }

  LIBXSMM_ATOMIC_ADD_FETCH(&internal_statistic[idx][bucket].ncol, ncol, LIBXSMM_ATOMIC_RELAXED);
  return LIBXSMM_ATOMIC_ADD_FETCH(&internal_statistic[idx][bucket].ntry, ntry, LIBXSMM_ATOMIC_RELAXED);
}


LIBXSMM_API_INLINE unsigned int internal_update_mmstatistic(const libxsmm_gemm_descriptor* desc,
  unsigned int ntry, unsigned int ncol)
{
  LIBXSMM_ASSERT(NULL != desc);
  return libxsmm_update_mmstatistic((libxsmm_gemm_precision)LIBXSMM_GETENUM_OUT(desc->datatype),
    desc->m, desc->n, desc->k, ntry, ncol);
}


LIBXSMM_API_INLINE unsigned int internal_print_number(unsigned int n, char default_unit, char* unit)
{
  unsigned int number = n;
  LIBXSMM_ASSERT(0 != unit);
  *unit = default_unit;
  if ((1000000) <= n) {
    number = (n + 500000) / 1000000;
    *unit = 'm';
  }
  else if (9999 < n) {
    number = (n + 500) / 1000;
    *unit = 'k';
  }
  return number;
}


LIBXSMM_API_INLINE unsigned int internal_print_statistic(FILE* ostream,
  const char* target_arch, int precision, unsigned int linebreaks, unsigned int indent)
{
  const internal_statistic_type statistic_sml = internal_statistic[precision][0/*SML*/];
  const internal_statistic_type statistic_med = internal_statistic[precision][1/*MED*/];
  const internal_statistic_type statistic_big = internal_statistic[precision][2/*BIG*/];
  const internal_statistic_type statistic_xxx = internal_statistic[precision][3/*XXX*/];
  int printed = 0;
  LIBXSMM_ASSERT(0 != ostream && (0 <= precision && precision < 2));

  if (/* omit to print anything if it is superfluous */
    0 != statistic_sml.ntry || 0 != statistic_sml.njit || 0 != statistic_sml.nsta || 0 != statistic_sml.ncol ||
    0 != statistic_med.ntry || 0 != statistic_med.njit || 0 != statistic_med.nsta || 0 != statistic_med.ncol ||
    0 != statistic_big.ntry || 0 != statistic_big.njit || 0 != statistic_big.nsta || 0 != statistic_big.ncol ||
    0 != statistic_xxx.ntry || 0 != statistic_xxx.njit || 0 != statistic_xxx.nsta || 0 != statistic_xxx.ncol)
  {
    char title[256], range[256], unit[4];
    unsigned int counter[4];
    {
      unsigned int n;
      if (NULL != target_arch && 0 != *target_arch) {
        assert(strlen(target_arch) < sizeof(title)); /* !LIBXSMM_ASSERT */
        for (n = 0; 0 != target_arch[n] /*avoid code-gen. issue with some clang versions: && n < sizeof(title)*/; ++n) {
          const char c = target_arch[n];
          title[n] = (char)(('a' <= c && c <= 'z') ? (c - 32) : c); /* toupper */
        }
        LIBXSMM_SNPRINTF(title + n, sizeof(title) - n, "/%s", 0 == precision ? "DP" : "SP");
      }
      else {
        LIBXSMM_SNPRINTF(title, sizeof(title), "%s", 0 == precision ? "DP" : "SP");
      }
      for (n = 0; n < linebreaks; ++n) fprintf(ostream, "\n");
    }
    fprintf(ostream, "%*s%-8s %6s %6s %6s %6s\n", (int)indent, "", title, "TRY", "JIT", "STA", "COL");
    LIBXSMM_SNPRINTF(range, sizeof(range), "%u..%u", 0u, internal_statistic_sml);
    counter[0] = internal_print_number(statistic_sml.ntry, ' ', unit + 0);
    counter[1] = internal_print_number(statistic_sml.njit, ' ', unit + 1);
    counter[2] = internal_print_number(statistic_sml.nsta, ' ', unit + 2);
    counter[3] = internal_print_number(statistic_sml.ncol, ' ', unit + 3);
    fprintf(ostream, "%*s%8s %6u%c %5u%c %5u%c %5u%c\n", (int)indent, "", range,
      counter[0], unit[0], counter[1], unit[1], counter[2], unit[2], counter[3], unit[3]);
    LIBXSMM_SNPRINTF(range, sizeof(range), "%u..%u", internal_statistic_sml + 1u, internal_statistic_med);
    counter[0] = internal_print_number(statistic_med.ntry, ' ', unit + 0);
    counter[1] = internal_print_number(statistic_med.njit, ' ', unit + 1);
    counter[2] = internal_print_number(statistic_med.nsta, ' ', unit + 2);
    counter[3] = internal_print_number(statistic_med.ncol, ' ', unit + 3);
    fprintf(ostream, "%*s%8s %6u%c %5u%c %5u%c %5u%c\n", (int)indent, "", range,
      counter[0], unit[0], counter[1], unit[1], counter[2], unit[2], counter[3], unit[3]);
    LIBXSMM_SNPRINTF(range, sizeof(range), "%u..%u", internal_statistic_med + 1u, internal_statistic_mnk);
    counter[0] = internal_print_number(statistic_big.ntry, ' ', unit + 0);
    counter[1] = internal_print_number(statistic_big.njit, ' ', unit + 1);
    counter[2] = internal_print_number(statistic_big.nsta, ' ', unit + 2);
    counter[3] = internal_print_number(statistic_big.ncol, ' ', unit + 3);
    fprintf(ostream, "%*s%8s %6u%c %5u%c %5u%c %5u%c\n", (int)indent, "", range,
      counter[0], unit[0], counter[1], unit[1], counter[2], unit[2], counter[3], unit[3]);
    if (0 != statistic_xxx.ntry || 0 != statistic_xxx.njit || 0 != statistic_xxx.nsta || 0 != statistic_xxx.ncol) {
      LIBXSMM_SNPRINTF(range, sizeof(range), "> %u", internal_statistic_mnk);
      counter[0] = internal_print_number(statistic_xxx.ntry, ' ', unit + 0);
      counter[1] = internal_print_number(statistic_xxx.njit, ' ', unit + 1);
      counter[2] = internal_print_number(statistic_xxx.nsta, ' ', unit + 2);
      counter[3] = internal_print_number(statistic_xxx.ncol, ' ', unit + 3);
      fprintf(ostream, "%*s%8s %6u%c %5u%c %5u%c %5u%c\n", (int)indent, "", range,
        counter[0], unit[0], counter[1], unit[1], counter[2], unit[2], counter[3], unit[3]);
    }
    printed = 1;
  }

  return printed;
}


LIBXSMM_API_INLINE unsigned int internal_statistic_ntry(int precision)
{
  return internal_statistic[precision][0/*SML*/].ntry + internal_statistic[precision][1/*MED*/].ntry
       + internal_statistic[precision][2/*BIG*/].ntry + internal_statistic[precision][3/*XXX*/].ntry;
}


LIBXSMM_API_INLINE void internal_register_static_code(
  libxsmm_gemm_precision precision, libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  libxsmm_xmmfunction xgemm, libxsmm_code_pointer* registry)
{
  const libxsmm_blasint lda = m, ldb = k, ldc = m;
  /*const*/ int precondition = LIBXSMM_GEMM_NO_BYPASS_DIMS(m, n, k) && LIBXSMM_GEMM_NO_BYPASS_DIMS(lda, ldb, ldc);
  if (precondition) {
    const size_t size = (LIBXSMM_HASH_SIZE)-sizeof(libxsmm_descriptor_kind);
    libxsmm_descriptor_blob blob;
    const libxsmm_gemm_descriptor *const desc = libxsmm_gemm_descriptor_dinit(&blob, precision,
      m, n, k, lda, ldb, ldc, LIBXSMM_ALPHA, LIBXSMM_BETA, LIBXSMM_FLAGS, INTERNAL_PREFETCH);
    unsigned int i = LIBXSMM_MOD2(
      libxsmm_crc32(LIBXSMM_HASH_SEED, desc, LIBXSMM_MIN(sizeof(libxsmm_gemm_descriptor), size)),
      LIBXSMM_CAPACITY_REGISTRY);
    libxsmm_code_pointer* dst_entry = registry + i;
#if !defined(NDEBUG)
    libxsmm_code_pointer code; code.xgemm = xgemm;
    LIBXSMM_ASSERT(NULL != code.ptr_const && NULL != registry);
    LIBXSMM_ASSERT(0 == (LIBXSMM_CODE_STATIC & code.uval));
#endif
    if (NULL != dst_entry->ptr_const) { /* collision */
      const unsigned int i0 = i;
      do { /* continue to linearly search for an available slot */
        i = LIBXSMM_MOD2(i + 1, LIBXSMM_CAPACITY_REGISTRY);
        if (NULL == registry[i].ptr_const) break;
      } while (i != i0);
#if defined(LIBXSMM_HASH_COLLISION) /* mark entry as a collision */
      dst_entry->uval |= LIBXSMM_HASH_COLLISION;
#endif
      dst_entry = registry + i; /* update destination */
      internal_update_mmstatistic(desc, 0, 1/*collision*/);
      /* out of capacity (no registry slot available) */
      LIBXSMM_ASSERT(NULL == dst_entry->ptr_const || i == i0);
    }
    if (NULL == dst_entry->ptr_const) { /* registry not exhausted */
      internal_registry_keys[i].kind = LIBXSMM_KERNEL_KIND_MATMUL;
      internal_registry_keys[i].gemm.desc = *desc;
      dst_entry->xgemm = xgemm;
      /* mark current entry as static code (non-JIT) */
      dst_entry->uval |= LIBXSMM_CODE_STATIC;
    }
    internal_update_mmstatistic(desc, 1/*try*/, 0);
  }
}


LIBXSMM_API_INLINE void internal_finalize(void)
{
  char *const env_dump_build = getenv("LIBXSMM_DUMP_BUILD");
  char *const env_dump_files = (NULL != getenv("LIBXSMM_DUMP_FILES")
    ? getenv("LIBXSMM_DUMP_FILES") : getenv("LIBXSMM_DUMP_FILE"));
  libxsmm_finalize();
  if (0 != libxsmm_verbosity) { /* print statistic on termination */
    const char *const env_target_hidden = getenv("LIBXSMM_TARGET_HIDDEN");
    const char *const target_arch = (NULL == env_target_hidden || 0 == atoi(env_target_hidden))
      ? libxsmm_cpuid_name(libxsmm_target_archid)
      : NULL/*hidden*/;
    /* synchronize I/O */
    LIBXSMM_STDIO_ACQUIRE();
#if !defined(NDEBUG) && defined(__OPTIMIZE__)
    fprintf(stderr, "LIBXSMM WARNING: library is optimized without -DNDEBUG and contains debug code!\n");
#endif
    fprintf(stderr, "\nLIBXSMM_VERSION: %s-%s (%i)", LIBXSMM_BRANCH, LIBXSMM_VERSION, LIBXSMM_VERSION4(
      LIBXSMM_VERSION_MAJOR, LIBXSMM_VERSION_MINOR, LIBXSMM_VERSION_UPDATE, LIBXSMM_VERSION_PATCH));
    if (LIBXSMM_VERBOSITY_WARN <= libxsmm_verbosity || 0 > libxsmm_verbosity) {
      const int high_verbosity = (LIBXSMM_VERBOSITY_HIGH <= libxsmm_verbosity || 0 > libxsmm_verbosity);
      const double regsize = 1.0 * internal_registry_nbytes / (1ULL << 20);
      libxsmm_scratch_info scratch_info;
      unsigned int linebreak = (0 == internal_print_statistic(stderr, target_arch, 1/*SP*/, 1, 0)) ? 1 : 0;
      if (0 == internal_print_statistic(stderr, target_arch, 0/*DP*/, linebreak, 0) && 0 != linebreak && NULL != target_arch) {
        fprintf(stderr, "\nLIBXSMM_TARGET: %s\n", target_arch);
      }
      fprintf(stderr, "Registry: %.f MB", regsize);
      if (0 != high_verbosity) {
        size_t ngemms = 0;
        int i; for (i = 0; i < 4; ++i) {
          ngemms += (size_t)internal_statistic[0/*DP*/][i].nsta + internal_statistic[1/*SP*/][i].nsta;
          ngemms += (size_t)internal_statistic[0/*DP*/][i].njit + internal_statistic[1/*SP*/][i].njit;
        }
        fprintf(stderr, " (gemm=%lu mcopy=%u tcopy=%u)\n", (unsigned long int)ngemms,
          internal_statistic_num_mcopy, internal_statistic_num_tcopy);
      }
      else {
        fprintf(stderr, "\n");
      }
      if (EXIT_SUCCESS == libxsmm_get_scratch_info(&scratch_info)) {
        const unsigned int scratch_internal = (unsigned int)(((512ULL << 10)/*rounding*/ + scratch_info.internal) / (1ULL << 20));
        const unsigned int scratch_size = (unsigned int)(((512ULL << 10)/*rounding*/ + scratch_info.size) / (1ULL << 20));
        if (0 != scratch_size || (0 != high_verbosity && 0 != scratch_internal)) {
          fprintf(stderr, "Scratch: %u MB", scratch_size);
          if (0 != high_verbosity) {
#if (0 != LIBXSMM_SYNC)
            if (1 < libxsmm_threads_count) {
              fprintf(stderr, " (mallocs=%lu, pools=%u, threads=%u, internal=%u MB)\n",
                (unsigned long int)scratch_info.nmallocs, scratch_info.npools,
                libxsmm_threads_count, scratch_internal);
            }
            else
#endif
            {
              fprintf(stderr, " (mallocs=%lu, pools=%u, internal=%u MB)\n",
                (unsigned long int)scratch_info.nmallocs, scratch_info.npools,
                scratch_internal);
            }
          }
          else {
            fprintf(stderr, "\n");
          }
        }
      }
    }
    else {
      fprintf(stderr, "\nLIBXSMM_TARGET: %s\n", target_arch);
    }
    /* synchronize I/O */
    LIBXSMM_STDIO_RELEASE();
  }
  /* release scratch memory pool */
  libxsmm_release_scratch();
  /* release global services */
  libxsmm_hash_finalize();
#if defined(_WIN32)
  if (NULL != internal_singleton_handle)
#else
  if (0 <= internal_singleton_handle && 0 != *internal_singleton_fname)
#endif
  { /* dump per-node info */
    if (NULL != env_dump_build || NULL != env_dump_files) {
      LIBXSMM_STDIO_ACQUIRE();
      if (NULL != env_dump_files && 0 != *env_dump_files) {
        const char *filename = strtok(env_dump_files, INTERNAL_DELIMS);
        for (; NULL != filename; filename = strtok(NULL, INTERNAL_DELIMS)) {
          FILE *const file = fopen(filename, "r");
          if (NULL != file) {
            int c = fgetc(file);
            fprintf(stdout, "\n\nLIBXSMM_DUMP_FILE: %s\n", filename);
            while (EOF != c) {
              fputc(c, stdout);
              c = fgetc(file);
            }
            fputc('\n', stdout);
            fclose(file);
          }
        }
      }
      if (NULL != env_dump_build && 0 != *env_dump_build && '0' != *env_dump_build) {
        fprintf(stdout, "\n\nBUILD_DATE=%i\n", LIBXSMM_CONFIG_BUILD_DATE);
        if (NULL != internal_build_state) {
          fprintf(stdout, "%s\n", internal_build_state);
        }
      }
      LIBXSMM_STDIO_RELEASE();
    }
    /* cleanup singleton */
#if defined(_WIN32)
    ReleaseMutex(internal_singleton_handle);
#else
    unlink(internal_singleton_fname);
    close(internal_singleton_handle);
#endif
  }
#if (0 != LIBXSMM_SYNC)
  { /* release locks */
# if (1 < INTERNAL_REGLOCK_MAXN)
    int i; for (i = 0; i < internal_reglock_count; ++i) LIBXSMM_LOCK_DESTROY(LIBXSMM_REGLOCK, &internal_reglock[i].state);
# elif !defined(LIBXSMM_UNIFY_LOCKS)
    LIBXSMM_LOCK_DESTROY(LIBXSMM_REGLOCK, internal_reglock_ptr);
# endif
    LIBXSMM_LOCK_DESTROY(LIBXSMM_LOCK, &libxsmm_lock_global);
  }
#endif
}


LIBXSMM_API_INLINE size_t internal_strlen(const char* cstr, size_t maxlen)
{
  size_t result = 0;
  if (NULL != cstr) {
    while (0 != cstr[result] && result < maxlen) ++result;
  }
  return result;
}


LIBXSMM_API_INTERN
#if defined(__GNUC__)
LIBXSMM_ATTRIBUTE(no_instrument_function)
#endif
void internal_init(void);

LIBXSMM_API_INTERN void internal_init(void)
{
  int i;
  const libxsmm_malloc_function null_malloc_fn = { 0 };
  const libxsmm_free_function null_free_fn = { 0 };
#if (0 != LIBXSMM_SYNC) /* setup the locks in a thread-safe fashion */
  LIBXSMM_LOCK_ACQUIRE(LIBXSMM_LOCK, &libxsmm_lock_global);
# if (1 < INTERNAL_REGLOCK_MAXN)
  for (i = 0; i < internal_reglock_count; ++i) LIBXSMM_LOCK_ACQUIRE(LIBXSMM_REGLOCK, &internal_reglock[i].state);
# elif !defined(LIBXSMM_UNIFY_LOCKS)
  LIBXSMM_LOCK_ACQUIRE(LIBXSMM_REGLOCK, internal_reglock_ptr);
# endif
#endif
  if (NULL == internal_registry) { /* double-check after acquiring the lock(s) */
    libxsmm_code_pointer* new_registry;
    /* setup verbosity as early as possible since below code may rely on verbose output */
    const char *const env_verbose = getenv("LIBXSMM_VERBOSE");
    if (NULL != env_verbose && 0 != *env_verbose) {
      libxsmm_verbosity = atoi(env_verbose);
    }
#if !defined(NDEBUG)
    else {
      libxsmm_verbosity = INT_MAX; /* quiet -> verbose */
    }
#endif
    LIBXSMM_ASSERT(NULL == internal_registry_keys); /* should never happen */
#if !defined(_WIN32) && 0
    umask(S_IRUSR | S_IWUSR); /* setup default/secure file mask */
#endif
    libxsmm_xset_default_allocator(NULL/*lock*/, NULL/*context*/, null_malloc_fn, null_free_fn);
    libxsmm_xset_scratch_allocator(NULL/*lock*/, NULL/*context*/, null_malloc_fn, null_free_fn);
#if defined(LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS) && (0 < (LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS))
    { const char *const env = getenv("LIBXSMM_SCRATCH_POOLS");
      if (NULL == env || 0 == *env) {
        libxsmm_scratch_pools = LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS;
      }
      else {
        libxsmm_scratch_pools = LIBXSMM_CLMP(atoi(env), 0, LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS);
        /*libxsmm_scratch_pools_locked = 1;*/
      }
      LIBXSMM_ASSERT(libxsmm_scratch_pools <= LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS);
    }
    { const char *const env = getenv("LIBXSMM_SCRATCH_LIMIT");
      if (NULL == env || 0 == *env) {
        /*const*/ unsigned long long limit = LIBXSMM_MALLOC_SCRATCH_LIMIT;
        libxsmm_scratch_limit = (size_t)limit;
      }
      else {
        size_t u = internal_strlen(env, 32) - 1;
        const char *const unit = "kmgKMG", *const hit = strchr(unit, env[u]);
        libxsmm_scratch_limit = (size_t)strtoul(env, 0, 10);
        u = (0 != hit ? ((hit - unit) % 3) : 3);
        if (u < 3) {
          libxsmm_scratch_limit <<= (u + 1) * 10;
        }
        /*libxsmm_scratch_limit_locked = 1;*/
      }
    }
    { const char *const env = getenv("LIBXSMM_SCRATCH_SCALE");
      if (NULL == env || 0 == *env) {
        libxsmm_scratch_scale = LIBXSMM_MALLOC_SCRATCH_SCALE;
      }
      else {
        libxsmm_scratch_scale = LIBXSMM_CLMP(atof(env), 1.1, 3.0);
        /*libxsmm_scratch_scale_locked = 1;*/
      }
      LIBXSMM_ASSERT(1 <= libxsmm_scratch_scale);
    }
#endif /*defined(LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS) && (0 < (LIBXSMM_MALLOC_SCRATCH_MAX_NPOOLS))*/
#if defined(LIBXSMM_MAXTARGET)
    libxsmm_set_target_arch(LIBXSMM_STRINGIFY(LIBXSMM_MAXTARGET));
#else /* attempt to set libxsmm_target_archid per environment variable */
    libxsmm_set_target_arch(getenv("LIBXSMM_TARGET"));
#endif
    { const char *const env = getenv("LIBXSMM_SYNC");
      libxsmm_nosync = (NULL == env || 0 == *env) ? 0/*default*/ : atoi(env);
    }
    /* clear internal counters/statistic */
    for (i = 0; i < 4/*sml/med/big/xxx*/; ++i) {
      memset(&internal_statistic[0/*DP*/][i], 0, sizeof(internal_statistic_type));
      memset(&internal_statistic[1/*SP*/][i], 0, sizeof(internal_statistic_type));
    }
    libxsmm_nt = 2;
#if !defined(__MIC__) && (LIBXSMM_X86_AVX512_MIC != LIBXSMM_STATIC_TARGET_ARCH)
    if (LIBXSMM_X86_AVX512_MIC == libxsmm_target_archid)
#endif
    {
      libxsmm_nt = 4;
    }
    internal_statistic_mnk = LIBXSMM_MAX_DIM;
    internal_statistic_sml = 13;
    internal_statistic_med = 23;
#if !defined(NDEBUG) /* LIBXSMM_CAPACITY_REGISTRY: power of two */
    { const unsigned int npot = LIBXSMM_UP2POT(LIBXSMM_CAPACITY_REGISTRY);
      assert(LIBXSMM_CAPACITY_REGISTRY == npot); /* !LIBXSMM_ASSERT */
    }
#endif
    new_registry = (libxsmm_code_pointer*)malloc((LIBXSMM_CAPACITY_REGISTRY) * sizeof(libxsmm_code_pointer));
    internal_registry_keys = (libxsmm_descriptor*)malloc((LIBXSMM_CAPACITY_REGISTRY) * sizeof(libxsmm_descriptor));
    if (NULL != new_registry && NULL != internal_registry_keys) {
      libxsmm_trans_init(libxsmm_target_archid);
      libxsmm_hash_init(libxsmm_target_archid);
      libxsmm_dnn_init(libxsmm_target_archid);
#if defined(LIBXSMM_PERF)
      libxsmm_perf_init();
#endif
      { const char *const env = getenv("LIBXSMM_GEMM_PREFETCH");
#if defined(_WIN32) || defined(__CYGWIN__) /* TODO: full support for Windows calling convention */
        libxsmm_gemm_auto_prefetch_default = INTERNAL_PREFETCH;
#else
        libxsmm_gemm_auto_prefetch_default = (0 == internal_statistic_ntry(0/*DP*/) && 0 == internal_statistic_ntry(1/*SP*/))
          /* avoid special prefetch if static code is present, since such code uses INTERNAL_PREFETCH */
          ? (((LIBXSMM_X86_AVX512 >= libxsmm_target_archid || LIBXSMM_X86_AVX512_CORE <= libxsmm_target_archid))
            ? LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C : LIBXSMM_GEMM_PREFETCH_BL2_VIA_C)
          : INTERNAL_PREFETCH;
#endif
        libxsmm_gemm_auto_prefetch = INTERNAL_PREFETCH;
        if (NULL != env && 0 != *env) { /* user input beyond auto-prefetch is always considered */
          const int uid = atoi(env);
          if (0 <= uid) {
            libxsmm_gemm_auto_prefetch_default = libxsmm_gemm_uid2prefetch(uid);
            libxsmm_gemm_auto_prefetch = libxsmm_gemm_auto_prefetch_default;
            internal_gemm_auto_prefetch_locked = 1;
          }
        }
      }
      for (i = 0; i < (LIBXSMM_CAPACITY_REGISTRY); ++i) new_registry[i].pmm = NULL;
#if defined(LIBXSMM_BUILD)
#     include <libxsmm_dispatch.h>
#endif
      libxsmm_gemm_init(libxsmm_target_archid);
#if defined(LIBXSMM_TRACE)
      { int filter_threadid = 0/*only main-thread*/, filter_mindepth = 0, filter_maxnsyms = 0;
        const int init_code = libxsmm_trace_init(filter_threadid, filter_mindepth, filter_maxnsyms);
        if (EXIT_SUCCESS != init_code && 0 != libxsmm_verbosity) { /* library code is expected to be mute */
          fprintf(stderr, "LIBXSMM ERROR: failed to initialize TRACE (error #%i)!\n", init_code);
        }
      }
#endif
      { void *const pv_registry = &internal_registry;
        LIBXSMM_ATOMIC(LIBXSMM_ATOMIC_STORE, LIBXSMM_BITS)((void**)pv_registry, (void*)new_registry, LIBXSMM_ATOMIC_SEQ_CST);
      }
    }
    else {
      if (0 != libxsmm_verbosity) { /* library code is expected to be mute */
        fprintf(stderr, "LIBXSMM ERROR: failed to allocate code registry!\n");
      }
      free(internal_registry_keys);
      free(new_registry);
    }
  }
#if (0 != LIBXSMM_SYNC) /* release locks */
# if (1 < INTERNAL_REGLOCK_MAXN)
  for (i = 0; i < internal_reglock_count; ++i) LIBXSMM_LOCK_RELEASE(LIBXSMM_REGLOCK, &internal_reglock[i].state);
# elif !defined(LIBXSMM_UNIFY_LOCKS)
  LIBXSMM_LOCK_RELEASE(LIBXSMM_REGLOCK, internal_reglock_ptr);
# endif
  LIBXSMM_LOCK_RELEASE(LIBXSMM_LOCK, &libxsmm_lock_global);
#endif
}


LIBXSMM_API LIBXSMM_ATTRIBUTE_CTOR void libxsmm_init(void)
{
  if (0 == LIBXSMM_ATOMIC_LOAD(&internal_registry, LIBXSMM_ATOMIC_RELAXED)) {
#if (0 != LIBXSMM_SYNC)
    static int once = 0;
    /* libxsmm_ninit: serves as an ID to invalidate the thread-local cache; never decremented */
    if (1 == LIBXSMM_ATOMIC_ADD_FETCH(&libxsmm_ninit, 1, LIBXSMM_ATOMIC_SEQ_CST)) {
# if defined(LIBXSMM_REGLOCK_TRY)
      const char *const env_trylock = getenv("LIBXSMM_TRYLOCK");
# endif
      LIBXSMM_LOCK_ATTR_TYPE(LIBXSMM_LOCK) attr_global;
# if (1 < INTERNAL_REGLOCK_MAXN)
      int i;
      LIBXSMM_LOCK_ATTR_TYPE(LIBXSMM_REGLOCK) attr;
      LIBXSMM_LOCK_ATTR_INIT(LIBXSMM_REGLOCK, &attr);
# elif defined(LIBXSMM_UNIFY_LOCKS)
      internal_reglock_ptr = &libxsmm_lock_global;
# else
      static LIBXSMM_LOCK_TYPE(LIBXSMM_REGLOCK) internal_reglock;
      internal_reglock_ptr = &internal_reglock;
      LIBXSMM_LOCK_ATTR_TYPE(LIBXSMM_REGLOCK) attr;
      LIBXSMM_LOCK_ATTR_INIT(LIBXSMM_REGLOCK, &attr);
      LIBXSMM_LOCK_INIT(LIBXSMM_REGLOCK, internal_reglock_ptr, &attr);
      LIBXSMM_LOCK_ATTR_DESTROY(LIBXSMM_REGLOCK, &attr);
# endif
      LIBXSMM_LOCK_ATTR_INIT(LIBXSMM_LOCK, &attr_global);
      LIBXSMM_LOCK_INIT(LIBXSMM_LOCK, &libxsmm_lock_global, &attr_global);
      LIBXSMM_LOCK_ATTR_DESTROY(LIBXSMM_LOCK, &attr_global);
      /* control number of locks needed; LIBXSMM_TRYLOCK implies only 1 lock */
# if defined(LIBXSMM_REGLOCK_TRY)
      if (NULL == env_trylock || 0 == *env_trylock)
# endif
      { /* no LIBXSMM_TRYLOCK */
# if defined(LIBXSMM_VTUNE)
        internal_reglock_count = 1; /* avoid duplicated kernels */
# elif (1 < INTERNAL_REGLOCK_MAXN)
        const char *const env_nlocks = getenv("LIBXSMM_NLOCKS");
        const int reglock_count = (NULL == env_nlocks || 0 == *env_nlocks || 1 > atoi(env_nlocks))
          ? (INTERNAL_REGLOCK_MAXN) : LIBXSMM_MIN(atoi(env_nlocks), INTERNAL_REGLOCK_MAXN);
        internal_reglock_count = LIBXSMM_LO2POT(reglock_count);
# else
        internal_reglock_count = 0;
# endif
      }
# if defined(LIBXSMM_REGLOCK_TRY)
      else { /* LIBXSMM_TRYLOCK environment variable specified */
        internal_reglock_count = (0 != atoi(env_trylock) ? 1
#   if (1 < INTERNAL_REGLOCK_MAXN)
          : INTERNAL_REGLOCK_MAXN);
#   else
          : 0);
#   endif
      }
# endif
# if (1 < INTERNAL_REGLOCK_MAXN)
      LIBXSMM_ASSERT(1 <= internal_reglock_count);
      for (i = 0; i < internal_reglock_count; ++i) LIBXSMM_LOCK_INIT(LIBXSMM_REGLOCK, &internal_reglock[i].state, &attr);
      LIBXSMM_LOCK_ATTR_DESTROY(LIBXSMM_REGLOCK, &attr);
# endif
#endif
      { /* determine whether this instance is unique or not */
#if defined(_WIN32)
        internal_singleton_handle = CreateMutex(NULL, TRUE, "GlobalLIBXSMM");
#else
        const int result = LIBXSMM_SNPRINTF(internal_singleton_fname, sizeof(internal_singleton_fname), "/tmp/.libxsmm.%u",
          /*rely on user id to avoid permission issues in case of left-over files*/(unsigned int)getuid());
        struct flock singleton_flock;
        int singleton_handle;
        singleton_flock.l_start = 0;
        singleton_flock.l_len = 0; /* entire file */
        singleton_flock.l_type = F_WRLCK; /* exclusive across PIDs */
        singleton_flock.l_whence = SEEK_SET;
        singleton_handle = ((0 < result && (int)sizeof(internal_singleton_fname) > result) ? open(
          internal_singleton_fname, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR) : -1);
        internal_singleton_handle = fcntl(singleton_handle, F_SETLK, &singleton_flock);
        if (0 > internal_singleton_handle && 0 <= singleton_handle) close(singleton_handle);
#endif
      }
      { /* calibrate timer */
        libxsmm_timer_tickint s0, t0, s1, t1;
        libxsmm_timer_tick_rtc(); libxsmm_timer_tick(); /* warm-up */
        s0 = libxsmm_timer_tick_rtc(); t0 = libxsmm_timer_tick(); /* start timing */
        internal_init();
        atexit(internal_finalize); /* once */
        s1 = libxsmm_timer_tick_rtc(); t1 = libxsmm_timer_tick(); /* final timing */
        if (LIBXSMM_FEQ(0, libxsmm_timer_scale) && s0 != s1 && t0 != t1) {
          libxsmm_timer_scale = libxsmm_timer_duration(s0, s1) / (t0 < t1 ? (t1 - t0) : (t0 - t1));
        }
      }
#if (0 != LIBXSMM_SYNC)
      LIBXSMM_ATOMIC_STORE(&once, 1, LIBXSMM_ATOMIC_RELAXED); /* inc? */
    }
    else while (1) {
      if (0 != LIBXSMM_ATOMIC_LOAD(&once, LIBXSMM_ATOMIC_RELAXED)) {
        break;
      }
# if 1
      else LIBXSMM_SYNC_YIELD();
# else
      else LIBXSMM_SYNC_PAUSE;
# endif
    }
#endif
    internal_init();
  }
}


LIBXSMM_API
#if defined(__GNUC__)
LIBXSMM_ATTRIBUTE(no_instrument_function)
#endif
void libxsmm_finalize(void);

LIBXSMM_API LIBXSMM_ATTRIBUTE_DTOR void libxsmm_finalize(void)
{
  void *const regaddr = &internal_registry;
  uintptr_t regptr = LIBXSMM_ATOMIC(LIBXSMM_ATOMIC_LOAD, LIBXSMM_BITS)((uintptr_t*)regaddr, LIBXSMM_ATOMIC_RELAXED);
  libxsmm_code_pointer* registry = (libxsmm_code_pointer*)regptr;
  if (NULL != registry) {
    int i;
#if (0 != LIBXSMM_SYNC)
    LIBXSMM_LOCK_ACQUIRE(LIBXSMM_LOCK, &libxsmm_lock_global);
# if (1 < INTERNAL_REGLOCK_MAXN)
    { /* acquire locks and thereby shortcut lazy initialization later on */
      int ntry = 0, n;
      do {
        for (i = 0, n = 0; i < internal_reglock_count; ++i) {
          if (LIBXSMM_LOCK_ACQUIRED(LIBXSMM_REGLOCK) == LIBXSMM_LOCK_TRYLOCK(LIBXSMM_REGLOCK, &internal_reglock[i].state)) ++n;
        }
        ntry += (0 == n ? 1 : 0);
      } while (n < internal_reglock_count && ntry < LIBXSMM_CLEANUP_NTRY);
    }
# elif !defined(LIBXSMM_UNIFY_LOCKS)
    LIBXSMM_LOCK_ACQUIRE(LIBXSMM_REGLOCK, internal_reglock_ptr);
# endif
#endif
    regptr = LIBXSMM_ATOMIC(LIBXSMM_ATOMIC_LOAD, LIBXSMM_BITS)((uintptr_t*)regaddr, LIBXSMM_ATOMIC_RELAXED);
    registry = (libxsmm_code_pointer*)regptr;

    if (NULL != registry) {
      libxsmm_descriptor *const registry_keys = internal_registry_keys;
      unsigned int rest = 0, errors = 0;
      internal_registry_nbytes = (LIBXSMM_CAPACITY_REGISTRY) * (sizeof(libxsmm_code_pointer) + sizeof(libxsmm_descriptor));
      for (i = 0; i < (LIBXSMM_CAPACITY_REGISTRY); ++i) {
        /*const*/ libxsmm_code_pointer code = registry[i];
        if (NULL != code.ptr_const) {
          /* check if the registered entity is a GEMM kernel */
          switch (registry_keys[i].kind) {
            case LIBXSMM_KERNEL_KIND_MATMUL: {
              const libxsmm_gemm_descriptor *const desc = &registry_keys[i].gemm.desc;
              const unsigned long long kernel_size = LIBXSMM_MNK_SIZE(desc->m, desc->n, desc->k);
              const int precision = (LIBXSMM_GEMM_PRECISION_F64 == desc->datatype ? 0 : 1);
              int bucket = 3/*huge*/;
              LIBXSMM_ASSERT(0 < kernel_size);
              if (LIBXSMM_MNK_SIZE(internal_statistic_sml, internal_statistic_sml, internal_statistic_sml) >= kernel_size) {
                bucket = 0;
              }
              else if (LIBXSMM_MNK_SIZE(internal_statistic_med, internal_statistic_med, internal_statistic_med) >= kernel_size) {
                bucket = 1;
              }
              else if (LIBXSMM_MNK_SIZE(internal_statistic_mnk, internal_statistic_mnk, internal_statistic_mnk) >= kernel_size) {
                bucket = 2;
              }
              if (0 == (LIBXSMM_CODE_STATIC & code.uval)) { /* count whether kernel is static or JIT-code */
                ++internal_statistic[precision][bucket].njit;
              }
              else {
                ++internal_statistic[precision][bucket].nsta;
              }
              ++rest;
            } break;
            case LIBXSMM_KERNEL_KIND_MCOPY: {
              ++internal_statistic_num_mcopy;
            } break;
            case LIBXSMM_KERNEL_KIND_TRANS: {
              ++internal_statistic_num_tcopy;
            } break;
            case LIBXSMM_KERNEL_KIND_TRSM: {
              ++internal_statistic_num_trsm;
            } break;
            case LIBXSMM_KERNEL_KIND_TRMM: {
              ++internal_statistic_num_trmm;
            } break;
            default: if (LIBXSMM_KERNEL_KIND_INVALID <= registry_keys[i].kind) {
              ++errors;
            }
            else {
              ++rest;
            }
          }
          if (0 != libxsmm_verbosity) { /* library code is expected to be mute */
            if (0 != errors) {
              fprintf(stderr, "LIBXSMM ERROR: code registry is corrupted!\n");
            }
            if (LIBXSMM_CAPACITY_REGISTRY == (rest + errors +
              internal_statistic_num_mcopy + internal_statistic_num_tcopy +
              internal_statistic_num_trsm + internal_statistic_num_trmm))
            {
              fprintf(stderr, "LIBXSMM WARNING: code registry was exhausted!\n");
            }
          }
          if (0 == (LIBXSMM_CODE_STATIC & code.uval)) { /* check for allocated/generated JIT-code */
            void* buffer = NULL;
            size_t size = 0;
#if defined(LIBXSMM_HASH_COLLISION)
            code.uval &= ~LIBXSMM_HASH_COLLISION; /* clear collision flag */
#endif
            if (EXIT_SUCCESS == libxsmm_get_malloc_xinfo(code.ptr_const, &size, NULL/*flags*/, &buffer)) {
              libxsmm_xfree(code.ptr_const);
              /* round-up size (it is fine to assume 4 KB pages since it is likely more accurate than not rounding up) */
              internal_registry_nbytes += (unsigned int)LIBXSMM_UP2(size + (((char*)code.ptr_const) - (char*)buffer), 4096/*4KB*/);
            }
          }
        }
      }
#if defined(LIBXSMM_TRACE)
      i = libxsmm_trace_finalize();
      if (EXIT_SUCCESS != i && 0 != libxsmm_verbosity) { /* library code is expected to be mute */
        fprintf(stderr, "LIBXSMM ERROR: failed to finalize trace (error #%i)!\n", i);
      }
#endif
#if defined(LIBXSMM_PERF)
      libxsmm_perf_finalize();
#endif
      libxsmm_gemm_finalize();
      libxsmm_trans_finalize();
      libxsmm_dnn_finalize();
      /* make internal registry globally unavailable */
      LIBXSMM_ATOMIC(LIBXSMM_ATOMIC_STORE_ZERO, LIBXSMM_BITS)((uintptr_t*)regaddr, LIBXSMM_ATOMIC_SEQ_CST);
      internal_registry_keys = NULL;
      free(registry_keys);
      free(registry);
    }
#if (0 != LIBXSMM_SYNC) /* LIBXSMM_LOCK_RELEASE, but no LIBXSMM_LOCK_DESTROY */
# if (1 < INTERNAL_REGLOCK_MAXN)
    for (i = 0; i < internal_reglock_count; ++i) LIBXSMM_LOCK_RELEASE(LIBXSMM_REGLOCK, &internal_reglock[i].state);
# elif !defined(LIBXSMM_UNIFY_LOCKS)
    LIBXSMM_LOCK_RELEASE(LIBXSMM_REGLOCK, internal_reglock_ptr);
# endif
    LIBXSMM_LOCK_RELEASE(LIBXSMM_LOCK, &libxsmm_lock_global);
#endif
  }
}



LIBXSMM_API void libxsmm_sink(LIBXSMM_VARIADIC)
{
  /* does nothing else but sink the given arguments */
}


LIBXSMM_API int libxsmm_get_target_archid(void)
{
  LIBXSMM_INIT
#if !defined(__MIC__)
  return libxsmm_target_archid;
#else /* no JIT support */
  return LIBXSMM_MIN(libxsmm_target_archid, LIBXSMM_X86_SSE3);
#endif
}


LIBXSMM_API void libxsmm_set_target_archid(int id)
{
  int target_archid = LIBXSMM_TARGET_ARCH_UNKNOWN;
  switch (id) {
    case LIBXSMM_X86_AVX512_CPX:
    case LIBXSMM_X86_AVX512_CLX:
    case LIBXSMM_X86_AVX512_CORE:
    case LIBXSMM_X86_AVX512_KNM:
    case LIBXSMM_X86_AVX512_MIC:
    case LIBXSMM_X86_AVX512:
    case LIBXSMM_X86_AVX2:
    case LIBXSMM_X86_AVX:
    case LIBXSMM_X86_SSE4:
    case LIBXSMM_X86_SSE3:
    case LIBXSMM_TARGET_ARCH_GENERIC: {
      target_archid = id;
    } break;
    default: if (LIBXSMM_X86_GENERIC <= id) {
      target_archid = LIBXSMM_X86_GENERIC;
    }
    else {
      target_archid = libxsmm_cpuid();
    }
  }
  LIBXSMM_ATOMIC_STORE(&libxsmm_target_archid, target_archid, LIBXSMM_ATOMIC_RELAXED);
  if (0 != libxsmm_verbosity) { /* library code is expected to be mute */
    const int cpuid = libxsmm_cpuid();
    if (cpuid < target_archid) {
      const char *const target_arch = libxsmm_cpuid_name(target_archid);
      fprintf(stderr, "LIBXSMM WARNING: \"%s\" code may fail to run on \"%s\"!\n",
        target_arch, libxsmm_cpuid_name(cpuid));
    }
  }
}


LIBXSMM_API const char* libxsmm_get_target_arch(void)
{
  LIBXSMM_INIT
  return libxsmm_cpuid_name(libxsmm_target_archid);
}


/* function serves as a helper for implementing the Fortran interface */
LIBXSMM_API const char* libxsmmf_get_target_arch(int* length);
LIBXSMM_API const char* libxsmmf_get_target_arch(int* length)
{
  const char *const arch = libxsmm_get_target_arch();
  /* valid here since function is not in the public interface */
  LIBXSMM_ASSERT(NULL != arch && 0 != length);
  *length = (int)strlen(arch);
  return arch;
}


LIBXSMM_API void libxsmm_set_target_arch(const char* arch)
{
  const int cpuid = libxsmm_cpuid();
  int target_archid;
  if (NULL != arch && 0 != *arch) {
    const int jit = atoi(arch);
    if (0 == strcmp("0", arch)) {
      target_archid = LIBXSMM_X86_SSE3;
    }
    else if (0 < jit) {
      target_archid = LIBXSMM_X86_GENERIC + jit;
    }
    else if (0 == strcmp("cpx", arch)) {
      target_archid = LIBXSMM_X86_AVX512_CPX;
    }
    else if (0 == strcmp("clx", arch)) {
      target_archid = LIBXSMM_X86_AVX512_CLX;
    }
    else if (0 == strcmp("skx", arch) || 0 == strcmp("skl", arch)
          /* "avx3"/"avx512" previously enabled LIBXSMM_X86_AVX512 */
          || 0 == strcmp("avx3", arch) || 0 == strcmp("avx512", arch))
    {
      target_archid = LIBXSMM_X86_AVX512_CORE;
    }
    else if (0 == strcmp("knm", arch)) {
      target_archid = LIBXSMM_X86_AVX512_KNM;
    }
    else if (0 == strcmp("knl", arch) || 0 == strcmp("mic", arch)) {
      target_archid = LIBXSMM_X86_AVX512_MIC;
    }
    else if (0 == strcmp("hsw", arch) || 0 == strcmp("avx2", arch)) {
      target_archid = LIBXSMM_X86_AVX2;
    }
    else if (0 == strcmp("snb", arch) || 0 == strcmp("avx", arch)) {
      target_archid = LIBXSMM_X86_AVX;
    }
    else if (0 == strcmp("wsm", arch) || 0 == strcmp("nhm", arch) || 0 == strcmp("sse4", arch)
       || 0 == strcmp("sse4_1", arch) || 0 == strcmp("sse4.1", arch)
       || 0 == strcmp("sse4_2", arch) || 0 == strcmp("sse4.2", arch))
    {
      target_archid = LIBXSMM_X86_SSE4;
    }
    else if (0 == strcmp("sse", arch) || 0 == strcmp("sse3", arch)
        || 0 == strcmp("ssse3", arch) || 0 == strcmp("ssse", arch))
    {
      target_archid = LIBXSMM_X86_SSE3;
    }
    else if (0 == strcmp("x86", arch) || 0 == strcmp("x64", arch) || 0 == strcmp("sse2", arch)) {
      target_archid = LIBXSMM_X86_GENERIC;
    }
    else if (0 == strcmp("generic", arch) || 0 == strcmp("none", arch)) {
      target_archid = LIBXSMM_TARGET_ARCH_GENERIC;
    }
    else {
      target_archid = cpuid;
    }
  }
  else {
    target_archid = cpuid;
  }
  if (cpuid < target_archid) { /* warn about code path if beyond CPUID */
    if (0 != libxsmm_verbosity) { /* library code is expected to be mute */
      const char *const target_arch = libxsmm_cpuid_name(target_archid);
      fprintf(stderr, "LIBXSMM WARNING: \"%s\" code will fail to run on \"%s\"!\n",
        target_arch, libxsmm_cpuid_name(cpuid));
    }
#if 0 /* limit code path to confirmed features */
    target_archid = cpuid;
#endif
  }
  LIBXSMM_ATOMIC_STORE(&libxsmm_target_archid, target_archid, LIBXSMM_ATOMIC_RELAXED);
}


LIBXSMM_API int libxsmm_get_verbosity(void)
{
  LIBXSMM_INIT
  return libxsmm_verbosity;
}


LIBXSMM_API void libxsmm_set_verbosity(int level)
{
  LIBXSMM_INIT
  LIBXSMM_ATOMIC_STORE(&libxsmm_verbosity, level, LIBXSMM_ATOMIC_RELAXED);
}


LIBXSMM_API libxsmm_gemm_prefetch_type libxsmm_get_gemm_auto_prefetch(void)
{
  return (libxsmm_gemm_prefetch_type)libxsmm_gemm_auto_prefetch;
}


LIBXSMM_API void libxsmm_set_gemm_auto_prefetch(libxsmm_gemm_prefetch_type strategy)
{
  if (0 == internal_gemm_auto_prefetch_locked) { /* LIBXSMM_GEMM_PREFETCH environment takes precedence */
    LIBXSMM_ATOMIC_STORE(&libxsmm_gemm_auto_prefetch_default, strategy, LIBXSMM_ATOMIC_RELAXED);
    LIBXSMM_ATOMIC_STORE(&libxsmm_gemm_auto_prefetch, strategy, LIBXSMM_ATOMIC_RELAXED);
  }
}


LIBXSMM_API unsigned char libxsmm_typesize(libxsmm_datatype datatype)
{
  switch (datatype) {
    case LIBXSMM_DATATYPE_F64:  return 8;
    case LIBXSMM_DATATYPE_F32:  return 4;
    case LIBXSMM_DATATYPE_BF16: return 2;
    case LIBXSMM_DATATYPE_I64:  return 8;
    case LIBXSMM_DATATYPE_I32:  return 4;
    case LIBXSMM_DATATYPE_I16:  return 2;
    case LIBXSMM_DATATYPE_I8:   return 1;
    case LIBXSMM_DATATYPE_UNSUPPORTED: {
      static int error_once = 0;
      if (1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED)) {
        fprintf(stderr, "LIBXSMM ERROR: unsupported data type!\n");
      }
    } break;
  }
  LIBXSMM_ASSERT_MSG(0, "unsupported data type");
  return 1; /* avoid to return 0 to avoid div-by-zero in static analysis of depending code */
}


LIBXSMM_API_INTERN int libxsmm_dvalue(libxsmm_datatype datatype, const void* value, double* dvalue)
{
  int result = EXIT_SUCCESS;
  if (NULL != value && NULL != dvalue) {
    switch (datatype) {
      case LIBXSMM_DATATYPE_F64: *dvalue =         (*(const double*)value); break;
      case LIBXSMM_DATATYPE_F32: *dvalue = (double)(*(const float *)value); break;
      case LIBXSMM_DATATYPE_I32: *dvalue = (double)(*(const int   *)value); break;
      case LIBXSMM_DATATYPE_I16: *dvalue = (double)(*(const short *)value); break;
      case LIBXSMM_DATATYPE_I8:  *dvalue = (double)(*(const char  *)value); break;
      default: result = EXIT_FAILURE;
    }
  }
  else {
    result = EXIT_FAILURE;
  }
  return result;
}


LIBXSMM_API int libxsmm_cast(libxsmm_datatype datatype, double dvalue, void* value)
{
  int result = EXIT_SUCCESS;
  if (NULL != value) {
    switch (datatype) {
      case LIBXSMM_DATATYPE_F64: *(double     *)value =              dvalue; break;
      case LIBXSMM_DATATYPE_F32: *(float      *)value =       (float)dvalue; break;
      case LIBXSMM_DATATYPE_I32: *(int        *)value =         (int)dvalue; break;
      case LIBXSMM_DATATYPE_I16: *(short      *)value =       (short)dvalue; break;
      case LIBXSMM_DATATYPE_I8:  *(signed char*)value = (signed char)dvalue; break;
      default: result = EXIT_FAILURE;
    }
  }
  else {
    result = EXIT_FAILURE;
  }
  return result;
}


LIBXSMM_API const char* libxsmm_typename(libxsmm_datatype datatype)
{
  switch (datatype) {
    case LIBXSMM_DATATYPE_F64:  return "f64";
    case LIBXSMM_DATATYPE_F32:  return "f32";
    case LIBXSMM_DATATYPE_BF16: return "bf16";
    case LIBXSMM_DATATYPE_I64:  return "i64";
    case LIBXSMM_DATATYPE_I32:  return "i32";
    case LIBXSMM_DATATYPE_I16:  return "i16";
    case LIBXSMM_DATATYPE_I8:   return "i8";
    default: {
      if (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP(datatype) &&
          LIBXSMM_GEMM_PRECISION_I32 == LIBXSMM_GETENUM_OUT(datatype))
      {
        return "i16i32";
      }
      else if (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP(datatype) &&
               LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_OUT(datatype))
      {
        return "i16f32";
      }
      else if (LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP(datatype) &&
               LIBXSMM_GEMM_PRECISION_I32 == LIBXSMM_GETENUM_OUT(datatype))
      {
        return "i8i32";
      }
      else if (LIBXSMM_GEMM_PRECISION_BF16 == LIBXSMM_GETENUM_INP(datatype) &&
               LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_OUT(datatype))
      {
        return "bf16f32";
      }
      else {
        return "void";
      }
    }
  }
}


LIBXSMM_API_INLINE const char* internal_get_typesize_string(size_t typesize)
{
  static LIBXSMM_TLS char result[4];
  LIBXSMM_ASSERT(256 > typesize);
  if (10 > typesize) {
    result[0] = (char)('0' + typesize);
    result[1] = 0;
  }
  else {
    LIBXSMM_SNPRINTF(result, sizeof(result), "%i", (int)typesize);
  }
  return result;
}


LIBXSMM_API_INTERN int libxsmm_build(const libxsmm_build_request* request, unsigned int regindex, libxsmm_code_pointer* code)
{
  int result = EXIT_SUCCESS;
#if !defined(__MIC__)
  const char* target_arch = libxsmm_cpuid_name(libxsmm_target_archid);
  libxsmm_generated_code generated_code;
  char jit_name[256] = { 0 };

  /* large enough temporary buffer for generated code */
#if defined(NDEBUG)
  char jit_buffer[LIBXSMM_CODE_MAXSIZE];
  memset(&generated_code, 0, sizeof(generated_code));
  generated_code.generated_code = jit_buffer;
  generated_code.buffer_size = sizeof(jit_buffer);
#else
  memset(&generated_code, 0, sizeof(generated_code));
  generated_code.generated_code = malloc(LIBXSMM_CODE_MAXSIZE);
  generated_code.buffer_size = (NULL != generated_code.generated_code ? LIBXSMM_CODE_MAXSIZE : 0);
#endif
  /* setup code generation */
  generated_code.code_type = 2;
  generated_code.arch = libxsmm_target_archid;

  LIBXSMM_ASSERT(NULL != generated_code.generated_code || 0 == generated_code.buffer_size);
  LIBXSMM_ASSERT(NULL != request && 0 != libxsmm_target_archid);
  LIBXSMM_ASSERT(NULL != code && NULL == code->ptr_const);

  switch (request->kind) { /* generate kernel */
    case LIBXSMM_BUILD_KIND_GEMM: { /* small MxM kernel */
      LIBXSMM_ASSERT(NULL != request->descriptor.gemm);
      if (0 < request->descriptor.gemm->m   && 0 < request->descriptor.gemm->n   && 0 < request->descriptor.gemm->k &&
          0 < request->descriptor.gemm->lda && 0 < request->descriptor.gemm->ldb && 0 < request->descriptor.gemm->ldc)
      {
        const unsigned int m = request->descriptor.gemm->m, n = request->descriptor.gemm->n, k = request->descriptor.gemm->k;
# if !defined(LIBXSMM_DENY_RETARGET) /* disable: ECFLAGS=-DLIBXSMM_DENY_RETARGET */
        if (LIBXSMM_X86_AVX2 < libxsmm_target_archid &&
           (LIBXSMM_GEMM_PRECISION_F64 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.gemm->datatype) ||
            LIBXSMM_GEMM_PRECISION_F32 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.gemm->datatype)) &&
           (16 >= (m * k) || 16 >= (k * n) || 16 >= (m * n)))
        {
          generated_code.arch = LIBXSMM_X86_AVX2;
        }
# endif
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_gemm_kernel, &generated_code, request->descriptor.gemm);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const int uid = libxsmm_gemm_prefetch2uid((libxsmm_gemm_prefetch_type)request->descriptor.gemm->prefetch);
          const char *const tname = libxsmm_typename((libxsmm_datatype)request->descriptor.gemm->datatype);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_%s_%c%c_%ux%ux%u_%u_%u_%u_a%i_b%i_p%i_br%i.mxm", target_arch, tname,
            0 == (LIBXSMM_GEMM_FLAG_TRANS_A & request->descriptor.gemm->flags) ? 'n' : 't',
            0 == (LIBXSMM_GEMM_FLAG_TRANS_B & request->descriptor.gemm->flags) ? 'n' : 't', m, n, k,
            request->descriptor.gemm->lda, request->descriptor.gemm->ldb, request->descriptor.gemm->ldc,
          /*0 != (LIBXSMM_GEMM_FLAG_ALPHA_0 & request->descriptor.gemm->flags) ? 0 : */1,
            0 != (LIBXSMM_GEMM_FLAG_BETA_0  & request->descriptor.gemm->flags) ? 0 : 1, uid,
            0 == (LIBXSMM_GEMM_FLAG_BATCH_REDUCE & request->descriptor.gemm->flags) ? 0 : 1);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_SRSOA: { /* sparse SOA kernel, CSR format */
      LIBXSMM_ASSERT(NULL != request->descriptor.srsoa && 0 != request->descriptor.srsoa->gemm);
      LIBXSMM_ASSERT(NULL != request->descriptor.srsoa->row_ptr && 0 != request->descriptor.srsoa->column_idx && 0 != request->descriptor.srsoa->values);
      /* only floating point */
      if (LIBXSMM_GEMM_PRECISION_F64 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.srsoa->gemm->datatype) ||
          LIBXSMM_GEMM_PRECISION_F32 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.srsoa->gemm->datatype))
      {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_spgemm_csr_soa_kernel, &generated_code, request->descriptor.srsoa->gemm, target_arch,
          request->descriptor.srsoa->row_ptr, request->descriptor.srsoa->column_idx, request->descriptor.srsoa->values);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const int uid = libxsmm_gemm_prefetch2uid((libxsmm_gemm_prefetch_type)request->descriptor.srsoa->gemm->prefetch);
          const char *const tname = libxsmm_typename((libxsmm_datatype)request->descriptor.srsoa->gemm->datatype);
          const unsigned int nnz = (request->descriptor.srsoa->gemm->lda == 0) ?
            request->descriptor.srsoa->row_ptr[request->descriptor.srsoa->gemm->m] : request->descriptor.srsoa->row_ptr[request->descriptor.srsoa->gemm->k];
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_%s_%c%c_%ux%ux%u_%u_%u_%u_a%i_b%i_p%i_nnz%u.srsoa", target_arch, tname,
            0 == (LIBXSMM_GEMM_FLAG_TRANS_A & request->descriptor.srsoa->gemm->flags) ? 'n' : 't',
            0 == (LIBXSMM_GEMM_FLAG_TRANS_B & request->descriptor.srsoa->gemm->flags) ? 'n' : 't',
            request->descriptor.srsoa->gemm->m,   request->descriptor.srsoa->gemm->n,   request->descriptor.srsoa->gemm->k,
            request->descriptor.srsoa->gemm->lda, request->descriptor.srsoa->gemm->ldb, request->descriptor.srsoa->gemm->ldc,
          /*0 != (LIBXSMM_GEMM_FLAG_ALPHA_0 & request->descriptor.srsoa->gemm->flags) ? 0 : */1,
            0 != (LIBXSMM_GEMM_FLAG_BETA_0  & request->descriptor.srsoa->gemm->flags) ? 0 : 1,
            uid, nnz);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_SCSOA: { /* sparse SOA kernel, CSC format */
      LIBXSMM_ASSERT(NULL != request->descriptor.scsoa && 0 != request->descriptor.scsoa->gemm);
      LIBXSMM_ASSERT(NULL != request->descriptor.scsoa->row_idx && 0 != request->descriptor.scsoa->column_ptr && 0 != request->descriptor.scsoa->values);
      /* only floating point */
      if (LIBXSMM_GEMM_PRECISION_F64 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.scsoa->gemm->datatype) ||
          LIBXSMM_GEMM_PRECISION_F32 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.scsoa->gemm->datatype))
      {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_spgemm_csc_soa_kernel, &generated_code, request->descriptor.scsoa->gemm, target_arch,
          request->descriptor.scsoa->row_idx, request->descriptor.scsoa->column_ptr, request->descriptor.scsoa->values);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const int uid = libxsmm_gemm_prefetch2uid((libxsmm_gemm_prefetch_type)request->descriptor.scsoa->gemm->prefetch);
          const char *const tname = libxsmm_typename((libxsmm_datatype)request->descriptor.scsoa->gemm->datatype);
          const unsigned int nnz = (request->descriptor.scsoa->gemm->lda == 0) ?
            request->descriptor.scsoa->column_ptr[request->descriptor.scsoa->gemm->k] : request->descriptor.scsoa->column_ptr[request->descriptor.scsoa->gemm->n];
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_%s_%c%c_%ux%ux%u_%u_%u_%u_a%i_b%i_p%i_nnz%u.scsoa", target_arch, tname,
            0 == (LIBXSMM_GEMM_FLAG_TRANS_A & request->descriptor.scsoa->gemm->flags) ? 'n' : 't',
            0 == (LIBXSMM_GEMM_FLAG_TRANS_B & request->descriptor.scsoa->gemm->flags) ? 'n' : 't',
            request->descriptor.scsoa->gemm->m,   request->descriptor.scsoa->gemm->n,   request->descriptor.scsoa->gemm->k,
            request->descriptor.scsoa->gemm->lda, request->descriptor.scsoa->gemm->ldb, request->descriptor.scsoa->gemm->ldc,
          /*0 != (LIBXSMM_GEMM_FLAG_ALPHA_0 & request->descriptor.scsoa->gemm->flags) ? 0 : */1,
            0 != (LIBXSMM_GEMM_FLAG_BETA_0  & request->descriptor.scsoa->gemm->flags) ? 0 : 1,
            uid, nnz);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_RMACSOA: { /* dense SOA kernel, CSC format */
      LIBXSMM_ASSERT(NULL != request->descriptor.rmacsoa && 0 != request->descriptor.rmacsoa->gemm);
      /* only floating point */
      if (LIBXSMM_GEMM_PRECISION_F64 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.rmacsoa->gemm->datatype) ||
          LIBXSMM_GEMM_PRECISION_F32 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.rmacsoa->gemm->datatype))
      {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_gemm_rm_ac_soa, &generated_code, request->descriptor.rmacsoa->gemm, target_arch);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const int uid = libxsmm_gemm_prefetch2uid((libxsmm_gemm_prefetch_type)request->descriptor.rmacsoa->gemm->prefetch);
          const char *const tname = libxsmm_typename((libxsmm_datatype)request->descriptor.rmacsoa->gemm->datatype);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_%s_%c%c_%ux%ux%u_%u_%u_%u_a%i_b%i_p%i.rmacsoa", target_arch, tname,
            0 == (LIBXSMM_GEMM_FLAG_TRANS_A & request->descriptor.rmacsoa->gemm->flags) ? 'n' : 't',
            0 == (LIBXSMM_GEMM_FLAG_TRANS_B & request->descriptor.rmacsoa->gemm->flags) ? 'n' : 't',
            request->descriptor.rmacsoa->gemm->m,   request->descriptor.rmacsoa->gemm->n,   request->descriptor.rmacsoa->gemm->k,
            request->descriptor.rmacsoa->gemm->lda, request->descriptor.rmacsoa->gemm->ldb, request->descriptor.rmacsoa->gemm->ldc,
          /*0 != (LIBXSMM_GEMM_FLAG_ALPHA_0 & request->descriptor.rmacsoa->gemm->flags) ? 0 : */1,
            0 != (LIBXSMM_GEMM_FLAG_BETA_0  & request->descriptor.rmacsoa->gemm->flags) ? 0 : 1,
            uid);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_RMBCSOA: { /* sparse SOA kernel, CSC format */
      LIBXSMM_ASSERT(NULL != request->descriptor.rmbcsoa && 0 != request->descriptor.rmbcsoa->gemm);
      /* only floating point */
      if (LIBXSMM_GEMM_PRECISION_F64 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.rmbcsoa->gemm->datatype) ||
          LIBXSMM_GEMM_PRECISION_F32 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.rmbcsoa->gemm->datatype))
      {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_gemm_rm_bc_soa, &generated_code, request->descriptor.rmbcsoa->gemm, target_arch);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const int uid = libxsmm_gemm_prefetch2uid((libxsmm_gemm_prefetch_type)request->descriptor.rmbcsoa->gemm->prefetch);
          const char *const tname = libxsmm_typename((libxsmm_datatype)request->descriptor.rmbcsoa->gemm->datatype);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_%s_%c%c_%ux%ux%u_%u_%u_%u_a%i_b%i_p%i.rmbcsoa", target_arch, tname,
            0 == (LIBXSMM_GEMM_FLAG_TRANS_A & request->descriptor.rmbcsoa->gemm->flags) ? 'n' : 't',
            0 == (LIBXSMM_GEMM_FLAG_TRANS_B & request->descriptor.rmbcsoa->gemm->flags) ? 'n' : 't',
            request->descriptor.rmbcsoa->gemm->m,   request->descriptor.rmbcsoa->gemm->n,   request->descriptor.rmbcsoa->gemm->k,
            request->descriptor.rmbcsoa->gemm->lda, request->descriptor.rmbcsoa->gemm->ldb, request->descriptor.rmbcsoa->gemm->ldc,
          /*0 != (LIBXSMM_GEMM_FLAG_ALPHA_0 & request->descriptor.rmbcsoa->gemm->flags) ? 0 : */1,
            0 != (LIBXSMM_GEMM_FLAG_BETA_0  & request->descriptor.rmbcsoa->gemm->flags) ? 0 : 1,
            uid);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_SREG: { /* sparse register kernel */
      LIBXSMM_ASSERT(NULL != request->descriptor.sreg && 0 != request->descriptor.sreg->gemm);
      LIBXSMM_ASSERT(NULL != request->descriptor.sreg->row_ptr && 0 != request->descriptor.sreg->column_idx && 0 != request->descriptor.sreg->values);
#if 1
      if (LIBXSMM_GEMM_PRECISION_F64 == /*LIBXSMM_GETENUM_OUT*/(request->descriptor.sreg->gemm->datatype)) /* only double-precision */
#endif
      {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_spgemm_csr_reg_kernel, &generated_code, request->descriptor.sreg->gemm, target_arch,
          request->descriptor.sreg->row_ptr, request->descriptor.sreg->column_idx,
          (const double*)request->descriptor.sreg->values);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const int uid = libxsmm_gemm_prefetch2uid((libxsmm_gemm_prefetch_type)request->descriptor.sreg->gemm->prefetch);
          const char *const tname = libxsmm_typename((libxsmm_datatype)request->descriptor.sreg->gemm->datatype);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_%s_%c%c_%ux%ux%u_%u_%u_%u_a%i_b%i_p%i.sreg", target_arch, tname,
            0 == (LIBXSMM_GEMM_FLAG_TRANS_A & request->descriptor.sreg->gemm->flags) ? 'n' : 't',
            0 == (LIBXSMM_GEMM_FLAG_TRANS_B & request->descriptor.sreg->gemm->flags) ? 'n' : 't',
            request->descriptor.sreg->gemm->m,   request->descriptor.sreg->gemm->n,   request->descriptor.sreg->gemm->k,
            request->descriptor.sreg->gemm->lda, request->descriptor.sreg->gemm->ldb, request->descriptor.sreg->gemm->ldc,
          /*0 != (LIBXSMM_GEMM_FLAG_ALPHA_0 & request->descriptor.sreg->gemm->flags) ? 0 : */1,
            0 != (LIBXSMM_GEMM_FLAG_BETA_0  & request->descriptor.sreg->gemm->flags) ? 0 : 1,
            uid);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_CFWD: { /* forward convolution */
      LIBXSMM_ASSERT(NULL != request->descriptor.cfwd);
      if (0 < request->descriptor.cfwd->kw && 0 < request->descriptor.cfwd->kh &&
          0 != request->descriptor.cfwd->stride_w && 0 != request->descriptor.cfwd->stride_h)
      {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_convolution_forward_kernel, &generated_code, request->descriptor.cfwd, target_arch);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const char *const precision_in  = libxsmm_typename((libxsmm_datatype)request->descriptor.cfwd->datatype);
          const char *const precision_out = libxsmm_typename((libxsmm_datatype)request->descriptor.cfwd->datatype_itm);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          if (request->descriptor.cfwd->use_fwd_generator_for_bwd == 0) {
           LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_fwd_%s_%s_%ux%u_%ux%uu_s%ii%io_vl%ui%uo_ri%ux%u_ro%ux%u_r%ux%u_p%i_f%i.conv",
            target_arch/*code path name*/, precision_in, precision_out,
            (unsigned int)request->descriptor.cfwd->kw/*kernel width*/, (unsigned int)request->descriptor.cfwd->kh/*kernel height*/,
            (unsigned int)request->descriptor.cfwd->unroll_kw/*width*/, (unsigned int)request->descriptor.cfwd->unroll_kh/*height*/,
            (int)request->descriptor.cfwd->stride_w/*input offset*/, (int)request->descriptor.cfwd->stride_h/*output offsets*/,
            (unsigned int)request->descriptor.cfwd->ifm_block/*VLEN*/, (unsigned int)request->descriptor.cfwd->ofm_block/*VLEN*/,
            (unsigned int)request->descriptor.cfwd->ifw_padded, (unsigned int)request->descriptor.cfwd->ifh_padded,
            (unsigned int)request->descriptor.cfwd->ofw_padded/*1D and 2D register block*/,
            (unsigned int)request->descriptor.cfwd->ofh_padded/*2D register block*/,
            (unsigned int)request->descriptor.cfwd->ofw_rb/*register block ofw*/,
            (unsigned int)request->descriptor.cfwd->ofh_rb/*register block ofh*/,
            (int)request->descriptor.cfwd->prefetch/*binary OR'd prefetch flags*/,
            (int)request->descriptor.cfwd->format/*binary OR'd format flags*/);
          } else {
           LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_bwd_%s_%s_%ux%u_%ux%uu_s%ii%io_vl%ui%uo_ri%ux%u_ro%ux%u_r%ux%u_p%i_f%i.conv",
            target_arch/*code path name*/, precision_in, precision_out,
            (unsigned int)request->descriptor.cfwd->kw/*kernel width*/, (unsigned int)request->descriptor.cfwd->kh/*kernel height*/,
            (unsigned int)request->descriptor.cfwd->unroll_kw/*width*/, (unsigned int)request->descriptor.cfwd->unroll_kh/*height*/,
            (int)request->descriptor.cfwd->stride_w/*input offset*/, (int)request->descriptor.cfwd->stride_h/*output offsets*/,
            (unsigned int)request->descriptor.cfwd->ifm_block/*VLEN*/, (unsigned int)request->descriptor.cfwd->ofm_block/*VLEN*/,
            (unsigned int)request->descriptor.cfwd->ifw_padded, (unsigned int)request->descriptor.cfwd->ifh_padded,
            (unsigned int)request->descriptor.cfwd->ofw_padded/*1D and 2D register block*/,
            (unsigned int)request->descriptor.cfwd->ofh_padded/*2D register block*/,
            (unsigned int)request->descriptor.cfwd->ofw_rb/*register block ofw*/,
            (unsigned int)request->descriptor.cfwd->ofh_rb/*register block ofh*/,
            (int)request->descriptor.cfwd->prefetch/*binary OR'd prefetch flags*/,
            (int)request->descriptor.cfwd->format/*binary OR'd format flags*/);
          }
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_CUPD: { /* convolution update weights */
      LIBXSMM_ASSERT(NULL != request->descriptor.cupd);
      if (0 < request->descriptor.cupd->kw &&
          0 != request->descriptor.cupd->stride_w && 0 != request->descriptor.cupd->stride_h)
      {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_convolution_weight_update_kernel, &generated_code, request->descriptor.cupd, target_arch);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const char *const precision_in  = libxsmm_typename((libxsmm_datatype)request->descriptor.cupd->datatype);
          const char *const precision_out = libxsmm_typename((libxsmm_datatype)request->descriptor.cupd->datatype_itm);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_upd_%s_%s_%ux%u_s%ii%io_vl%ui%uo_ri%ux%u_ro%ux%u_r%ux%u_of%uu%ux%uu%u_if%uu_t%u_p%i_f%i.conv",
            target_arch/*code path name*/, precision_in, precision_out,
            (unsigned int)request->descriptor.cupd->kw/*kernel width*/, (unsigned int)request->descriptor.cupd->kh/*kernel height*/,
            (int)request->descriptor.cupd->stride_w/*input offset*/, (int)request->descriptor.cupd->stride_h/*output offsets*/,
            (unsigned int)request->descriptor.cupd->ifm_block/*VLEN*/, (unsigned int)request->descriptor.cupd->ofm_block/*VLEN*/,
            (unsigned int)request->descriptor.cupd->ifw_padded, (unsigned int)request->descriptor.cupd->ifh_padded,
            (unsigned int)request->descriptor.cupd->ofw_padded/*1D and 2D register block*/,
            (unsigned int)request->descriptor.cupd->ofh_padded/*2D register block*/,
            (unsigned int)request->descriptor.cupd->ofw_rb/*register block ofw*/,
            (unsigned int)request->descriptor.cupd->ofh_rb/*register block ofh*/,
            (unsigned int)request->descriptor.cupd->ofw/*ofw*/, (unsigned int)request->descriptor.cupd->ofw_unroll/*ofw_unroll*/,
            (unsigned int)request->descriptor.cupd->ofh/*ofh*/, (unsigned int)request->descriptor.cupd->ofh_unroll/*ofh_unroll*/,
            (unsigned int)request->descriptor.cupd->ifm_unroll/*ifm unroll*/,
            (unsigned int)request->descriptor.cupd->transpose_ofw_ifm/*transpose_ofw_ifm*/,
            (int)request->descriptor.cupd->prefetch/*binary OR'd prefetch flags*/,
            (int)request->descriptor.cupd->format/*binary OR'd format flags*/);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_MCOPY: { /* matcopy kernel */
      LIBXSMM_ASSERT(NULL != request->descriptor.mcopy);
      if (4 == request->descriptor.mcopy->typesize) {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_matcopy_kernel, &generated_code, request->descriptor.mcopy, target_arch);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const char *const tsizename = internal_get_typesize_string(request->descriptor.mcopy->typesize);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_tsize%s_%ux%u_%ux%u_p%u.mcopy", target_arch, tsizename,
            request->descriptor.mcopy->m, request->descriptor.mcopy->n, request->descriptor.mcopy->ldi, request->descriptor.mcopy->ldo,
            (unsigned int)request->descriptor.mcopy->prefetch);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_TRANS: { /* transpose kernel */
      LIBXSMM_ASSERT(NULL != request->descriptor.trans);
      if (4 == request->descriptor.trans->typesize || 8 == request->descriptor.trans->typesize) {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_transpose_kernel, &generated_code, request->descriptor.trans, libxsmm_target_archid);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const char *const tsizename = internal_get_typesize_string(request->descriptor.trans->typesize);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_tsize%s_%ux%u_%u.trans", target_arch, tsizename,
            request->descriptor.trans->m, request->descriptor.trans->n, request->descriptor.trans->ldo);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_PGEMM: { /* compact P/GEMM-kernel (packed) */
      unsigned int tsize;
      LIBXSMM_ASSERT(NULL != request->descriptor.pgemm);
      tsize = (unsigned int)request->descriptor.pgemm->typesize;
      if (4 == tsize || 8 == tsize) {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_pgemm_kernel, &generated_code, request->descriptor.pgemm, libxsmm_target_archid);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const char *const tsizename = internal_get_typesize_string(tsize);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_tsize%s_%c%c%c_%ux%ux%u_%u_%u_%u_%i.pgemm", target_arch, tsizename,
            request->descriptor.pgemm->transa, request->descriptor.pgemm->transb, request->descriptor.pgemm->layout,
            request->descriptor.pgemm->m, request->descriptor.pgemm->n, request->descriptor.pgemm->k,
            request->descriptor.pgemm->lda, request->descriptor.pgemm->ldb, request->descriptor.pgemm->ldc,
            (int)request->descriptor.pgemm->alpha_val);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_GETRF: { /* compact GETRF kernel (packed) */
      unsigned int tsize;
      LIBXSMM_ASSERT(NULL != request->descriptor.getrf);
      tsize = (unsigned int)request->descriptor.getrf->typesize;
      if (4 == tsize || 8 == tsize) {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_getrf_kernel, &generated_code, request->descriptor.getrf, libxsmm_target_archid);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const char *const tsizename = internal_get_typesize_string(tsize);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_tsize%s_%c_%ux%u_%u.getrf", target_arch, tsizename,
            request->descriptor.getrf->layout, request->descriptor.getrf->m, request->descriptor.getrf->n, request->descriptor.getrf->lda);
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_TRMM: { /* compact TRMM kernel (packed) */
      unsigned int tsize;
      LIBXSMM_ASSERT(NULL != request->descriptor.trmm);
      tsize = (unsigned int)request->descriptor.trmm->typesize;
      if (4 == tsize || 8 == tsize) {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_trmm_kernel, &generated_code, request->descriptor.trmm, target_arch);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const char *const tsizename = internal_get_typesize_string(tsize);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_tsize%s_%c%c%c%c_%ux%u_%u_%u.trmm", target_arch, tsizename,
            request->descriptor.trmm->transa, request->descriptor.trmm->layout, request->descriptor.trmm->side, request->descriptor.trmm->uplo,
            request->descriptor.trmm->m, request->descriptor.trmm->n, request->descriptor.trmm->lda, request->descriptor.trmm->ldb); /* TODO: alpha */
        }
      }
    } break;
    case LIBXSMM_BUILD_KIND_TRSM: { /* compact TRSM kernel (packed) */
      unsigned int tsize;
      LIBXSMM_ASSERT(NULL != request->descriptor.trsm);
      tsize = (unsigned int)request->descriptor.trsm->typesize;
      if (4 == tsize || 8 == tsize) {
        LIBXSMM_NO_OFFLOAD(void, libxsmm_generator_trsm_kernel, &generated_code, request->descriptor.trsm, target_arch);
# if !defined(LIBXSMM_VTUNE)
        if (0 > libxsmm_verbosity)
# endif
        {
          const char *const tsizename = internal_get_typesize_string(tsize);
          /* adopt scheme which allows kernel names of LIBXSMM to appear in order (Intel VTune, etc.) */
          LIBXSMM_SNPRINTF(jit_name, sizeof(jit_name), "libxsmm_%s_tsize%s_%c%c%c%c_%ux%u_%u_%u.trsm", target_arch, tsizename,
            request->descriptor.trsm->transa, request->descriptor.trsm->layout, request->descriptor.trsm->side, request->descriptor.trsm->uplo,
            request->descriptor.trsm->m, request->descriptor.trsm->n, request->descriptor.trsm->lda, request->descriptor.trsm->ldb); /* TODO: alpha */
        }
      }
    } break;
# if !defined(NDEBUG) /* library code is expected to be mute */
    default: { /* unknown kind */
      static int error_once = 0;
      if (1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED)) {
        fprintf(stderr, "LIBXSMM ERROR: invalid build request discovered!\n");
      }
      result = EXIT_FAILURE;
    }
# endif
  }

  /* handle an eventual error in the else-branch */
  if (0 < generated_code.code_size) {
    LIBXSMM_ASSERT(generated_code.code_size <= LIBXSMM_CODE_MAXSIZE);
    LIBXSMM_ASSERT(NULL != generated_code.generated_code);
    if (0 == generated_code.last_error) { /* no error raised */
      char* code_buffer = NULL;
      void* code_buffer_result = &code_buffer;
      /* attempt to create executable buffer */
      result = libxsmm_xmalloc((void**)code_buffer_result, generated_code.code_size, 0/*auto*/,
        /* flag must be a superset of what's populated by libxsmm_malloc_attrib */
        LIBXSMM_MALLOC_FLAG_RWX, &regindex, sizeof(regindex));
      if (EXIT_SUCCESS == result) { /* check for success */
        LIBXSMM_ASSERT(NULL != code_buffer);
        /* copy temporary buffer into the prepared executable buffer */
#if defined(NDEBUG)
        { int i; /* precondition: jit_buffer == generated_code.generated_code */
          for (i = 0; i < (int)generated_code.code_size; ++i) code_buffer[i] = jit_buffer[i];
        }
#else
        memcpy(code_buffer, generated_code.generated_code, generated_code.code_size);
#endif
        /* attribute/protect buffer and revoke unnecessary flags */
        result = libxsmm_malloc_attrib((void**)code_buffer_result, LIBXSMM_MALLOC_FLAG_X, jit_name);
        if (EXIT_SUCCESS == result) { /* check for success */
          code->pmm = code_buffer; /* commit buffer */
          LIBXSMM_ASSERT(NULL != code->pmm && 0 == (LIBXSMM_CODE_STATIC & code->uval));
        }
        else { /* release buffer */
          libxsmm_xfree(code_buffer);
        }
      }
    }
    else {
      result = generated_code.last_error;
    }
  }
  else {
    result = EXIT_FAILURE;
  }
# if !defined(NDEBUG)
  free(generated_code.generated_code); /* free temporary/initial code buffer */
# endif
#else /* unsupported platform */
  LIBXSMM_UNUSED(request); LIBXSMM_UNUSED(regindex); LIBXSMM_UNUSED(code);
  /* libxsmm_get_target_arch also serves as a runtime check whether JIT is available or not */
  if (LIBXSMM_X86_SSE3 <= libxsmm_target_archid) result = EXIT_FAILURE;
#endif
  LIBXSMM_ASSERT(NULL != code->pmm || EXIT_FAILURE == result);
  return result;
}


#if defined(LIBXSMM_DESC_PAD)
LIBXSMM_API_INLINE void internal_pad_descriptor(libxsmm_descriptor* desc, size_t size)
{
  size_t i = size;
  size = LIBXSMM_MAX(LIBXSMM_DIFF_SIZE, LIBXSMM_HASH_SIZE);
  LIBXSMM_ASSERT(NULL != desc && i <= size && size <= LIBXSMM_DESCRIPTOR_MAXSIZE);
  for (; i < size; ++i) desc->data[i] = 0;
}
#endif


LIBXSMM_API_INLINE libxsmm_code_pointer internal_find_code(libxsmm_descriptor* desc, size_t desc_size)
{
  libxsmm_code_pointer flux_entry = { 0 };
  const size_t size = sizeof(libxsmm_descriptor_kind) + desc_size;
#if !defined(NDEBUG) && (0 != LIBXSMM_JIT)
  int build = EXIT_SUCCESS;
#endif
#if defined(LIBXSMM_CACHE_MAXSIZE) && (0 < (LIBXSMM_CACHE_MAXSIZE))
  static LIBXSMM_TLS struct {
    libxsmm_descriptor keys[LIBXSMM_CACHE_MAXSIZE];
    libxsmm_code_pointer code[LIBXSMM_CACHE_MAXSIZE];
    unsigned int id; /* to invalidate cache */
    unsigned char size, hit;
  } cache;
  unsigned char cache_index;
# if defined(LIBXSMM_DESC_PAD)
#   if defined(LIBXSMM_DESC_INLINE)
  LIBXSMM_DIFF_DECL(LIBXSMM_DIFF_SIZE, xdesc);
  internal_pad_descriptor(desc, size);
  LIBXSMM_DIFF_LOAD(LIBXSMM_DIFF_SIZE, xdesc, desc);
  LIBXSMM_DIFF_N(unsigned char, cache_index, LIBXSMM_DIFF(LIBXSMM_DIFF_SIZE),
    xdesc, &cache.keys, LIBXSMM_DIFF_SIZE, LIBXSMM_DESCRIPTOR_MAXSIZE, cache.hit, cache.size);
#   else
  internal_pad_descriptor(desc, size);
  cache_index = (unsigned char)libxsmm_diff_n(desc, &cache.keys,
    LIBXSMM_DIFF_SIZE, LIBXSMM_DESCRIPTOR_MAXSIZE, cache.hit, cache.size);
#   endif
# else
  LIBXSMM_ASSERT(NULL != desc);
  cache_index = (unsigned char)libxsmm_diff_n(desc, &cache.keys,
    LIBXSMM_MIN(size, LIBXSMM_DIFF_SIZE), LIBXSMM_DESCRIPTOR_MAXSIZE, cache.hit, cache.size);
# endif
  if (cache_index < cache.size && cache.id == libxsmm_ninit) { /* valid hit */
    flux_entry = cache.code[cache_index];
    cache.hit = cache_index;
  }
  else
#else
  LIBXSMM_ASSERT(NULL != desc);
# if defined(LIBXSMM_DESC_PAD)
# if defined(LIBXSMM_DESC_INLINE)
  LIBXSMM_DIFF_DECL(LIBXSMM_DIFF_SIZE, xdesc);
  internal_pad_descriptor(desc, size);
  LIBXSMM_DIFF_LOAD(LIBXSMM_DIFF_SIZE, xdesc, desc);
# else
  internal_pad_descriptor(desc, size);
# endif
# endif
#endif
  {
#if defined(LIBXSMM_DESC_PAD)
    unsigned int i = LIBXSMM_CONCATENATE(libxsmm_crc32_b, LIBXSMM_HASH_SIZE)(LIBXSMM_HASH_SEED, desc);
#else
    unsigned int i = libxsmm_crc32(LIBXSMM_HASH_SEED, desc, LIBXSMM_MIN(size, LIBXSMM_HASH_SIZE));
#endif
    unsigned int i0 = i = LIBXSMM_MOD2(i, LIBXSMM_CAPACITY_REGISTRY), mode = 0, diff = 1;
    LIBXSMM_ASSERT(NULL != internal_registry);
    LIBXSMM_ASSERT(&desc->kind == &desc->gemm.pad && desc->kind == desc->gemm.pad);
    do { /* use calculated location and check if the requested code is already JITted */
#if (1 < INTERNAL_REGLOCK_MAXN) || !LIBXSMM_LOCK_TYPE_ISRW(LIBXSMM_REGLOCK) /* read registered code */
# if 1 /* omitting an atomic load is safe but avoids race-detectors to highlight this location */
      uintptr_t *const fluxaddr = &internal_registry[i].uval;
      flux_entry.uval = LIBXSMM_ATOMIC(LIBXSMM_ATOMIC_LOAD, LIBXSMM_BITS)(fluxaddr, LIBXSMM_ATOMIC_RELAXED);
# else
      flux_entry = internal_registry[i];
# endif
#else
      LIBXSMM_LOCK_ACQREAD(LIBXSMM_REGLOCK, internal_reglock_ptr);
      flux_entry = internal_registry[i]; /* read registered code */
      LIBXSMM_LOCK_RELREAD(LIBXSMM_REGLOCK, internal_reglock_ptr);
#endif
      if ((NULL != flux_entry.ptr_const || 1 == mode) && 2 > mode) { /* check existing entry further */
        if (NULL != flux_entry.ptr_const) {
#if defined(LIBXSMM_DESC_PAD)
# if defined(LIBXSMM_DIFF_INLINE)
#   if !defined(LIBXSMM_DESC_INLINE)
          LIBXSMM_DIFF_DECL(LIBXSMM_DIFF_SIZE, xdesc);
          LIBXSMM_DIFF_LOAD(LIBXSMM_DIFF_SIZE, xdesc, desc);
#   endif
          diff = LIBXSMM_DIFF(LIBXSMM_DIFF_SIZE)(xdesc, internal_registry_keys + i, 0/*dummy*/);
# else
          diff = libxsmm_diff(desc, internal_registry_keys + i, LIBXSMM_DIFF_SIZE);
# endif
#else
          diff = libxsmm_diff(desc, internal_registry_keys + i, LIBXSMM_MIN(size, LIBXSMM_DIFF_SIZE));
#endif
        }
#if !defined(NDEBUG)
        else LIBXSMM_ASSERT(0 != diff);
#endif
        if (0 != diff) { /* search for code version */
          if (0 == mode) { /* transition to higher mode */
            i0 = i; /* keep current position on record */
#if defined(LIBXSMM_HASH_COLLISION)
            /* enter code generation, and collision fix-up */
            if (0 == (LIBXSMM_HASH_COLLISION & flux_entry.uval)) {
              LIBXSMM_ASSERT(NULL != flux_entry.ptr_const); /* collision */
              mode = 3;
            }
            else
#endif      /* search for an existing code version */
            mode = 1; /* else */
          }
          i = LIBXSMM_MOD2(i + 1, LIBXSMM_CAPACITY_REGISTRY);
          if (i == i0) { /* search finished, no code version exists */
#if defined(LIBXSMM_HASH_COLLISION)
            mode = 3; /* enter code generation, and collision fix-up */
#else
            mode = 2; /* enter code generation */
#endif
            if (LIBXSMM_KERNEL_KIND_MATMUL == desc->kind) {
              internal_update_mmstatistic(&desc->gemm.desc, 0, 1/*collision*/);
            }
          }
          LIBXSMM_ASSERT(0 != diff); /* continue */
        }
      }
      else { /* enter code generation (there is no code version yet) */
        LIBXSMM_ASSERT(0 == mode || 1 < mode);
#if (0 != LIBXSMM_JIT)
        if (LIBXSMM_X86_AVX <= libxsmm_target_archid || /* check if JIT is supported (CPUID) */
           (LIBXSMM_X86_SSE3 <= libxsmm_target_archid && LIBXSMM_BUILD_KIND_GEMM == desc->kind))
        {
          LIBXSMM_ASSERT(0 != mode || NULL == flux_entry.ptr_const/*code version does not exist*/);
          INTERNAL_FIND_CODE_LOCK(lock, i, diff, flux_entry.pmm); /* lock the registry entry */
          if (NULL == internal_registry[i].ptr_const) { /* double-check registry after acquiring the lock */
            libxsmm_build_request request; /* setup the code build request */
            LIBXSMM_ASSERT(desc->kind < LIBXSMM_KERNEL_KIND_INVALID);
            request.kind = (libxsmm_build_kind)desc->kind;
            request.descriptor.ptr = &desc->gemm.desc;
#if defined(NDEBUG)
            if (EXIT_SUCCESS == libxsmm_build(&request, i, &flux_entry) && NULL != flux_entry.ptr_const)
#else
            build = libxsmm_build(&request, i, &flux_entry);
            if (EXIT_SUCCESS == build && NULL != flux_entry.ptr_const)
#endif
            {
              internal_registry_keys[i] = *desc;
# if (1 < INTERNAL_REGLOCK_MAXN)
              LIBXSMM_ATOMIC(LIBXSMM_ATOMIC_STORE, LIBXSMM_BITS)(&internal_registry[i].pmm, flux_entry.pmm, LIBXSMM_ATOMIC_SEQ_CST);
# else
              internal_registry[i] = flux_entry;
# endif
# if defined(LIBXSMM_HASH_COLLISION)
              if (2 < mode) { /* arrived from collision state; now mark as collision */
                libxsmm_code_pointer fix_entry;
#   if (1 < INTERNAL_REGLOCK_MAXN)
                fix_entry.pmm = LIBXSMM_ATOMIC_LOAD(&internal_registry[i0].pmm, LIBXSMM_ATOMIC_RELAXED);
#   else
                fix_entry = internal_registry[i0];
#   endif
                LIBXSMM_ASSERT(NULL != fix_entry.ptr_const);
                if (0 == (LIBXSMM_HASH_COLLISION & fix_entry.uval)) {
                  fix_entry.uval |= LIBXSMM_HASH_COLLISION; /* mark current entry as collision */
#   if (1 < INTERNAL_REGLOCK_MAXN)
                  LIBXSMM_ATOMIC_STORE(&internal_registry[i0].pmm, fix_entry.pmm, LIBXSMM_ATOMIC_RELAXED);
#   else
                  internal_registry[i0] = fix_entry;
#   endif
                }
              }
# endif
            }
            /* leave here even in case of a build-error; do not use break (inside of locked region) */
            diff = 0;
          }
          INTERNAL_FIND_CODE_UNLOCK(lock);
          if (0 != diff) { /* acquire registry slot */
            if (0 == mode) { /* initial condition */
              mode = 2; /* continue to linearly search for an empty slot */
              i0 = i; /* keep current position on record */
            }
            do { /* continue to linearly search for an available slot */
              i = LIBXSMM_MOD2(i + 1, LIBXSMM_CAPACITY_REGISTRY);
              if (NULL == internal_registry[i].ptr_const) break;
            } while (i != i0);
            if (i == i0) { /* out of capacity (no registry slot available) */
              diff = 0; /* inside of locked region (do not use break!) */
            }
            flux_entry.pmm = NULL; /* no result */
          }
        }
        else /* JIT-code generation not available */
#endif
        { /* leave the dispatch loop */
#if !defined(NDEBUG) && (0 != LIBXSMM_JIT)
          build = EXIT_FAILURE;
#endif
          flux_entry.pmm = NULL;
          diff = 0;
        }
        if (((int)LIBXSMM_KERNEL_KIND_MATMUL) == desc->kind) {
          internal_update_mmstatistic(&desc->gemm.desc, 1/*try*/, 0);
        }
      }
    } while (0 != diff);
#if defined(LIBXSMM_CACHE_MAXSIZE) && (0 < (LIBXSMM_CACHE_MAXSIZE))
    if (NULL != flux_entry.ptr_const) { /* keep code version on record (cache) */
      if (cache.id != libxsmm_ninit) { /* invalidate */
        memset(cache.keys, 0, sizeof(cache.keys));
        cache.id = libxsmm_ninit;
        cache.size = cache.hit = 0;
      }
      if (cache.size < (LIBXSMM_CACHE_MAXSIZE)) { /* grow */
        INTERNAL_FIND_CODE_CACHE_GROW(cache_index, cache.size);
        LIBXSMM_ASSERT(cache.size <= LIBXSMM_CACHE_MAXSIZE);
      }
      else { /* evict */
        INTERNAL_FIND_CODE_CACHE_EVICT(cache_index, cache.size, cache.hit);
      }
      cache.keys[cache_index] = *desc;
      cache.code[cache_index] = flux_entry;
      cache.hit = cache_index;
      LIBXSMM_ASSERT(0 == diff);
    }
#endif
  }
#if defined(LIBXSMM_HASH_COLLISION)
  flux_entry.uval &= ~(LIBXSMM_CODE_STATIC | LIBXSMM_HASH_COLLISION); /* clear non-JIT and collision flag */
#else
  flux_entry.uval &= ~LIBXSMM_CODE_STATIC; /* clear non-JIT flag */
#endif
#if (0 != LIBXSMM_JIT)
  assert(LIBXSMM_BUILD_KIND_GEMM != desc->kind || NULL != flux_entry.ptr_const || EXIT_SUCCESS != build || 1 == internal_reglock_count); /*!LIBXSMM_ASSERT*/
#endif
  return flux_entry;
}


LIBXSMM_API const libxsmm_descriptor* libxsmm_get_kernel_info(libxsmm_code_pointer code, size_t* size)
{
  const libxsmm_descriptor* result;
  void* extra = NULL;
  if (NULL != size) *size = 0;
  if (NULL != code.ptr_const && NULL != internal_registry && NULL != internal_registry_keys
    && EXIT_SUCCESS == libxsmm_get_malloc_xinfo(code.ptr_const, size, NULL/*flags*/, &extra)
    && NULL != extra && *((const unsigned int*)extra) < (LIBXSMM_CAPACITY_REGISTRY)
#if defined(LIBXSMM_HASH_COLLISION)
    && code.uval == (~LIBXSMM_HASH_COLLISION & internal_registry[*((const unsigned int*)extra)].uval)
#else
    && code.ptr_const == internal_registry[*((const unsigned int*)extra)].ptr_const
#endif
    && internal_registry_keys[*((const unsigned int*)extra)].kind < LIBXSMM_KERNEL_KIND_INVALID)
  {
    result = internal_registry_keys + *((const unsigned int*)extra);
  }
  else {
    result = NULL;
  }
  return result;
}


LIBXSMM_API int libxsmm_get_kernel_kind(const void* kernel, libxsmm_kernel_kind* kind)
{
  const libxsmm_descriptor* info;
  libxsmm_code_pointer code;
  int result;
  code.ptr_const = kernel;
  info = libxsmm_get_kernel_info(code, NULL/*code_size*/);
  if (NULL != info && NULL != kind) {
    *kind = (libxsmm_kernel_kind)info->kind;
    result = EXIT_SUCCESS;
  }
  else {
    if (NULL != kind) *kind = LIBXSMM_KERNEL_KIND_INVALID;
    result = EXIT_FAILURE;
  }
  return result;
}


LIBXSMM_API int libxsmm_get_mmkernel_info(libxsmm_xmmfunction kernel, libxsmm_mmkernel_info* info, size_t* code_size)
{
  libxsmm_code_pointer code;
  static int error_once = 0;
  int result;
  code.xgemm = kernel;
  if (NULL != info || NULL != code_size) {
    const libxsmm_descriptor *const kernel_info = libxsmm_get_kernel_info(code, code_size);
    if (NULL != kernel_info && LIBXSMM_KERNEL_KIND_MATMUL == kernel_info->kind) {
      if (NULL != info) {
        info->iprecision = (libxsmm_gemm_precision)LIBXSMM_GETENUM_INP(kernel_info->gemm.desc.datatype);
        info->oprecision = (libxsmm_gemm_precision)LIBXSMM_GETENUM_OUT(kernel_info->gemm.desc.datatype);
        info->prefetch = (libxsmm_gemm_prefetch_type)kernel_info->gemm.desc.prefetch;
        info->flags = kernel_info->gemm.desc.flags;
        info->lda = kernel_info->gemm.desc.lda;
        info->ldb = kernel_info->gemm.desc.ldb;
        info->ldc = kernel_info->gemm.desc.ldc;
        info->m = kernel_info->gemm.desc.m;
        info->n = kernel_info->gemm.desc.n;
        info->k = kernel_info->gemm.desc.k;
      }
      result = EXIT_SUCCESS;
    }
    else {
      if (0 != libxsmm_verbosity /* library code is expected to be mute */
        && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
      {
        fprintf(stderr, "LIBXSMM ERROR: invalid kernel cannot be inspected!\n");
      }
      result = EXIT_FAILURE;
    }
  }
  else {
    if (0 != libxsmm_verbosity /* library code is expected to be mute */
      && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
    {
      fprintf(stderr, "LIBXSMM ERROR: invalid argument!\n");
    }
    result = EXIT_FAILURE;
  }
  return result;
}


LIBXSMM_API int libxsmm_get_transkernel_info(libxsmm_xtransfunction kernel, libxsmm_transkernel_info* info, size_t* code_size)
{
  libxsmm_code_pointer code;
  static int error_once = 0;
  int result;
  code.xtrans = kernel;
  if (NULL != info || 0 != code_size) {
    const libxsmm_descriptor *const kernel_info = libxsmm_get_kernel_info(code, code_size);
    if (NULL != kernel_info && LIBXSMM_KERNEL_KIND_TRANS == kernel_info->kind) {
      if (NULL != info) {
        info->typesize = kernel_info->trans.desc.typesize;
        info->ldo = kernel_info->trans.desc.ldo;
        info->m = kernel_info->trans.desc.m;
        info->n = kernel_info->trans.desc.n;
      }
      result = EXIT_SUCCESS;
    }
    else {
      if (0 != libxsmm_verbosity /* library code is expected to be mute */
        && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
      {
        fprintf(stderr, "LIBXSMM ERROR: invalid kernel cannot be inspected!\n");
      }
      result = EXIT_FAILURE;
    }
  }
  else {
    if (0 != libxsmm_verbosity /* library code is expected to be mute */
      && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
    {
      fprintf(stderr, "LIBXSMM ERROR: invalid argument!\n");
    }
    result = EXIT_FAILURE;
  }
  return result;
}


LIBXSMM_API int libxsmm_get_mcopykernel_info(libxsmm_xmcopyfunction kernel, libxsmm_mcopykernel_info* info, size_t* code_size)
{
  libxsmm_code_pointer code;
  static int error_once = 0;
  int result;
  code.xmatcopy = kernel;
  if (NULL != info || 0 != code_size) {
    const libxsmm_descriptor *const kernel_info = libxsmm_get_kernel_info(code, code_size);
    if (NULL != kernel_info && LIBXSMM_KERNEL_KIND_MCOPY == kernel_info->kind) {
      if (NULL != info) {
        info->typesize = kernel_info->mcopy.desc.typesize;
        info->prefetch = kernel_info->mcopy.desc.prefetch;
        info->flags = kernel_info->mcopy.desc.flags;
        info->ldi = kernel_info->mcopy.desc.ldi;
        info->ldo = kernel_info->mcopy.desc.ldo;
        info->m = kernel_info->mcopy.desc.m;
        info->n = kernel_info->mcopy.desc.n;
      }
      result = EXIT_SUCCESS;
    }
    else {
      if (0 != libxsmm_verbosity /* library code is expected to be mute */
        && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
      {
        fprintf(stderr, "LIBXSMM ERROR: invalid kernel cannot be inspected!\n");
      }
      result = EXIT_FAILURE;
    }
  }
  else {
    if (0 != libxsmm_verbosity /* library code is expected to be mute */
      && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
    {
      fprintf(stderr, "LIBXSMM ERROR: invalid argument!\n");
    }
    result = EXIT_FAILURE;
  }
  return result;
}


LIBXSMM_API int libxsmm_get_registry_info(libxsmm_registry_info* info)
{
  int result = EXIT_SUCCESS;
  if (0 != info) {
    LIBXSMM_INIT
    if (0 != internal_registry) {
      size_t i;
      memset(info, 0, sizeof(libxsmm_registry_info)); /* info->nstatic = 0; info->size = 0; */
      info->nbytes = (LIBXSMM_CAPACITY_REGISTRY) * (sizeof(libxsmm_code_pointer) + sizeof(libxsmm_descriptor));
      info->capacity = LIBXSMM_CAPACITY_REGISTRY;
#if defined(LIBXSMM_CACHE_MAXSIZE)
      info->ncache = LIBXSMM_CACHE_MAXSIZE;
#else
      info->ncache = 0;
#endif
      for (i = 0; i < (LIBXSMM_CAPACITY_REGISTRY); ++i) {
        libxsmm_code_pointer code = internal_registry[i];
        if (0 != code.ptr_const && EXIT_SUCCESS == result) {
          if (0 == (LIBXSMM_CODE_STATIC & code.uval)) { /* check for allocated/generated JIT-code */
            size_t buffer_size = 0;
            void* buffer = 0;
#if defined(LIBXSMM_HASH_COLLISION)
            code.uval &= ~LIBXSMM_HASH_COLLISION; /* clear collision flag */
#endif
            result = libxsmm_get_malloc_xinfo(code.ptr_const, &buffer_size, NULL/*flags*/, &buffer);
            if (EXIT_SUCCESS == result) {
              info->nbytes += (unsigned int)(buffer_size + (((char*)code.ptr_const) - (char*)buffer));
            }
          }
          else {
            ++info->nstatic;
          }
          ++info->size;
        }
      }
    }
    else {
      result = EXIT_FAILURE;
    }
  }
  else {
    result = EXIT_FAILURE;
  }
  return result;
}


LIBXSMM_API libxsmm_xmmfunction libxsmm_xmmdispatch(const libxsmm_gemm_descriptor* descriptor)
{
  libxsmm_xmmfunction result;
  if (NULL != descriptor) {
    libxsmm_descriptor wrap;
    wrap.gemm.desc = *descriptor;
    wrap.kind = LIBXSMM_KERNEL_KIND_MATMUL;
    if (0 != (0x80 & descriptor->prefetch)) { /* "sign"-bit of byte-value is set */
      wrap.gemm.desc.prefetch = (unsigned char)libxsmm_get_gemm_prefetch(LIBXSMM_PREFETCH_AUTO);
    }
    result = internal_find_code(&wrap, sizeof(*descriptor)).xgemm;
#if defined(_DEBUG)
    if (LIBXSMM_VERBOSITY_HIGH <= libxsmm_verbosity && INT_MAX != libxsmm_verbosity && NULL != result.xmm) {
      LIBXSMM_STDIO_ACQUIRE();
      fprintf(stderr, "\nLIBXSMM: ");
      libxsmm_gemm_xprint(stderr, result, NULL/*a*/, NULL/*b*/, NULL/*c*/);
      LIBXSMM_STDIO_RELEASE();
    }
#endif
  }
  else { /* quietly accept NULL-descriptor */
    result.xmm = NULL;
  }
  return result;
}


LIBXSMM_API libxsmm_dmmfunction libxsmm_dmmdispatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const double* alpha, const double* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_dgemm_descriptor_init(&blob, m, n, k,
    NULL != lda ? *lda : (0 == (LIBXSMM_GEMM_FLAG_TRANS_A & gemm_flags) ? m : k),
    NULL != ldb ? *ldb : (0 == (LIBXSMM_GEMM_FLAG_TRANS_B & gemm_flags) ? k : n),
    NULL != ldc ? *ldc : m, NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.dmm;
}


LIBXSMM_API libxsmm_smmfunction libxsmm_smmdispatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const float* alpha, const float* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_sgemm_descriptor_init(&blob, m, n, k,
    NULL != lda ? *lda : (0 == (LIBXSMM_GEMM_FLAG_TRANS_A & gemm_flags) ? m : k),
    NULL != ldb ? *ldb : (0 == (LIBXSMM_GEMM_FLAG_TRANS_B & gemm_flags) ? k : n),
    NULL != ldc ? *ldc : m, NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.smm;
}


LIBXSMM_API libxsmm_wimmfunction libxsmm_wimmdispatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const int* alpha, const int* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_wigemm_descriptor_init(&blob, m, n, k,
    NULL != lda ? *lda : (0 == (LIBXSMM_GEMM_FLAG_TRANS_A & gemm_flags) ? m : k),
    NULL != ldb ? *ldb : (0 == (LIBXSMM_GEMM_FLAG_TRANS_B & gemm_flags) ? k : n),
    NULL != ldc ? *ldc : m, NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.wimm;
}


LIBXSMM_API libxsmm_wsmmfunction libxsmm_wsmmdispatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const float* alpha, const float* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_wsgemm_descriptor_init(&blob, m, n, k,
    NULL != lda ? *lda : (0 == (LIBXSMM_GEMM_FLAG_TRANS_A & gemm_flags) ? m : k),
    NULL != ldb ? *ldb : (0 == (LIBXSMM_GEMM_FLAG_TRANS_B & gemm_flags) ? k : n),
    NULL != ldc ? *ldc : m, NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.wsmm;
}


LIBXSMM_API libxsmm_bsmmfunction libxsmm_bsmmdispatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const float* alpha, const float* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_bsgemm_descriptor_init(&blob, m, n, k,
    NULL != lda ? *lda : (0 == (LIBXSMM_GEMM_FLAG_TRANS_A & gemm_flags) ? m : k),
    NULL != ldb ? *ldb : (0 == (LIBXSMM_GEMM_FLAG_TRANS_B & gemm_flags) ? k : n),
    NULL != ldc ? *ldc : m, NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.bsmm;
}


LIBXSMM_API libxsmm_bmmfunction libxsmm_bmmdispatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const float* alpha, const float* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_bgemm_descriptor_init(&blob, m, n, k,
    NULL != lda ? *lda : (0 == (LIBXSMM_GEMM_FLAG_TRANS_A & gemm_flags) ? m : k),
    NULL != ldb ? *ldb : (0 == (LIBXSMM_GEMM_FLAG_TRANS_B & gemm_flags) ? k : n),
    NULL != ldc ? *ldc : m, NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.bmm;
}


LIBXSMM_API libxsmm_dmmfunction_reducebatch libxsmm_dmmdispatch_reducebatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc, const double* alpha, const double* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_dgemm_descriptor_init(&blob,
    m, n, k, NULL != lda ? *lda : m, NULL != ldb ? *ldb : k, NULL != ldc ? *ldc : m,
    NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags | LIBXSMM_GEMM_FLAG_BATCH_REDUCE, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.dmr;
}


LIBXSMM_API libxsmm_smmfunction_reducebatch libxsmm_smmdispatch_reducebatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc, const float* alpha, const float* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_sgemm_descriptor_init(&blob,
    m, n, k, NULL != lda ? *lda : m, NULL != ldb ? *ldb : k, NULL != ldc ? *ldc : m,
    NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags | LIBXSMM_GEMM_FLAG_BATCH_REDUCE, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.smr;
}


LIBXSMM_API libxsmm_bsmmfunction_reducebatch libxsmm_bsmmdispatch_reducebatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc, const float* alpha, const float* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_bsgemm_descriptor_init(&blob,
    m, n, k, NULL != lda ? *lda : m, NULL != ldb ? *ldb : k, NULL != ldc ? *ldc : m,
    NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags | LIBXSMM_GEMM_FLAG_BATCH_REDUCE, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.bsmr;
}


LIBXSMM_API libxsmm_bmmfunction_reducebatch libxsmm_bmmdispatch_reducebatch(libxsmm_blasint m, libxsmm_blasint n, libxsmm_blasint k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc, const float* alpha, const float* beta, const int* flags, const int* prefetch)
{
  const int gemm_flags = (NULL == flags ? LIBXSMM_FLAGS : *flags);
  libxsmm_descriptor_blob blob;
  const libxsmm_gemm_descriptor *const desc = libxsmm_bgemm_descriptor_init(&blob,
    m, n, k, NULL != lda ? *lda : m, NULL != ldb ? *ldb : k, NULL != ldc ? *ldc : m,
    NULL != alpha ? *alpha : LIBXSMM_ALPHA, NULL != beta ? *beta : LIBXSMM_BETA,
    gemm_flags | LIBXSMM_GEMM_FLAG_BATCH_REDUCE, libxsmm_get_gemm_xprefetch(prefetch));
  /*const*/ libxsmm_xmmfunction result = libxsmm_xmmdispatch(desc);
  return result.bmr;
}


LIBXSMM_API libxsmm_xmcopyfunction libxsmm_dispatch_mcopy(const libxsmm_mcopy_descriptor* descriptor)
{
  libxsmm_xmcopyfunction result;
  if (NULL != descriptor) {
    libxsmm_descriptor wrap;
    LIBXSMM_INIT
    wrap.mcopy.desc = *descriptor;
    wrap.kind = LIBXSMM_KERNEL_KIND_MCOPY;
#if defined(_WIN32) || defined(__CYGWIN__) /* TODO: full support for Windows calling convention */
    wrap.mcopy.desc.prefetch = 0;
#endif
    result = internal_find_code(&wrap, sizeof(*descriptor)).xmatcopy;
  }
  else {
    result = NULL;
  }
  return result;
}


LIBXSMM_API libxsmm_xtransfunction libxsmm_dispatch_trans(const libxsmm_trans_descriptor* descriptor)
{
  libxsmm_xtransfunction result;
  if (NULL != descriptor) {
    libxsmm_descriptor wrap;
    LIBXSMM_INIT
    wrap.trans.desc = *descriptor;
    wrap.kind = LIBXSMM_KERNEL_KIND_TRANS;
    result = internal_find_code(&wrap, sizeof(*descriptor)).xtrans;
  }
  else {
    result = NULL;
  }
  return result;
}


LIBXSMM_API libxsmm_pgemm_xfunction libxsmm_dispatch_pgemm(const libxsmm_pgemm_descriptor* descriptor)
{
  libxsmm_trmm_xfunction result;
  if (NULL != descriptor) {
    libxsmm_descriptor wrap;
    LIBXSMM_INIT
    wrap.pgemm.desc = *descriptor;
    wrap.kind = LIBXSMM_KERNEL_KIND_PGEMM;
    result = internal_find_code(&wrap, sizeof(*descriptor)).xpgemm;
  }
  else {
    result = NULL;
  }
  return result;
}


LIBXSMM_API libxsmm_getrf_xfunction libxsmm_dispatch_getrf(const libxsmm_getrf_descriptor* descriptor)
{
  libxsmm_trmm_xfunction result;
  if (NULL != descriptor) {
    libxsmm_descriptor wrap;
    LIBXSMM_INIT
    wrap.getrf.desc = *descriptor;
    wrap.kind = LIBXSMM_KERNEL_KIND_GETRF;
    result = internal_find_code(&wrap, sizeof(*descriptor)).xgetrf;
  }
  else {
    result = NULL;
  }
  return result;
}


LIBXSMM_API libxsmm_trmm_xfunction libxsmm_dispatch_trmm(const libxsmm_trmm_descriptor* descriptor)
{
  libxsmm_trmm_xfunction result;
  if (NULL != descriptor) {
    libxsmm_descriptor wrap;
    LIBXSMM_INIT
    wrap.trmm.desc = *descriptor;
    wrap.kind = LIBXSMM_KERNEL_KIND_TRMM;
    result = internal_find_code(&wrap, sizeof(*descriptor)).xtrmm;
  }
  else {
    result = NULL;
  }
  return result;
}


LIBXSMM_API libxsmm_trsm_xfunction libxsmm_dispatch_trsm(const libxsmm_trsm_descriptor* descriptor)
{
  libxsmm_trsm_xfunction result;
  if (NULL != descriptor) {
    libxsmm_descriptor wrap;
    LIBXSMM_INIT
    wrap.trsm.desc = *descriptor;
    wrap.kind = LIBXSMM_KERNEL_KIND_TRSM;
    result = internal_find_code(&wrap, sizeof(*descriptor)).xtrsm;
  }
  else {
    result = NULL;
  }
  return result;
}


LIBXSMM_API libxsmm_xmmfunction libxsmm_create_xcsr_soa(const libxsmm_gemm_descriptor* descriptor,
  const unsigned int* row_ptr, const unsigned int* column_idx, const void* values)
{
  libxsmm_code_pointer result = { 0 };
  if (NULL != descriptor && NULL != row_ptr && NULL != column_idx && NULL != values) {
    libxsmm_csr_soa_descriptor srsoa;
    libxsmm_build_request request;
    libxsmm_gemm_descriptor desc;
    if (0 == (0x80 & descriptor->prefetch)) {
      srsoa.gemm = descriptor;
    }
    else { /* "sign"-bit of byte-value is set */
      desc = *descriptor;
      desc.prefetch = (unsigned char)libxsmm_get_gemm_prefetch(LIBXSMM_PREFETCH_AUTO);
      srsoa.gemm = &desc;
    }
    srsoa.row_ptr = row_ptr;
    srsoa.column_idx = column_idx;
    srsoa.values = values;
    request.descriptor.srsoa = &srsoa;
    request.kind = LIBXSMM_BUILD_KIND_SRSOA;
    libxsmm_build(&request, LIBXSMM_CAPACITY_REGISTRY/*not managed*/, &result);
  }
  return result.xgemm;
}


LIBXSMM_API libxsmm_xmmfunction libxsmm_create_xcsc_soa(const libxsmm_gemm_descriptor* descriptor,
  const unsigned int* column_ptr, const unsigned int* row_idx, const void* values)
{
  libxsmm_code_pointer result = { 0 };
  if (NULL != descriptor && NULL != column_ptr && NULL != row_idx && NULL != values) {
    libxsmm_csc_soa_descriptor scsoa;
    libxsmm_build_request request;
    libxsmm_gemm_descriptor desc;
    if (0 == (0x80 & descriptor->prefetch)) {
      scsoa.gemm = descriptor;
    }
    else { /* "sign"-bit of byte-value is set */
      desc = *descriptor;
      desc.prefetch = (unsigned char)libxsmm_get_gemm_prefetch(LIBXSMM_PREFETCH_AUTO);
      scsoa.gemm = &desc;
    }
    scsoa.column_ptr = column_ptr;
    scsoa.row_idx = row_idx;
    scsoa.values = values;
    request.descriptor.scsoa = &scsoa;
    request.kind = LIBXSMM_BUILD_KIND_SCSOA;
    libxsmm_build(&request, LIBXSMM_CAPACITY_REGISTRY/*not managed*/, &result);
  }
  return result.xgemm;
}


LIBXSMM_API libxsmm_xmmfunction libxsmm_create_rm_ac_soa(const libxsmm_gemm_descriptor* descriptor)
{
  libxsmm_code_pointer result = { 0 };
  if (NULL != descriptor) {
    libxsmm_rm_ac_soa_descriptor rmacsoa;
    libxsmm_build_request request;
    libxsmm_gemm_descriptor desc;
    if (0 == (0x80 & descriptor->prefetch)) {
      rmacsoa.gemm = descriptor;
    }
    else { /* "sign"-bit of byte-value is set */
      desc = *descriptor;
      desc.prefetch = (unsigned char)libxsmm_get_gemm_prefetch(LIBXSMM_PREFETCH_AUTO);
      rmacsoa.gemm = &desc;
    }
    request.descriptor.rmacsoa = &rmacsoa;
    request.kind = LIBXSMM_BUILD_KIND_RMACSOA;
    libxsmm_build(&request, LIBXSMM_CAPACITY_REGISTRY/*not managed*/, &result);
  }
  return result.xgemm;
}


LIBXSMM_API libxsmm_xmmfunction libxsmm_create_rm_bc_soa(const libxsmm_gemm_descriptor* descriptor)
{
  libxsmm_code_pointer result = { 0 };
  if (NULL != descriptor) {
    libxsmm_rm_bc_soa_descriptor rmbcsoa;
    libxsmm_build_request request;
    libxsmm_gemm_descriptor desc;
    if (0 == (0x80 & descriptor->prefetch)) {
      rmbcsoa.gemm = descriptor;
    }
    else { /* "sign"-bit of byte-value is set */
      desc = *descriptor;
      desc.prefetch = (unsigned char)libxsmm_get_gemm_prefetch(LIBXSMM_PREFETCH_AUTO);
      rmbcsoa.gemm = &desc;
    }
    request.descriptor.rmbcsoa = &rmbcsoa;
    request.kind = LIBXSMM_BUILD_KIND_RMBCSOA;
    libxsmm_build(&request, LIBXSMM_CAPACITY_REGISTRY/*not managed*/, &result);
  }
  return result.xgemm;
}


LIBXSMM_API libxsmm_dmmfunction libxsmm_create_dcsr_reg(const libxsmm_gemm_descriptor* descriptor,
  const unsigned int* row_ptr, const unsigned int* column_idx, const double* values)
{
  libxsmm_code_pointer result = { 0 };
  if (NULL != descriptor && NULL != row_ptr && NULL != column_idx && NULL != values) {
    libxsmm_csr_reg_descriptor sreg;
    libxsmm_build_request request;
    libxsmm_gemm_descriptor desc;
    if (0 == (0x80 & descriptor->prefetch)) {
      sreg.gemm = descriptor;
    }
    else { /* "sign"-bit of byte-value is set */
      desc = *descriptor;
      desc.prefetch = (unsigned char)libxsmm_get_gemm_prefetch(LIBXSMM_PREFETCH_AUTO);
      sreg.gemm = &desc;
    }
    sreg.row_ptr = row_ptr;
    sreg.column_idx = column_idx;
    sreg.values = values;
    request.descriptor.sreg = &sreg;
    request.kind = LIBXSMM_BUILD_KIND_SREG;
    libxsmm_build(&request, LIBXSMM_CAPACITY_REGISTRY/*not managed*/, &result);
  }
  return result.xgemm.dmm;
}


LIBXSMM_API libxsmm_smmfunction libxsmm_create_scsr_reg(const libxsmm_gemm_descriptor* descriptor,
  const unsigned int* row_ptr, const unsigned int* column_idx, const float* values)
{
  libxsmm_code_pointer result = { 0 };
  if (NULL != descriptor && NULL != row_ptr && NULL != column_idx && NULL != values) {
    libxsmm_csr_reg_descriptor sreg;
    libxsmm_build_request request;
    const unsigned int n = row_ptr[descriptor->m];
    double *const d_values = (double*)(0 != n ? malloc(n * sizeof(double)) : NULL);
    if (NULL != d_values) {
      libxsmm_gemm_descriptor desc;
      unsigned int i;
      /* we need to copy the values into a double precision buffer */
      for (i = 0; i < n; ++i) d_values[i] = (double)values[i];
      if (0 == (0x80 & descriptor->prefetch)) {
        sreg.gemm = descriptor;
      }
      else { /* "sign"-bit of byte-value is set */
        desc = *descriptor;
        desc.prefetch = (unsigned char)libxsmm_get_gemm_prefetch(LIBXSMM_PREFETCH_AUTO);
        sreg.gemm = &desc;
      }
      sreg.row_ptr = row_ptr;
      sreg.column_idx = column_idx;
      sreg.values = d_values;
      request.descriptor.sreg = &sreg;
      request.kind = LIBXSMM_BUILD_KIND_SREG;
      libxsmm_build(&request, LIBXSMM_CAPACITY_REGISTRY/*not managed*/, &result);
      free(d_values);
    }
  }
  return result.xgemm.smm;
}


LIBXSMM_API void libxsmm_release_kernel(const void* jit_kernel)
{
  if (NULL != jit_kernel) {
    static int error_once = 0;
    void* extra = NULL;
    LIBXSMM_INIT
    if (EXIT_SUCCESS == libxsmm_get_malloc_xinfo(jit_kernel, NULL/*size*/, NULL/*flags*/, &extra) && NULL != extra) {
      const unsigned int regindex = *((const unsigned int*)extra);
      if ((LIBXSMM_CAPACITY_REGISTRY) <= regindex) {
        libxsmm_xfree(jit_kernel);
      }
      else
#if !defined(LIBXSMM_ENABLE_DEREG)
      if (0 != libxsmm_verbosity /* library code is expected to be mute */
       && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
      {
        fprintf(stderr, "LIBXSMM WARNING: attempt to unregister a JIT-kernel!\n");
      }
#else
      { /* unregister kernel */
        internal_registry[regindex].pmm = NULL;
# if !defined(NDEBUG)
        memset(internal_registry_keys + regindex, 0, sizeof(libxsmm_descriptor));
# endif
        libxsmm_xfree(jit_kernel);
      }
#endif
    }
    else if (0 != libxsmm_verbosity /* library code is expected to be mute */
      && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
    {
      fprintf(stderr, "LIBXSMM ERROR: failed to release kernel!\n");
    }
  }
}


#if defined(LIBXSMM_BUILD)

/* implementation provided for Fortran 77 compatibility */
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_init)(void);
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_init)(void)
{
  libxsmm_init();
}


/* implementation provided for Fortran 77 compatibility */
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_finalize)(void);
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_finalize)(void)
{
  libxsmm_finalize();
}


/* implementation provided for Fortran 77 compatibility */
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_release_kernel)(const void** jit_kernel);
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_release_kernel)(const void** jit_kernel)
{
#if !defined(NDEBUG)
  if (NULL != jit_kernel)
#endif
  {
    libxsmm_release_kernel(*jit_kernel);
  }
#if !defined(NDEBUG)
  else {
    static int error_once = 0;
    if (0 != libxsmm_verbosity /* library code is expected to be mute */
     && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
    {
      fprintf(stderr, "LIBXSMM ERROR: invalid argument passed into libxsmm_release_kernel!\n");
    }
  }
#endif
}


/* implementation provided for Fortran 77 compatibility */
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmdispatch2)(intptr_t* fn,
  const libxsmm_gemm_precision* iprec, const libxsmm_gemm_precision* oprec,
  const libxsmm_blasint* m, const libxsmm_blasint* n, const libxsmm_blasint* k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const void* alpha, const void* beta, const int* flags, const int* prefetch);
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmdispatch2)(intptr_t* fn,
  const libxsmm_gemm_precision* iprec, const libxsmm_gemm_precision* oprec,
  const libxsmm_blasint* m, const libxsmm_blasint* n, const libxsmm_blasint* k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const void* alpha, const void* beta, const int* flags, const int* prefetch)
{
#if !defined(NDEBUG)
  if (NULL != fn && NULL != m)
#endif
  {
    const libxsmm_gemm_precision precision = (NULL != iprec ? *iprec : LIBXSMM_GEMM_PRECISION_F64);
    const libxsmm_blasint kk = *(NULL != k ? k : m), nn = (NULL != n ? *n : kk);
    const int gemm_flags = (NULL != flags ? *flags : LIBXSMM_FLAGS);
    libxsmm_descriptor_blob blob;
    libxsmm_gemm_descriptor *descriptor = libxsmm_gemm_descriptor_init2(&blob,
      precision, NULL != oprec ? *oprec : precision, *m, nn, kk,
      NULL != lda ? *lda : (0 == (LIBXSMM_GEMM_FLAG_TRANS_A & gemm_flags) ? *m : kk),
      NULL != ldb ? *ldb : (0 == (LIBXSMM_GEMM_FLAG_TRANS_B & gemm_flags) ? kk : nn),
      *(NULL != ldc ? ldc : m), alpha, beta, gemm_flags, libxsmm_get_gemm_xprefetch(prefetch));
    if (NULL != descriptor) {
      libxsmm_code_pointer result;
      result.xgemm = libxsmm_xmmdispatch(descriptor);
      *fn = result.ival;
    }
    else { /* quiet */
      *fn = 0;
    }
  }
#if !defined(NDEBUG)
  else {
    static int error_once = 0;
    if (0 != libxsmm_verbosity /* library code is expected to be mute */
     && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
    {
      fprintf(stderr, "LIBXSMM ERROR: invalid argument passed into libxsmm_xmmdispatch!\n");
    }
    if (NULL != fn) *fn = 0;
  }
#endif
}


/* implementation provided for Fortran 77 compatibility */
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmdispatch)(intptr_t* fn,
  const libxsmm_gemm_precision* precision, const libxsmm_blasint* m, const libxsmm_blasint* n, const libxsmm_blasint* k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const void* alpha, const void* beta, const int* flags, const int* prefetch);
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmdispatch)(intptr_t* fn,
  const libxsmm_gemm_precision* precision, const libxsmm_blasint* m, const libxsmm_blasint* n, const libxsmm_blasint* k,
  const libxsmm_blasint* lda, const libxsmm_blasint* ldb, const libxsmm_blasint* ldc,
  const void* alpha, const void* beta, const int* flags, const int* prefetch)
{
  LIBXSMM_FSYMBOL(libxsmm_xmmdispatch2)(fn, precision, precision, m, n, k, lda, ldb, ldc, alpha, beta, flags, prefetch);
}


/* implementation provided for Fortran 77 compatibility */
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmcall_abc)(
  const libxsmm_xmmfunction* fn, const void* a, const void* b, void* c);
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmcall_abc)(
  const libxsmm_xmmfunction* fn, const void* a, const void* b, void* c)
{
#if !defined(NDEBUG)
  static int error_once = 0;
  if (NULL != fn && NULL != a && NULL != b && NULL != c)
#endif
  {
#if !defined(NDEBUG)
    if (NULL != fn->xmm)
#endif
    {
      fn->xmm(a, b, c);
    }
#if !defined(NDEBUG)
    else if (0 != libxsmm_verbosity /* library code is expected to be mute */
          && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
    {
      fprintf(stderr, "LIBXSMM ERROR: NULL-function passed into libxsmm_xmmcall_abc!\n");
    }
#endif
  }
#if !defined(NDEBUG)
  else if (0 != libxsmm_verbosity /* library code is expected to be mute */
        && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
  {
    fprintf(stderr, "LIBXSMM ERROR: invalid arguments for libxsmm_xmmcall_abc specified!\n");
  }
#endif
}


/* implementation provided for Fortran 77 compatibility */
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmcall_prf)(
  const libxsmm_xmmfunction* fn, const void* a, const void* b, void* c,
  const void* pa, const void* pb, const void* pc);
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmcall_prf)(
  const libxsmm_xmmfunction* fn, const void* a, const void* b, void* c,
  const void* pa, const void* pb, const void* pc)
{
#if !defined(NDEBUG)
  static int error_once = 0;
  if (NULL != fn && NULL != a && NULL != b && NULL != c)
#endif
  {
#if !defined(NDEBUG)
    if (NULL != fn->xmm)
#endif
    {
      fn->xmm(a, b, c, pa, pb, pc);
    }
#if !defined(NDEBUG)
    else if (0 != libxsmm_verbosity /* library code is expected to be mute */
          && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
    {
      fprintf(stderr, "LIBXSMM ERROR: NULL-function passed into libxsmm_xmmcall_prf!\n");
    }
#endif
  }
#if !defined(NDEBUG)
  else if (0 != libxsmm_verbosity /* library code is expected to be mute */
        && 1 == LIBXSMM_ATOMIC_ADD_FETCH(&error_once, 1, LIBXSMM_ATOMIC_RELAXED))
  {
    fprintf(stderr, "LIBXSMM ERROR: invalid arguments for libxsmm_xmmcall_prf specified!\n");
  }
#endif
}


/* implementation provided for Fortran 77 compatibility */
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmcall)(
  const libxsmm_xmmfunction* fn, const void* a, const void* b, void* c,
  const void* pa, const void* pb, const void* pc);
LIBXSMM_API void LIBXSMM_FSYMBOL(libxsmm_xmmcall)(
  const libxsmm_xmmfunction* fn, const void* a, const void* b, void* c,
  const void* pa, const void* pb, const void* pc)
{
  LIBXSMM_FSYMBOL(libxsmm_xmmcall_prf)(fn, a, b, c, pa, pb, pc);
}

#endif /*defined(LIBXSMM_BUILD)*/

