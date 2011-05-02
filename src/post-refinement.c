/*
 * post-refinement.c
 *
 * Post refinement
 *
 * (c) 2006-2011 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <stdlib.h>
#include <assert.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_linalg.h>

#include "image.h"
#include "post-refinement.h"
#include "peaks.h"
#include "symmetry.h"
#include "geometry.h"
#include "cell.h"


/* Maximum number of iterations of NLSq to do for each image per macrocycle. */
#define MAX_CYCLES (10)


/* Refineable parameters */
enum {
	REF_ASX,
	NUM_PARAMS,
	REF_BSX,
	REF_CSX,
	REF_ASY,
	REF_BSY,
	REF_CSY,
	REF_ASZ,
	REF_BSZ,
	REF_CSZ,
	REF_DIV,
	REF_R,
};


/* Returns dp/dr at "r" */
static double partiality_gradient(double r, double profile_radius)
{
	double q, dpdq, dqdr;

	/* Calculate degree of penetration */
	q = (r + profile_radius)/(2.0*profile_radius);

	/* dp/dq */
	dpdq = 6.0*(q-pow(q, 2.0));

	/* dq/dr */
	dqdr = 1.0 / (2.0*profile_radius);

	return dpdq * dqdr;
}


/* Returns dp/drad at "r" */
static double partiality_rgradient(double r, double profile_radius)
{
	double q, dpdq, dqdrad;

	/* Calculate degree of penetration */
	q = (r + profile_radius)/(2.0*profile_radius);

	/* dp/dq */
	dpdq = 6.0*(q-pow(q, 2.0));

	/* dq/drad */
	dqdrad = 0.5 * (1.0 - r * pow(profile_radius, -2.0));

	return dpdq * dqdrad;
}


/* Return the gradient of parameter 'k' given the current status of 'image'. */
static double gradient(struct image *image, int k, Reflection *refl, double r)
{
	double ds, tt, azi;
	double nom, den;
	double g = 0.0;
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	double xl, yl, zl;
	signed int hi, ki, li;
	double r1, r2, p;
	int clamp_low, clamp_high;

	get_indices(refl, &hi, &ki, &li);

	cell_get_reciprocal(image->indexed_cell, &asx, &asy, &asz,
	                                         &bsx, &bsy, &bsz,
	                                         &csx, &csy, &csz);
	xl = hi*asx + ki*bsx + li*csx;
	yl = hi*asy + ki*bsy + li*csy;
	zl = hi*asz + ki*bsz + li*csz;

	ds = 2.0 * resolution(image->indexed_cell, hi, ki, li);
	tt = angle_between(0.0, 0.0, 1.0,  xl, yl, zl+k);
	azi = angle_between(1.0, 0.0, 0.0, xl, yl, 0.0);

	get_partial(refl, &r1, &r2, &p, &clamp_low, &clamp_high);

	/* Calculate the gradient of partiality wrt excitation error. */
	if ( clamp_low == 0 ) {
		g += partiality_gradient(r1, r);
	}
	if ( clamp_high == 0 ) {
		g += partiality_gradient(r2, r);
	}

	/* For many gradients, just multiply the above number by the gradient
	 * of excitation error wrt whatever. */
	switch ( k ) {

	case REF_DIV :
		nom = sqrt(2.0) * ds * sin(image->div/2.0);
		den = sqrt(1.0 - cos(image->div/2.0));
		return (nom/den) * g;

	case REF_R :
		if ( !clamp_low ) {
			g += partiality_rgradient(r1, r);
		}
		if ( !clamp_high ) {
			g += partiality_rgradient(r2, r);
		}
		return g;

	/* Cell parameters and orientation */
	case REF_ASX :
		return hi * sin(tt) * cos(azi) * g;
	case REF_BSX :
		return ki * sin(tt) * g;
	case REF_CSX :
		return li * sin(tt) * g;
	case REF_ASY :
		return hi * sin(tt) * g;
	case REF_BSY :
		return ki * sin(tt) * g;
	case REF_CSY :
		return li * sin(tt) * g;
	case REF_ASZ :
		return hi * cos(tt) * g;
	case REF_BSZ :
		return ki * cos(tt) * g;
	case REF_CSZ :
		return li * cos(tt) * g;

	}

	ERROR("No gradient defined for parameter %i\n", k);
	abort();
}


