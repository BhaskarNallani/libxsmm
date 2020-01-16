/******************************************************************************
* Copyright (c) Intel Corporation - All rights reserved.                      *
* This file is part of the LIBXSMM library.                                   *
*                                                                             *
* For information on the license, see the LICENSE file.                       *
* Further information: https://github.com/hfp/libxsmm/                        *
* SPDX-License-Identifier: BSD-3-Clause                                       *
******************************************************************************/
/* Alexander Heinecke, Evangelos Georganas (Intel Corp.)
******************************************************************************/
#include "generator_gemm_avx512_microkernel.h"
#include "generator_x86_instructions.h"
#include "libxsmm_main.h"

#if defined(LIBXSMM_OFFLOAD_TARGET)
# pragma offload_attribute(push,target(LIBXSMM_OFFLOAD_TARGET))
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(LIBXSMM_OFFLOAD_TARGET)
# pragma offload_attribute(pop)
#endif

LIBXSMM_API_INTERN
void libxsmm_generator_gemm_avx512_microkernel_nofsdbcst( libxsmm_generated_code*             io_generated_code,
                                                          const libxsmm_gp_reg_mapping*      i_gp_reg_mapping,
                                                          const libxsmm_micro_kernel_config* i_micro_kernel_config,
                                                          const libxsmm_gemm_descriptor*     i_xgemm_desc,
                                                          const unsigned int                 i_m_blocking,
                                                          const unsigned int                 i_n_blocking,
                                                          const int                          i_offset )
{
  /* deriving register blocking from kernel config */
  unsigned int l_m_blocking = ( i_m_blocking % i_micro_kernel_config->vector_length  == 0 ) ? i_m_blocking/i_micro_kernel_config->vector_length : (i_m_blocking/i_micro_kernel_config->vector_length)+1;
  /* register blocking counter in n */
  unsigned int l_n = 0;
  /* register blocking counter in m */
  unsigned int l_m = 0;
  /* start register of accumulator */
  unsigned int l_vec_reg_acc_start = i_micro_kernel_config->vector_reg_count - (i_n_blocking * l_m_blocking);
  /* temp variable for b-offset to handle no-trans/trans B */
  int l_b_offset = 0;

#if !defined(NDEBUG)
  if ( (i_n_blocking > 7) || (i_n_blocking < 1) ) {
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_N_BLOCK );
    return;
  }
  if ( (l_m_blocking < 1) || (l_m_blocking > 6) ) {
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_M_BLOCK );
    return;
  }
