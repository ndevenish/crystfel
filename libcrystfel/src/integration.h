/*
 * integration.h
 *
 * Integration of intensities
 *
 * Copyright © 2012-2021 Deutsches Elektronen-Synchrotron DESY,
 *                  a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2010-2020 Thomas White <taw@physics.org>
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

#ifndef INTEGRATION_H
#define INTEGRATION_H

#include "geometry.h"

/**
 * \file integration.h
 * Integration of reflections
 */

/**
 * An IntDiag describes the condition under which the integration subsystem
 * should display diagnostic information to the user.
 **/
typedef enum {

	/** Never show diagnostics */
	INTDIAG_NONE,

	/** Show diagnostics for a randomly selected 1% of reflections */
	INTDIAG_RANDOM,

	/** Show diagnostics for all reflections */
	INTDIAG_ALL,

	/** Show diagnostics when the Miller indices of the reflection are the
	 * ones specified */
	INTDIAG_INDICES,

	/** Show diagnostics when the measured intensity is less than minus
	 * three times its estimated error. */
	INTDIAG_NEGATIVE,

	/** Show diagnostics when the measured intensity is less than minus five
	 * times its estimated error. */
	INTDIAG_IMPLAUSIBLE,

	/** Show diagnostics when the measured intensity is more than three
	 * times its estimated error. */
	INTDIAG_STRONG

} IntDiag;

#define INTEGRATION_DEFAULTS_RINGS (INTEGRATION_RINGS)
#define INTEGRATION_DEFAULTS_PROF2D (INTEGRATION_PROF2D | INTEGRATION_CENTER)

/**
 * An enumeration of all the available integration methods.  The first items
 * are the actual integration methods.  The later ones are flags which can be
 * ORed with the method to affect its behaviour.
 */
typedef enum {

	/** No integration at all */
	INTEGRATION_NONE   = 0,

	/** Summation of pixel values inside ring, minus background */
	INTEGRATION_RINGS  = 1,

	/** Two dimensional profile fitting */
	INTEGRATION_PROF2D = 2,

	/** Integrate saturated reflections */
	INTEGRATION_SATURATED = 256,

	/** Center the peak in the box prior to integration */
	INTEGRATION_CENTER = 512,

	/* 1024 was INTEGRATION_RESCUT, which is no longer used */

	/** Fit a gradient to the background */
	INTEGRATION_GRADIENTBG = 2048,

} IntegrationMethod;

/** This defines the bits in \ref IntegrationMethod which are used to represent the
 * core of the integration method. */
#define INTEGRATION_METHOD_MASK (0xff)

#ifdef __cplusplus
extern "C" {
#endif

struct intcontext;

extern IntegrationMethod integration_method(const char *t, int *err);

extern char *str_integration_method(IntegrationMethod m);

extern struct intcontext *intcontext_new(struct image *image,
                                         UnitCell *cell,
                                         IntegrationMethod meth,
                                         int ir_inn, int ir_mid, int ir_out,
                                         int **masks);

extern int integrate_rings_once(Reflection *refl,
                                struct intcontext *ic,
                                pthread_mutex_t *term_lock);

extern void intcontext_free(struct intcontext *ic);

extern void integrate_all(struct image *image, IntegrationMethod meth,
                          double ir_inn, double ir_mid, double ir_out,
                          IntDiag int_diag,
                          signed int idh, signed int idk, signed int idl);

extern void integrate_all_2(struct image *image, IntegrationMethod meth,
                            double push_res,
                            double ir_inn, double ir_mid, double ir_out,
                            IntDiag int_diag,
                            signed int idh, signed int idk, signed int idl);

extern void integrate_all_3(struct image *image, IntegrationMethod meth,
                            PartialityModel pmodel, double push_res,
                            double ir_inn, double ir_mid, double ir_out,
                            IntDiag int_diag,
                            signed int idh, signed int idk, signed int idl);

extern void integrate_all_4(struct image *image, IntegrationMethod meth,
                            PartialityModel pmodel, double push_res,
                            double ir_inn, double ir_mid, double ir_out,
                            IntDiag int_diag,
                            signed int idh, signed int idk, signed int idl,
                            pthread_mutex_t *term_lock);

extern void integrate_all_5(struct image *image, IntegrationMethod meth,
                     PartialityModel pmodel, double push_res,
                     double ir_inn, double ir_mid, double ir_out,
                     IntDiag int_diag,
                     signed int idh, signed int idk, signed int idl,
                     pthread_mutex_t *term_lock, int overpredict);

#ifdef __cplusplus
}
#endif

#endif	/* INTEGRATION_H */