static void apply_cell_shift(UnitCell *cell, int k, double shift)
{
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;

	cell_get_reciprocal(cell, &asx, &asy, &asz,
	                          &bsx, &bsy, &bsz,
	                          &csx, &csy, &csz);
	switch ( k )
	{
		case REF_ASX :  asx += shift;  break;
		case REF_ASY :  asy += shift;  break;
		case REF_ASZ :  asz += shift;  break;
		case REF_BSX :  bsx += shift;  break;
		case REF_BSY :  bsy += shift;  break;
		case REF_BSZ :  bsz += shift;  break;
		case REF_CSX :  csx += shift;  break;
		case REF_CSY :  csy += shift;  break;
		case REF_CSZ :  csz += shift;  break;
	}

	cell_set_reciprocal(cell, asx, asy, asz,
	                          bsx, bsy, bsz,
	                          csx, csy, csz);

	if ( k == REF_CSZ ) {
		double a, b, c, al, be, ga;
		cell_get_parameters(cell, &a, &b, &c, &al, &be, &ga);
		STATUS("New cell: %5.2f %5.2f %5.2f nm %5.2f %5.2f %5.2f deg\n",
		       a/1.0e-9, b/1.0e-9, c/1.0e-9,
		       rad2deg(al), rad2deg(be), rad2deg(ga));
	}
}


/* Apply the given shift to the 'k'th parameter of 'image'. */
static void apply_shift(struct image *image, int k, double shift)
{
	switch ( k ) {

	case REF_DIV :
		image->div += shift;
		break;

	case REF_R :
		image->profile_radius += shift;
		break;

	case REF_ASX :
	case REF_ASY :
	case REF_ASZ :
	case REF_BSX :
	case REF_BSY :
	case REF_BSZ :
	case REF_CSX :
	case REF_CSY :
	case REF_CSZ :
		apply_cell_shift(image->indexed_cell, k, shift);
		break;

	default :
		ERROR("No shift defined for parameter %i\n", k);
		abort();

	}
}


/* Perform one cycle of post refinement on 'image' against 'full' */
static double pr_iterate(struct image *image, const RefList *full,
                         const char *sym)
{
	gsl_matrix *M;
	gsl_vector *v;
	gsl_vector *shifts;
	int param;
	Reflection *refl;
	RefListIterator *iter;
	RefList *reflections;
	double max_shift;

	reflections = image->reflections;

	M = gsl_matrix_calloc(NUM_PARAMS, NUM_PARAMS);
	v = gsl_vector_calloc(NUM_PARAMS);

	/* Construct the equations, one per reflection in this image */
	for ( refl = first_refl(reflections, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) ) {

		signed int ha, ka, la;
		double I_full, delta_I;
		double I_partial;
		int k;
		double p;
		Reflection *match;
		double gradients[NUM_PARAMS];

		if ( !get_scalable(refl) ) continue;

		/* Find the full version */
		get_indices(refl, &ha, &ka, &la);
		match = find_refl(full, ha, ka, la);
		assert(match != NULL);  /* Never happens because all scalable
		                         * reflections had their LSQ intensities
		                         * calculated in lsq_intensities(). */
		I_full = image->osf * get_intensity(match);

		/* Actual measurement of this reflection from this pattern? */
		I_partial = get_intensity(refl);
		p = get_partiality(refl);
		delta_I = I_partial - (p * I_full);

		/* Calculate all gradients for this reflection */
		for ( k=0; k<NUM_PARAMS; k++ ) {
			double gr;
			gr = gradient(image, k, refl,
			              image->profile_radius);
			gradients[k] = gr;
		}

		for ( k=0; k<NUM_PARAMS; k++ ) {

			int g;
			double v_c, v_curr;
			double gr;

			for ( g=0; g<NUM_PARAMS; g++ ) {

				double M_c, M_curr;

				M_c = gradients[g] * gradients[k];
				M_c *= pow(I_full, 2.0);

				M_curr = gsl_matrix_get(M, g, k);
				gsl_matrix_set(M, g, k, M_curr + M_c);

			}

			gr = gradients[k];
			v_c = delta_I * I_full * gr;
			v_curr = gsl_vector_get(v, k);
			gsl_vector_set(v, k, v_curr + v_c);

		}

	}
	double tg = gsl_matrix_get(M, 0, 0);
	STATUS("total gradient = %e\n", tg);
	//show_matrix_eqn(M, v, NUM_PARAMS);

	shifts = gsl_vector_alloc(NUM_PARAMS);
	gsl_linalg_HH_solve(M, v, shifts);

	max_shift = 0.0;
	for ( param=0; param<NUM_PARAMS; param++ ) {
		double shift = gsl_vector_get(shifts, param);
		apply_shift(image, param, shift);
		if ( fabs(shift) > max_shift ) max_shift = fabs(shift);
	}

	gsl_matrix_free(M);
	gsl_vector_free(v);
	gsl_vector_free(shifts);

	return max_shift;
}