#endif

  if (l_m_blocking == 1) {
    /* load column vectors of A */
    libxsmm_x86_instruction_vec_move( io_generated_code,
        i_micro_kernel_config->instruction_set,
        i_micro_kernel_config->a_vmove_instruction,
        i_gp_reg_mapping->gp_reg_a,
        LIBXSMM_X86_GP_REG_UNDEF, 0,
        0,
        i_micro_kernel_config->vector_name,
        i_n_blocking, i_micro_kernel_config->use_masking_a_c, 1, 0 );
    /* loop over columns of B */
    for ( l_n = 0; l_n < i_n_blocking; l_n++ ) {
      /* post increment of a pointer early */
      if ( l_n == 0 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code,
            i_micro_kernel_config->alu_add_instruction,
            i_gp_reg_mapping->gp_reg_a,
            (i_xgemm_desc->lda)*(i_micro_kernel_config->datatype_size) );
      }
      /* different ways of using B */
      if ( i_offset != (-1) ) {
        /* handle trans B */
        if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
          l_b_offset = (i_micro_kernel_config->datatype_size * i_offset * i_xgemm_desc->ldb) + (l_n * i_micro_kernel_config->datatype_size);
        } else {
          l_b_offset = (i_micro_kernel_config->datatype_size * i_offset) + (i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size);
        }

        libxsmm_x86_instruction_vec_move( io_generated_code,
            i_micro_kernel_config->instruction_set,
            i_micro_kernel_config->b_vmove_instruction,
            i_gp_reg_mapping->gp_reg_b,
            LIBXSMM_X86_GP_REG_UNDEF, 0,
            l_b_offset,
            i_micro_kernel_config->vector_name,
            l_n, 0, 1, 0 );
      } else {
        /* handle trans B */
        if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
          l_b_offset = l_n * i_micro_kernel_config->datatype_size;
        } else {
          l_b_offset = i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size;
        }

        libxsmm_x86_instruction_vec_move( io_generated_code,
            i_micro_kernel_config->instruction_set,
            i_micro_kernel_config->b_vmove_instruction,
            i_gp_reg_mapping->gp_reg_b,
            LIBXSMM_X86_GP_REG_UNDEF, 0,
            l_b_offset,
            i_micro_kernel_config->vector_name,
            l_n, 0, 1, 0 );
        if ( l_n == (i_n_blocking -1) ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = i_xgemm_desc->ldb * i_micro_kernel_config->datatype_size;
          } else {
            l_b_offset = i_micro_kernel_config->datatype_size;
          }

          libxsmm_x86_instruction_alu_imm( io_generated_code,
              i_micro_kernel_config->alu_add_instruction,
              i_gp_reg_mapping->gp_reg_b,
              l_b_offset );
        }
      }
      /* issue fma */
      if ( LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
        if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_A_UNSIGNED) > 0 ) {
          libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                i_micro_kernel_config->instruction_set,
                i_micro_kernel_config->vmul_instruction,
                i_micro_kernel_config->vector_name,
                l_n,
                i_n_blocking,
                l_vec_reg_acc_start + l_n );
        } else if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_B_UNSIGNED) > 0 ) {
          libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                i_micro_kernel_config->instruction_set,
                i_micro_kernel_config->vmul_instruction,
                i_micro_kernel_config->vector_name,
                i_n_blocking,
                l_n,
                l_vec_reg_acc_start + l_n );
        } else {
          /* should not happen */
        }
      } else {
        libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->vmul_instruction,
              i_micro_kernel_config->vector_name,
              i_n_blocking,
              l_n,
              l_vec_reg_acc_start + l_n );
      }
    }
  } else {
    /* Special case that arises in GEMMS from Resnet50 layers  */
    if (i_n_blocking == 7 && l_m_blocking == 4) {
      if ( i_offset != (-1) ) {
        for ( l_n = 0; l_n < 3; l_n++ ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset * i_xgemm_desc->ldb) + (l_n * i_micro_kernel_config->datatype_size);
          } else {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset) + (i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size);
          }

          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->b_vmove_instruction,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset,
              i_micro_kernel_config->vector_name,
              l_n, 0, 1, 0 );
        }
        if ( i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1 ) {
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset * i_xgemm_desc->ldb);
          } else {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset);
          }
          libxsmm_x86_instruction_prefetch(io_generated_code,
              LIBXSMM_X86_INSTR_PREFETCHT0,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset + 16 * i_xgemm_desc->ldb * i_micro_kernel_config->datatype_size);
        }
      } else {
        for ( l_n = 0; l_n < 3; l_n++ ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = l_n * i_micro_kernel_config->datatype_size;
          } else {
            l_b_offset = i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size;
          }

          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->b_vmove_instruction,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset,
              i_micro_kernel_config->vector_name,
              l_n, 0, 1, 0 );
        }
        if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
          libxsmm_x86_instruction_prefetch(io_generated_code,
            LIBXSMM_X86_INSTR_PREFETCHT0,
            i_gp_reg_mapping->gp_reg_b,
            LIBXSMM_X86_GP_REG_UNDEF, 0,
            16 * i_xgemm_desc->ldb * i_micro_kernel_config->datatype_size);
        }
      }

      /* load column vectors of A and multiply with all broadcasted row entries of B */
      for ( l_m = 0; l_m < l_m_blocking; l_m++ ) {
        libxsmm_x86_instruction_vec_move( io_generated_code,
            i_micro_kernel_config->instruction_set,
            i_micro_kernel_config->a_vmove_instruction,
            i_gp_reg_mapping->gp_reg_a,
            LIBXSMM_X86_GP_REG_UNDEF, 0,
            (i_micro_kernel_config->datatype_size) * (i_micro_kernel_config->vector_length) * l_m,
            i_micro_kernel_config->vector_name,
            3, ( l_m == (l_m_blocking - 1) ) ? i_micro_kernel_config->use_masking_a_c : 0, 1, 0 );

        /* In case of batch reduce try to prefetch a few more columns ahead...  */
        if ((i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_BATCH_REDUCE_ADDRESS) || (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_BATCH_REDUCE_OFFSET) || (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_BATCH_REDUCE_STRIDE)) {
          unsigned int pf_a_cols_ahead = 16;
          if (i_xgemm_desc->lda == 1024) {
            pf_a_cols_ahead = 4;
          }
          libxsmm_x86_instruction_prefetch( io_generated_code,
              LIBXSMM_X86_INSTR_PREFETCHT0,
              i_gp_reg_mapping->gp_reg_a,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              (i_micro_kernel_config->datatype_size) * (i_micro_kernel_config->vector_length) * l_m + pf_a_cols_ahead * i_xgemm_desc->lda * i_micro_kernel_config->datatype_size);
        }

        for ( l_n = 0; l_n < 3; l_n++ ) {
          /* issue fma */
          if ( LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
            if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_A_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                    i_micro_kernel_config->instruction_set,
                    i_micro_kernel_config->vmul_instruction,
                    i_micro_kernel_config->vector_name,
                    l_n,
                    3,
                    l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
            } else if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_B_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                    i_micro_kernel_config->instruction_set,
                    i_micro_kernel_config->vmul_instruction,
                    i_micro_kernel_config->vector_name,
                    3,
                    l_n,
                    l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
            } else {
              /* should not happen */
            }
          } else {
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                  i_micro_kernel_config->instruction_set,
                  i_micro_kernel_config->vmul_instruction,
                  i_micro_kernel_config->vector_name,
                  3,
                  l_n,
                  l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
          }
        }
      }

      if ( i_offset != (-1) ) {
        for ( l_n = 3; l_n < 6; l_n++ ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset * i_xgemm_desc->ldb) + (l_n * i_micro_kernel_config->datatype_size);
          } else {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset) + (i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size);
          }

          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->b_vmove_instruction,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset,
              i_micro_kernel_config->vector_name,
              l_n-3, 0, 1, 0 );
        }
      } else {
        for ( l_n = 3; l_n < 6; l_n++ ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = l_n * i_micro_kernel_config->datatype_size;
          } else {
            l_b_offset = i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size;
          }

          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->b_vmove_instruction,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset,
              i_micro_kernel_config->vector_name,
              l_n-3, 0, 1, 0 );
        }
      }

      for ( l_m = 0; l_m < l_m_blocking; l_m++ ) {
        libxsmm_x86_instruction_vec_move( io_generated_code,
            i_micro_kernel_config->instruction_set,
            i_micro_kernel_config->a_vmove_instruction,
            i_gp_reg_mapping->gp_reg_a,
            LIBXSMM_X86_GP_REG_UNDEF, 0,
            (i_micro_kernel_config->datatype_size) * (i_micro_kernel_config->vector_length) * l_m,
            i_micro_kernel_config->vector_name,
            3, ( l_m == (l_m_blocking - 1) ) ? i_micro_kernel_config->use_masking_a_c : 0, 1, 0 );
        for ( l_n = 3; l_n < 6; l_n++ ) {
          /* issue fma */
          if ( LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
            if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_A_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                    i_micro_kernel_config->instruction_set,
                    i_micro_kernel_config->vmul_instruction,
                    i_micro_kernel_config->vector_name,
                    l_n-3,
                    3,
                    l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
            } else if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_B_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                    i_micro_kernel_config->instruction_set,
                    i_micro_kernel_config->vmul_instruction,
                    i_micro_kernel_config->vector_name,
                    3,
                    l_n-3,
                    l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
            } else {
              /* should not happen */
            }
          } else {
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                  i_micro_kernel_config->instruction_set,
                  i_micro_kernel_config->vmul_instruction,
                  i_micro_kernel_config->vector_name,
                  3,
                  l_n-3,
                  l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
          }
        }
      }

      if ( i_offset != (-1) ) {
        for ( l_n = 6; l_n < 7; l_n++ ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset * i_xgemm_desc->ldb) + (l_n * i_micro_kernel_config->datatype_size);
          } else {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset) + (i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size);
          }
          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->b_vmove_instruction,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset,
              i_micro_kernel_config->vector_name,
              l_n-6, 0, 1, 0 );
        }
      } else {
        for ( l_n = 6; l_n < 7; l_n++ ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = l_n * i_micro_kernel_config->datatype_size;
          } else {
            l_b_offset = i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size;
          }
          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->b_vmove_instruction,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset,
              i_micro_kernel_config->vector_name,
              l_n-6, 0, 1, 0 );
        }
        /* handle trans B */
        if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
          l_b_offset = i_xgemm_desc->ldb * i_micro_kernel_config->datatype_size;
        } else {
          l_b_offset = i_micro_kernel_config->datatype_size;
        }

        libxsmm_x86_instruction_alu_imm( io_generated_code,
            i_micro_kernel_config->alu_add_instruction,
            i_gp_reg_mapping->gp_reg_b,
            l_b_offset );
      }

      for ( l_m = 0; l_m < l_m_blocking; l_m++ ) {
        libxsmm_x86_instruction_vec_move( io_generated_code,
            i_micro_kernel_config->instruction_set,
            i_micro_kernel_config->a_vmove_instruction,
            i_gp_reg_mapping->gp_reg_a,
            LIBXSMM_X86_GP_REG_UNDEF, 0,
            (i_micro_kernel_config->datatype_size) * (i_micro_kernel_config->vector_length) * l_m,
            i_micro_kernel_config->vector_name,
            3, ( l_m == (l_m_blocking - 1) ) ? i_micro_kernel_config->use_masking_a_c : 0, 1, 0 );

        for ( l_n = 6; l_n < 7; l_n++ ) {
          /* post increment early */
          if ( (l_m == (l_m_blocking-1)) && (l_n == 6) ) {
            libxsmm_x86_instruction_alu_imm( io_generated_code,
                i_micro_kernel_config->alu_add_instruction,
                i_gp_reg_mapping->gp_reg_a,
                (i_xgemm_desc->lda)*(i_micro_kernel_config->datatype_size) );
          }

          /* issue fma */
          if ( LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
            if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_A_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                    i_micro_kernel_config->instruction_set,
                    i_micro_kernel_config->vmul_instruction,
                    i_micro_kernel_config->vector_name,
                    l_n-6,
                    3,
                    l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
            } else if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_B_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                    i_micro_kernel_config->instruction_set,
                    i_micro_kernel_config->vmul_instruction,
                    i_micro_kernel_config->vector_name,
                    3,
                    l_n-6,
                    l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
            } else {
              /* should not happen */
            }
          } else {
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                  i_micro_kernel_config->instruction_set,
                  i_micro_kernel_config->vmul_instruction,
                  i_micro_kernel_config->vector_name,
                  3,
                  l_n-6,
                  l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
          }
        }
      }
    } else {
      /* broadcast from B -> into vec registers 0 to i_n_blocking */
      if ( i_offset != (-1) ) {
        for ( l_n = 0; l_n < i_n_blocking; l_n++ ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset * i_xgemm_desc->ldb) + (l_n * i_micro_kernel_config->datatype_size);
          } else {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset) + (i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size);
          }
          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->b_vmove_instruction,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset,
              i_micro_kernel_config->vector_name,
              l_n, 0, 1, 0 );
        }
        if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset * i_xgemm_desc->ldb);
          } else {
            l_b_offset = (i_micro_kernel_config->datatype_size * i_offset);
          }
          libxsmm_x86_instruction_prefetch(io_generated_code,
              LIBXSMM_X86_INSTR_PREFETCHT0,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset + 16 * i_xgemm_desc->ldb * i_micro_kernel_config->datatype_size);
        }
      } else {
        for ( l_n = 0; l_n < i_n_blocking; l_n++ ) {
          /* handle trans B */
          if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
            l_b_offset = l_n * i_micro_kernel_config->datatype_size;
          } else {
            l_b_offset = i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size;
          }

          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->b_vmove_instruction,
              i_gp_reg_mapping->gp_reg_b,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              l_b_offset,
              i_micro_kernel_config->vector_name,
              l_n, 0, 1, 0 );
        }
        if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
          libxsmm_x86_instruction_prefetch(io_generated_code,
            LIBXSMM_X86_INSTR_PREFETCHT0,
            i_gp_reg_mapping->gp_reg_b,
            LIBXSMM_X86_GP_REG_UNDEF, 0,
            16 * i_xgemm_desc->ldb * i_micro_kernel_config->datatype_size);
        }

        /* handle trans B */
        if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
          l_b_offset = i_xgemm_desc->ldb * i_micro_kernel_config->datatype_size;
        } else {
          l_b_offset = i_micro_kernel_config->datatype_size;
        }

        libxsmm_x86_instruction_alu_imm( io_generated_code,
            i_micro_kernel_config->alu_add_instruction,
            i_gp_reg_mapping->gp_reg_b,
            l_b_offset );
      }

      if (l_m_blocking == 4) {
        /* load column vectors of A and multiply with all broadcasted row entries of B */
        for ( l_m = 0; l_m < l_m_blocking; l_m++ ) {
          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->a_vmove_instruction,
              i_gp_reg_mapping->gp_reg_a,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              (i_micro_kernel_config->datatype_size) * (i_micro_kernel_config->vector_length) * l_m,
              i_micro_kernel_config->vector_name,
              i_n_blocking, ( l_m == (l_m_blocking - 1) ) ? i_micro_kernel_config->use_masking_a_c : 0, 1, 0 );

          /* In case of batch reduce try to prefetch a few more columns ahead...  */
          if ((i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_BATCH_REDUCE_ADDRESS) || (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_BATCH_REDUCE_OFFSET) || (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_BATCH_REDUCE_STRIDE)) {
            unsigned int pf_a_cols_ahead = 16;
            if (i_xgemm_desc->lda == 1024) {
              pf_a_cols_ahead = 4;
            }
            libxsmm_x86_instruction_prefetch( io_generated_code,
                LIBXSMM_X86_INSTR_PREFETCHT0,
                i_gp_reg_mapping->gp_reg_a,
                LIBXSMM_X86_GP_REG_UNDEF, 0,
                (i_micro_kernel_config->datatype_size) * (i_micro_kernel_config->vector_length) * l_m + pf_a_cols_ahead * i_xgemm_desc->lda * i_micro_kernel_config->datatype_size);
          }

          for ( l_n = 0; l_n < i_n_blocking; l_n++ ) {
            /* post increment early */
            if ( (l_m == (l_m_blocking-1)) && (l_n == 0) ) {
              libxsmm_x86_instruction_alu_imm( io_generated_code,
                  i_micro_kernel_config->alu_add_instruction,
                  i_gp_reg_mapping->gp_reg_a,
                  (i_xgemm_desc->lda)*(i_micro_kernel_config->datatype_size) );
            }

            /* issue fma */
            if ( LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
              if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_A_UNSIGNED) > 0 ) {
                libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                      i_micro_kernel_config->instruction_set,
                      i_micro_kernel_config->vmul_instruction,
                      i_micro_kernel_config->vector_name,
                      l_n,
                      i_n_blocking,
                      l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
              } else if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_B_UNSIGNED) > 0 ) {
                libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                      i_micro_kernel_config->instruction_set,
                      i_micro_kernel_config->vmul_instruction,
                      i_micro_kernel_config->vector_name,
                      i_n_blocking,
                      l_n,
                      l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
              } else {
                /* should not happen */
              }
            } else {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                    i_micro_kernel_config->instruction_set,
                    i_micro_kernel_config->vmul_instruction,
                    i_micro_kernel_config->vector_name,
                    i_n_blocking,
                    l_n,
                    l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
            }
          }
        }
      } else {
        /* load column vectors of A and multiply with all broadcasted row entries of B */
        for ( l_m = 0; l_m < l_m_blocking; l_m++ ) {
          libxsmm_x86_instruction_vec_move( io_generated_code,
              i_micro_kernel_config->instruction_set,
              i_micro_kernel_config->a_vmove_instruction,
              i_gp_reg_mapping->gp_reg_a,
              LIBXSMM_X86_GP_REG_UNDEF, 0,
              (i_micro_kernel_config->datatype_size) * (i_micro_kernel_config->vector_length) * l_m,
              i_micro_kernel_config->vector_name,
              i_n_blocking+l_m, ( l_m == (l_m_blocking - 1) ) ? i_micro_kernel_config->use_masking_a_c : 0, 1, 0 );
        }
        for ( l_m = 0; l_m < l_m_blocking; l_m++ ) {
          for ( l_n = 0; l_n < i_n_blocking; l_n++ ) {
            /* post increment early */
            if ( (l_m == (l_m_blocking-1)) && (l_n == 0) ) {
              libxsmm_x86_instruction_alu_imm( io_generated_code,
                  i_micro_kernel_config->alu_add_instruction,
                  i_gp_reg_mapping->gp_reg_a,
                  (i_xgemm_desc->lda)*(i_micro_kernel_config->datatype_size) );
            }
            /* issue fma */
            if ( LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
              if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_A_UNSIGNED) > 0 ) {
                libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                      i_micro_kernel_config->instruction_set,
                      i_micro_kernel_config->vmul_instruction,
                      i_micro_kernel_config->vector_name,
                      l_n,
                      i_n_blocking+l_m,
                      l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
              } else if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_B_UNSIGNED) > 0 ) {
                libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                      i_micro_kernel_config->instruction_set,
                      i_micro_kernel_config->vmul_instruction,
                      i_micro_kernel_config->vector_name,
                      i_n_blocking+l_m,
                      l_n,
                      l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
              } else {
                /* should not happen */
              }
            } else {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                    i_micro_kernel_config->instruction_set,
                    i_micro_kernel_config->vmul_instruction,
                    i_micro_kernel_config->vector_name,
                    i_n_blocking+l_m,
                    l_n,
                    l_vec_reg_acc_start + l_m + (l_m_blocking * l_n) );
            }
          }
        }
      }
    }
  }
}


