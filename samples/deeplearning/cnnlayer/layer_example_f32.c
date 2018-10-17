/******************************************************************************
** Copyright (c) 2016-2018, Intel Corporation                                **
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
/* Alexander Heinecke, Hans Pabst, Dhiraj Kalamkar,
   Rajkishore Barik (Intel Corp.)
******************************************************************************/
#include <libxsmm.h>
#include <libxsmm_intrinsics_x86.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#if defined(_OPENMP)
# include <omp.h>
#endif

# define USE_OVERWRITE
/*# define USE_BWD_NO_FILTER_TRANSPOSE_OVERWRITE*/
/* Below should fuse only the stats part of bn */
# define USE_FUSED_BATCH_STATS 
/* Below should fuse stats + apply part of bn  */
//# define USE_FUSED_BATCHNORM
/*# define USE_FUSED_RELU_BWD*/

#if !defined(USE_FUSED_BIAS) && 0
# define USE_FUSED_BIAS
#endif
#if !defined(USE_FUSED_RELU) && 0
# define USE_FUSED_RELU
#endif

#if !defined(USE_FUSED) && 0
# define USE_FUSED_BIAS_RELU
#endif

#define CHKERR_LIBXSMM_DNN(A) { const int chkerr_libxsmm_dnn_ = A; if (LIBXSMM_DNN_SUCCESS != chkerr_libxsmm_dnn_) { \
  fprintf(stderr, "%s\n", libxsmm_dnn_get_error(chkerr_libxsmm_dnn_)); global_status = chkerr_libxsmm_dnn_; } \
}

typedef struct {
  int nImg;
  int nIfm;
  int nOfm;
  int ifhp;
  int ifwp;
  int ifh;
  int ifw;
  int ofhp;
  int ofwp;
  int ofh;
  int ofw;
  int pad_h;
  int pad_w;
  int pad_h_in;
  int pad_w_in;
  int pad_h_out;
  int pad_w_out;
  int kh;
  int kw;
  int stride_h;
  int stride_w;
} naive_conv_t;

typedef struct {
  int N;
  int C;
  int H;
  int W;
  int stride_h;
  int stride_w;
  int norm_type;  /* 0: full batchnorm, 1: batch scaling only */
  int fuse_type;  /* 0: nothing fused, 1: relu fused, 2: elementwise fused, 3: relu and elementwise fused */
} naive_fusedbatchnorm_t;

LIBXSMM_INLINE void zero_buf(float* buf, size_t size) {
  int i;
#if defined(_OPENMP)
# pragma omp parallel for private(i)
#endif
  for (i = 0; i < (int)size; ++i) {
    buf[i] = 0.0f;
  }
}

LIBXSMM_INLINE void copy_buf(float* src, float* dst, size_t size) {
  int i;
#if defined(_OPENMP)
# pragma omp parallel for private(i)
#endif
  for (i = 0; i < (int)size; ++i) {
    dst[i] = src[i];
  }
}

LIBXSMM_INLINE void init_buf(float* buf, size_t size, int initPos, int initOne)
{
  int i;
  zero_buf(buf, size);
  for (i = 0; i < (int)size; ++i) {
    buf[i] = (float)((initOne != 0) ? 1.0 : ((initPos != 0) ? libxsmm_rand_f64() : (0.05 - libxsmm_rand_f64()/10.0)));
  }
}

LIBXSMM_INLINE void set_zeropad_nchw(float* nchw, int N, int C, int H, int W, int pad_h, int pad_w)
{
  LIBXSMM_VLA_DECL(4, float, input, nchw, C, H, W);
  int n, h, w, c;

  for ( n = 0; n < N; n++ ) {
    for ( c = 0; c < C; c++ ) {
      for ( h = 0; h < H; h++ ) {
        for ( w = 0; w < W; w++ ) {
          if (h < pad_h || h >= H-pad_h || w < pad_w || w >= W-pad_w)
            LIBXSMM_VLA_ACCESS(4,  input, n, c, h, w, C, H, W) = 0.0;
        }
      }
    }
  }
}

LIBXSMM_INLINE void copy_internal_nchw(float* dst , float* src, int N, int C, int H, int W, int pad_h, int pad_w)
{
  LIBXSMM_VLA_DECL(4, float, input, src, C, H, W);
  LIBXSMM_VLA_DECL(4, float, new_input, dst, C, H+2*pad_h, W+2*pad_w);
  int n, h, w, c;

  for ( n = 0; n < N; n++ ) {
    for ( c = 0; c < C; c++ ) {
      for ( h = 0; h < H; h++ ) {
        for ( w = 0; w < W; w++ ) {
          LIBXSMM_VLA_ACCESS(4, new_input, n, c, h+pad_h, w+pad_w, C, H+2*pad_h, W+2*pad_w) =  LIBXSMM_VLA_ACCESS(4,  input, n, c, h, w, C, H, W);
        }
      }
    }
  }
}



LIBXSMM_INLINE void naive_copy_KCRS_to_RSCK(const float* kcrs, float* rsck, int R, int S, int C, int K)
{
  LIBXSMM_VLA_DECL(4,       float, output, rsck, S, C, K);
  LIBXSMM_VLA_DECL(4, const float,  input, kcrs, C, R, S);
  int r, s, c, k;

  for ( r = 0; r < R; r++ ) {
    for ( s = 0; s < S; s++ ) {
      for ( c = 0; c < C; c++ ) {
        for ( k = 0; k < K; k++ ) {
          LIBXSMM_VLA_ACCESS(4, output, r, s, c, k, S, C, K) =
          LIBXSMM_VLA_ACCESS(4,  input, k, c, r, s, C, R, S);
        }
      }
    }
  }
}


LIBXSMM_INLINE void naive_copy_RSCK_to_KCRS(const float* rsck, float* kcrs, int R, int S, int C, int K)
{
  LIBXSMM_VLA_DECL(4, const float,  input, rsck, S, C, K);
  LIBXSMM_VLA_DECL(4,       float, output, kcrs, C, R, S);
  int r, s, c, k;

  for ( r = 0; r < R; r++ ) {
    for ( s = 0; s < S; s++ ) {
      for ( c = 0; c < C; c++ ) {
        for ( k = 0; k < K; k++ ) {
          LIBXSMM_VLA_ACCESS(4, output, k, c, r, s, C, R, S) =
            LIBXSMM_VLA_ACCESS(4,  input, r, s, c, k, S, C, K);
        }
      }
    }
  }
}

LIBXSMM_INLINE void naive_fusedbatchnorm_fp(naive_fusedbatchnorm_t* param, const float* input_ptr, float* output_ptr, const float* input_add_ptr,
                                     const float* beta_ptr, const float* gamma_ptr, float* expectval_ptr, float* rcpstddev_ptr, float* variance_ptr)
{
  const int nImg = param->N;
  const int nFm = param->C;
  const int ifh = param->H;
  const int ifw = param->W;
  const int sh = param->stride_h;
  const int sw = param->stride_w;
  const int ofh = ifh/sh;
  const int ofw = ifw/sw;
  const float nhw = (float)(nImg * ifh * ifw);
  const float recp_nhw = 1.0f/nhw;
  const float sqrt_eps = 1e-7f;

  int img, fm, hi, wi, ho, wo;

  LIBXSMM_VLA_DECL(4, const float, input,     input_ptr,     nFm, ifh, ifw);
  LIBXSMM_VLA_DECL(4, const float, input_add, input_add_ptr, nFm, ifh, ifw);
  LIBXSMM_VLA_DECL(4,       float, output,    output_ptr,    nFm, ofh, ofw);

  if ( param->norm_type == 0 ) {
#if defined(_OPENMP)
#pragma omp parallel for private(img, fm, hi, wi)
#endif
    for (fm = 0; fm < nFm; fm++) {
      float ch_sum = 0.0f;
      float ch_sumsq = 0.0f;
      float tbmean = 0.0f;
      float tbmeansq = 0.0f;
      float tsqbmean = 0.0f;
      float tbrstd = 0.0f;
      float tvariance = 0.0f;

      for ( img = 0; img < nImg; img++ ) {
        for ( hi = 0; hi < ifh; hi++ ) {
          for ( wi = 0; wi < ifw; wi++ ) {
            const float input_val = LIBXSMM_VLA_ACCESS(4, input, img, fm, hi, wi, nFm, ifh, ifw);
            ch_sum   += input_val;
            ch_sumsq += (input_val * input_val);
          }
        }
      }

      tbmean = (recp_nhw * ch_sum) ;
      tbmeansq  = tbmean * tbmean;
      tsqbmean = recp_nhw * ch_sumsq;
      tvariance = tsqbmean - tbmeansq;
      tbrstd = (float)(1.0/sqrt(tvariance + sqrt_eps));
      expectval_ptr[fm] = tbmean;
      rcpstddev_ptr[fm] = tbrstd;
      variance_ptr[fm] = tvariance;
    }
  }

#if 0
#if defined(_OPENMP)
#pragma omp parallel for private(img, fm, hi, wi, ho, wo)
#endif
  for ( img = 0; img < nImg; img++ ) {
    for ( fm = 0; fm < nFm; fm++ ) {
      for ( hi = 0, ho = 0; hi < ifh; hi += sh, ho++ ) {
        for ( wi = 0, wo = 0; wi < ifw; wi += sw, wo++ ) {
          const float  input_val     =  LIBXSMM_VLA_ACCESS(4, input,     img, fm, hi, wi, nFm, ifh, ifw);
          const float  input_add_val =  LIBXSMM_VLA_ACCESS(4, input_add, img, fm, hi, wi, nFm, ifh, ifw);
                float* output_ptr2   = &LIBXSMM_VLA_ACCESS(4, output,    img, fm, ho, wo, nFm, ofh, ofw);

          /* BN + scale (gamma, beta) */
          float o = gamma_ptr[fm]*(input_val - expectval_ptr[fm])*rcpstddev_ptr[fm] + beta_ptr[fm];
          /* Eltwise */
          if ( (param->fuse_type == 2) || (param->fuse_type == 3) ) {
            o += input_add_val;
          }
          /* ReLU */
          if ( (param->fuse_type == 1) || (param->fuse_type == 3) ) {
            o = ( o < 0.0f ) ? 0.0f : o;
          }
          *output_ptr2 = o;
        }
      }
    }
  }
#endif
}


