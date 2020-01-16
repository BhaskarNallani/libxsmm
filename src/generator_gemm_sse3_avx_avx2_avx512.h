/******************************************************************************
* Copyright (c) Intel Corporation - All rights reserved.                      *
* This file is part of the LIBXSMM library.                                   *
*                                                                             *
* For information on the license, see the LICENSE file.                       *
* Further information: https://github.com/hfp/libxsmm/                        *
* SPDX-License-Identifier: BSD-3-Clause                                       *
******************************************************************************/
/* Alexander Heinecke (Intel Corp.)
******************************************************************************/

#ifndef GENERATOR_GEMM_SSE3_AVX_AVX2_AVX512_H
#define GENERATOR_GEMM_SSE3_AVX_AVX2_AVX512_H

#include "generator_common.h"
#include "generator_gemm_common.h"

LIBXSMM_API_INTERN
void libxsmm_generator_gemm_sse3_avx_avx2_avx512_kernel( libxsmm_generated_code*        io_generated_code,
                                                         const libxsmm_gemm_descriptor* i_xgemm_desc );

LIBXSMM_API_INTERN
unsigned int libxsmm_generator_gemm_sse3_avx_avx2_avx512_get_initial_m_blocking( libxsmm_micro_kernel_config*   io_micro_kernel_config,
                                                                                 const unsigned int             i_arch,
                                                                                 const libxsmm_gemm_descriptor* i_xgemm_desc );

LIBXSMM_API_INTERN
unsigned int libxsmm_generator_gemm_sse3_avx_avx2_avx512_update_m_blocking( libxsmm_micro_kernel_config*   io_micro_kernel_config,
                                                                            const unsigned int             i_arch,
                                                                            const libxsmm_gemm_descriptor* i_xgemm_desc,
                                                                            const unsigned int             i_current_m_blocking );

LIBXSMM_API_INTERN
unsigned int libxsmm_generator_gemm_avx512_fsdbcst_get_max_n_blocking( const libxsmm_gemm_descriptor* i_xgemm_desc,
                                                                       const unsigned int             i_arch );

LIBXSMM_API_INTERN
void libxsmm_generator_gemm_avx512_kernel_fsdbcst( libxsmm_generated_code*        io_generated_code,
                                                   const libxsmm_gemm_descriptor* i_xgemm_desc );

#endif /* GENERATOR_GEMM_SSE3_AVX_AVX2_AVX512_H */