LIBXSMM_API_INTERN
void libxsmm_generator_gemm_avx512_microkernel_fsdbcst( libxsmm_generated_code*            io_generated_code,
                                                        const libxsmm_gp_reg_mapping*      i_gp_reg_mapping,
                                                        const libxsmm_micro_kernel_config* i_micro_kernel_config,
                                                        const libxsmm_gemm_descriptor*     i_xgemm_desc,
                                                        const unsigned int                 i_n_blocking,
                                                        const unsigned int                 i_k_blocking )
{
  unsigned int l_n;
  unsigned int l_k;
  unsigned int l_displacement_k_b = 0;
  unsigned int l_k_b_updates = 0;
  unsigned int l_displacement_k_a = 0;
  unsigned int l_k_a_update = 0;
  unsigned int l_n_accs = 0;
  unsigned int b_prefetch_blocks = 0;
  unsigned int idx = 0;
  unsigned int point = 0;

#if !defined(NDEBUG)
  if ( i_n_blocking > 30 ) {
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_N_BLOCK );
    return;
  }
#endif

  /* compute number of n accumulators to hide FMA latencies */
  if (i_n_blocking >= 12) {
    l_n_accs = 1;
  } else if (i_n_blocking >= 6) {
    l_n_accs = 2;
  } else {
    l_n_accs = 4;
  }

  /* if we are in non-trans B we optimize column addressing for small instruction sizes */
  if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) == 0 ) {
    /* Initialize helper registers for SIB addressing */
    /* helper 0: Index register holding ldb*datatype_size */
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                     i_gp_reg_mapping->gp_reg_help_0, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
    /* helper 1: Index register holding 3*ldb*datatype_size */
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                     i_gp_reg_mapping->gp_reg_help_1, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb * 3 );
    /* helper 2: Index register holding 5*ldb*datatype_size */
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                     i_gp_reg_mapping->gp_reg_help_2, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb * 5 );
    /* helper 3: Index register holding 7*ldb*datatype_size */
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                     i_gp_reg_mapping->gp_reg_help_3, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb * 7 );

    /* helper 4: B + 9*ldb, additional base address
       helper 5: B + 18*ldb, additional base address, using the reg_c register, which was saved to stack
       helper 6: B + 27*ldb, additional base address WARNING: If i_n_blocking is > 27, then we can not prefetch C in L1 */
    if ( i_n_blocking > 9 ) {
      libxsmm_x86_instruction_alu_reg( io_generated_code, i_micro_kernel_config->alu_mov_instruction, i_gp_reg_mapping->gp_reg_b, i_gp_reg_mapping->gp_reg_help_4);
      libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                       i_gp_reg_mapping->gp_reg_help_4,  9 * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
    }
    if ( i_n_blocking > 18 ) {
      libxsmm_x86_instruction_alu_reg( io_generated_code, i_micro_kernel_config->alu_mov_instruction, i_gp_reg_mapping->gp_reg_b, i_gp_reg_mapping->gp_reg_c);
      libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                       i_gp_reg_mapping->gp_reg_c, 18 * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
    }
    if ( i_n_blocking > 27 ) {
      libxsmm_x86_instruction_alu_reg( io_generated_code, i_micro_kernel_config->alu_mov_instruction, i_gp_reg_mapping->gp_reg_b, i_gp_reg_mapping->gp_reg_help_5);
      libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                       i_gp_reg_mapping->gp_reg_help_5, 27 * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
    }
    l_displacement_k_b = 0;
  }

  l_displacement_k_a = 0;

  /* xor additional accumulator, if needed */
  for ( l_k = 1; l_k < l_n_accs; l_k++) {
    for ( l_n = 0; l_n < i_n_blocking; l_n++) {
      libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                           io_generated_code->arch,
                                           i_micro_kernel_config->vxor_instruction,
                                           i_micro_kernel_config->vector_name,
                                           i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n,
                                           i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n,
                                           i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n );
    }
  }

  /* in case of int8 GEMM on SKX use zmm2 for 16bit 1's */
  if ( (LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype )) &&
         (io_generated_code->arch < LIBXSMM_X86_AVX512_CLX) ) {
    short l_all_ones[32] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    libxsmm_x86_instruction_full_vec_load_of_constants ( io_generated_code,
                                                         (const unsigned char *)l_all_ones,
                                                         "my_int16_ones",
                                                         i_micro_kernel_config->vector_name,
                                                         2 );
  }

  /* apply k blocking */
  for ( l_k = 0; l_k < i_k_blocking; l_k++ ) {
    /* advance b pointer if needed */
    if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) == 0 ) {
      if ( (l_k > 0) && ((l_k%128) == 0) ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                         i_gp_reg_mapping->gp_reg_b, 128*i_micro_kernel_config->datatype_size );
        /* advance the second base pointer only if it's needed */
        if ( i_n_blocking > 9 ) {
          libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                           i_gp_reg_mapping->gp_reg_help_4, 128*i_micro_kernel_config->datatype_size );
        }
        /* advance the third base pointer only if it's needed */
        if ( i_n_blocking > 18 ) {
          libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                           i_gp_reg_mapping->gp_reg_c, 128*i_micro_kernel_config->datatype_size );
        }
        /* advance the fourth base pointer only if it's needed */
        if ( i_n_blocking > 27 ) {
          libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                           i_gp_reg_mapping->gp_reg_help_5, 128*i_micro_kernel_config->datatype_size );
        }

        l_displacement_k_b = 0;
        l_k_b_updates++;
      }
    }

    if ( l_k == 0 ) {
       /* load A */
      libxsmm_x86_instruction_vec_move( io_generated_code,
                                        io_generated_code->arch,
                                        i_micro_kernel_config->a_vmove_instruction,
                                        i_gp_reg_mapping->gp_reg_a,
                                        LIBXSMM_X86_GP_REG_UNDEF, 0,
                                        i_xgemm_desc->lda * l_displacement_k_a * i_micro_kernel_config->datatype_size,
                                        i_micro_kernel_config->vector_name,
                                        0,
                                        i_micro_kernel_config->use_masking_a_c, 1, 0 );
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * l_displacement_k_a * i_micro_kernel_config->datatype_size) + 64 );
      }
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }
      if ( i_k_blocking > 1 ) {
        /* second A load in first iteration, in case of large blockings -> hiding L1 latencies */
        libxsmm_x86_instruction_vec_move( io_generated_code,
                                          io_generated_code->arch,
                                          i_micro_kernel_config->a_vmove_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          i_xgemm_desc->lda * l_displacement_k_a * i_micro_kernel_config->datatype_size,
                                          i_micro_kernel_config->vector_name,
                                          1,
                                          i_micro_kernel_config->use_masking_a_c, 1, 0 );
        /* current A prefetch, next 8 rows for the current column */
        if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
             i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
          libxsmm_x86_instruction_prefetch( io_generated_code,
                                            i_micro_kernel_config->prefetch_instruction,
                                            i_gp_reg_mapping->gp_reg_a,
                                            LIBXSMM_X86_GP_REG_UNDEF, 0,
                                            (i_xgemm_desc->lda * l_displacement_k_a * i_micro_kernel_config->datatype_size) + 64 );
        }
        /* handle large displacements */
        if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
          libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
          l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
          l_displacement_k_a = 0;
        } else {
          l_displacement_k_a++;
        }
      }
    } else if ( l_k < (i_k_blocking - 1) ) {
      /* pipelined load of A, one k iteration ahead */
      libxsmm_x86_instruction_vec_move( io_generated_code,
                                        io_generated_code->arch,
                                        i_micro_kernel_config->a_vmove_instruction,
                                        i_gp_reg_mapping->gp_reg_a,
                                        LIBXSMM_X86_GP_REG_UNDEF, 0,
                                        i_xgemm_desc->lda * l_displacement_k_a * i_micro_kernel_config->datatype_size,
                                        i_micro_kernel_config->vector_name,
                                        (l_k+1)%2,
                                        i_micro_kernel_config->use_masking_a_c, 1, 0 );
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * l_displacement_k_a * i_micro_kernel_config->datatype_size) + 64 );
      }
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }
    }

    /* next A prefetch "same" rows in "same" column, but in a different matrix */
    if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_JPST ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_JPST ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2 ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C) {
      libxsmm_x86_instruction_prefetch( io_generated_code,
                                        i_micro_kernel_config->prefetch_instruction,
                                        i_gp_reg_mapping->gp_reg_a_prefetch,
                                        LIBXSMM_X86_GP_REG_UNDEF, 0,
                                        (i_xgemm_desc->lda * l_k * i_micro_kernel_config->datatype_size) );
      if ( l_k == (i_k_blocking - 1) && (i_k_blocking != (unsigned int)i_xgemm_desc->k) ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code,
                                         i_micro_kernel_config->alu_add_instruction,
                                         i_gp_reg_mapping->gp_reg_a_prefetch,
                                         i_k_blocking * i_micro_kernel_config->datatype_size * i_xgemm_desc->lda );
      }
    }

    /* in last k-iteration: advance pointers */
    if ( (l_k == (i_k_blocking - 1)) && (i_k_blocking != (unsigned int)i_xgemm_desc->k) ) {
      libxsmm_x86_instruction_alu_imm( io_generated_code,
                                       i_micro_kernel_config->alu_add_instruction,
                                       i_gp_reg_mapping->gp_reg_a,
                                       i_k_blocking * i_micro_kernel_config->datatype_size * i_xgemm_desc->lda );
    }

    /* in case of bfloat16 "prepare" A matrix in registers zmm l_k%2 and zmm3 using FP32 numbers */
    if ( (LIBXSMM_GEMM_PRECISION_BF16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype )) &&
         (io_generated_code->arch < LIBXSMM_X86_AVX512_CPX)                ) {
      /* we put "0" elements of A matrix into zmm3 */
      libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
          io_generated_code->arch,
          LIBXSMM_X86_INSTR_VPSLLD,
          i_micro_kernel_config->vector_name,
          l_k%2,
          3,
          LIBXSMM_X86_VEC_REG_UNDEF,
          16);

      /* we put "1" elements of A matrix into l_k%2 zmm*/
      libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
          io_generated_code->arch,
          LIBXSMM_X86_INSTR_VPSRAD,
          i_micro_kernel_config->vector_name,
          l_k%2,
          l_k%2,
          LIBXSMM_X86_VEC_REG_UNDEF,
          16);
      libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
          io_generated_code->arch,
          LIBXSMM_X86_INSTR_VPSLLD,
          i_micro_kernel_config->vector_name,
          l_k%2,
          l_k%2,
          LIBXSMM_X86_VEC_REG_UNDEF,
          16);
    }

    /* compute vectorwidth (A) * column broadcast (B) */
    if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) == 0 ) {
      for ( l_n = 0; l_n < i_n_blocking; l_n++) {
        /* determining base, idx and scale values */
        unsigned int l_b_reg = i_gp_reg_mapping->gp_reg_b;
        unsigned int l_b_idx = LIBXSMM_X86_GP_REG_UNDEF;
        unsigned int l_scale = 0;
        unsigned int l_disp = l_displacement_k_b*i_micro_kernel_config->datatype_size;
        /* select the base register */
        if ( l_n > 26 ) {
          l_b_reg = i_gp_reg_mapping->gp_reg_help_5;
        } else if ( l_n > 17 ) {
          l_b_reg = i_gp_reg_mapping->gp_reg_c;
        } else if ( l_n > 8 ) {
          l_b_reg = i_gp_reg_mapping->gp_reg_help_4;
        } else {
          l_b_reg = i_gp_reg_mapping->gp_reg_b;
        }
        /* Select SIB */
        if ( l_n % 9 == 0 ) {
          l_b_idx = LIBXSMM_X86_GP_REG_UNDEF;
          l_scale = 0;
        } else if ( l_n % 9 == 1 ) {
          l_b_idx = i_gp_reg_mapping->gp_reg_help_0;
          l_scale = 1;
        } else if ( l_n % 9 == 2 ) {
          l_b_idx = i_gp_reg_mapping->gp_reg_help_0;
          l_scale = 2;
        } else if ( l_n % 9 == 3 ) {
          l_b_idx = i_gp_reg_mapping->gp_reg_help_1;
          l_scale = 1;
        } else if ( l_n % 9 == 4 ) {
          l_b_idx = i_gp_reg_mapping->gp_reg_help_0;
          l_scale = 4;
        } else if ( l_n % 9 == 5 ) {
          l_b_idx = i_gp_reg_mapping->gp_reg_help_2;
          l_scale = 1;
        } else if ( l_n % 9 == 6 ) {
          l_b_idx = i_gp_reg_mapping->gp_reg_help_1;
          l_scale = 2;
        } else if ( l_n % 9 == 7 ) {
          l_b_idx = i_gp_reg_mapping->gp_reg_help_3;
          l_scale = 1;
        } else if ( l_n % 9 == 8 ) {
          l_b_idx = i_gp_reg_mapping->gp_reg_help_0;
          l_scale = 8;
        } else {
          assert(0/*should not happen*/);
        }

#if 1
        if ( LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) || LIBXSMM_GEMM_PRECISION_F64 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                   io_generated_code->arch,
                                                   i_micro_kernel_config->vmul_instruction,
                                                   1,
                                                   l_b_reg,
                                                   l_b_idx,
                                                   l_scale,
                                                   l_disp,
                                                   i_micro_kernel_config->vector_name,
                                                   l_k%2,
                                                   i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
        } else if (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          if ( io_generated_code->arch == LIBXSMM_X86_AVX512_CORE ) {
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPBROADCASTD,
                                              l_b_reg,
                                              l_b_idx, l_scale,
                                              l_disp,
                                              i_micro_kernel_config->vector_name,
                                              3, 0, 1, 0 );
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPMADDWD,
                                              i_micro_kernel_config->vector_name,
                                              l_k%2,
                                              3,
                                              3 );
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPADDD,
                                              i_micro_kernel_config->vector_name,
                                              3,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          } else if ( ( io_generated_code->arch >= LIBXSMM_X86_AVX512_CLX ) || ( io_generated_code->arch <= LIBXSMM_X86_ALLFEAT ) ) {
            libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                     io_generated_code->arch,
                                                     LIBXSMM_X86_INSTR_VPDPWSSD,
                                                     1,
                                                     l_b_reg,
                                                     l_b_idx,
                                                     l_scale,
                                                     l_disp,
                                                     i_micro_kernel_config->vector_name,
                                                     l_k%2,
                                                     i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          } else {
            /* shouldn't happen */
          }
        } else if (LIBXSMM_GEMM_PRECISION_I8 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          if ( io_generated_code->arch < LIBXSMM_X86_AVX512_CLX ) {
            /* let's broadcast B into zmm3 */
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPBROADCASTD,
                                              l_b_reg,
                                              l_b_idx, l_scale,
                                              l_disp,
                                              i_micro_kernel_config->vector_name,
                                              3, 0, 1, 0 );

            /* 8 bit mix-sign Mul */
            if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_A_UNSIGNED) > 0  ) {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                                       io_generated_code->arch,
                                                       LIBXSMM_X86_INSTR_VPMADDUBSW,
                                                       i_micro_kernel_config->vector_name,
                                                       3,
                                                       l_k%2,
                                                       3 );
            } else if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_B_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                                       io_generated_code->arch,
                                                       LIBXSMM_X86_INSTR_VPMADDUBSW,
                                                       i_micro_kernel_config->vector_name,
                                                       l_k%2,
                                                       3,
                                                       3 );
            } else {
              LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_ARCH_PREC );
              return;
            }

            /* 16 bit mul with 1 */
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPMADDWD,
                                              i_micro_kernel_config->vector_name,
                                              2,
                                              3,
                                              3 );

            /* add to accumulator */
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPADDD,
                                              i_micro_kernel_config->vector_name,
                                              3,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          } else if ( io_generated_code->arch <= LIBXSMM_X86_ALLFEAT ) {
            if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_A_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                       io_generated_code->arch,
                                                       LIBXSMM_X86_INSTR_VPDPBUSD,
                                                       1,
                                                       l_b_reg,
                                                       l_b_idx,
                                                       l_scale,
                                                       l_disp,
                                                       i_micro_kernel_config->vector_name,
                                                       l_k%2,
                                                       i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
            } else if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_B_UNSIGNED) > 0 ) {
              libxsmm_x86_instruction_vec_move( io_generated_code,
                                                io_generated_code->arch,
                                                LIBXSMM_X86_INSTR_VPBROADCASTD,
                                                l_b_reg,
                                                l_b_idx, l_scale,
                                                l_disp,
                                                i_micro_kernel_config->vector_name,
                                                3, 0, 1, 0 );

              libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                                io_generated_code->arch,
                                                LIBXSMM_X86_INSTR_VPDPBUSD,
                                                i_micro_kernel_config->vector_name,
                                                l_k%2,
                                                3,
                                                i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );

            } else {
              LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_ARCH_PREC );
              return;
            }
          } else {
            LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_ARCH_PREC );
            return;
          }
        } else if (LIBXSMM_GEMM_PRECISION_BF16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          if ( io_generated_code->arch < LIBXSMM_X86_AVX512_CPX ) {
            /* broadcast pair of B matrix values into zmm2 */
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VBROADCASTSS,
                                              l_b_reg,
                                              l_b_idx, l_scale,
                                              l_disp,
                                              i_micro_kernel_config->vector_name,
                                              2, 0, 1, 0 );

            /* we put "1" elements of B matrix into zmm2 */
            libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
                io_generated_code->arch,
                LIBXSMM_X86_INSTR_VPSRAD,
                i_micro_kernel_config->vector_name,
                2,
                2,
                LIBXSMM_X86_VEC_REG_UNDEF,
                16);
            libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
                io_generated_code->arch,
                LIBXSMM_X86_INSTR_VPSLLD,
                i_micro_kernel_config->vector_name,
                2,
                2,
                LIBXSMM_X86_VEC_REG_UNDEF,
                16);

            /* perform fma operations for multiplying "1" elements of A and B */
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VFMADD231PS,
                                              i_micro_kernel_config->vector_name,
                                              l_k%2,
                                              2,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );

            /* broadcast pair of B matrix values into zmm2 */
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VBROADCASTSS,
                                              l_b_reg,
                                              l_b_idx, l_scale,
                                              l_disp,
                                              i_micro_kernel_config->vector_name,
                                              2, 0, 1, 0 );

            /* we put "0" elements of B matrix into zmm2 */
            libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
                io_generated_code->arch,
                LIBXSMM_X86_INSTR_VPSLLD,
                i_micro_kernel_config->vector_name,
                2,
                2,
                LIBXSMM_X86_VEC_REG_UNDEF,
                16);

            /* perform fma operations for multiplying "0" elements of A and B */
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VFMADD231PS,
                                              i_micro_kernel_config->vector_name,
                                              3,
                                              2,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          } else {
            libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                     io_generated_code->arch,
                                                     LIBXSMM_X86_INSTR_VDPBF16PS,
                                                     1,
                                                     l_b_reg,
                                                     l_b_idx,
                                                     l_scale,
                                                     l_disp,
                                                     i_micro_kernel_config->vector_name,
                                                     l_k%2,
                                                     i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          }
        } else {
          /* shoudn't happen */
        }

        if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_AL1_BL1_CL1) {
          b_prefetch_blocks = i_xgemm_desc->k/i_micro_kernel_config->vector_length;
          idx = l_k;
          point = l_n;

          /* Case where K == cache line */
          if (b_prefetch_blocks == 1) {
            /* Spread out prefetches */
            if ( i_n_blocking <= i_k_blocking ) {
              if ( point == 2 ) {
                if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_AL1) {
                  libxsmm_x86_instruction_prefetch( io_generated_code,
                                                   LIBXSMM_X86_INSTR_PREFETCHT0,
                                                   i_gp_reg_mapping->gp_reg_a_prefetch,
                                                   LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                   (i_xgemm_desc->lda * idx * i_micro_kernel_config->datatype_size) );
                }
              }

              if ( (point == 7) && (idx < i_n_blocking) ) {
                if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                  libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx * i_micro_kernel_config->datatype_size) );
                }
              }

              if ( (point == 12) && (idx < i_n_blocking) ) {
                if ((i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_CL1) && (i_n_blocking <= 27) ) {
                  libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_c_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldc * idx * i_micro_kernel_config->datatype_size) );
                }
              }
            } else {
              if ( point == 1) {
                if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_AL1) {
                  libxsmm_x86_instruction_prefetch( io_generated_code,
                                                   LIBXSMM_X86_INSTR_PREFETCHT0,
                                                   i_gp_reg_mapping->gp_reg_a_prefetch,
                                                   LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                   (i_xgemm_desc->lda * idx * i_micro_kernel_config->datatype_size) );
                }
              }

              if ( point == 5 && (idx < i_n_blocking) ) {
                if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                  libxsmm_x86_instruction_prefetch( io_generated_code,
                                                   LIBXSMM_X86_INSTR_PREFETCHT0,
                                                   i_gp_reg_mapping->gp_reg_b_prefetch,
                                                   LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                   (i_xgemm_desc->ldb * idx * i_micro_kernel_config->datatype_size) );
                }
              }

              if ( point == 9 && (idx < i_n_blocking) ) {
                if ((i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_CL1) && (i_n_blocking <= 27)) {
                  libxsmm_x86_instruction_prefetch( io_generated_code,
                                                   LIBXSMM_X86_INSTR_PREFETCHT0,
                                                   i_gp_reg_mapping->gp_reg_c_prefetch,
                                                   LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                   (i_xgemm_desc->ldc * idx * i_micro_kernel_config->datatype_size) );
                }
              }

              if ( point == 13 && ( idx+16 < i_n_blocking) ) {
                if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                  libxsmm_x86_instruction_prefetch( io_generated_code,
                                                   LIBXSMM_X86_INSTR_PREFETCHT0,
                                                   i_gp_reg_mapping->gp_reg_b_prefetch,
                                                   LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                   (i_xgemm_desc->ldb * (idx+16) * i_micro_kernel_config->datatype_size) );
                }
              }

              if ( point == 15 && (idx+16 < i_n_blocking) ) {
                if ((i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_CL1) && (i_n_blocking <= 27)) {
                  libxsmm_x86_instruction_prefetch( io_generated_code,
                                                   LIBXSMM_X86_INSTR_PREFETCHT0,
                                                   i_gp_reg_mapping->gp_reg_c_prefetch,
                                                   LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                   (i_xgemm_desc->ldc * (idx+16) * i_micro_kernel_config->datatype_size) );
                }
              }
            }
          }

          /* Case where K == 2 cache lines */
          if (b_prefetch_blocks == 2) {
            if ( point == 2 ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_AL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_a_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->lda * idx * i_micro_kernel_config->datatype_size) );
              }
            }

            if ( (point == 7) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx * i_micro_kernel_config->datatype_size) );
              }
            }

            if ( (point == 12) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx + 16) * i_micro_kernel_config->datatype_size );
              }
            }

            if ( (point == 15) && (idx < i_n_blocking) ) {
              if ((i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_CL1) && (i_n_blocking <= 27)) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_c_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldc * idx * i_micro_kernel_config->datatype_size) );
              }
            }
          }

          /* Case where K == 3 cache lines */
          if (b_prefetch_blocks == 3) {
            if ( point == 1 ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_AL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_a_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->lda * idx * i_micro_kernel_config->datatype_size) );
              }
            }

            if ( (point == 5) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx * i_micro_kernel_config->datatype_size) );
              }
            }

            if ( (point == 9) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx + 16) * i_micro_kernel_config->datatype_size );
              }
            }

            if ( (point == 13) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx + 32) * i_micro_kernel_config->datatype_size );
              }
            }

            if ( (point == 15) && (idx < i_n_blocking) ) {
              if ((i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_CL1) && (i_n_blocking <= 27)) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_c_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldc * idx * i_micro_kernel_config->datatype_size) );
              }
            }
          }

          /* Case where K == 4 cache lines */
          if (b_prefetch_blocks == 4) {
            if ( point == 1 ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_AL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_a_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->lda * idx * i_micro_kernel_config->datatype_size) );
              }
            }

            if ( (point == 4) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx * i_micro_kernel_config->datatype_size) );
              }
            }

            if ( (point == 7) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx + 16) * i_micro_kernel_config->datatype_size );
              }
            }

            if ( (point == 10) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx + 32) * i_micro_kernel_config->datatype_size );
              }
            }

            if ( (point == 13) && (idx < i_n_blocking) ) {
              if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldb * idx + 48) * i_micro_kernel_config->datatype_size );
              }
            }

            if ( (point == 15) && (idx < i_n_blocking) ) {
              if ((i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_CL1) && (i_n_blocking <= 27) ) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_c_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 (i_xgemm_desc->ldc * idx * i_micro_kernel_config->datatype_size) );
              }
            }
          }
        }