LIBXSMM_INLINE void naive_fusedbatchnorm_bp(naive_fusedbatchnorm_t* param, const float* input_ptr, float* doutput_ptr, float* dinput_add_ptr, float* del_beta_ptr,  float* del_gamma_ptr, const float* expectval_ptr, const float* rcpstddev_ptr)
{
  const int nImg = param->N;
  const int nFm = param->C;
  const int ifh = param->H;
  const int ifw = param->W;
  const int sh = param->stride_h;
  const int sw = param->stride_w;
  const int ofh = ifh/sh;
  const int ofw = ifw/sw;
  int img, fm, hi, wi, ho, wo;
  int iph ;
  int ipw ;
  int oph ;
  int opw ;
  int ifhp = ifh + 2*iph;
  int ifwp = ifw + 2*ipw;
  int ofhp = ofh + 2*oph;
  int ofwp = ofw + 2*opw;

  LIBXSMM_VLA_DECL(4, const float, input,      input_ptr,      nFm, ifhp, ifwp);
  LIBXSMM_VLA_DECL(4,       float, dinput_add, dinput_add_ptr, nFm, ifhp, ifwp);
  LIBXSMM_VLA_DECL(4,       float, doutput,    doutput_ptr,    nFm, ofhp, ofwp);

#if defined(_OPENMP)
#pragma omp parallel for private(img, fm, hi, wi, ho, wo)
#endif
  for ( fm = 0; fm < nFm; fm++ ) {
    del_gamma_ptr[fm] = 0.0f;
    del_beta_ptr[fm] = 0.0f;

    for ( img = 0; img < nImg; img++ ) {
      for ( hi = iph , ho = oph; hi < (ifh+iph); hi += sh, ho++ ) {
        for ( wi = ipw, wo = opw; wi < (ifw+ipw); wi += sw, wo++ ) {
          float* del_input_add_ptr = &LIBXSMM_VLA_ACCESS(4, dinput_add, img, fm, hi, wi, fm, ifhp, ifwp);
          const float  input_val   =  LIBXSMM_VLA_ACCESS(4,      input, img, fm, hi, wi, fm, ifhp, ifwp);
          float* del_output_ptr    = &LIBXSMM_VLA_ACCESS(4,    doutput, img, fm, ho, wo, fm, ofhp, ofwp);
          /* The ReLU is done in the convolution reference...  */
          /* elementwise */
          *del_input_add_ptr = *del_output_ptr;
          del_gamma_ptr[fm] += (input_val - expectval_ptr[fm]) * (*del_output_ptr) * rcpstddev_ptr[fm];
          del_beta_ptr[fm]  += *del_output_ptr;
        }
      }
    }
  }
}



LIBXSMM_INLINE void naive_conv_fp(naive_conv_t* param, const float* input, float* output, const float* filter, const float* bias)
{
  int nImg      = param->nImg;
  int nIfm      = param->nIfm;
  int nOfm      = param->nOfm;
  int ifhp      = param->ifhp;
  int ifwp      = param->ifwp;
  int ofhp      = param->ofhp;
  int ofwp      = param->ofwp;
  int ifh       = param->ifh;
  int ifw       = param->ifw;
  int ofh       = param->ofh;
  int ofw       = param->ofw;
  int pad_h     = param->pad_h;
  int pad_w     = param->pad_w;
  int pad_h_in  = param->pad_h_in;
  int pad_w_in  = param->pad_w_in;
  int pad_h_out = param->pad_h_out;
  int pad_w_out = param->pad_w_out;
  int kh        = param->kh;
  int kw        = param->kw;
  int stride_h  = param->stride_h;
  int stride_w  = param->stride_w;
  /* loop counters */
  int img, ofm, ifm, oj, oi, ij, ii, kj, ki;

  LIBXSMM_VLA_DECL(4,       float, output_t, output , nOfm, ofh, ofw);
  LIBXSMM_VLA_DECL(4, const float,  input_t, input , nIfm, ifh, ifw);
  LIBXSMM_VLA_DECL(4, const float, filter_t, filter, nIfm, kh, kw);

#if defined(USE_FUSED_BIAS) || defined(USE_FUSED_BIAS_RELU)
#if defined(_OPENMP)
# pragma omp parallel for LIBXSMM_OPENMP_COLLAPSE(2) private(img, ofm, ifm, oj, oi, ij, ii, kj, ki)
#endif
  for (img = 0; img < nImg; ++img) {
    for (ofm = 0; ofm < nOfm; ++ofm) {
      for (oj = 0; oj < ofh; ++oj) {
        for (oi = 0; oi < ofw; ++oi) {
          LIBXSMM_VLA_ACCESS(  4, output_t, img, ofm, oj, oi, nOfm, ofh, ofw) = bias[ofm];
        }
      }
    }
  }
#else
  LIBXSMM_UNUSED(bias);
#endif

#if defined(_OPENMP)
# pragma omp parallel for LIBXSMM_OPENMP_COLLAPSE(2) private(img, ofm, ifm, oj, oi, ij, ii, kj, ki)
#endif
  for (img = 0; img < nImg; ++img) {
    for (ofm = 0; ofm < nOfm; ++ofm) {
      for (ifm = 0; ifm < nIfm; ++ifm) {
        for (oj = 0; oj < ofh; ++oj) {
          ij = oj * stride_h - pad_h;
          for (oi = 0; oi < ofw; ++oi) {
            ii = oi * stride_w - pad_w;
            for (kj = 0; kj < kh; ++kj) {
              if (ij+kj < 0 || ij+kj >= ifh) continue;
              for (ki = 0; ki < kw; ++ki) {
                if (ii+ki < 0 || ii+ki >= ifw) continue;
                LIBXSMM_VLA_ACCESS(  4, output_t, img, ofm, oj, oi, nOfm, ofh, ofw) +=
                  LIBXSMM_VLA_ACCESS(4,  input_t, img, ifm, ij + kj, ii + ki, nIfm, ifh, ifw)
                  * LIBXSMM_VLA_ACCESS(4, filter_t, ofm, ifm, kj, ki, nIfm, kh, kw);
              }
            }
          }
        }
      }
#if defined(USE_FUSED_RELU) || defined(USE_FUSED_BIAS_RELU)
      for (oj = 0; oj < ofh; ++oj) {
        for (oi = 0; oi < ofw; ++oi) {
          LIBXSMM_VLA_ACCESS(  4, output_t, img, ofm, oj, oi, nOfm, ofh, ofw) =
            (LIBXSMM_VLA_ACCESS(  4, output_t, img, ofm, oj, oi, nOfm, ofh, ofw) < 0.0f) ? 0.0f : LIBXSMM_VLA_ACCESS(  4, output_t, img, ofm, oj, oi, nOfm, ofh, ofw);
        }
      }
#endif
    }
  }
}

LIBXSMM_INLINE void naive_conv_bp(naive_conv_t* param, float* input, const float* output, const float* filter, const float* naive_input_save)
{
  int nImg      = param->nImg;
  int nIfm      = param->nIfm;
  int nOfm      = param->nOfm;
  int ifhp      = param->ifhp;
  int ifwp      = param->ifwp;
  int ofhp      = param->ofhp;
  int ofwp      = param->ofwp;
  int ifh       = param->ifh;
  int ifw       = param->ifw;
  int ofh       = param->ofh;
  int ofw       = param->ofw;
  int pad_h     = param->pad_h;
  int pad_w     = param->pad_w;
  int pad_h_in  = param->pad_h_in;
  int pad_w_in  = param->pad_w_in;
  int pad_h_out = param->pad_h_out;
  int pad_w_out = param->pad_w_out;
  int kh        = param->kh;
  int kw        = param->kw;
  int stride_h  = param->stride_h;
  int stride_w  = param->stride_w;
  /* loop counters */
  int img, ofm, ifm, oj, oi, ij, ii, kj, ki;

  LIBXSMM_VLA_DECL(4, const float, output_t, output, nOfm, ofh, ofw);
  LIBXSMM_VLA_DECL(4,       float,  input_t,  input, nIfm, ifh, ifw);
  LIBXSMM_VLA_DECL(4, const float, filter_t, filter, nIfm, kh, kw);
#if defined(USE_FUSED_RELU_BWD)
  LIBXSMM_VLA_DECL(4, const float, naive_input_t, naive_input_save, nIfm, ifh, ifw);
#else
  LIBXSMM_UNUSED(naive_input_save);
#endif

#if defined(_OPENMP)
# pragma omp parallel for LIBXSMM_OPENMP_COLLAPSE(2) private(img, ofm, ifm, oj, oi, ij, ii, kj, ki)
#endif
  for (img = 0; img < nImg; ++img) {
    for (ifm = 0; ifm < nIfm; ++ifm) {
      for (ofm = 0; ofm < nOfm; ++ofm) {
        for (oj = 0; oj < ofh; ++oj) {
          ij = oj * stride_h - pad_h;
          for (oi = 0; oi < ofw; ++oi) {
            ii = oi * stride_w - pad_w;
            for (kj = 0; kj < kh; ++kj) {
              if (ij+kj < 0 || ij+kj >= ifh) continue;
              for (ki = 0; ki < kw; ++ki) {
                if (ii+ki < 0 || ii+ki >= ifw) continue;
                LIBXSMM_VLA_ACCESS(4,  input_t, img, ifm, ij + kj, ii + ki, nIfm, ifh, ifw) +=
                  LIBXSMM_VLA_ACCESS(4, output_t, img, ofm, oj, oi, nOfm, ofh, ofw)
                  * LIBXSMM_VLA_ACCESS(4, filter_t, ofm, ifm, kj, ki, nIfm, kh, kw);
              }
            }
          }
        }
      }
#if defined(USE_FUSED_RELU_BWD)
      for (ij = 0; ij < ifh; ij++) {
        for (ii = 0; ii < ifw; ii++) {
          if ( LIBXSMM_VLA_ACCESS(4,  naive_input_t, img, ifm, ij, ii , nIfm, ifh, ifw) == 0.0 ) {
            LIBXSMM_VLA_ACCESS(4, input_t, img, ifm, ij, ii , nIfm, ifh, ifw) = 0.0;
          }
        }
      }
#endif
    }
  }
}

