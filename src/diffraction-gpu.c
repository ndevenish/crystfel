/*
 * diffraction-gpu.c
 *
 * Calculate diffraction patterns by Fourier methods (GPU version)
 *
 * Copyright © 2012-2021 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2009-2020 Thomas White <taw@physics.org>
 *   2013      Alexandra Tolstikova
 *   2013-2014 Chun Hong Yoon <chun.hong.yoon@desy.de>
 *
 * This file is part of CrystFEL.
 *
 * CrystFEL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CrystFEL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CrystFEL.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <complex.h>

#define CL_TARGET_OPENCL_VERSION 220
#ifdef HAVE_CL_CL_H
#include <CL/cl.h>
#else
#include <cl.h>
#endif

#include "image.h"
#include "utils.h"
#include "cell.h"
#include "diffraction.h"
#include "cl-utils.h"
#include "pattern_sim.h"

#include "diffraction.cl.h"

#define SINC_LUT_ELEMENTS (4096)


struct gpu_context
{
	cl_context ctx;
	cl_command_queue cq;
	cl_program prog;
	cl_kernel kern;
	cl_mem intensities;
	cl_mem flags;

	/* Array of sinc LUTs */
	cl_mem *sinc_luts;
	cl_float **sinc_lut_ptrs;
	int max_sinc_lut;  /* Number of LUTs, i.e. one greater than the maximum
	                    * index.  This equals the highest allowable "n". */
};


static void check_sinc_lut(struct gpu_context *gctx, int n,
                           int no_fringes, int flat)
{
	cl_int err;
	cl_image_format fmt;
	int i;

	if ( n > gctx->max_sinc_lut ) {

		gctx->sinc_luts = realloc(gctx->sinc_luts,
		                          n*sizeof(*gctx->sinc_luts));
		gctx->sinc_lut_ptrs = realloc(gctx->sinc_lut_ptrs,
		                              n*sizeof(*gctx->sinc_lut_ptrs));

		for ( i=gctx->max_sinc_lut; i<n; i++ ) {
			gctx->sinc_lut_ptrs[i] = NULL;
		}

		gctx->max_sinc_lut = n;
	}

	if ( gctx->sinc_lut_ptrs[n-1] != NULL ) return;

	/* Create a new sinc LUT */
	gctx->sinc_lut_ptrs[n-1] = malloc(SINC_LUT_ELEMENTS*sizeof(cl_float));
	gctx->sinc_lut_ptrs[n-1][0] = n;
	if ( n == 1 ) {
		for ( i=1; i<SINC_LUT_ELEMENTS; i++ ) {
			gctx->sinc_lut_ptrs[n-1][i] = 1.0;
		}
	} else {
		for ( i=1; i<SINC_LUT_ELEMENTS; i++ ) {
			double x, val;
			x = (double)i/SINC_LUT_ELEMENTS;
			if ( (flat || no_fringes) && (x > 1.0/n) && (1.0-x > 1.0/n) ) {
				val = 0.0;
			} else if ( flat ) {
				val = n;
			} else {
				val = fabs(sin(M_PI*n*x)/sin(M_PI*x));
			}
			gctx->sinc_lut_ptrs[n-1][i] = val;
		}
	}

	fmt.image_channel_order = CL_INTENSITY;
	fmt.image_channel_data_type = CL_FLOAT;

	gctx->sinc_luts[n-1] = clCreateImage2D(gctx->ctx,
	                                CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
	                                &fmt, SINC_LUT_ELEMENTS, 1, 0,
	                                gctx->sinc_lut_ptrs[n-1], &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't create LUT for %i\n", n);
		return;
	}
}


static int set_arg_float(struct gpu_context *gctx, int idx, float val)
{
	cl_int err;
	err = clSetKernelArg(gctx->kern, idx, sizeof(cl_float), &val);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set kernel argument %i: %s\n",
		      idx, clError(err));
		return 1;
	}

	return 0;
}


static int set_arg_int(struct gpu_context *gctx, int idx, int val)
{
	cl_int err;

	err = clSetKernelArg(gctx->kern, idx, sizeof(cl_int), &val);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set kernel argument %i: %s\n",
		      idx, clError(err));
		return 1;
	}

	return 0;
}


static int set_arg_mem(struct gpu_context *gctx, int idx, cl_mem val)
{
	cl_int err;

	err = clSetKernelArg(gctx->kern, idx, sizeof(cl_mem), &val);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set kernel argument %i: %s\n",
		      idx, clError(err));
		return 1;
	}

	return 0;
}