#else
        if ( LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) || LIBXSMM_GEMM_PRECISION_F64 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                    io_generated_code->arch,
                                                    i_micro_kernel_config->vmul_instruction,
                                                    1,
                                                    l_b_reg,
                                                    l_b_idx,
                                                    l_scale,
                                                    l_disp,
                                                    i_micro_kernel_config->vector_name,
                                                    l_k%2,
                                                    i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
        } else if (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          if ( io_generated_code->arch == LIBXSMM_X86_AVX512_CORE ) {
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPBROADCASTD,
                                              l_b_reg,
                                              l_b_idx, l_scale,
                                              l_disp,
                                              i_micro_kernel_config->vector_name,
                                              3, 0, 1, 0 );
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPMADDWD,
                                              i_micro_kernel_config->vector_name,
                                              l_k%2,
                                              3,
                                              3 );
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VPADDD,
                                              i_micro_kernel_config->vector_name,
                                              3,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          } else if ( ( io_generated_code->arch >= LIBXSMM_X86_AVX512_CLX ) || ( io_generated_code->arch <= LIBXSMM_X86_ALLFEAT ) ) {
            libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                     io_generated_code->arch,
                                                     LIBXSMM_X86_INSTR_VPDPWSSDS,
                                                     1,
                                                     l_b_reg,
                                                     l_b_idx,
                                                     l_scale,
                                                     l_disp,
                                                     i_micro_kernel_config->vector_name,
                                                     l_k%2,
                                                     i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          } else {
            /* shouldn't happen */
          }
        } else if (LIBXSMM_GEMM_PRECISION_BF16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          if ( io_generated_code->arch < LIBXSMM_X86_AVX512_CPX ) {
            /* broadcast pair of B matrix values into zmm2 */
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VBROADCASTSS,
                                              l_b_reg,
                                              l_b_idx, l_scale,
                                              l_disp,
                                              i_micro_kernel_config->vector_name,
                                              2, 0, 1, 0 );

            /* we put "1" elements of B matrix into zmm2 */
            libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
                io_generated_code->arch,
                LIBXSMM_X86_INSTR_VPSRAD,
                i_micro_kernel_config->vector_name,
                2,
                2,
                LIBXSMM_X86_VEC_REG_UNDEF,
                16);
            libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
                io_generated_code->arch,
                LIBXSMM_X86_INSTR_VPSLLD,
                i_micro_kernel_config->vector_name,
                2,
                2,
                LIBXSMM_X86_VEC_REG_UNDEF,
                16);

            /* perform fma operations for multiplying "1" elements of A and B */
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VFMADD231PS,
                                              i_micro_kernel_config->vector_name,
                                              l_k%2,
                                              2,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );

            /* broadcast pair of B matrix values into zmm2 */
            libxsmm_x86_instruction_vec_move( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VBROADCASTSS,
                                              l_b_reg,
                                              l_b_idx, l_scale,
                                              l_disp,
                                              i_micro_kernel_config->vector_name,
                                              2, 0, 1, 0 );

            /* we put "0" elements of B matrix into zmm2 */
            libxsmm_x86_instruction_vec_shuffle_reg(io_generated_code,
                io_generated_code->arch,
                LIBXSMM_X86_INSTR_VPSLLD,
                i_micro_kernel_config->vector_name,
                2,
                2,
                LIBXSMM_X86_VEC_REG_UNDEF,
                16);

            /* perform fma operations for multiplying "0" elements of A and B */
            libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                              io_generated_code->arch,
                                              LIBXSMM_X86_INSTR_VFMADD231PS,
                                              i_micro_kernel_config->vector_name,
                                              3,
                                              2,
                                              i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          } else {
            libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                     io_generated_code->arch,
                                                     LIBXSMM_X86_INSTR_VDPBF16PS,
                                                     1,
                                                     l_b_reg,
                                                     l_b_idx,
                                                     l_scale,
                                                     l_disp,
                                                     i_micro_kernel_config->vector_name,
                                                     l_k%2,
                                                     i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
          }
        } else {
          /* shoudn't happen */
        }