LIBXSMM_INLINE void naive_conv_wu(naive_conv_t* param, const float* input, const float* output, float* filter)
{
  int nImg      = param->nImg;
  int nIfm      = param->nIfm;
  int nOfm      = param->nOfm;
  int ifhp      = param->ifhp;
  int ifwp      = param->ifwp;
  int ofhp      = param->ofhp;
  int ofwp      = param->ofwp;
  int ifh       = param->ifh;
  int ifw       = param->ifw;
  int ofh       = param->ofh;
  int ofw       = param->ofw;
  int pad_h     = param->pad_h;
  int pad_w     = param->pad_w;
  int pad_h_in  = param->pad_h_in;
  int pad_w_in  = param->pad_w_in;
  int pad_h_out = param->pad_h_out;
  int pad_w_out = param->pad_w_out;
  int kh        = param->kh;
  int kw        = param->kw;
  int stride_h  = param->stride_h;
  int stride_w  = param->stride_w;
  /* loop counters */
  int img, ofm, ifm, oj, oi, ij, ii, kj, ki;

  LIBXSMM_VLA_DECL(4, const float, output_t, output, nOfm, ofh, ofw);
  LIBXSMM_VLA_DECL(4, const float,  input_t,  input, nIfm, ifh, ifw);
  LIBXSMM_VLA_DECL(4,       float, filter_t, filter, nIfm, kh, kw);

#if defined(_OPENMP)
# pragma omp parallel for LIBXSMM_OPENMP_COLLAPSE(2) private(img, ofm, ifm, oj, oi, ij, ii, kj, ki)
#endif
  for (ofm = 0; ofm < nOfm; ++ofm) {
    for (ifm = 0; ifm < nIfm; ++ifm) {
      for (img = 0; img < nImg; ++img) {
        for (oj = 0; oj < ofh; ++oj) {
          ij = oj * stride_h - pad_h;
          for (oi = 0; oi < ofw; ++oi) {
            ii = oi * stride_w - pad_w;
            for (kj = 0; kj < kh; ++kj) {
              if (ij+kj < 0 || ij+kj >= ifh) continue;
              for (ki = 0; ki < kw; ++ki) {
                if (ii+ki < 0 || ii+ki >= ifw) continue;
                LIBXSMM_VLA_ACCESS(4, filter_t, ofm, ifm, kj, ki, nIfm, kh, kw) +=
                  LIBXSMM_VLA_ACCESS(4,  input_t, img, ifm, ij + kj, ii + ki, nIfm, ifh, ifw)
                  * LIBXSMM_VLA_ACCESS(4, output_t, img, ofm, oj, oi, nOfm, ofh, ofw);
              }
            }
          }
        }
      }
    }
  }
}