static int do_panels(struct gpu_context *gctx, struct image *image,
                      double k, double weight,
                      int *n_inf, int *n_neg, int *n_nan)
{
	int i;
	const int sampling = 4;  /* This, squared, number of samples / pixel */

	if ( set_arg_float(gctx, 1, k) ) return 1;
	if ( set_arg_float(gctx, 2, weight) ) return 1;

	/* Iterate over panels */
	for ( i=0; i<image->detgeom->n_panels; i++ ) {

		size_t dims[2];
		size_t ldims[2];
		struct detgeom_panel *p;
		cl_mem diff;
		size_t diff_size;
		float *diff_ptr;
		int fs, ss;
		cl_int err;

		p = &image->detgeom->panels[i];

		/* Buffer for the results of this panel */
		diff_size = p->w * p->h * sizeof(cl_float);
		diff = clCreateBuffer(gctx->ctx, CL_MEM_WRITE_ONLY,
	                              diff_size, NULL, &err);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't allocate diffraction memory\n");
			return 1;
		}

		if ( set_arg_mem(gctx, 0, diff) ) return 1;

		if ( set_arg_int(gctx, 3, p->w) ) return 1;
		if ( set_arg_float(gctx, 4, p->cnx) ) return 1;
		if ( set_arg_float(gctx, 5, p->cny) ) return 1;
		if ( set_arg_float(gctx, 6, p->fsx) ) return 1;
		if ( set_arg_float(gctx, 7, p->fsy) ) return 1;
		if ( set_arg_float(gctx, 8, p->fsz) ) return 1;
		if ( set_arg_float(gctx, 9, p->ssx) ) return 1;
		if ( set_arg_float(gctx, 10, p->ssy) ) return 1;
		if ( set_arg_float(gctx, 11, p->ssz) ) return 1;
		if ( set_arg_float(gctx, 12, 1.0/p->pixel_pitch) ) return 1;
		if ( set_arg_float(gctx, 13, p->cnz*p->pixel_pitch) ) return 1;

		dims[0] = p->w * sampling;
		dims[1] = p->h * sampling;

		ldims[0] = sampling;
		ldims[1] = sampling;

		err = clSetKernelArg(gctx->kern, 20,
		                     sampling*sampling*sizeof(cl_float), NULL);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't set local memory: %s\n", clError(err));
			return 1;
		}

		err = clEnqueueNDRangeKernel(gctx->cq, gctx->kern, 2, NULL,
		                             dims, ldims, 0, NULL, NULL);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't enqueue diffraction kernel: %s\n",
			      clError(err));
			return 1;
		}

		clFinish(gctx->cq);

		diff_ptr = clEnqueueMapBuffer(gctx->cq, diff, CL_TRUE,
		                              CL_MAP_READ, 0, diff_size,
		                              0, NULL, NULL, &err);
		if ( err != CL_SUCCESS ) {
			ERROR("Couldn't map diffraction buffer: %s\n",
			      clError(err));
			return 1;
		}

		for ( ss=0; ss<p->h; ss++ ) {
		for ( fs=0; fs<p->w; fs++ ) {

			float val;

			val = diff_ptr[fs + p->w*ss];
			if ( isinf(val) ) (*n_inf)++;
			if ( val < 0.0 ) (*n_neg)++;
			if ( isnan(val) ) (*n_nan)++;

			image->dp[i][fs + p->w*ss] += val;

		}
		}

		clEnqueueUnmapMemObject(gctx->cq, diff, diff_ptr,
		                        0, NULL, NULL);

		clReleaseMemObject(diff);

	}

	return 0;
}