#endif
      }
      l_displacement_k_b++;
    } else {
      for ( l_n = 0; l_n < i_n_blocking; l_n++) {
        libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                 io_generated_code->arch,
                                                 i_micro_kernel_config->vmul_instruction,
                                                 1,
                                                 i_gp_reg_mapping->gp_reg_b,
                                                 LIBXSMM_X86_GP_REG_UNDEF,
                                                 0,
                                                 (l_k*i_xgemm_desc->ldb*i_micro_kernel_config->datatype_size) + (l_n*i_micro_kernel_config->datatype_size),
                                                 i_micro_kernel_config->vector_name,
                                                 l_k%2,
                                                 i_micro_kernel_config->vector_reg_count - (i_n_blocking*((l_k%l_n_accs)+1)) + l_n );
      }
    }
  }

  /* Adjust a pointer */
  if (l_k_a_update > 0) {
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_sub_instruction, i_gp_reg_mapping->gp_reg_a,
                                     l_k_a_update );
  }

  if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) == 0 ) {
    /* advance pointers of B only when we are not fully unrolling K and taking care of intermediate advances */
    if ( i_k_blocking < (unsigned int)i_xgemm_desc->k ) {
      /* advance pointers of B */
      libxsmm_x86_instruction_alu_imm( io_generated_code,
                                       i_micro_kernel_config->alu_add_instruction,
                                       i_gp_reg_mapping->gp_reg_b,
                                       (i_k_blocking * i_micro_kernel_config->datatype_size) - (128*(i_micro_kernel_config->datatype_size)*l_k_b_updates) );
    } else {
      /* we have to make sure that we are reseting the pointer to its original value in case a full unroll */
      if ( l_k_b_updates > 0 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code,
                                         i_micro_kernel_config->alu_sub_instruction,
                                         i_gp_reg_mapping->gp_reg_b, 128*(i_micro_kernel_config->datatype_size)*l_k_b_updates );
      }
    }
  } else {
    /* advance pointers of B only when we are not fully unrolling K and taking care of intermediate advances */
    if ( i_k_blocking < (unsigned int)i_xgemm_desc->k ) {
      /* advance B ptr by K rows */
      libxsmm_x86_instruction_alu_imm( io_generated_code,
                                       i_micro_kernel_config->alu_add_instruction,
                                       i_gp_reg_mapping->gp_reg_b,
                                       (i_k_blocking * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb) );
    }
  }

  /* add additional accumulators, if needed */
  for ( l_k = 1; l_k < l_n_accs; l_k++) {
    for ( l_n = 0; l_n < i_n_blocking; l_n++) {
      if ( LIBXSMM_GEMM_PRECISION_F32  == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ||
           LIBXSMM_GEMM_PRECISION_F64  == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ||
           LIBXSMM_GEMM_PRECISION_BF16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype )    ) {
        libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                             io_generated_code->arch,
                                             i_micro_kernel_config->vadd_instruction,
                                             i_micro_kernel_config->vector_name,
                                             i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n,
                                             i_micro_kernel_config->vector_reg_count - i_n_blocking + l_n,
                                             i_micro_kernel_config->vector_reg_count - i_n_blocking + l_n );
      } else if (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
        libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                             io_generated_code->arch,
                                             LIBXSMM_X86_INSTR_VPADDD,
                                             i_micro_kernel_config->vector_name,
                                             i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n,
                                             i_micro_kernel_config->vector_reg_count - i_n_blocking + l_n,
                                             i_micro_kernel_config->vector_reg_count - i_n_blocking + l_n );
      } else {
        /* shouldn't happen */
      }
    }
  }
}


LIBXSMM_API_INTERN
void libxsmm_generator_gemm_avx512_microkernel_fsdbcst_qfma( libxsmm_generated_code*            io_generated_code,
                                                             const libxsmm_gp_reg_mapping*      i_gp_reg_mapping,
                                                             const libxsmm_micro_kernel_config* i_micro_kernel_config,
                                                             const libxsmm_gemm_descriptor*     i_xgemm_desc,
                                                             const unsigned int                 i_n_blocking,
                                                             const unsigned int                 i_k_blocking )
{
  unsigned int l_n;
  unsigned int l_k;
  unsigned int l_z;
  unsigned int l_b_reg;
  unsigned int l_b_idx;
  unsigned int l_scale;
  unsigned int l_disp;
  unsigned int l_displacement_k_b = 0;
  unsigned int l_k_b_updates = 0;
  unsigned int l_displacement_k_a = 0;
  unsigned int l_k_a_update = 0;
  unsigned int l_n_accs = 0;
  unsigned int b_prefetch_blocks = 0;
  unsigned int b_pref_freq = 1;

#if !defined(NDEBUG)
  /* @TODO, this is just worst case handling, int16 and bf16 might require 28 as upper limit on certain platforms */
  if ( i_n_blocking > 30 ) {
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_N_BLOCK );
    return;
  }