int main(int argc, char* argv[])
{
  float *naive_input, *naive_output, *naive_filter, *naive_input_padded, *naive_doutput_padded, *naive_dinput_test, *naive_output_test;
  float *naive_dinput, *naive_doutput, *naive_dfilter;
  float *naive_libxsmm_dinput, *naive_libxsmm_output, *naive_libxsmm_dfilter;
  float *naive_bias, *bias_libxsmm, *naive_dbias, *dbias_libxsmm, *naive_libxsmm_del_input_add, *naive_bn_input, *naive_dbeta, *naive_dgamma, *naive_bmean, *naive_rcpstddev, *naive_del_input_add;
  float *input_libxsmm, *filter_libxsmm, *output_libxsmm, *dinput_libxsmm, *dfilter_libxsmm, *doutput_libxsmm, *filtertr_libxsmm;
#if (defined(USE_FUSED_BATCH_STATS) || defined(USE_FUSED_BATCHNORM))
  float *expectval_libxsmm, *rcpstddev_libxsmm, *variance_libxsmm;
  libxsmm_dnn_fusedbatchnorm_desc fusedbatchnorm_desc_post;
  libxsmm_dnn_fusedbatchnorm *libxsmm_bn_handle_post;
  float *dbeta_libxsmm, *dgamma_libxsmm, *bmean_libxsmm, *brcpstddev_libxsmm, *del_input_add_libxsmm, *bn_input_libxsmm;
  libxsmm_dnn_fusedbatchnorm_desc  fusedbatchnorm_desc_pre;
  libxsmm_dnn_fusedbatchnorm *libxsmm_bn_handle_pre;
#endif
 
  int ifhp, ifwp, ofhp, ofwp, ofh, ofw;
  int stride_h, stride_w, pad_h, pad_w, pad_h_in, pad_w_in, pad_h_out, pad_w_out;
  int pad_bn = 0, stride_bn = 1;
  naive_conv_t naive_param;
  void* scratch;
  size_t scratch_size = 0;

  /* some parameters we can overwrite via cli,
     default is some inner layer of overfeat */
  int iters = 10;         /* repetitions of benchmark */
  int ifw = 14;           /* input width, "W" */
  int ifh = 20;           /* input height, "H" */
  int nImg = 32;          /* mini-batch size, "N" */
  int nIfm = 256;         /* number of input feature maps, "C" */
  int nOfm = 512;         /* number of output feature maps, "K" */
  int kh = 3;             /* filter height, "R" */
  int kw = 3;             /* filter width, "S" */
  int padh = 0;           /* padding in input, height */
  int padw = 0;           /* padding in input, width */
  int stride = 1;         /* stride when accessing inputs */
  int padding_mode = 0;   /* padding mode */
  char type = 'A';        /* 'A': ALL, 'F': FP, 'B': BP, 'U', WU */
  char format = 'A';      /* 'A': ALL, 'L': LIBXSMM, 'T': Tensorflow, 'M', Mixed */

  const char *const env_check = getenv("CHECK"), *const env_winograd = getenv("WINOGRAD");
  const double check = LIBXSMM_ABS(0 == env_check ? 1 : atof(env_check));
  const int algo_winograd = (0 == env_winograd ? 0 : atoi(env_winograd));

#if defined(_OPENMP)
  int nThreads = omp_get_max_threads(); /* number of threads */
#else
  int nThreads = 1; /* number of threads */
#endif

  unsigned long long l_start, l_end;
  double l_total = 0.0;
  double flops = 0.0;
  int i;

  libxsmm_dnn_conv_desc conv_desc;
  libxsmm_dnn_layer* libxsmm_handle;
  libxsmm_dnn_tensor* libxsmm_input;
  libxsmm_dnn_tensor* libxsmm_output;
  libxsmm_dnn_tensor* libxsmm_filter;
  libxsmm_dnn_tensor* libxsmm_dinput;
  libxsmm_dnn_tensor* libxsmm_doutput;
  libxsmm_dnn_tensor* libxsmm_dfilter;
  libxsmm_dnn_tensor* libxsmm_filter_tr;
  libxsmm_dnn_tensor* libxsmm_bias;
  libxsmm_dnn_tensor* libxsmm_dbias;
#if (defined(USE_FUSED_BATCH_STATS) || defined(USE_FUSED_BATCHNORM))
  libxsmm_dnn_tensor*  libxsmm_expectval;
  libxsmm_dnn_tensor*  libxsmm_rcpstddev;
  libxsmm_dnn_tensor*  libxsmm_variance;
  libxsmm_dnn_tensor*  libxsmm_dbeta;
  libxsmm_dnn_tensor*  libxsmm_dgamma;
  libxsmm_dnn_tensor*  libxsmm_bmean;
  libxsmm_dnn_tensor*  libxsmm_brcpstddev;
  libxsmm_dnn_tensor*  libxsmm_del_input_add;
  libxsmm_dnn_tensor*  libxsmm_bn_input;
#endif
  libxsmm_dnn_tensor_datalayout* libxsmm_layout;
  libxsmm_dnn_err_t status;
  libxsmm_dnn_err_t global_status = LIBXSMM_DNN_SUCCESS;

  libxsmm_matdiff_info norms_fwd, norms_bwd, norms_upd, diff, norms_batchstats;
  memset(&norms_fwd, 0, sizeof(norms_fwd));
  memset(&norms_bwd, 0, sizeof(norms_bwd));
  memset(&norms_upd, 0, sizeof(norms_upd));
  memset(&norms_batchstats, 0, sizeof(norms_batchstats));
  memset(&diff, 0, sizeof(diff));

  if (argc > 1 && !strncmp(argv[1], "-h", 3)) {
    printf("Usage: %s iters inpWidth inpHeight nImg nIfm nOfm kw kh pad stride type format padding_mode\n", argv[0]);
    return 0;
  }
  libxsmm_srand(1);

  /* reading new values from cli */
  i = 1;
  if (argc > i) iters      = atoi(argv[i++]);
  if (argc > i) ifw        = atoi(argv[i++]);
  if (argc > i) ifh        = atoi(argv[i++]);
  if (argc > i) nImg       = atoi(argv[i++]);
  if (argc > i) nIfm       = atoi(argv[i++]);
  if (argc > i) nOfm       = atoi(argv[i++]);
  if (argc > i) kw         = atoi(argv[i++]);
  if (argc > i) kh         = atoi(argv[i++]);
  if (argc > i) padw       = atoi(argv[i++]);
  if (argc > i) padh       = atoi(argv[i++]);
  if (argc > i) stride     = atoi(argv[i++]);
  if (argc > i) type       = *(argv[i++]);
  if (argc > i) format     = *(argv[i++]);
  if (argc > i) padding_mode = atoi(argv[i++]);
  if (argc > i) pad_bn     = atoi(argv[i++]);
  if (argc > i) stride_bn  = atoi(argv[i++]);

  if (type != 'A' && type != 'F' && type != 'B' && type != 'U') {
    printf("type needs to be 'A' (All), 'F' (FP only), 'B' (BP only), 'U' (WU only)\n");
    return 0;
  }

  stride_w = stride;
  stride_h = stride;
  pad_w = padw;
  pad_h = padh;

  if (0 == padding_mode) {
    pad_h_in = 0;
    pad_w_in = 0;
    pad_h_out = 0;
    pad_w_out = 0;
  }
  else {
    /* TODO: change "1" to "0" if "padding_mode = -1" is acknowledged */
    if (1 < padding_mode) pad_w = padding_mode;
    pad_h_in = pad_h;
    pad_w_in = pad_w;
    pad_h_out = pad_h;
    pad_w_out = pad_w;
  }

  /* deriving some values for naive code */
  ofh = (ifh + 2 * pad_h - kh) / stride_h + 1;
  ofw = (ifw + 2 * pad_w - kw) / stride_w + 1;
  ifhp = ifh + 2 * pad_h_in;
  ifwp = ifw + 2 * pad_w_in;
  ofhp = ofh + 2 * pad_h_out;
  ofwp = ofw + 2 * pad_w_out;

  /* set struct for naive convolution */
  naive_param.nImg = nImg;
  naive_param.nIfm = nIfm;
  naive_param.nOfm = nOfm;
  naive_param.ifhp = ifhp;
  naive_param.ifwp = ifwp;
  naive_param.ofhp = ofhp;
  naive_param.ofwp = ofwp;
  naive_param.ifh = ifh;
  naive_param.ifw = ifw;
  naive_param.ofh = ofh;
  naive_param.ofw = ofw;
  naive_param.pad_h = pad_h;
  naive_param.pad_w = pad_w;
  naive_param.pad_h_in = pad_h_in;
  naive_param.pad_w_in = pad_w_in;
  naive_param.pad_h_out = pad_h_out;
  naive_param.pad_w_out = pad_w_out;
  naive_param.kh = kh;
  naive_param.kw = kw;
  naive_param.stride_h = stride_h;
  naive_param.stride_w = stride_w;

#if defined(__SSE3__)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  _MM_SET_ROUNDING_MODE(_MM_ROUND_NEAREST);
#endif

  /* print some summary */
  printf("##########################################\n");
  printf("#          Setting Up (Common)           #\n");
  printf("##########################################\n");
  printf("PARAMS: W:%d  H:%d  N:%d  C:%d  K:%d  R:%d  S:%d  P:%d  Q:%d  STRIDE:%d\n", ifw, ifh, nImg, nIfm, nOfm, kw, kh, ofh, ofw, stride);
  printf("PARAMS: ITERS:%d", iters); if (LIBXSMM_FEQ(0, check)) printf("  Threads:%d\n", nThreads); else printf("\n");
  printf(" InImg %dx%d Padded (%dx%d)\n", ifh, ifw, ifhp, ifwp);
  printf("OutImg %dx%d Padded (%dx%d)\n", ofh, ofw, ofhp, ofwp);
  printf("SIZE Input  (MB): %10.2f MiB\n", (double)(nImg*nIfm*ifhp*ifwp*sizeof(float))/(1024.0*1024.0) );
  printf("SIZE Output (MB): %10.2f MiB\n", (double)(nImg*nOfm*ofhp*ofwp*sizeof(float))/(1024.0*1024.0) );
  printf("SIZE Input   (1): %10.2f MiB\n", (double)(1*nIfm*ifhp*ifwp*   sizeof(float))/(1024.0*1024.0) );
  printf("SIZE Output  (1): %10.2f MiB\n", (double)(1*nOfm*ofhp*ofwp*   sizeof(float))/(1024.0*1024.0) );
  printf("SIZE Weight     : %10.2f MiB\n", (double)(nIfm*nOfm*kw*kh*    sizeof(float))/(1024.0*1024.0) );
#if defined(USE_OVERWRITE)
  printf("Using Overwrite Option\n");
#endif

  /* allocate data */
  naive_input           = (float*)libxsmm_aligned_malloc( nImg*nIfm*ifhp*ifwp*sizeof(float), 2097152);
  naive_dinput          = (float*)libxsmm_aligned_malloc( nImg*nIfm*ifhp*ifwp*sizeof(float), 2097152);
  naive_output          = (float*)libxsmm_aligned_malloc( nImg*nOfm*ofhp*ofwp*sizeof(float), 2097152);
  naive_doutput         = (float*)libxsmm_aligned_malloc( nImg*nOfm*ofhp*ofwp*sizeof(float), 2097152);
  naive_libxsmm_output  = (float*)libxsmm_aligned_malloc( nImg*nOfm*ofhp*ofwp*sizeof(float), 2097152);
  naive_libxsmm_dinput  = (float*)libxsmm_aligned_malloc( nImg*nIfm*ifhp*ifwp*sizeof(float), 2097152);
  naive_libxsmm_dfilter = (float*)libxsmm_aligned_malloc( nOfm*nIfm*kh*kw*    sizeof(float), 2097152);
  naive_filter          = (float*)libxsmm_aligned_malloc( nOfm*nIfm*kh*kw*    sizeof(float), 2097152);
  naive_dfilter         = (float*)libxsmm_aligned_malloc( nOfm*nIfm*kh*kw*    sizeof(float), 2097152);
  input_libxsmm         = (float*)libxsmm_aligned_malloc( nImg*nIfm*ifhp*ifwp*sizeof(float), 2097152);
  filter_libxsmm        = (float*)libxsmm_aligned_malloc( nOfm*nIfm*kh*kw*    sizeof(float), 2097152);
  output_libxsmm        = (float*)libxsmm_aligned_malloc( nImg*nOfm*ofhp*ofwp*sizeof(float), 2097152);
  dinput_libxsmm        = (float*)libxsmm_aligned_malloc( nImg*nIfm*ifhp*ifwp*sizeof(float), 2097152);
  dfilter_libxsmm       = (float*)libxsmm_aligned_malloc( nOfm*nIfm*kh*kw*    sizeof(float), 2097152);
  doutput_libxsmm       = (float*)libxsmm_aligned_malloc( nImg*nOfm*ofhp*ofwp*sizeof(float), 2097152);
  filtertr_libxsmm      = (float*)libxsmm_aligned_malloc( nOfm*nIfm*kh*kw*    sizeof(float), 2097152);
#if (defined(USE_FUSED_BATCH_STATS) ||  defined(USE_FUSED_BATCHNORM))
  expectval_libxsmm     = (float*)libxsmm_aligned_malloc( nOfm*               sizeof(float), 2097152);
  rcpstddev_libxsmm     = (float*)libxsmm_aligned_malloc( nOfm*               sizeof(float), 2097152);
  variance_libxsmm      = (float*)libxsmm_aligned_malloc( nOfm*               sizeof(float), 2097152);
  naive_bn_input        = (float*)libxsmm_aligned_malloc( nImg*nIfm*(stride_bn*ifhp)*(stride_bn*ifwp)*sizeof(float), 2097152);
  naive_del_input_add   = (float*)libxsmm_aligned_malloc( nImg*nIfm*(stride_bn*ifhp)*(stride_bn*ifwp)*sizeof(float), 2097152);
  naive_dbeta           = (float*)libxsmm_aligned_malloc( nIfm*               sizeof(float), 2097152);
  naive_dgamma          = (float*)libxsmm_aligned_malloc( nIfm*               sizeof(float), 2097152);
  naive_bmean           = (float*)libxsmm_aligned_malloc( nIfm*               sizeof(float), 2097152);
  naive_rcpstddev       = (float*)libxsmm_aligned_malloc( nIfm*               sizeof(float), 2097152);
  dbeta_libxsmm         = (float*)libxsmm_aligned_malloc( nIfm*               sizeof(float), 2097152);
  dgamma_libxsmm        = (float*)libxsmm_aligned_malloc( nIfm*               sizeof(float), 2097152);
  del_input_add_libxsmm = (float*)libxsmm_aligned_malloc( nImg*nIfm*(stride_bn*ifhp)*(stride_bn*ifwp)*sizeof(float), 2097152);
  naive_libxsmm_del_input_add = (float*)libxsmm_aligned_malloc( nImg*nIfm*(stride_bn*ifhp)*(stride_bn*ifwp)*sizeof(float), 2097152);
  bn_input_libxsmm      = (float*)libxsmm_aligned_malloc( nImg*nIfm*(stride_bn*ifhp)*(stride_bn*ifwp)*sizeof(float), 2097152);
  brcpstddev_libxsmm    = (float*)libxsmm_aligned_malloc( nIfm*               sizeof(float), 2097152);
  bmean_libxsmm         = (float*)libxsmm_aligned_malloc( nIfm*               sizeof(float), 2097152);
#endif
  naive_bias            = (float*)libxsmm_aligned_malloc( nOfm*               sizeof(float), 2097152);
  naive_dbias           = (float*)libxsmm_aligned_malloc( nOfm*               sizeof(float), 2097152);
  bias_libxsmm          = (float*)libxsmm_aligned_malloc( nOfm*               sizeof(float), 2097152);
  dbias_libxsmm         = (float*)libxsmm_aligned_malloc( nOfm*               sizeof(float), 2097152);

  /* initialize data */
  if (padding_mode == 0 ) {
    init_buf(naive_input,          nImg*nIfm*ifhp*ifwp, 0, 0);
    naive_input_padded = naive_input; 
  } else {
    naive_input_padded = (float*)libxsmm_aligned_scratch( nImg*nIfm*ifhp*ifwp*sizeof(float), 2097152);
    naive_dinput_test  = (float*)libxsmm_aligned_scratch( nImg*nIfm*ifhp*ifwp*sizeof(float), 2097152);
    zero_buf(naive_dinput_test, nImg*nIfm*ifhp*ifwp);
    init_buf(naive_input, nImg*nIfm*ifh*ifw, 0, 0);
#if (defined(USE_FUSED_RELU_BWD) || defined(USE_FUSED_BATCH_STATS) ||  defined(USE_FUSED_BATCHNORM))
    for (i = 0; i < nImg*nIfm*ifhp*ifwp; i++ ) {
      if ( ((i%16) == 2) || ((i%16) == 3) || ((i%16) == 7) || ((i%16) == 14) ) {
        naive_input[i] = 0.0;
      }
    }
#endif    
    zero_buf(naive_input_padded,  nImg*nIfm*ifhp*ifwp);
    copy_internal_nchw( naive_input_padded , naive_input, nImg, nIfm, ifh, ifw, pad_h, pad_w);
  }

  if (padding_mode == 0 ) {
    init_buf(naive_doutput,      nImg*nOfm*ofhp*ofwp, 0, 0);
    naive_doutput_padded = naive_doutput;
  } else {
    naive_doutput_padded = (float*)libxsmm_aligned_scratch( nImg*nOfm*ofhp*ofwp*sizeof(float), 2097152);
    naive_output_test    = (float*)libxsmm_aligned_scratch( nImg*nOfm*ofhp*ofwp*sizeof(float), 2097152);
    zero_buf(naive_output_test,  nImg*nOfm*ofhp*ofwp);
    init_buf(naive_doutput,      nImg*nOfm*ofh*ofw, 0, 0);
    zero_buf(naive_doutput_padded,  nImg*nOfm*ofhp*ofwp);
    copy_internal_nchw( naive_doutput_padded , naive_doutput, nImg, nOfm, ofh, ofw, pad_h, pad_w);
  }

  zero_buf(naive_libxsmm_output,  nImg*nOfm*ofhp*ofwp);
  zero_buf(naive_libxsmm_dinput,  nImg*nIfm*ifhp*ifwp);
  zero_buf(naive_libxsmm_dfilter, nOfm*nIfm*kh*kw);
  init_buf(naive_filter,         nOfm*nIfm*kh*kw, 0, 0);
  copy_buf(naive_filter, naive_dfilter, nOfm*nIfm*kh*kw);
  init_buf(naive_bias,           nOfm, 0, 0);
  init_buf(naive_dbias,          nOfm, 0, 0);

#if (defined(USE_FUSED_BATCH_STATS) ||  defined(USE_FUSED_BATCHNORM))
  init_buf(naive_bmean,           nIfm, 0, 0);
  init_buf(naive_rcpstddev,       nIfm, 0, 0);
  init_buf(naive_dgamma,          nIfm, 0, 0);
  init_buf(naive_dbeta,           nIfm, 0, 0);
  zero_buf(naive_del_input_add,   nImg*nIfm*(stride_bn*ifhp)*(stride_bn*ifwp) );
  init_buf(naive_bn_input,        nImg*nIfm*(stride_bn*ifhp)*(stride_bn*ifwp), 0, 0);
#endif

  /* first touch LIBXSMM */
  zero_buf( input_libxsmm    , nImg*nIfm*ifhp*ifwp );
  zero_buf( filter_libxsmm   , nOfm*nIfm*kh*kw );
  zero_buf( output_libxsmm   , nImg*nOfm*ofhp*ofwp );
  zero_buf( dinput_libxsmm   , nImg*nIfm*ifhp*ifwp );
  zero_buf( dfilter_libxsmm  , nOfm*nIfm*kh*kw );
  zero_buf( doutput_libxsmm  , nImg*nOfm*ofhp*ofwp );
  zero_buf( filtertr_libxsmm , nOfm*nIfm*kh*kw );

  if (LIBXSMM_NEQ(0, check)) {
    printf("##########################################\n");
    printf("#         Computing Reference ...        #\n");
    printf("##########################################\n");
    if (type == 'A' || type == 'F') {
#ifdef USE_OVERWRITE
      zero_buf(naive_output,    nImg*nOfm*ofhp*ofwp);
#endif
      naive_conv_fp(&naive_param, naive_input, naive_output, naive_filter, naive_bias);     
    }
    if ( (type == 'A' || type == 'B') && (nIfm > 3) ) {
#ifdef USE_OVERWRITE
      zero_buf(naive_dinput,         nImg*nIfm*ifhp*ifwp);
#endif
      naive_conv_bp(&naive_param, naive_dinput, naive_doutput, naive_filter, naive_input);
    }
    if (type == 'A' || type == 'U') {
#ifdef USE_OVERWRITE
      zero_buf(naive_dfilter,          nOfm*nIfm*kh*kw);
#endif
      naive_conv_wu(&naive_param, naive_input, naive_doutput, naive_dfilter);
    }
    printf("##########################################\n");
    printf("#      Computing Reference ... done      #\n");
    printf("##########################################\n");
  }

  if (format == 'A' || format == 'L') {
    printf("\n");
    printf("##########################################\n");
    printf("#      Setting Up  (custom-Storage)      #\n");
    printf("##########################################\n");

    /* setup LIBXSMM handle */
    conv_desc.N = nImg;
    conv_desc.C = nIfm;
    conv_desc.H = ifh;
    conv_desc.W = ifw;
    conv_desc.K = nOfm;
    conv_desc.R = kh;
    conv_desc.S = kw;
    conv_desc.u = stride_h;
    conv_desc.v = stride_w;
    conv_desc.pad_h = pad_h;
    conv_desc.pad_w = pad_w;
    conv_desc.pad_h_in = pad_h_in;
    conv_desc.pad_w_in = pad_w_in;
    conv_desc.pad_h_out = pad_h_out;
    conv_desc.pad_w_out = pad_w_out;
    conv_desc.threads = nThreads;
    conv_desc.algo = (0 == algo_winograd ? LIBXSMM_DNN_CONV_ALGO_DIRECT : LIBXSMM_DNN_CONV_ALGO_AUTO);
    conv_desc.buffer_format = LIBXSMM_DNN_TENSOR_FORMAT_LIBXSMM;
    conv_desc.filter_format = LIBXSMM_DNN_TENSOR_FORMAT_LIBXSMM;
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_NONE;
#if defined(USE_BWD_NO_FILTER_TRANSPOSE_OVERWRITE)
    conv_desc.options = LIBXSMM_DNN_CONV_OPTION_BWD_NO_FILTER_TRANSPOSE_OVERWRITE;
#elif defined(USE_OVERWRITE)
    conv_desc.options = LIBXSMM_DNN_CONV_OPTION_OVERWRITE;
#else
    conv_desc.options = LIBXSMM_DNN_CONV_OPTION_NONE;
#endif
#if defined(USE_FUSED_BIAS)
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_BIAS;
#elif defined(USE_FUSED_RELU)
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_RELU;
#elif defined(USE_FUSED_BIAS_RELU)
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_BIAS_RELU;
#elif defined(USE_FUSED_BATCH_STATS)
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_BATCHNORM_STATS;
#elif defined(USE_FUSED_BATCHNORM)
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_BATCHNORM;
#elif defined(USE_FUSED_RELU_BWD)
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_RELU_BWD;
#elif defined(USE_FUSED_BATCH_STATCH_RELU_BWD)
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_BATCH_STATS_FWD_RELU_BWD;
#else
    conv_desc.fuse_ops = LIBXSMM_DNN_CONV_FUSE_NONE;
#endif
    /*conv_desc.options = LIBXSMM_DNN_CONV_OPTION_UPD_NO_FILTER_REDUCE;*/
    conv_desc.datatype_in = LIBXSMM_DNN_DATATYPE_F32;
    conv_desc.datatype_out = LIBXSMM_DNN_DATATYPE_F32;
    conv_desc.pre_bn = NULL;
    conv_desc.post_bn = NULL;

#if (defined(USE_FUSED_BATCH_STATS) || defined(USE_FUSED_BATCHNORM))
    fusedbatchnorm_desc_post.N = nImg;
    fusedbatchnorm_desc_post.C = nOfm;
    fusedbatchnorm_desc_post.H = ofh;
    fusedbatchnorm_desc_post.W = ofw;
    fusedbatchnorm_desc_post.u = stride_h;
    fusedbatchnorm_desc_post.v = stride_w;
    fusedbatchnorm_desc_post.pad_h_in = pad_h_in;
    fusedbatchnorm_desc_post.pad_w_in = pad_w_in;
    fusedbatchnorm_desc_post.pad_h_out = pad_h_out;
    fusedbatchnorm_desc_post.pad_w_out = pad_w_out;
    fusedbatchnorm_desc_post.threads = nThreads;
    fusedbatchnorm_desc_post.datatype_in = LIBXSMM_DNN_DATATYPE_F32;
    fusedbatchnorm_desc_post.datatype_out = LIBXSMM_DNN_DATATYPE_F32;
    fusedbatchnorm_desc_post.datatype_stats = LIBXSMM_DNN_DATATYPE_F32;
    fusedbatchnorm_desc_post.buffer_format = LIBXSMM_DNN_TENSOR_FORMAT_LIBXSMM;
    fusedbatchnorm_desc_post.fuse_ops = LIBXSMM_DNN_FUSEDBN_OPS_BN;
    libxsmm_bn_handle_post = libxsmm_dnn_create_fusedbatchnorm( fusedbatchnorm_desc_post, &status );
    CHKERR_LIBXSMM_DNN( status );
    conv_desc.post_bn = libxsmm_bn_handle_post;
#if defined(USE_FUSED_BATCHNORM)
    fusedbatchnorm_desc_pre.N = nImg;
    fusedbatchnorm_desc_pre.C = nIfm;
    fusedbatchnorm_desc_pre.H = ifh*stride_bn;
    fusedbatchnorm_desc_pre.W = ifw*stride_bn;
    fusedbatchnorm_desc_pre.u = stride_bn;
    fusedbatchnorm_desc_pre.v = stride_bn;
    fusedbatchnorm_desc_pre.pad_h_in = pad_bn;
    fusedbatchnorm_desc_pre.pad_w_in = pad_bn;
    fusedbatchnorm_desc_pre.pad_h_out = pad_h_out;
    fusedbatchnorm_desc_pre.pad_w_out = pad_w_out;
    fusedbatchnorm_desc_pre.threads = nThreads;
    fusedbatchnorm_desc_pre.datatype_in = LIBXSMM_DNN_DATATYPE_F32;
    fusedbatchnorm_desc_pre.datatype_out = LIBXSMM_DNN_DATATYPE_F32;
    fusedbatchnorm_desc_pre.datatype_stats = LIBXSMM_DNN_DATATYPE_F32;
    fusedbatchnorm_desc_pre.buffer_format = LIBXSMM_DNN_TENSOR_FORMAT_LIBXSMM;
    fusedbatchnorm_desc_pre.fuse_ops = LIBXSMM_DNN_FUSEDBN_OPS_BN;
    libxsmm_bn_handle_pre = libxsmm_dnn_create_fusedbatchnorm( fusedbatchnorm_desc_pre, &status );
    CHKERR_LIBXSMM_DNN( status );
    conv_desc.pre_bn = libxsmm_bn_handle_pre;
#endif
#endif
    libxsmm_handle = libxsmm_dnn_create_conv_layer( conv_desc, &status );
    CHKERR_LIBXSMM_DNN( status );

    /* setup LIBXSMM buffers and filter */
    libxsmm_layout = libxsmm_dnn_create_tensor_datalayout( libxsmm_handle, LIBXSMM_DNN_INPUT, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_input  = libxsmm_dnn_link_tensor( libxsmm_layout,  input_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dinput = libxsmm_dnn_link_tensor( libxsmm_layout, dinput_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_create_tensor_datalayout( libxsmm_handle, LIBXSMM_DNN_OUTPUT, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_output  = libxsmm_dnn_link_tensor( libxsmm_layout,  output_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_doutput = libxsmm_dnn_link_tensor( libxsmm_layout, doutput_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_create_tensor_datalayout( libxsmm_handle, LIBXSMM_DNN_FILTER, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_filter  = libxsmm_dnn_link_tensor( libxsmm_layout,  filter_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dfilter = libxsmm_dnn_link_tensor( libxsmm_layout, dfilter_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_create_tensor_datalayout( libxsmm_handle, LIBXSMM_DNN_CHANNEL_BIAS, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_bias  = libxsmm_dnn_link_tensor( libxsmm_layout,  bias_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dbias = libxsmm_dnn_link_tensor( libxsmm_layout, dbias_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_create_tensor_datalayout( libxsmm_handle, LIBXSMM_DNN_REGULAR_FILTER_TRANS, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_filter_tr  = libxsmm_dnn_link_tensor( libxsmm_layout, filtertr_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

#if (defined(USE_FUSED_BATCH_STATS) || defined(USE_FUSED_BATCHNORM))
    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_post, LIBXSMM_DNN_CHANNEL_EXPECTVAL, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_expectval  = libxsmm_dnn_link_tensor( libxsmm_layout, expectval_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_post, LIBXSMM_DNN_CHANNEL_RCPSTDDEV, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_rcpstddev  = libxsmm_dnn_link_tensor( libxsmm_layout, rcpstddev_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_post, LIBXSMM_DNN_CHANNEL_VARIANCE, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_variance  = libxsmm_dnn_link_tensor( libxsmm_layout, variance_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_pre, LIBXSMM_DNN_GRADIENT_CHANNEL_BETA, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dbeta  = libxsmm_dnn_link_tensor( libxsmm_layout, dbeta_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_pre, LIBXSMM_DNN_GRADIENT_CHANNEL_GAMMA, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dgamma  = libxsmm_dnn_link_tensor( libxsmm_layout, dgamma_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_pre, LIBXSMM_DNN_CHANNEL_EXPECTVAL, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_bmean  = libxsmm_dnn_link_tensor( libxsmm_layout, bmean_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_pre, LIBXSMM_DNN_CHANNEL_RCPSTDDEV, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_brcpstddev  = libxsmm_dnn_link_tensor( libxsmm_layout, brcpstddev_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_pre, LIBXSMM_DNN_GRADIENT_INPUT_ADD, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_del_input_add  = libxsmm_dnn_link_tensor( libxsmm_layout, del_input_add_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );

    libxsmm_layout = libxsmm_dnn_fusedbatchnorm_create_tensor_datalayout( libxsmm_bn_handle_pre, LIBXSMM_DNN_REGULAR_INPUT, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_bn_input  = libxsmm_dnn_link_tensor( libxsmm_layout, bn_input_libxsmm, &status ); CHKERR_LIBXSMM_DNN( status );
    libxsmm_dnn_destroy_tensor_datalayout( libxsmm_layout );
#endif

    /* copy in data to LIBXSMM format */
    /* we can also use the layout functions and set the data on our
       own external to the library, @TODO, we plan to add an example here */
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_input,  (void*)naive_input_padded,  LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_filter, (void*)naive_filter,      LIBXSMM_DNN_TENSOR_FORMAT_KCRS ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_bias,   (void*)naive_bias,        LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
    zero_buf(filtertr_libxsmm, nOfm*nIfm*kh*kw);

    /* bind buffers and filter to handle */
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_input,      LIBXSMM_DNN_REGULAR_INPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_dinput,     LIBXSMM_DNN_GRADIENT_INPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_output,     LIBXSMM_DNN_REGULAR_OUTPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_doutput,    LIBXSMM_DNN_GRADIENT_OUTPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_filter,     LIBXSMM_DNN_REGULAR_FILTER ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_dfilter,    LIBXSMM_DNN_GRADIENT_FILTER ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_bias,       LIBXSMM_DNN_REGULAR_CHANNEL_BIAS ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_dbias,      LIBXSMM_DNN_GRADIENT_CHANNEL_BIAS ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_tensor( libxsmm_handle, libxsmm_filter_tr,  LIBXSMM_DNN_REGULAR_FILTER_TRANS ) );
#if (defined(USE_FUSED_BATCH_STATS) || defined(USE_FUSED_BATCHNORM))
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_post, libxsmm_expectval,    LIBXSMM_DNN_CHANNEL_EXPECTVAL ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_post, libxsmm_rcpstddev,    LIBXSMM_DNN_CHANNEL_RCPSTDDEV ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_post, libxsmm_variance,     LIBXSMM_DNN_CHANNEL_VARIANCE  ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_pre, libxsmm_dbeta,         LIBXSMM_DNN_GRADIENT_CHANNEL_BETA ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_pre, libxsmm_dgamma,        LIBXSMM_DNN_GRADIENT_CHANNEL_GAMMA ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_pre, libxsmm_bmean,         LIBXSMM_DNN_CHANNEL_EXPECTVAL ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_pre, libxsmm_brcpstddev,    LIBXSMM_DNN_CHANNEL_RCPSTDDEV ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_pre, libxsmm_del_input_add, LIBXSMM_DNN_GRADIENT_INPUT_ADD ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_pre, libxsmm_input,         LIBXSMM_DNN_REGULAR_OUTPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_pre, libxsmm_bn_input,      LIBXSMM_DNN_REGULAR_INPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_fusedbatchnorm_bind_tensor( libxsmm_bn_handle_pre, libxsmm_dinput,        LIBXSMM_DNN_GRADIENT_OUTPUT ) );

    CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_bn_input,  (void*)naive_bn_input,  LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_del_input_add,  (void*)naive_del_input_add,  LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
    copy_buf(naive_bmean, bmean_libxsmm, nIfm);
    copy_buf(naive_rcpstddev, brcpstddev_libxsmm, nIfm);
    copy_buf(naive_dbeta, dbeta_libxsmm, nIfm);
    copy_buf(naive_dgamma, dgamma_libxsmm, nIfm);
#endif

    /* let's allocate and bind scratch */
    scratch_size = libxsmm_dnn_get_scratch_size( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_ALL, &status );
    CHKERR_LIBXSMM_DNN( status );

    scratch = libxsmm_aligned_scratch( scratch_size, 2097152 );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_bind_scratch( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_ALL, scratch ) );

    /* set scratch to bogus to make sure that libxsmm takes care of zeroing internally */
    init_buf( (float*)scratch, scratch_size/4, 0, 0 );

    if ((type == 'A' || type == 'F') && LIBXSMM_NEQ(0, check)) {
      printf("##########################################\n");
      printf("#   Correctness - FWD (custom-Storage)   #\n");
      printf("##########################################\n");
      /* run LIBXSMM convolutions */
#if defined(_OPENMP)
#     pragma omp parallel
#endif
      {
#if defined(_OPENMP)
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        CHKERR_LIBXSMM_DNN( libxsmm_dnn_execute_st( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_FWD, 0, tid ) );
      }
      /* copy out data */
      CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyout_tensor( libxsmm_output, (void*)naive_libxsmm_output, LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
      if (padding_mode == 0 ) {
        naive_output_test = naive_output; 
      } else {
        copy_internal_nchw( naive_output_test, naive_output, nImg, nOfm, ofh, ofw, pad_h, pad_w);
      }

      /* compare */
      libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nImg*nOfm*ofhp*ofwp, 1, naive_output_test, naive_libxsmm_output, 0, 0, &norms_fwd);
      printf("L1 reference  : %.25g\n", norms_fwd.l1_ref);
      printf("L1 test       : %.25g\n", norms_fwd.l1_tst);
      printf("L2 abs.error  : %.24f\n", norms_fwd.l2_abs);
      printf("L2 rel.error  : %.24f\n", norms_fwd.l2_rel);
      printf("Linf abs.error: %.24f\n", norms_fwd.linf_abs);
      printf("Linf rel.error: %.24f\n", norms_fwd.linf_rel);
      printf("Check-norm    : %.24f\n", norms_fwd.normf_rel);
      libxsmm_matdiff_reduce(&diff, &norms_fwd);

#if defined(USE_FUSED_BATCH_STATS)
      {
        float *expectval_naive, *rcpstddev_naive, *variance_naive;
        expectval_naive = (float*) malloc(nOfm*sizeof(float));
        rcpstddev_naive    = (float*) malloc(nOfm*sizeof(float));
        variance_naive  = (float*) malloc(nOfm*sizeof(float));

        /* set struct for naive convolution */
        naive_fusedbatchnorm_t naive_param;
        naive_param.N = nImg;
        naive_param.C = nOfm;
        naive_param.H = ofh;
        naive_param.W = ofw;
        naive_param.stride_h = stride_h;
        naive_param.stride_w = stride_w;
        naive_param.norm_type = 0;
        naive_param.fuse_type = 0;

        naive_fusedbatchnorm_fp(&naive_param, naive_output, NULL, NULL,  NULL, NULL, expectval_naive, rcpstddev_naive, variance_naive);

        libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nOfm, 1, expectval_naive, expectval_libxsmm, 0, 0, &norms_batchstats);
        printf("Expected values:\n");
        printf("L1 reference  : %.25g\n", norms_batchstats.l1_ref);
        printf("L1 test       : %.25g\n", norms_batchstats.l1_tst);
        printf("L2 abs.error  : %.24f\n", norms_batchstats.l2_abs);
        printf("L2 rel.error  : %.24f\n", norms_batchstats.l2_rel);
        printf("Linf abs.error: %.24f\n", norms_batchstats.linf_abs);
        printf("Linf rel.error: %.24f\n", norms_batchstats.linf_rel);
        printf("Check-norm    : %.24f\n", norms_batchstats.normf_rel);
        libxsmm_matdiff_reduce(&diff, &norms_batchstats);

        libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nOfm, 1, rcpstddev_naive, rcpstddev_libxsmm, 0, 0, &norms_batchstats);
        printf("rcpstddev values:\n");
        printf("L1 reference  : %.25g\n", norms_batchstats.l1_ref);
        printf("L1 test       : %.25g\n", norms_batchstats.l1_tst);
        printf("L2 abs.error  : %.24f\n", norms_batchstats.l2_abs);
        printf("L2 rel.error  : %.24f\n", norms_batchstats.l2_rel);
        printf("Linf abs.error: %.24f\n", norms_batchstats.linf_abs);
        printf("Linf rel.error: %.24f\n", norms_batchstats.linf_rel);
        printf("Check-norm    : %.24f\n", norms_batchstats.normf_rel);
        libxsmm_matdiff_reduce(&diff, &norms_batchstats);

        libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nOfm, 1, variance_naive, variance_libxsmm, 0, 0, &norms_batchstats);
        printf("Variance values:\n");
        printf("L1 reference  : %.25g\n", norms_batchstats.l1_ref);
        printf("L1 test       : %.25g\n", norms_batchstats.l1_tst);
        printf("L2 abs.error  : %.24f\n", norms_batchstats.l2_abs);
        printf("L2 rel.error  : %.24f\n", norms_batchstats.l2_rel);
        printf("Linf abs.error: %.24f\n", norms_batchstats.linf_abs);
        printf("Linf rel.error: %.24f\n", norms_batchstats.linf_rel);
        printf("Check-norm    : %.24f\n", norms_batchstats.normf_rel);
        libxsmm_matdiff_reduce(&diff, &norms_batchstats);

        free(expectval_naive);
        free(rcpstddev_naive);
        free(variance_naive);
      }
#endif
    }

    if ( (type == 'A' || type == 'B') && (nIfm > 3) && LIBXSMM_NEQ(0, check) ) {
      printf("##########################################\n");
      printf("#   Correctness - BWD (custom-Storage)   #\n");
      printf("##########################################\n");
      /* let's do some additional init such that we can run passes standalone */
      CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_doutput, (void*)naive_doutput_padded, LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
#if defined(USE_BWD_NO_FILTER_TRANSPOSE_OVERWRITE)
      CHKERR_LIBXSMM_DNN( libxsmm_dnn_trans_reg_filter( libxsmm_handle ) );
#endif
      /* run LIBXSMM convolutions */
#if defined(_OPENMP)
#     pragma omp parallel
#endif
      {
#if defined(_OPENMP)
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        CHKERR_LIBXSMM_DNN( libxsmm_dnn_execute_st( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_BWD, 0, tid ) );
      }

      /* copy out data */
      CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyout_tensor( libxsmm_dinput, (void*)naive_libxsmm_dinput, LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
      if (padding_mode == 0 ) {
        naive_dinput_test = naive_dinput; 
      } else {
        copy_internal_nchw( naive_dinput_test, naive_dinput, nImg, nIfm, ifh, ifw, pad_h, pad_w);
      }

      /* compare */
      libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nImg*nIfm*ifhp*ifwp, 1, naive_dinput_test, naive_libxsmm_dinput, 0, 0, &norms_bwd);
      printf("L1 reference  : %.25g\n", norms_bwd.l1_ref);
      printf("L1 test       : %.25g\n", norms_bwd.l1_tst);
      printf("L2 abs.error  : %.24f\n", norms_bwd.l2_abs);
      printf("L2 rel.error  : %.24f\n", norms_bwd.l2_rel);
      printf("Linf abs.error: %.24f\n", norms_bwd.linf_abs);
      printf("Linf rel.error: %.24f\n", norms_bwd.linf_rel);
      printf("Check-norm    : %.24f\n", norms_bwd.normf_rel);
      libxsmm_matdiff_reduce(&diff, &norms_bwd);

#if defined(USE_FUSED_BATCH_STATS)
      {
        naive_fusedbatchnorm_t naive_param;
        naive_param.N = nImg;
        naive_param.C = nIfm;
        naive_param.H = ifh*stride_bn;
        naive_param.W = ifw*stride_bn;
        naive_param.stride_h = stride_bn;
        naive_param.stride_w = stride_bn;

        naive_fusedbatchnorm_bp(&naive_param, naive_bn_input, naive_input, naive_del_input_add, naive_dbeta, naive_dgamma, naive_bmean, naive_rcpstddev);

        libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nIfm, 1, naive_dbeta, dbeta_libxsmm, 0, 0, &norms_batchstats);
        printf("\nDelta beta values:\n");
        printf("L1 reference  : %.25g\n", norms_batchstats.l1_ref);
        printf("L1 test       : %.25g\n", norms_batchstats.l1_tst);
        printf("L2 abs.error  : %.24f\n", norms_batchstats.l2_abs);
        printf("L2 rel.error  : %.24f\n", norms_batchstats.l2_rel);
        printf("Linf abs.error: %.24f\n", norms_batchstats.linf_abs);
        printf("Linf rel.error: %.24f\n", norms_batchstats.linf_rel);
        printf("Check-norm    : %.24f\n\n", norms_batchstats.normf_rel);
        libxsmm_matdiff_reduce(&diff, &norms_batchstats);

        libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nIfm, 1, naive_dgamma, dgamma_libxsmm, 0, 0, &norms_batchstats);
        printf("Delta gamma values:\n");
        printf("L1 reference  : %.25g\n", norms_batchstats.l1_ref);
        printf("L1 test       : %.25g\n", norms_batchstats.l1_tst);
        printf("L2 abs.error  : %.24f\n", norms_batchstats.l2_abs);
        printf("L2 rel.error  : %.24f\n", norms_batchstats.l2_rel);
        printf("Linf abs.error: %.24f\n", norms_batchstats.linf_abs);
        printf("Linf rel.error: %.24f\n", norms_batchstats.linf_rel);
        printf("Check-norm    : %.24f\n\n", norms_batchstats.normf_rel);
        libxsmm_matdiff_reduce(&diff, &norms_batchstats);

        CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyout_tensor( libxsmm_del_input_add, (void*)naive_libxsmm_del_input_add, LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
        libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nImg*nIfm*(stride_bn*ifhp)*(stride_bn*ifwp), 1, naive_del_input_add, naive_libxsmm_del_input_add, 0, 0, &norms_bwd);
        printf("Del input add values:\n");
        printf("L1 reference  : %.25g\n", norms_bwd.l1_ref);
        printf("L1 test       : %.25g\n", norms_bwd.l1_tst);
        printf("L2 abs.error  : %.24f\n", norms_bwd.l2_abs);
        printf("L2 rel.error  : %.24f\n", norms_bwd.l2_rel);
        printf("Linf abs.error: %.24f\n", norms_bwd.linf_abs);
        printf("Linf rel.error: %.24f\n", norms_bwd.linf_rel);
        printf("Check-norm    : %.24f\n", norms_bwd.normf_rel);
        libxsmm_matdiff_reduce(&diff, &norms_bwd);
      }
#endif

    }

    if ((type == 'A' || type == 'U') && LIBXSMM_NEQ(0, check)) {
      printf("##########################################\n");
      printf("#   Correctness - UPD (custom-Storage)   #\n");
      printf("##########################################\n");
      /* let's do some additional init such that we can run passes standalone */
      CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_input, (void*)naive_input_padded, LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
      CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_doutput, (void*)naive_doutput_padded, LIBXSMM_DNN_TENSOR_FORMAT_NCHW ) );
      CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyin_tensor( libxsmm_dfilter, (void*)naive_dfilter, LIBXSMM_DNN_TENSOR_FORMAT_KCRS ) );
      /* run LIBXSMM convolutions */
#if defined(_OPENMP)
#     pragma omp parallel
#endif
      {
#if defined(_OPENMP)
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        CHKERR_LIBXSMM_DNN( libxsmm_dnn_execute_st( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_UPD, 0, tid ) );
      }
      if (conv_desc.options == LIBXSMM_DNN_CONV_OPTION_UPD_NO_FILTER_REDUCE) {
        CHKERR_LIBXSMM_DNN( libxsmm_dnn_reduce_wu_filters( libxsmm_handle, LIBXSMM_DNN_GRADIENT_FILTER ) );
      }
      /* copy out data */
      CHKERR_LIBXSMM_DNN( libxsmm_dnn_copyout_tensor( libxsmm_dfilter, (void*)naive_libxsmm_dfilter, LIBXSMM_DNN_TENSOR_FORMAT_KCRS ) );

      /* compare */
      libxsmm_matdiff(LIBXSMM_DATATYPE_F32, nOfm*nIfm*kh*kw, 1, naive_dfilter, naive_libxsmm_dfilter, 0, 0, &norms_upd);
      printf("L1 reference  : %.25g\n", norms_upd.l1_ref);
      printf("L1 test       : %.25g\n", norms_upd.l1_tst);
      printf("L2 abs.error  : %.24f\n", norms_upd.l2_abs);
      printf("L2 rel.error  : %.24f\n", norms_upd.l2_rel);
      printf("Linf abs.error: %.24f\n", norms_upd.linf_abs);
      printf("Linf rel.error: %.24f\n", norms_upd.linf_rel);
      printf("Check-norm    : %.24f\n", norms_upd.normf_rel);
      libxsmm_matdiff_reduce(&diff, &norms_upd);
    }

    if (type == 'A' || type == 'F') {
      printf("##########################################\n");
      printf("#   Performance - FWD (custom-Storage)   #\n");
      printf("##########################################\n");
      /* run LIBXSMM convolution for performance */
      l_start = libxsmm_timer_tick();
#if defined(_OPENMP)
#     pragma omp parallel private(i)
#endif
      {
#if defined(_OPENMP)
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        for (i = 0; i < iters; ++i) {
          libxsmm_dnn_execute_st( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_FWD, 0, tid );
        }
      }
      l_end = libxsmm_timer_tick();
      l_total = libxsmm_timer_duration(l_start, l_end);
      flops = (double)nImg * (double)nIfm * (double)nOfm * (double)ofh * (double)ofw * (double)(2 * kh * kw) * (double)iters;

      printf("GFLOP  = %.5g\n", flops*1e-9/(double)iters);
      printf("fp time = %.5g\n", ((double)(l_total/iters)));
      printf("GFLOPS  = %.5g\n", (flops*1e-9)/l_total);

      printf("PERFDUMP,FP,%s,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%.5g,%.5g,%f,%f,%f,%f,%f,%f,%f\n", LIBXSMM_VERSION, nThreads, nImg, nIfm, nOfm,
          ifw, ifh, kw, kh, stride, padw, padh, ((double)(l_total/iters)), (flops*1e-9)/l_total, norms_fwd.l1_ref, norms_fwd.l1_tst,
          norms_fwd.l2_abs, norms_fwd.l2_rel, norms_fwd.linf_abs, norms_fwd.linf_rel, norms_fwd.normf_rel);
    }

    if ( (type == 'A' || type == 'B') && (nIfm > 3) ) {
      printf("##########################################\n");
      printf("#   Performance - BWD (custom-Storage)   #\n");
      printf("##########################################\n");
      /* run LIBXSMM convolution for performance */
      l_start = libxsmm_timer_tick();

#if defined(_OPENMP)
#     pragma omp parallel  private(i)
#endif
      {
#if defined(_OPENMP)
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        for (i = 0; i < iters; ++i) {
          libxsmm_dnn_execute_st( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_BWD, 0, tid );
        }
      }
      l_end = libxsmm_timer_tick();
      l_total = libxsmm_timer_duration(l_start, l_end);
      flops = (double)nImg * (double)nIfm * (double)nOfm * (double)ofh * (double)ofw * (double)(2 * kh * kw) * (double)iters;

      printf("GFLOP  = %.5g\n", flops*1e-9/(double)iters);
      printf("bp time = %.5g\n", ((double)(l_total/iters)));
      printf("GFLOPS  = %.5g\n", (flops*1e-9)/l_total);

      printf("PERFDUMP,BP,%s,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%.5g,%.5g,%f,%f,%f,%f,%f,%f,%f\n", LIBXSMM_VERSION, nThreads, nImg, nIfm, nOfm,
          ifw, ifh, kw, kh, stride, padw, padh, ((double)(l_total/iters)), (flops*1e-9)/l_total, norms_bwd.l1_ref, norms_bwd.l1_tst,
          norms_bwd.l2_abs, norms_bwd.l2_rel, norms_bwd.linf_abs, norms_bwd.linf_rel, norms_bwd.normf_rel);
    }

    if (type == 'A' || type == 'U') {
      printf("##########################################\n");
      printf("#   Performance - UPD (custom-Storage)   #\n");
      printf("##########################################\n");
      /* run LIBXSMM convolution for performance */
      l_start = libxsmm_timer_tick();

#if defined(_OPENMP)
#     pragma omp parallel private(i)
#endif
      {
#if defined(_OPENMP)
        const int tid = omp_get_thread_num();
#else
        const int tid = 0;
#endif
        for (i = 0; i < iters; ++i) {
          libxsmm_dnn_execute_st( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_UPD, 0, tid );
          if (conv_desc.options == LIBXSMM_DNN_CONV_OPTION_UPD_NO_FILTER_REDUCE) {
            CHKERR_LIBXSMM_DNN( libxsmm_dnn_reduce_wu_filters( libxsmm_handle, LIBXSMM_DNN_GRADIENT_FILTER ) );
          }
        }
      }
      l_end = libxsmm_timer_tick();
      l_total = libxsmm_timer_duration(l_start, l_end);
      flops = (double)nImg * (double)nIfm * (double)nOfm * (double)ofh * (double)ofw * (double)(2 * kh * kw) * (double)iters;

      printf("GFLOP  = %.5g\n", flops*1e-9/(double)iters);
      printf("wu time = %.5g\n", ((double)(l_total/iters)));
      printf("GFLOPS  = %.5g\n", (flops*1e-9)/l_total);

      printf("PERFDUMP,WU,%s,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%.5g,%.5g,%f,%f,%f,%f,%f,%f,%f\n", LIBXSMM_VERSION, nThreads, nImg, nIfm, nOfm,
          ifw, ifh, kw, kh, stride, padw, padh, ((double)(l_total/iters)), (flops*1e-9)/l_total, norms_upd.l1_ref, norms_upd.l1_tst,
          norms_upd.l2_abs, norms_upd.l2_rel, norms_upd.linf_abs, norms_upd.linf_rel, norms_upd.normf_rel);
    }

    /* clean-up */
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_scratch( libxsmm_handle, LIBXSMM_DNN_COMPUTE_KIND_ALL ) );
    libxsmm_free(scratch);
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_REGULAR_INPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_GRADIENT_INPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_REGULAR_OUTPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_GRADIENT_OUTPUT ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_REGULAR_FILTER ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_GRADIENT_FILTER ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_REGULAR_CHANNEL_BIAS ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_GRADIENT_CHANNEL_BIAS ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_release_tensor( libxsmm_handle, LIBXSMM_DNN_REGULAR_FILTER_TRANS ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_input ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_output ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_filter ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_dinput ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_doutput ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_dfilter ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_bias ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_dbias ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_tensor( libxsmm_filter_tr ) );
    CHKERR_LIBXSMM_DNN( libxsmm_dnn_destroy_conv_layer( libxsmm_handle ) );
  }

  /* deallocate data */
  libxsmm_free(naive_input);
  libxsmm_free(naive_dinput);
  libxsmm_free(naive_output);
  libxsmm_free(naive_doutput);
  libxsmm_free(naive_libxsmm_output);
  libxsmm_free(naive_libxsmm_dinput);
  libxsmm_free(naive_libxsmm_dfilter);
  libxsmm_free(naive_filter);
  libxsmm_free(naive_dfilter);
  libxsmm_free(input_libxsmm);
  libxsmm_free(filter_libxsmm);
  libxsmm_free(output_libxsmm);
  libxsmm_free(dinput_libxsmm);
  libxsmm_free(dfilter_libxsmm);
  libxsmm_free(doutput_libxsmm);
  libxsmm_free(filtertr_libxsmm);
  libxsmm_free(naive_bias);
  libxsmm_free(naive_dbias);
  libxsmm_free(bias_libxsmm);
  libxsmm_free(dbias_libxsmm);

  { const char *const env_check_scale = getenv("CHECK_SCALE");
    const double check_scale = LIBXSMM_ABS(0 == env_check_scale ? 1.0 : atof(env_check_scale));
    if (LIBXSMM_NEQ(0, check) && (check < 100.0 * check_scale * diff.normf_rel) && (global_status == LIBXSMM_DNN_SUCCESS)) {
      fprintf(stderr, "FAILED with an error of %f%%!\n", 100.0 * diff.normf_rel);
      exit(EXIT_FAILURE);
    }
  }

  /* some empty lines at the end */
  printf("\n\n\n");

  return EXIT_SUCCESS;
}