int get_diffraction_gpu(struct gpu_context *gctx, struct image *image,
                        int na, int nb, int nc, UnitCell *ucell,
                        int no_fringes, int flat, int n_samples)
{
	double ax, ay, az;
	double bx, by, bz;
	double cx, cy, cz;
	cl_float16 cell;
	cl_int err;
	int n_inf = 0;
	int n_neg = 0;
	int n_nan = 0;
	int i;
	double kmin, kmax, step, norm;

	if ( gctx == NULL ) {
		ERROR("GPU setup failed.\n");
		return 1;
	}

	/* Ensure all required LUTs are available */
	check_sinc_lut(gctx, na, no_fringes, flat);
	check_sinc_lut(gctx, nb, no_fringes, flat);
	check_sinc_lut(gctx, nc, no_fringes, flat);

	/* Unit cell */
	cell_get_cartesian(ucell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);
	cell.s[0] = ax;  cell.s[1] = ay;  cell.s[2] = az;
	cell.s[3] = bx;  cell.s[4] = by;  cell.s[5] = bz;
	cell.s[6] = cx;  cell.s[7] = cy;  cell.s[8] = cz;

	err = clSetKernelArg(gctx->kern, 14, sizeof(cl_float16), &cell);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't set unit cell: %s\n", clError(err));
		return 1;
	}

	if ( set_arg_mem(gctx, 15, gctx->intensities) ) return 1;
	if ( set_arg_mem(gctx, 16, gctx->flags) ) return 1;
	if ( set_arg_mem(gctx, 17, gctx->sinc_luts[na-1]) ) return 1;
	if ( set_arg_mem(gctx, 18, gctx->sinc_luts[nb-1]) ) return 1;
	if ( set_arg_mem(gctx, 19, gctx->sinc_luts[nc-1]) ) return 1;

	/* Allocate memory for the result */
	image->dp = malloc(image->detgeom->n_panels * sizeof(float *));
	if ( image->dp == NULL ) {
		ERROR("Couldn't allocate memory for result.\n");
		return 1;
	}
	for ( i=0; i<image->detgeom->n_panels; i++ ) {
		struct detgeom_panel *p = &image->detgeom->panels[i];
		image->dp[i] = calloc(p->w * p->h, sizeof(float));
		if ( image->dp[i] == NULL ) {
			ERROR("Couldn't allocate memory for panel %i\n", i);
			return 1;
		}
	}

	spectrum_get_range(image->spectrum, &kmin, &kmax);
	step = (kmax-kmin)/(n_samples+1);

	/* Determine normalisation factor such that weights add up to 1 after
	 * sampling (bins must have constant width) */
	norm = 0.0;
	for ( i=1; i<=n_samples; i++ ) {
		double k = kmin + i*step;
		norm += spectrum_get_density_at_k(image->spectrum, k);
	}
	for ( i=1; i<=n_samples; i++ ) {

		double k = kmin + i*step;
		double prob;

		/* Probability = p.d.f. times step width */
		prob = spectrum_get_density_at_k(image->spectrum, k)/norm;

		STATUS("Wavelength: %e m, weight = %.5f\n", 1.0/k, prob);

		err = do_panels(gctx, image, k, prob, &n_inf, &n_neg, &n_nan);
		if ( err ) return 1;

	}

	if ( n_neg + n_inf + n_nan ) {
		ERROR("WARNING: The GPU calculation produced %i negative"
		      " values, %i infinities and %i NaNs.\n",
		      n_neg, n_inf, n_nan);
	}

	return 0;
}


