/*********************************************************************
 * convolve.c
 *********************************************************************/

/* Standard includes */
#include <assert.h>
#include <math.h>
#include <stdlib.h>   /* malloc(), realloc() */
#include <stdint.h>

/* Our includes */
#include "base.h"
#include "error.h"
#include "convolve.h"
#include "klt_util.h"   /* printing */

#if __SSE__
#include <xmmintrin.h>
#define USE_SSE
#endif

#define MAX_KERNEL_WIDTH 	71


typedef struct  {
  int width;
  float data[MAX_KERNEL_WIDTH];
}  ConvolutionKernel;

/* Kernels */
static ConvolutionKernel gauss_kernel;
static ConvolutionKernel gaussderiv_kernel;
static float sigma_last = -10.0;


/*********************************************************************
 * _KLTToFloatImage
 *
 * Given a pointer to image data (probably unsigned chars), copy
 * data to a float image.
 */

void _KLTToFloatImage(
  KLT_PixelType *img,
  int ncols, int nrows,
  _KLT_FloatImage floatimg)
{
  int size = ncols*nrows;
  int i;

  /* Output image must be large enough to hold result */
  assert(floatimg->ncols >= ncols);
  assert(floatimg->nrows >= nrows);

  floatimg->ncols = ncols;
  floatimg->nrows = nrows;

  for(i = 0; i < size; i++)
    floatimg->data[i] = (float)img[i];
}


/*********************************************************************
 * _computeKernels
 */

static void _computeKernels(
  float sigma,
  ConvolutionKernel *gauss,
  ConvolutionKernel *gaussderiv)
{
  const float factor = 0.01;   /* for truncating tail */
  int i;

  assert(MAX_KERNEL_WIDTH % 2 == 1);
  assert(sigma >= 0.0);

  /* Compute kernels, and automatically determine widths */
  {
    const int hw = MAX_KERNEL_WIDTH / 2;
    float max_gauss = 1.0f, max_gaussderiv = sigma*expf(-0.5f);
	
    /* Compute gauss and deriv */
    for (i = -hw ; i <= hw ; i++)  {
      gauss->data[i+hw]      = expf(-i*i / (2*sigma*sigma));
      gaussderiv->data[i+hw] = -i * gauss->data[i+hw];
    }

    /* Compute widths */
    gauss->width = MAX_KERNEL_WIDTH;
    for (i = -hw ; fabs(gauss->data[i+hw] / max_gauss) < factor ; 
         i++, gauss->width -= 2);
    gaussderiv->width = MAX_KERNEL_WIDTH;
    for (i = -hw ; fabs(gaussderiv->data[i+hw] / max_gaussderiv) < factor ; 
         i++, gaussderiv->width -= 2);
    if (gauss->width == MAX_KERNEL_WIDTH || 
        gaussderiv->width == MAX_KERNEL_WIDTH)
      KLTError("(_computeKernels) MAX_KERNEL_WIDTH %d is too small for "
               "a sigma of %f", MAX_KERNEL_WIDTH, sigma);
  }

  /* Shift if width less than MAX_KERNEL_WIDTH */
  for (i = 0 ; i < gauss->width ; i++)
    gauss->data[i] = gauss->data[i+(MAX_KERNEL_WIDTH-gauss->width)/2];
  for (i = 0 ; i < gaussderiv->width ; i++)
    gaussderiv->data[i] = gaussderiv->data[i+(MAX_KERNEL_WIDTH-gaussderiv->width)/2];
  /* Normalize gauss and deriv */
  {
    const int hw = gaussderiv->width / 2;
    float den;
			
    den = 0.0;
    for (i = 0 ; i < gauss->width ; i++)  den += gauss->data[i];
    for (i = 0 ; i < gauss->width ; i++)  gauss->data[i] /= den;
    den = 0.0;
    for (i = -hw ; i <= hw ; i++)  den -= i*gaussderiv->data[i+hw];
    for (i = -hw ; i <= hw ; i++)  gaussderiv->data[i+hw] /= den;
  }

  sigma_last = sigma;
}
	

/*********************************************************************
 * _KLTGetKernelWidths
 *
 */

void _KLTGetKernelWidths(
  float sigma,
  int *gauss_width,
  int *gaussderiv_width)
{
  _computeKernels(sigma, &gauss_kernel, &gaussderiv_kernel);
  *gauss_width = gauss_kernel.width;
  *gaussderiv_width = gaussderiv_kernel.width;
}