#endif

  /* compute number of n accumulators to hide FMA latencies */
  if (i_n_blocking >= 14) {
    l_n_accs = 1;
  } else if (i_n_blocking >= 7) {
    l_n_accs = 2;
  } else {
    l_n_accs = 4;
  }

  /* Initialize helper registers for SIB addressing */
  /* helper 0: Index register holding ldb*datatype_size */
  libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                   i_gp_reg_mapping->gp_reg_help_0, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
  /* helper 1: Index register holding 3*ldb*datatype_size */
  libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                   i_gp_reg_mapping->gp_reg_help_1, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb * 3 );
  /* helper 2: Index register holding 5*ldb*datatype_size */
  libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                   i_gp_reg_mapping->gp_reg_help_2, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb * 5 );
  /* helper 3: Index register holding 7*ldb*datatype_size */
  libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                   i_gp_reg_mapping->gp_reg_help_3, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb * 7 );

  /* helper 4: B + 9*ldb, additional base address
     helper 5: B + 18*ldb, additional base address, using the reg_c register, which was saved to stack
     helper 6: B + 27*ldb, additional base address, WARNING: If i_n_blocking is > 27, then we can not prefetch C in L1 */
  if ( i_n_blocking > 9 ) {
    libxsmm_x86_instruction_alu_reg( io_generated_code, i_micro_kernel_config->alu_mov_instruction, i_gp_reg_mapping->gp_reg_b, i_gp_reg_mapping->gp_reg_help_4);
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                     i_gp_reg_mapping->gp_reg_help_4,  9 * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
  }
  if ( i_n_blocking > 18 ) {
    libxsmm_x86_instruction_alu_reg( io_generated_code, i_micro_kernel_config->alu_mov_instruction, i_gp_reg_mapping->gp_reg_b, i_gp_reg_mapping->gp_reg_c);
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                     i_gp_reg_mapping->gp_reg_c, 18 * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
  }
  if ( i_n_blocking > 27 ) {
    libxsmm_x86_instruction_alu_reg( io_generated_code, i_micro_kernel_config->alu_mov_instruction, i_gp_reg_mapping->gp_reg_b, i_gp_reg_mapping->gp_reg_help_5);
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                     i_gp_reg_mapping->gp_reg_help_5, 27 * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
  }

  l_displacement_k_b = 0;
  l_displacement_k_a = 0;

  /* xor additional accumulator, if needed */
  for ( l_k = 1; l_k < l_n_accs; l_k++) {
    for ( l_n = 0; l_n < i_n_blocking; l_n++) {
      libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                           io_generated_code->arch,
                                           i_micro_kernel_config->vxor_instruction,
                                           i_micro_kernel_config->vector_name,
                                           i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n,
                                           i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n,
                                           i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n );
    }
  }

  /* apply k blocking */
  for ( l_k = 0; l_k < i_k_blocking; ++l_k ) {
    unsigned int l_lcl_k = (l_k+4 <= i_k_blocking) ? 4 : 1;

    /* advance b pointer if needed */
    if ( (l_k > 0) && ((l_k%128) == 0) ) {
      libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                       i_gp_reg_mapping->gp_reg_b, 128*i_micro_kernel_config->datatype_size );
      /* advance the second base pointer only if it's needed */
      if ( i_n_blocking > 9 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                         i_gp_reg_mapping->gp_reg_help_4, 128*i_micro_kernel_config->datatype_size );
      }
      /* advance the third base pointer only if it's needed */
      if ( i_n_blocking > 18 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                         i_gp_reg_mapping->gp_reg_c, 128*i_micro_kernel_config->datatype_size );
      }
      /* advance the fourth base pointer only if it's needed */
      if ( i_n_blocking > 27 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                         i_gp_reg_mapping->gp_reg_help_5, 128*i_micro_kernel_config->datatype_size );
      }

      l_displacement_k_b = 0;
      l_k_b_updates++;
    }

    for ( l_z = 0; l_z < l_lcl_k; l_z++ ) {
      libxsmm_x86_instruction_vec_move( io_generated_code,
                                        io_generated_code->arch,
                                        i_micro_kernel_config->a_vmove_instruction,
                                        i_gp_reg_mapping->gp_reg_a,
                                        LIBXSMM_X86_GP_REG_UNDEF, 0,
                                        i_xgemm_desc->lda * l_displacement_k_a * i_micro_kernel_config->datatype_size,
                                        i_micro_kernel_config->vector_name,
                                        l_z,
                                        i_micro_kernel_config->use_masking_a_c, 1, 0 );
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * l_displacement_k_a * i_micro_kernel_config->datatype_size) + 64 );
      }
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }
    }

    /* next A prefetch "same" rows in "same" column, but in a different matrix */
    if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_JPST ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_JPST ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2 ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C) {
      for ( l_z = 0; l_z < l_lcl_k; l_z++ ) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a_prefetch,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_k+l_z) * i_micro_kernel_config->datatype_size) );
      }
      if ( ((l_k+l_lcl_k) == i_k_blocking) && (i_k_blocking != (unsigned int)i_xgemm_desc->k) ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code,
                                         i_micro_kernel_config->alu_add_instruction,
                                         i_gp_reg_mapping->gp_reg_a_prefetch,
                                         i_k_blocking * i_micro_kernel_config->datatype_size * i_xgemm_desc->lda );
      }
    }

    /* in last k-iteration: advance pointers */
    if ( ((l_k+l_lcl_k) == i_k_blocking) && (i_k_blocking != (unsigned int)i_xgemm_desc->k) ) {
      libxsmm_x86_instruction_alu_imm( io_generated_code,
                                       i_micro_kernel_config->alu_add_instruction,
                                       i_gp_reg_mapping->gp_reg_a,
                                       i_k_blocking * i_micro_kernel_config->datatype_size * i_xgemm_desc->lda );
    }

    /* compute vectorwidth (A) * column broadcast (B) */
    for ( l_n = 0; l_n < i_n_blocking; l_n++) {
      /* determining base, idx and scale values */
      l_b_reg = i_gp_reg_mapping->gp_reg_b;
      l_b_idx = LIBXSMM_X86_GP_REG_UNDEF;
      l_scale = 0;
      l_disp = l_displacement_k_b*i_micro_kernel_config->datatype_size;
      /* select the base register */
      if ( l_n > 26 ) {
        l_b_reg = i_gp_reg_mapping->gp_reg_help_5;
      } else if ( l_n > 17 ) {
        l_b_reg = i_gp_reg_mapping->gp_reg_c;
      } else if ( l_n > 8 ) {
        l_b_reg = i_gp_reg_mapping->gp_reg_help_4;
      } else {
        l_b_reg = i_gp_reg_mapping->gp_reg_b;
      }
      /* Select SIB */
      if ( l_n % 9 == 0 ) {
        l_b_idx = LIBXSMM_X86_GP_REG_UNDEF;
        l_scale = 0;
      } else if ( l_n % 9 == 1 ) {
        l_b_idx = i_gp_reg_mapping->gp_reg_help_0;
        l_scale = 1;
      } else if ( l_n % 9 == 2 ) {
        l_b_idx = i_gp_reg_mapping->gp_reg_help_0;
        l_scale = 2;
      } else if ( l_n % 9 == 3 ) {
        l_b_idx = i_gp_reg_mapping->gp_reg_help_1;
        l_scale = 1;
      } else if ( l_n % 9 == 4 ) {
        l_b_idx = i_gp_reg_mapping->gp_reg_help_0;
        l_scale = 4;
      } else if ( l_n % 9 == 5 ) {
        l_b_idx = i_gp_reg_mapping->gp_reg_help_2;
        l_scale = 1;
      } else if ( l_n % 9 == 6 ) {
        l_b_idx = i_gp_reg_mapping->gp_reg_help_1;
        l_scale = 2;
      } else if ( l_n % 9 == 7 ) {
        l_b_idx = i_gp_reg_mapping->gp_reg_help_3;
        l_scale = 1;
      } else if ( l_n % 9 == 8 ) {
        l_b_idx = i_gp_reg_mapping->gp_reg_help_0;
        l_scale = 8;
      } else {
        /* shouldn't happen.... */
      }

      if ( l_lcl_k == 4 ) {
        if (LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          libxsmm_x86_instruction_vec_compute_qfma( io_generated_code,
                                                    io_generated_code->arch,
                                                    LIBXSMM_X86_INSTR_V4FMADDPS,
                                                    l_b_reg,
                                                    l_b_idx,
                                                    l_scale,
                                                    l_disp,
                                                    i_micro_kernel_config->vector_name,
                                                    0,
                                                    i_micro_kernel_config->vector_reg_count - (i_n_blocking*(((l_k/4)%l_n_accs)+1)) + l_n );
        } else if (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          libxsmm_x86_instruction_vec_compute_qfma( io_generated_code,
                                                    io_generated_code->arch,
                                                    LIBXSMM_X86_INSTR_VP4DPWSSD,
                                                    l_b_reg,
                                                    l_b_idx,
                                                    l_scale,
                                                    l_disp,
                                                    i_micro_kernel_config->vector_name,
                                                    0,
                                                    i_micro_kernel_config->vector_reg_count - (i_n_blocking*(((l_k/4)%l_n_accs)+1)) + l_n );
        } else {
          /* shouldn't happen */
        }
      } else {
        if (LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                                   io_generated_code->arch,
                                                   i_micro_kernel_config->vmul_instruction,
                                                   1,
                                                   l_b_reg,
                                                   l_b_idx,
                                                   l_scale,
                                                   l_disp,
                                                   i_micro_kernel_config->vector_name,
                                                   0,
                                                   i_micro_kernel_config->vector_reg_count - (i_n_blocking*(((l_k)%l_n_accs)+1)) + l_n );
        } else if (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) {
          LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_K_BLOCK );
          return;
        } else {
          /* shouldn't happen */
        }
      }

      if ( (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_AL1_BL1_CL1) &&
           (l_k % 4 == 0) &&
           (i_k_blocking >= i_micro_kernel_config->vector_length) ) {

        b_prefetch_blocks = i_k_blocking/i_micro_kernel_config->vector_length;
        b_pref_freq = ((i_k_blocking/4)/b_prefetch_blocks)/2;

        if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_AL1) {
          if ( (l_n == 1) || (l_n == 6) || (l_n == 10) || (l_n == 15) ) {
            libxsmm_x86_instruction_prefetch( io_generated_code,
                                              LIBXSMM_X86_INSTR_PREFETCHT0,
                                              i_gp_reg_mapping->gp_reg_a_prefetch,
                                              LIBXSMM_X86_GP_REG_UNDEF, 0,
                                              i_xgemm_desc->lda * (l_k + l_n/4) * i_micro_kernel_config->datatype_size );
          }
        }

        if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_BL1) {
          assert(0 != b_pref_freq);
          if ( l_k % (b_pref_freq*4) == 0 ) {
            if ( l_k % (b_pref_freq*8) == 0) {
              /* prefetch N/2 columns of B */
              if (l_n % 2 == 0) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size + (l_k /(b_pref_freq*8)) * i_micro_kernel_config->vector_length * i_micro_kernel_config->datatype_size );
              }
            } else {
              /* prefetch the remaining N/2 columns of B */
              if (l_n % 2 == 1) {
                libxsmm_x86_instruction_prefetch( io_generated_code,
                                                 LIBXSMM_X86_INSTR_PREFETCHT0,
                                                 i_gp_reg_mapping->gp_reg_b_prefetch,
                                                 LIBXSMM_X86_GP_REG_UNDEF, 0,
                                                 i_xgemm_desc->ldb * l_n * i_micro_kernel_config->datatype_size + (l_k /(b_pref_freq*8))  * i_micro_kernel_config->vector_length * i_micro_kernel_config->datatype_size );
              }
            }
          }
        }

        if (i_xgemm_desc->prefetch & LIBXSMM_GEMM_PREFETCH_CL1) {
          if ( l_k == 4 ) {
            libxsmm_x86_instruction_prefetch( io_generated_code,
                                             LIBXSMM_X86_INSTR_PREFETCHT0,
                                             i_gp_reg_mapping->gp_reg_c_prefetch,
                                             LIBXSMM_X86_GP_REG_UNDEF, 0,
                                             i_xgemm_desc->ldc * l_n * i_micro_kernel_config->datatype_size );
          }
        }
      }
    }
    if (l_lcl_k == 4) {
      l_displacement_k_b+=4;
      l_k+=3;
    } else {
      l_displacement_k_b++;
    }
  }

  /* Adjust a pointer */
  if (l_k_a_update > 0) {
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_sub_instruction, i_gp_reg_mapping->gp_reg_a,
                                     l_k_a_update );
  }

  /* advance pointers of B only when we are not fully unrolling K and taking care of intermediate advances */
  if ( i_k_blocking < (unsigned int)i_xgemm_desc->k ) {
    /* advance pointers of B */
    libxsmm_x86_instruction_alu_imm( io_generated_code,
                                     i_micro_kernel_config->alu_add_instruction,
                                     i_gp_reg_mapping->gp_reg_b,
                                     (i_k_blocking * i_micro_kernel_config->datatype_size) - (128*(i_micro_kernel_config->datatype_size)*l_k_b_updates) );
  } else {
    /* we have to make sure that we are reseting the pointer to its original value in case a full unroll */
    if ( l_k_b_updates > 0 ) {
      libxsmm_x86_instruction_alu_imm( io_generated_code,
                                       i_micro_kernel_config->alu_sub_instruction,
                                       i_gp_reg_mapping->gp_reg_b, 128*(i_micro_kernel_config->datatype_size)*l_k_b_updates );
    }
  }

  /* add additional accumulators, if needed */
  for ( l_k = 1; l_k < l_n_accs; l_k++) {
    for ( l_n = 0; l_n < i_n_blocking; l_n++) {
      libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                           io_generated_code->arch,
                                           i_micro_kernel_config->vadd_instruction,
                                           i_micro_kernel_config->vector_name,
                                           i_micro_kernel_config->vector_reg_count - (i_n_blocking*(l_k+1)) + l_n,
                                           i_micro_kernel_config->vector_reg_count - i_n_blocking + l_n,
                                           i_micro_kernel_config->vector_reg_count - i_n_blocking + l_n );
    }
  }
}