/* Setup the OpenCL stuff, create buffers, load the structure factor table */
struct gpu_context *setup_gpu(int no_sfac,
                              const double *intensities, unsigned char *flags,
                              const char *sym, int dev_num)
{
	struct gpu_context *gctx;
	cl_uint nplat;
	cl_platform_id platforms[8];
	cl_context_properties prop[3];
	cl_int err;
	cl_device_id dev;
	size_t intensities_size;
	float *intensities_ptr;
	size_t flags_size;
	float *flags_ptr;
	size_t maxwgsize;
	int iplat;
	int have_ctx = 0;
	char cflags[512] = "";
	char *insert_stuff = NULL;

	STATUS("Setting up GPU...\n");

	err = clGetPlatformIDs(8, platforms, &nplat);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't get platform IDs: %i\n", err);
		return NULL;
	}
	if ( nplat == 0 ) {
		ERROR("Couldn't find at least one platform!\n");
		return NULL;
	}

	/* Find a GPU platform in the list */
	for ( iplat=0; iplat<nplat; iplat++ ) {

		prop[0] = CL_CONTEXT_PLATFORM;
		prop[1] = (cl_context_properties)platforms[iplat];
		prop[2] = 0;

		gctx = malloc(sizeof(*gctx));
		gctx->ctx = clCreateContextFromType(prop, CL_DEVICE_TYPE_GPU,
		                                    NULL, NULL, &err);

		if ( err != CL_SUCCESS ) {
			if ( err == CL_DEVICE_NOT_FOUND ) {
				/* No GPU device in this platform */
				continue; /* Try next platform */
			} else {
				ERROR("Couldn't create OpenCL context: %s (%i)\n",
				clError(err), err);
				free(gctx);
				return NULL;
			}
		} else {
			STATUS("Using OpenCL platform %i (%i total)\n",
			       iplat, nplat);
			have_ctx = 1;
			break;
		}
	}

	if ( !have_ctx ) {
		ERROR("Couldn't find a GPU device in any platform.\n");
		return NULL;
	}

	dev = get_cl_dev(gctx->ctx, dev_num);

	gctx->cq = clCreateCommandQueue(gctx->ctx, dev, 0, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't create OpenCL command queue\n");
		free(gctx);
		return NULL;
	}

	/* Create a single-precision version of the scattering factors */
	intensities_size = IDIM*IDIM*IDIM*sizeof(cl_float);
	intensities_ptr = malloc(intensities_size);
	if ( intensities != NULL ) {
		int i;
		for ( i=0; i<IDIM*IDIM*IDIM; i++ ) {
			intensities_ptr[i] = intensities[i];
		}
	} else {
		int i;
		for ( i=0; i<IDIM*IDIM*IDIM; i++ ) {
			intensities_ptr[i] = 100.0;  /* Does nothing */
		}
		strncat(cflags, "-DFLAT_INTENSITIES ", 511-strlen(cflags));
	}
	gctx->intensities = clCreateBuffer(gctx->ctx,
	                             CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
	                             intensities_size, intensities_ptr, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't allocate intensities memory\n");
		free(gctx);
		return NULL;
	}
	free(intensities_ptr);

	if ( sym != NULL ) {

		int i, n;
		SymOpList *pg;
		size_t islen = 0;

		insert_stuff = malloc(16384);
		if ( insert_stuff == NULL ) return NULL;
		insert_stuff[0] = '\0';

		pg = get_pointgroup(sym);
		n = num_equivs(pg, NULL);
		for ( i=0; i<n; i++ ) {

			IntegerMatrix *op = get_symop(pg, NULL, i);
			char line[1024];

			snprintf(line, 1023,
			         "val += lookup_flagged_intensity(intensities, "
			         "flags, %s, %s, %s);\n\t",
			         get_matrix_name(op, 0),
				 get_matrix_name(op, 1),
				 get_matrix_name(op, 2));

			islen += strlen(line);
			if ( islen > 16383 ) {
				ERROR("Too many symmetry operators.\n");
				return NULL;
			}
			strcat(insert_stuff, line);

		}

		free_symoplist(pg);

		printf("Inserting --->%s<---\n", insert_stuff);

	} else {
		if ( intensities != NULL ) {
			ERROR("You gave me an intensities file but no point"
			      " group.  I'm assuming '1'.\n");
			strncat(cflags, "-DPG1 ", 511-strlen(cflags));
		}
	}

	/* Create a flag array */
	flags_size = IDIM*IDIM*IDIM*sizeof(cl_float);
	flags_ptr = malloc(flags_size);
	if ( flags != NULL ) {
		int i;
		for ( i=0; i<IDIM*IDIM*IDIM; i++ ) {
			flags_ptr[i] = flags[i];
		}
	} else {
		int i;
		for ( i=0; i<IDIM*IDIM*IDIM; i++ ) {
			flags_ptr[i] = 1.0;
		}
	}
	gctx->flags = clCreateBuffer(gctx->ctx,
	                             CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
	                             flags_size, flags_ptr, &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't allocate flag buffer\n");
		free(gctx);
		return NULL;
	}
	free(flags_ptr);

	gctx->prog = load_program_from_string((char *)data_diffraction_cl,
	                                      data_diffraction_cl_len, gctx->ctx,
	                                      dev, &err, cflags, insert_stuff);
	if ( err != CL_SUCCESS ) {
		free(gctx);
		return NULL;
	}

	gctx->kern = clCreateKernel(gctx->prog, "diffraction", &err);
	if ( err != CL_SUCCESS ) {
		ERROR("Couldn't create kernel\n");
		free(gctx);
		return NULL;
	}

	gctx->max_sinc_lut = 0;
	gctx->sinc_lut_ptrs = NULL;
	gctx->sinc_luts = NULL;

	clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_GROUP_SIZE,
	                sizeof(size_t), &maxwgsize, NULL);
	STATUS("Maximum work group size = %lli\n", (long long int)maxwgsize);

	return gctx;
}


void cleanup_gpu(struct gpu_context *gctx)
{
	int i;

	clReleaseProgram(gctx->prog);
	clReleaseMemObject(gctx->intensities);

	/* Release LUTs */
	for ( i=1; i<=gctx->max_sinc_lut; i++ ) {
		if ( gctx->sinc_lut_ptrs[i-1] != NULL ) {
			clReleaseMemObject(gctx->sinc_luts[i-1]);
			free(gctx->sinc_lut_ptrs[i-1]);
		}
	}

	free(gctx->sinc_luts);
	free(gctx->sinc_lut_ptrs);

	clReleaseCommandQueue(gctx->cq);
	clReleaseContext(gctx->ctx);
	free(gctx);
}