/*********************************************************************
 * _convolveImageHoriz
 */

static void _convolveImageHoriz(
  _KLT_FloatImage imgin,
  ConvolutionKernel kernel,
  _KLT_FloatImage imgout)
{
  int rowidx = 0;           /* Points to row's first pixel */
  int outidx = 0;
  int ppidx;
  
  register float sum;
  register int radius = kernel.width / 2;
  register int ncols = imgin->ncols, nrows = imgin->nrows;
  register int i, j, k;

  /* Kernel width must be odd */
  assert(kernel.width % 2 == 1);

  /* Must read from and write to different images */
  assert(imgin != imgout);

  /* Output image must be large enough to hold result */
  assert(imgout->ncols >= imgin->ncols);
  assert(imgout->nrows >= imgin->nrows);

  /* For each row, do ... */
  for (j = 0 ; j < nrows ; j++)  {

    /* Zero leftmost columns */
    for (i = 0 ; i < radius ; i++)
      imgout->data[outidx++] = 0.0;

    /* Convolve middle columns with kernel */
    for ( ; i < ncols - radius ; i++)  {
      ppidx = rowidx + i - radius;
      sum = 0.0;
      for (k = kernel.width-1 ; k >= 0 ; k--)
        sum += imgin->data[ppidx++] * kernel.data[k];
      
      imgout->data[outidx++] = sum;
    }

    /* Zero rightmost columns */
    for ( ; i < ncols ; i++)
      imgout->data[outidx++] = 0.0;

    rowidx += ncols;
  }
}


/*********************************************************************
 * _convolveImageVert
 */