LIBXSMM_API_INTERN
void libxsmm_generator_gemm_avx512_microkernel_fsdbcst_k_large_n_nine( libxsmm_generated_code*            io_generated_code,
                                                                       const libxsmm_gp_reg_mapping*      i_gp_reg_mapping,
                                                                       const libxsmm_micro_kernel_config* i_micro_kernel_config,
                                                                       const libxsmm_gemm_descriptor*     i_xgemm_desc,
                                                                       const unsigned int                 i_k_blocking )
{
  unsigned int l_n;
  unsigned int l_k;
  const unsigned int l_n_blocking = 9;
  unsigned int l_displacement_k_b = 0;
  unsigned int l_k_b_updates = 0;
  unsigned int l_displacement_k_a = 0;
  unsigned int l_k_a_update = 0;

#if !defined(NDEBUG)
  if ( i_k_blocking < 8 ) {
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_K_BLOCK );
    return;
  }
#endif

  /* Initialize helper registers for SIB addressing */
  if ( i_k_blocking != 9 ) {
    /* helper 0: Index register holding ldb*datatype_size */
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_mov_instruction,
                                     i_gp_reg_mapping->gp_reg_help_0, i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
    /* helper 1: B + 3*ldb, additional base address
      helper 2: B + 6*ldb, additional base address */
    libxsmm_x86_instruction_alu_reg( io_generated_code, i_micro_kernel_config->alu_mov_instruction, i_gp_reg_mapping->gp_reg_b, i_gp_reg_mapping->gp_reg_help_1);
    libxsmm_x86_instruction_alu_reg( io_generated_code, i_micro_kernel_config->alu_mov_instruction, i_gp_reg_mapping->gp_reg_b, i_gp_reg_mapping->gp_reg_help_2);
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                     i_gp_reg_mapping->gp_reg_help_1, 3 * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction,
                                     i_gp_reg_mapping->gp_reg_help_2, 6 * i_micro_kernel_config->datatype_size * i_xgemm_desc->ldb );
  }

  /* init a displacement for k unrolling */
  l_displacement_k_a = 0;
  l_k_a_update = 0;

  /* apply k blocking */
  for ( l_k = 0; l_k < i_k_blocking; l_k++ ) {
    unsigned int l_vcompute = 0;
    unsigned int l_register_offset = 0;

    if ( i_k_blocking != 9 ) {
      if ( (l_k > 0) && ((l_k%128) == 0) ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_b, 128*i_micro_kernel_config->datatype_size );
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_help_2, 128*i_micro_kernel_config->datatype_size );
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_help_1, 128*i_micro_kernel_config->datatype_size );

        l_displacement_k_b = 0;
        l_k_b_updates++;
      }
    } else {
      l_displacement_k_b = 0;
      l_k_b_updates = 0;
    }

    if ( l_k == 0 ) {
      /* load A, zmm0 + 1 */
      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    0,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }

      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    1,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }

      /* handle prefetch */


    } else if ( l_k == 1 ) {
      /* load A, zmm2 + 3 */
      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    2,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }

      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    3,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }
    } else if ( l_k == 2 ) {
      /* load A, zmm4 + 5 */
      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    4,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }

      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    5,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }
    } else if ( l_k == 3 ) {
      /* load A, zmm6 + 7 */
      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    6,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }

      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    7,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }
    } else if ( l_k < (i_k_blocking - 4) ) {
      /* pipelined load of A, one k iteration ahead */
      libxsmm_x86_instruction_vec_move( io_generated_code,
                                    io_generated_code->arch,
                                    i_micro_kernel_config->a_vmove_instruction,
                                    i_gp_reg_mapping->gp_reg_a,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size,
                                    i_micro_kernel_config->vector_name,
                                    (l_k+4)%8,
                                    i_micro_kernel_config->use_masking_a_c, 1, 0 );
#if 0
      /* current A prefetch, next 8 rows for the current column */
      if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
           i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
        libxsmm_x86_instruction_prefetch( io_generated_code,
                                          i_micro_kernel_config->prefetch_instruction,
                                          i_gp_reg_mapping->gp_reg_a,
                                          LIBXSMM_X86_GP_REG_UNDEF, 0,
                                          (i_xgemm_desc->lda * (l_displacement_k_a) * i_micro_kernel_config->datatype_size) + 64 );
      }
#endif
      /* handle large displacements */
      if ( ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) >= 8192 ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_add_instruction, i_gp_reg_mapping->gp_reg_a, ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size) );
        l_k_a_update += ((l_displacement_k_a+1)*i_xgemm_desc->lda*i_micro_kernel_config->datatype_size);
        l_displacement_k_a = 0;
      } else {
        l_displacement_k_a++;
      }
    }

    /* current A prefetch, next 8 rows for the current column */
    if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_AHEAD ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_AHEAD) {
      libxsmm_x86_instruction_prefetch( io_generated_code,
                                        i_micro_kernel_config->prefetch_instruction,
                                        i_gp_reg_mapping->gp_reg_a,
                                        LIBXSMM_X86_GP_REG_UNDEF, 0,
                                        (i_xgemm_desc->lda * l_k * i_micro_kernel_config->datatype_size) + 64 - l_k_a_update );
    }

    /* next A prefetch "same" rows in "same" column, but in a different matrix */
    if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2 ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C) {
      libxsmm_x86_instruction_prefetch( io_generated_code,
                                    i_micro_kernel_config->prefetch_instruction,
                                    i_gp_reg_mapping->gp_reg_a_prefetch,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    (i_xgemm_desc->lda * l_k * i_micro_kernel_config->datatype_size) );
      if ( l_k == (i_k_blocking - 1) && (i_k_blocking != (unsigned int)i_xgemm_desc->k) ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code,
                                     i_micro_kernel_config->alu_add_instruction,
                                     i_gp_reg_mapping->gp_reg_a_prefetch,
                                     i_k_blocking * i_micro_kernel_config->datatype_size * i_xgemm_desc->lda );
      }
    }

    if ( i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2_JPST ||
         i_xgemm_desc->prefetch == LIBXSMM_GEMM_PREFETCH_AL2BL2_VIA_C_JPST) {
      libxsmm_x86_instruction_prefetch( io_generated_code,
                                    i_micro_kernel_config->prefetch_instruction,
                                    i_gp_reg_mapping->gp_reg_a_prefetch,
                                    LIBXSMM_X86_GP_REG_UNDEF, 0,
                                    (i_xgemm_desc->lda * l_k * i_micro_kernel_config->datatype_size) );
      if ( l_k == (i_k_blocking - 1) ) {
        libxsmm_x86_instruction_alu_imm( io_generated_code,
                                     i_micro_kernel_config->alu_add_instruction,
                                     i_gp_reg_mapping->gp_reg_a_prefetch,
                                     i_k_blocking * i_micro_kernel_config->datatype_size * i_xgemm_desc->lda );
      }
    }

    /* in last k-iteration: advance pointers */
    if ( l_k == (i_k_blocking - 1) && (i_k_blocking != (unsigned int)i_xgemm_desc->k) ) {
      libxsmm_x86_instruction_alu_imm( io_generated_code,
                                   i_micro_kernel_config->alu_add_instruction,
                                   i_gp_reg_mapping->gp_reg_a,
                                   i_k_blocking * i_micro_kernel_config->datatype_size * i_xgemm_desc->lda );
    }

    /* compute vectorwidth (A) * column broadcast (B) */
    /* defining the compute routine */
    l_vcompute = i_micro_kernel_config->vmul_instruction;
    l_register_offset = l_n_blocking;

    if ( i_k_blocking != 9 ) {
      if (l_k == 1) {
        if ( LIBXSMM_GEMM_PRECISION_F64 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype )  ) {
          l_vcompute = LIBXSMM_X86_INSTR_VMULPD;
        } else {
          l_vcompute = LIBXSMM_X86_INSTR_VMULPS;
        }
      }
      l_register_offset = (l_n_blocking*((l_k%2)+1));
    }

    if (0 != io_generated_code->last_error) {
      return; /* propagate error */
    }

    /* compute vectorwidth (A) * column broadcast (B) */
    /* we just use displacements for very small GEMMS to save GPR instructions */
    if ( i_k_blocking == 9 ) {
      for ( l_n = 0; l_n < 9; l_n++) {
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                               io_generated_code->arch,
                                               i_micro_kernel_config->vmul_instruction,
                                               1,
                                               i_gp_reg_mapping->gp_reg_b,
                                               LIBXSMM_X86_GP_REG_UNDEF,
                                               0,
                                               (l_k * i_micro_kernel_config->datatype_size)+(i_xgemm_desc->ldb * i_micro_kernel_config->datatype_size * l_n),
                                               i_micro_kernel_config->vector_name,
                                               l_k%8,
                                               i_micro_kernel_config->vector_reg_count - 9 + l_n );
      }
    } else {
      /* l_n = 0 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_b,
                                           LIBXSMM_X86_GP_REG_UNDEF,
                                           0,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 0 );
      /* l_n = 1 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_b,
                                           i_gp_reg_mapping->gp_reg_help_0,
                                           1,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 1 );
      /* l_n = 2 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_b,
                                           i_gp_reg_mapping->gp_reg_help_0,
                                           2,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 2 );
      /* l_n = 3 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_help_1,
                                           LIBXSMM_X86_GP_REG_UNDEF,
                                           0,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 3 );
      /* l_n = 4 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_b,
                                           i_gp_reg_mapping->gp_reg_help_0,
                                           4,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 4 );
      /* l_n = 5 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_help_1,
                                           i_gp_reg_mapping->gp_reg_help_0,
                                           2,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 5 );
      /* l_n = 6 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_help_2,
                                           LIBXSMM_X86_GP_REG_UNDEF,
                                           0,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 6 );
      /* l_n = 7 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_help_1,
                                           i_gp_reg_mapping->gp_reg_help_0,
                                           4,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 7 );
      /* l_n = 8 */
      libxsmm_x86_instruction_vec_compute_mem( io_generated_code,
                                           io_generated_code->arch,
                                           l_vcompute,
                                           1,
                                           i_gp_reg_mapping->gp_reg_b,
                                           i_gp_reg_mapping->gp_reg_help_0,
                                           8,
                                           l_displacement_k_b*i_micro_kernel_config->datatype_size,
                                           i_micro_kernel_config->vector_name,
                                           l_k%8,
                                           i_micro_kernel_config->vector_reg_count - l_register_offset + 8 );

      l_displacement_k_b++;
    }
  }

  if (l_k_b_updates > 0) {
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_sub_instruction, i_gp_reg_mapping->gp_reg_b,
                                     128*(i_micro_kernel_config->datatype_size)*l_k_b_updates );
  }

  if (l_k_a_update > 0) {
    libxsmm_x86_instruction_alu_imm( io_generated_code, i_micro_kernel_config->alu_sub_instruction, i_gp_reg_mapping->gp_reg_a,
                                     l_k_a_update );
  }

  /* add C buffers */
  if ( i_k_blocking != 9 ) {
    for ( l_n = 0; l_n < l_n_blocking; l_n++ ) {
      libxsmm_x86_instruction_vec_compute_reg( io_generated_code,
                                           io_generated_code->arch,
                                           i_micro_kernel_config->vadd_instruction,
                                           i_micro_kernel_config->vector_name,
                                           i_micro_kernel_config->vector_reg_count - (l_n_blocking*2) + l_n,
                                           i_micro_kernel_config->vector_reg_count - l_n_blocking + l_n,
                                           i_micro_kernel_config->vector_reg_count - l_n_blocking + l_n );
    }
  }

  /* advance pointers of B only when we are not fully unrolling K*/
  if ( i_k_blocking < (unsigned int)i_xgemm_desc->k ) {
    libxsmm_x86_instruction_alu_imm( io_generated_code,
                                 i_micro_kernel_config->alu_add_instruction,
                                 i_gp_reg_mapping->gp_reg_b,
                                 i_k_blocking * i_micro_kernel_config->datatype_size );
  }
}