static double mean_partial_dev(struct image *image,
                               const RefList *full, const char *sym)
{
	double dev = 0.0;

	/* For each reflection */
	Reflection *refl;
	RefListIterator *iter;

	for ( refl = first_refl(image->reflections, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) ) {

		double G, p;
		signed int h, k, l;
		Reflection *full_version;
		double I_full, I_partial;

		if ( !get_scalable(refl) ) continue;

		get_indices(refl, &h, &k, &l);
		assert ((h!=0) || (k!=0) || (l!=0));

		full_version = find_refl(full, h, k, l);
		assert(full_version != NULL);

		G = image->osf;
		p = get_partiality(refl);
		I_partial = get_intensity(refl);
		I_full = get_intensity(full_version);
		//STATUS("%3i %3i %3i  %5.2f  %5.2f  %5.2f  %5.2f  %5.2f\n",
		//       h, k, l, G, p, I_partial, I_full,
		//       I_partial - p*G*I_full);

		dev += pow(I_partial - p*G*I_full, 2.0);

	}

	return dev;
}


static void plot_curve(struct image *image, const RefList *full,
                       const char *sym)
{
	double ax, ay, az;
	double bx, by, bz;
	double cx, cy, cz;
	UnitCell *cell = image->indexed_cell;
	double shval, origval;
	int i;

	cell_get_reciprocal(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);
	shval = 0.001*ax;
	origval = ax;

	for ( i=-10; i<=10; i++ ) {

		double dev;

		cell_get_reciprocal(cell, &ax, &ay, &az, &bx,
		                         &by, &bz, &cx, &cy, &cz);
		ax = origval + (double)i*shval;
		cell_set_reciprocal(cell, ax, ay, az, bx, by, bz, cx, cy, cz);

		update_partialities_and_asymm(image, sym,
		                              NULL, NULL, NULL, NULL);

		dev = mean_partial_dev(image, full, sym);
		STATUS("%i %e %e\n", i, ax, dev);

	}
}


void pr_refine(struct image *image, const RefList *full, const char *sym)
{
	double max_shift, dev;
	int i;

	/* FIXME: This is for debugging */
	//plot_curve(image, full, sym);
	//return;

	dev = mean_partial_dev(image, full, sym);
	STATUS("PR starting dev = %5.2f\n", dev);

	i = 0;
	do {

		double dev;

		max_shift = pr_iterate(image, full, sym);
		update_partialities_and_asymm(image, sym,
		                              NULL, NULL, NULL, NULL);

		dev = mean_partial_dev(image, full, sym);
		STATUS("PR Iteration %2i: max shift = %5.2f dev = %5.2f\n",
		       i, max_shift, dev);

		i++;

	} while ( (max_shift > 0.01) && (i < MAX_CYCLES) );
}