static void _convolveImageVert(
  _KLT_FloatImage imgin,
  ConvolutionKernel kernel,
  _KLT_FloatImage imgout)
{
  int colidx = 0;
  int outidx = 0;
  int ppidx;
  register float sum;
  register int radius = kernel.width / 2;
  register int ncols = imgin->ncols, nrows = imgin->nrows;
  register int i, j, k;

  /* Kernel width must be odd */
  assert(kernel.width % 2 == 1);

  /* Must read from and write to different images */
  assert(imgin != imgout);

  /* require input cols to be a multiple of 8 */
  assert(ncols % 8 == 0);

  /* Output image must be large enough to hold result */
  assert(imgout->ncols >= imgin->ncols);
  assert(imgout->nrows >= imgin->nrows);

#if 0
  /* For each column, do ... */
  for (i = 0 ; i < ncols ; i++)  {

    /* Zero topmost rows */
    for (j = 0 ; j < radius ; j++)  {
      imgout->data[outidx] = 0.0;
      outidx += ncols;
    }

    /* Convolve middle rows with kernel */
    for ( ; j < nrows - radius ; j++)  {
      ppidx = colidx + ncols * (j - radius);
      sum = 0.0;
      for (k = kernel.width-1 ; k >= 0 ; k--)  {
        sum += imgin->data[ppidx] * kernel.data[k];
        ppidx += ncols;
      }
      imgout->data[outidx] = sum;
      outidx += ncols;
    }

    /* Zero bottommost rows */
    for ( ; j < nrows ; j++)  {
      imgout->data[outidx] = 0.;
      outidx += ncols;
    }

    colidx++;
    outidx -= nrows * ncols - 1;
  }
#else
  /* alternate loop structure: go across the output image, but we're
     still going down the input image.
  */
  /* Zero top */
  outidx = 0;
  for(j = 0; j < (ncols*radius); j++)
    imgout->data[outidx++] = 0.;

  /* For each row, do ... */
  for (j = radius ; j < nrows-radius ; j++)  {
#if !__OPTIMIZE__
    /* Convolve middle columns with kernel */
    for (i = 0 ; i < ncols ; i++)  {
      int ppidx = (j-radius)*ncols + i;
      float sum = 0.0;
      for (k = kernel.width-1 ; k >= 0 ; k--) {
        sum += imgin->data[ppidx] * kernel.data[k];
	ppidx += ncols;
      }
      
      imgout->data[outidx++] = sum;
#else  /* __OPTIMIZE__ */
#ifdef USE_SSE
    assert((intptr_t)(imgin->data) % 16 == 0);
    for (i = 0 ; i < ncols ; i += 8)  {
      int ppidx = (j-radius)*ncols + i;
      __m128 sum0, sum1;
      float *in = &imgin->data[ppidx];
      sum0 = sum1 = _mm_set_ps(0,0,0,0);

      for (k = kernel.width-1 ; k >= 0 ; k--) {
	__m128 coeff;
	coeff = _mm_load1_ps(&kernel.data[k]);

	_mm_prefetch(&in[32], _MM_HINT_NTA);
	sum0 += _mm_load_ps(&in[0]) * coeff;
	sum1 += _mm_load_ps(&in[4]) * coeff;
	in += ncols;
      }

      _mm_stream_ps(&imgout->data[outidx+0], sum0);
      _mm_stream_ps(&imgout->data[outidx+4], sum1);
      outidx += 8;
#else  /* !USE_SSE */
    /* This should be vectorizable so that the unrolled sum[0-3]
       calculations are done in parallel. */
    for (i = 0 ; i < ncols ; i += 4)  {
      int ppidx = (j-radius)*ncols + i;
      float sum0, sum1, sum2, sum3;
      sum0 = sum1 = sum2 = sum3 = 0.f;

      for (k = kernel.width-1 ; k >= 0 ; k--) {
	float coeff = kernel.data[k];
        sum0 += imgin->data[ppidx+0] * coeff;
        sum1 += imgin->data[ppidx+1] * coeff;
        sum2 += imgin->data[ppidx+2] * coeff;
        sum3 += imgin->data[ppidx+3] * coeff;
	ppidx += ncols;
      }
      
      imgout->data[outidx+0] = sum0;
      imgout->data[outidx+1] = sum1;
      imgout->data[outidx+2] = sum2;
      imgout->data[outidx+3] = sum3;
      outidx += 4;
#endif	/* USE_SSE */
#endif	/* __OPTIMIZE__ */
    }
  }
  /* Zero bottom */
  for (i = 0; i < ncols*radius; i++)
    imgout->data[outidx++] = 0.0;

#endif
}


/*********************************************************************
 * _convolveSeparate
 */

static void _convolveSeparate(
  _KLT_FloatImage imgin,
  ConvolutionKernel horiz_kernel,
  ConvolutionKernel vert_kernel,
  _KLT_FloatImage imgout)
{
  /* Create temporary image */
  _KLT_FloatImage tmpimg;
  tmpimg = _KLTCreateFloatImage(imgin->ncols, imgin->nrows);
  
  /* Do convolution */
  _convolveImageHoriz(imgin, horiz_kernel, tmpimg);

  _convolveImageVert(tmpimg, vert_kernel, imgout);

  /* Free memory */
  _KLTFreeFloatImage(tmpimg);
}

	
/*********************************************************************
 * _KLTComputeGradients
 */

void _KLTComputeGradients(
  _KLT_FloatImage img,
  float sigma,
  _KLT_FloatImage gradx,
  _KLT_FloatImage grady)
{
				
  /* Output images must be large enough to hold result */
  assert(gradx->ncols >= img->ncols);
  assert(gradx->nrows >= img->nrows);
  assert(grady->ncols >= img->ncols);
  assert(grady->nrows >= img->nrows);

  /* Compute kernels, if necessary */
  if (fabs(sigma - sigma_last) > 0.05)
    _computeKernels(sigma, &gauss_kernel, &gaussderiv_kernel);
	
  _convolveSeparate(img, gaussderiv_kernel, gauss_kernel, gradx);
  _convolveSeparate(img, gauss_kernel, gaussderiv_kernel, grady);

}
	

/*********************************************************************
 * _KLTComputeSmoothedImage
 */

void _KLTComputeSmoothedImage(
  _KLT_FloatImage img,
  float sigma,
  _KLT_FloatImage smooth)
{
  /* Output image must be large enough to hold result */
  assert(smooth->ncols >= img->ncols);
  assert(smooth->nrows >= img->nrows);

  /* Compute kernel, if necessary; gauss_deriv is not used */
  if (fabs(sigma - sigma_last) > 0.05)
    _computeKernels(sigma, &gauss_kernel, &gaussderiv_kernel);

  _convolveSeparate(img, gauss_kernel, gauss_kernel, smooth);
}