LIBXSMM_API_INTERN
unsigned int libxsmm_generator_gemm_avx512_kernel_fsdbcst_kloop( libxsmm_generated_code*            io_generated_code,
                                                                 libxsmm_loop_label_tracker*        io_loop_label_tracker,
                                                                 const libxsmm_gp_reg_mapping*      i_gp_reg_mapping,
                                                                 const libxsmm_micro_kernel_config* i_micro_kernel_config,
                                                                 const libxsmm_gemm_descriptor*     i_xgemm_desc,
                                                                 unsigned int                       i_n_blocking ) {
  /* l_k_blocking must be smaller than l_k_threshold */
  /*const*/ unsigned int l_k_blocking = 8;
  /*const*/ unsigned int l_k_threshold = 64;
  unsigned int l_k_unrolled = 0;

  if ( (l_k_blocking >= l_k_threshold) ) {
    LIBXSMM_HANDLE_ERROR( io_generated_code, LIBXSMM_ERR_K_BLOCK );
    return 0;
  }

  /* Let's do something special for SeisSol/EDGE high-order (N == 9 holds true) */
  if ( (i_xgemm_desc->k >= 8) && (i_xgemm_desc->n == 9) && (i_xgemm_desc->k <= 64) && ((i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) == 0) ) {
    if ( ((io_generated_code->arch == LIBXSMM_X86_AVX512_KNM)
            && ((LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) || (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ))) ) {
      libxsmm_generator_gemm_avx512_microkernel_fsdbcst_qfma( io_generated_code,
                                                              i_gp_reg_mapping,
                                                              i_micro_kernel_config,
                                                              i_xgemm_desc,
                                                              i_xgemm_desc->n,
                                                              i_xgemm_desc->k );
    } else {
      if ( (LIBXSMM_GEMM_PRECISION_I16  != LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype )) &&
           (LIBXSMM_GEMM_PRECISION_I8   != LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype )) &&
           (LIBXSMM_GEMM_PRECISION_BF16 != LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ))    ) {
        libxsmm_generator_gemm_avx512_microkernel_fsdbcst_k_large_n_nine( io_generated_code,
                                                                          i_gp_reg_mapping,
                                                                          i_micro_kernel_config,
                                                                          i_xgemm_desc,
                                                                          i_xgemm_desc->k );
      } else {
        libxsmm_generator_gemm_avx512_microkernel_fsdbcst( io_generated_code,
                                                           i_gp_reg_mapping,
                                                           i_micro_kernel_config,
                                                           i_xgemm_desc,
                                                           i_xgemm_desc->n,
                                                           i_xgemm_desc->k );
      }
    }
    l_k_unrolled = 1;
  } else if ( (unsigned int)i_xgemm_desc->k <= l_k_threshold ) {
    if ( ((io_generated_code->arch == LIBXSMM_X86_AVX512_KNM)
            && ((LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) || (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) )))
            && ((i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) == 0) ) {
      libxsmm_generator_gemm_avx512_microkernel_fsdbcst_qfma( io_generated_code,
                                                              i_gp_reg_mapping,
                                                              i_micro_kernel_config,
                                                              i_xgemm_desc,
                                                              i_n_blocking,
                                                              i_xgemm_desc->k );
    } else {
      libxsmm_generator_gemm_avx512_microkernel_fsdbcst( io_generated_code,
                                                         i_gp_reg_mapping,
                                                         i_micro_kernel_config,
                                                         i_xgemm_desc,
                                                         i_n_blocking,
                                                         i_xgemm_desc->k );
    }
    l_k_unrolled = 1;
  } else if ( (i_xgemm_desc->k % l_k_blocking == 0) && (l_k_threshold < (unsigned int)i_xgemm_desc->k) ) {
    libxsmm_generator_gemm_header_kloop( io_generated_code, io_loop_label_tracker, i_gp_reg_mapping, i_micro_kernel_config,
                                          i_micro_kernel_config->vector_length, l_k_blocking);

    if ( ((io_generated_code->arch == LIBXSMM_X86_AVX512_KNM)
           && ((LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) || (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) )))
           && ((i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) == 0) ) {
      libxsmm_generator_gemm_avx512_microkernel_fsdbcst_qfma( io_generated_code,
                                                              i_gp_reg_mapping,
                                                              i_micro_kernel_config,
                                                              i_xgemm_desc,
                                                              i_n_blocking,
                                                              l_k_blocking );
    } else {
      libxsmm_generator_gemm_avx512_microkernel_fsdbcst( io_generated_code,
                                                         i_gp_reg_mapping,
                                                         i_micro_kernel_config,
                                                         i_xgemm_desc,
                                                         i_n_blocking,
                                                         l_k_blocking );
    }

    libxsmm_generator_gemm_footer_kloop( io_generated_code, io_loop_label_tracker, i_gp_reg_mapping, i_micro_kernel_config,
                                          i_xgemm_desc, i_micro_kernel_config->vector_length, i_xgemm_desc->k, 1 );
  } else {
    unsigned int l_max_blocked_k = (i_xgemm_desc->k/l_k_blocking)*l_k_blocking;
    if (l_max_blocked_k > 0 ) {
      libxsmm_generator_gemm_header_kloop( io_generated_code, io_loop_label_tracker, i_gp_reg_mapping, i_micro_kernel_config,
                                            i_micro_kernel_config->vector_length, l_k_blocking);

      if ( ((io_generated_code->arch == LIBXSMM_X86_AVX512_KNM)
             && ((LIBXSMM_GEMM_PRECISION_F32 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) ) || (LIBXSMM_GEMM_PRECISION_I16 == LIBXSMM_GETENUM_INP( i_xgemm_desc->datatype ) )) )
             && ((i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) == 0) ) {
        libxsmm_generator_gemm_avx512_microkernel_fsdbcst_qfma( io_generated_code,
                                                                i_gp_reg_mapping,
                                                                i_micro_kernel_config,
                                                                i_xgemm_desc,
                                                                i_n_blocking,
                                                                l_k_blocking );
      } else {
        libxsmm_generator_gemm_avx512_microkernel_fsdbcst( io_generated_code,
                                                           i_gp_reg_mapping,
                                                           i_micro_kernel_config,
                                                           i_xgemm_desc,
                                                           i_n_blocking,
                                                           l_k_blocking );
      }

      libxsmm_generator_gemm_footer_kloop( io_generated_code, io_loop_label_tracker, i_gp_reg_mapping, i_micro_kernel_config,
                                          i_xgemm_desc, i_micro_kernel_config->vector_length, l_max_blocked_k, 0 );
    }

    /* let's handle the remainder */
    libxsmm_generator_gemm_avx512_microkernel_fsdbcst( io_generated_code,
                                                       i_gp_reg_mapping,
                                                       i_micro_kernel_config,
                                                       i_xgemm_desc,
                                                       i_n_blocking,
                                                       i_xgemm_desc->k-l_max_blocked_k );
    /* reset B manually */
    if (l_max_blocked_k > 0 ) {
      int l_b_offset = 0;
      if ( (i_xgemm_desc->flags & LIBXSMM_GEMM_FLAG_TRANS_B) > 0 ) {
        l_b_offset = i_xgemm_desc->ldb * i_xgemm_desc->k * i_micro_kernel_config->datatype_size;
      } else {
        l_b_offset = i_xgemm_desc->k * i_micro_kernel_config->datatype_size;
      }

      libxsmm_x86_instruction_alu_imm( io_generated_code,
                                       i_micro_kernel_config->alu_sub_instruction,
                                       i_gp_reg_mapping->gp_reg_b,
                                       l_b_offset );
    }
  }

  return l_k_unrolled;
}

