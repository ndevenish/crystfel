/*
 * rational.c
 *
 * A small rational number library
 *
 * Copyright © 2019 Deutsches Elektronen-Synchrotron DESY,
 *                  a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2019 Thomas White <taw@physics.org>
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
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include "rational.h"
#include "integer_matrix.h"
#include "utils.h"


/**
 * SECTION:rational
 * @short_description: Rational numbers
 * @title: Rational numbers
 * @section_id:
 * @see_also:
 * @include: "rational.h"
 * @Image:
 *
 * A rational number library
 */


/* Eucliden algorithm for finding greatest common divisor */
static signed int gcd(signed int a, signed int b)
{
	while ( b != 0 ) {
		signed int t = b;
		b = a % b;
		a = t;
	}
	return a;
}


static void squish(Rational *rt)
{
	signed int g;

	if ( rt->num == 0 ) {
		rt->den = 1;
		return;
	}

	g = gcd(rt->num, rt->den);
	assert(g != 0);
	rt->num /= g;
	rt->den /= g;

	if ( rt->den < 0 ) {
		rt->num = -rt->num;
		rt->den = -rt->den;
	}
}


Rational rtnl_zero()
{
	Rational r;
	r.num = 0;
	r.den = 1;
	return r;
}


Rational rtnl(signed long long int num, signed long long int den)
{
	Rational r;
	r.num = num;
	r.den = den;
	squish(&r);
	return r;
}


double rtnl_as_double(Rational r)
{
	return (double)r.num/r.den;
}


static void overflow(long long int c, long long int a, long long int b)
{
	setlocale(LC_ALL, "");
	ERROR("Overflow detected in rational number library.\n");
	ERROR("%'lli < %'lli * %'lli\n", c, a, b);
	abort();
}


static void check_overflow(long long int c, long long int a, long long int b)
{
	if ( (a==0) || (b==0) ) {
		if ( c != 0 ) overflow(c,a,b);
	} else if ( (llabs(c) < llabs(a)) || (llabs(c) < llabs(b)) ) {
		overflow(c,a,b);
	}
}


Rational rtnl_mul(Rational a, Rational b)
{
	Rational r;
	r.num = a.num * b.num;
	r.den = a.den * b.den;
	check_overflow(r.num, a.num, b.num);
	check_overflow(r.den, a.den, b.den);
	squish(&r);
	return r;
}


Rational rtnl_div(Rational a, Rational b)
{
	signed int t = b.num;
	b.num = b.den;
	b.den = t;
	return rtnl_mul(a, b);
}


Rational rtnl_add(Rational a, Rational b)
{
	Rational r, trt1, trt2;

	trt1.num = a.num * b.den;
	trt2.num = b.num * a.den;
	check_overflow(trt1.num, a.num, b.den);
	check_overflow(trt2.num, b.num, a.den);

	trt1.den = a.den * b.den;
	trt2.den = trt1.den;
	check_overflow(trt1.den, a.den, b.den);

	r.num = trt1.num + trt2.num;
	r.den = trt1.den;
	squish(&r);
	return r;
}


Rational rtnl_sub(Rational a, Rational b)
{
	b.num = -b.num;
	return rtnl_add(a, b);
}


/* -1, 0 +1 respectively for a<b, a==b, a>b */
signed int rtnl_cmp(Rational a, Rational b)
{
	Rational trt1, trt2;

	trt1.num = a.num * b.den;
	trt2.num = b.num * a.den;

	trt1.den = a.den * b.den;
	trt2.den = a.den * b.den;

	if ( trt1.num > trt2.num ) return +1;
	if ( trt1.num < trt2.num ) return -1;
	return 0;
}


Rational rtnl_abs(Rational a)
{
	Rational r = a;
	squish(&r);
	if ( r.num < 0 ) r.num = -r.num;
	return r;
}


/**
 * rtnl_format
 * @rt: A %Rational
 *
 * Formats @rt as a string
 *
 * Returns: a string which should be freed by the caller
 */
char *rtnl_format(Rational rt)
{
	char *v = malloc(32);
	if ( v == NULL ) return NULL;
	if ( rt.den == 1 ) {
		snprintf(v, 31, "%lli", rt.num);
	} else {
		snprintf(v, 31, "%lli/%lli", rt.num, rt.den);
	}
	return v;
}


/**
 * SECTION:rational_matrix
 * @short_description: Rational matrices
 * @title: Rational matrices
 * @section_id:
 * @see_also:
 * @include: "rational.h"
 * @Image:
 *
 * A rational matrix library
 */


struct _rationalmatrix
{
	unsigned int rows;
	unsigned int cols;
	Rational *v;
};


/**
 * rtnl_mtx_new:
 * @rows: Number of rows that the new matrix is to have
 * @cols: Number of columns that the new matrix is to have
 *
 * Allocates a new %RationalMatrix with all elements set to zero.
 *
 * Returns: a new %RationalMatrix, or NULL on error.
 **/
RationalMatrix *rtnl_mtx_new(unsigned int rows, unsigned int cols)
{
	RationalMatrix *m;

	m = malloc(sizeof(RationalMatrix));
	if ( m == NULL ) return NULL;

	m->v = calloc(rows*cols, sizeof(Rational));
	if ( m->v == NULL ) {
		free(m);
		return NULL;
	}

	m->rows = rows;
	m->cols = cols;

	return m;
}


RationalMatrix *rtnl_mtx_copy(const RationalMatrix *m)
{
	RationalMatrix *n;
	int i;

	n = rtnl_mtx_new(m->rows, m->cols);
	if ( n == NULL ) return NULL;

	for ( i=0; i<m->rows*m->cols; i++ ) {
		n->v[i] = m->v[i];
	}

	return n;
}


Rational rtnl_mtx_get(const RationalMatrix *m, int i, int j)
{
	assert(m != NULL);
	return m->v[j+m->cols*i];
}


void rtnl_mtx_set(const RationalMatrix *m, int i, int j, Rational v)
{
	assert(m != NULL);
	m->v[j+m->cols*i] = v;
}


RationalMatrix *rtnl_mtx_from_intmat(const IntegerMatrix *m)
{
	RationalMatrix *n;
	unsigned int rows, cols;
	int i, j;

	intmat_size(m, &rows, &cols);
	n = rtnl_mtx_new(rows, cols);
	if ( n == NULL ) return NULL;

	for ( i=0; i<rows; i++ ) {
		for ( j=0; j<cols; j++ ) {
			n->v[j+cols*i] = rtnl(intmat_get(m, i, j), 1);
		}
	}

	return n;
}


void rtnl_mtx_free(RationalMatrix *mtx)
{
	if ( mtx == NULL ) return;
	free(mtx->v);
	free(mtx);
}


/* rtnl_mtx_solve:
 * @m: A %RationalMatrix
 * @vec: An array of %Rational
 * @ans: An array of %Rational in which to store the result
 *
 * Solves the matrix equation m*ans = vec, where @ans and @vec are
 * vectors of %Rational.
 *
 * In this version, @m must be square.
 *
 * The number of columns in @m must equal the length of @ans, and the number
 * of rows in @m must equal the length of @vec, but note that there is no way
 * for this function to check that this is the case.
 *
 * Returns: non-zero on error
 **/
int rtnl_mtx_solve(const RationalMatrix *m, const Rational *ivec, Rational *ans)
{
	RationalMatrix *cm;
	Rational *vec;
	int h, k;
	int i;

	if ( m->rows != m->cols ) return 1;

	/* Copy the matrix and vector because the calculation will
	 * be done in-place */
	cm = rtnl_mtx_copy(m);
	if ( cm == NULL ) return 1;

	vec = malloc(m->rows*sizeof(Rational));
	if ( vec == NULL ) return 1;
	for ( h=0; h<m->rows; h++ ) vec[h] = ivec[h];

	/* Gaussian elimination with partial pivoting */
	h = 0;
	k = 0;
	while ( h<=m->rows && k<=m->cols ) {

		int prow = 0;
		Rational pval = rtnl_zero();
		Rational t;

		/* Find the row with the largest value in column k */
		for ( i=h; i<m->rows; i++ ) {
			if ( rtnl_cmp(rtnl_abs(rtnl_mtx_get(cm, i, k)), pval) > 0 ) {
				pval = rtnl_abs(rtnl_mtx_get(cm, i, k));
				prow = i;
			}
		}

		if ( rtnl_cmp(pval, rtnl_zero()) == 0 ) {
			k++;
			continue;
		}

		/* Swap 'prow' with row h */
		for ( i=0; i<m->cols; i++ ) {
			t = rtnl_mtx_get(cm, h, i);
			rtnl_mtx_set(cm, h, i, rtnl_mtx_get(cm, prow, i));
			rtnl_mtx_set(cm, prow, i, t);
		}
		t = vec[prow];
		vec[prow] = vec[h];
		vec[h] = t;

		/* Divide and subtract rows below */
		for ( i=h+1; i<m->rows; i++ ) {

			int j;
			Rational dval;

			dval = rtnl_div(rtnl_mtx_get(cm, i, k),
			                rtnl_mtx_get(cm, h, k));

			for ( j=0; j<m->cols; j++ ) {
				Rational t = rtnl_mtx_get(cm, i, j);
				Rational p = rtnl_mul(dval, rtnl_mtx_get(cm, h, j));
				t = rtnl_sub(t, p);
				rtnl_mtx_set(cm, i, j, t);
			}

			/* Divide the right hand side as well */
			Rational t = vec[i];
			Rational p = rtnl_mul(dval, vec[h]);
			vec[i] = rtnl_sub(t, p);
		}

		h++;
		k++;


	}

	/* Back-substitution */
	for ( i=m->rows-1; i>=0; i-- ) {
		int j;
		Rational sum = rtnl_zero();
		for ( j=i+1; j<m->cols; j++ ) {
			Rational av;
			av = rtnl_mul(rtnl_mtx_get(cm, i, j), ans[j]);
			sum = rtnl_add(sum, av);
		}
		sum = rtnl_sub(vec[i], sum);
		ans[i] = rtnl_div(sum, rtnl_mtx_get(cm, i, i));
	}

	free(vec);
	rtnl_mtx_free(cm);

	return 0;
}


/**
 * rtnl_mtx_print
 * @m: A %RationalMatrix
 *
 * Prints @m to stderr.
 *
 */
void rtnl_mtx_print(const RationalMatrix *m)
{
	unsigned int i, j;

	for ( i=0; i<m->rows; i++ ) {

		fprintf(stderr, "[ ");
		for ( j=0; j<m->cols; j++ ) {
			char *v = rtnl_format(m->v[j+m->cols*i]);
			fprintf(stderr, "%4s ", v);
			free(v);
		}
		fprintf(stderr, "]\n");
	}
}


void rtnl_mtx_mult(const RationalMatrix *m, const Rational *vec, Rational *ans)
{
	int i, j;

	for ( i=0; i<m->rows; i++ ) {
		ans[i] = rtnl_zero();
		for ( j=0; j<m->cols; j++ ) {
			Rational add;
			add = rtnl_mul(rtnl_mtx_get(m, i, j), vec[j]);
			ans[i] = rtnl_add(ans[i], add);
		}
	}
}


static RationalMatrix *delete_row_and_column(const RationalMatrix *m,
                                             unsigned int di, unsigned int dj)
{
	RationalMatrix *n;
	unsigned int i, j;

	n = rtnl_mtx_new(m->rows-1, m->cols-1);
	if ( n == NULL ) return NULL;

	for ( i=0; i<n->rows; i++ ) {
	for ( j=0; j<n->cols; j++ ) {

		Rational val;
		unsigned int gi, gj;

		gi = (i>=di) ? i+1 : i;
		gj = (j>=dj) ? j+1 : j;
		val = rtnl_mtx_get(m, gi, gj);
		rtnl_mtx_set(n, i, j, val);

	}
	}

	return n;
}


static Rational cofactor(const RationalMatrix *m,
                         unsigned int i, unsigned int j)
{
	RationalMatrix *n;
	Rational t, C;

	n = delete_row_and_column(m, i, j);
	if ( n == NULL ) {
		fprintf(stderr, "Failed to allocate matrix.\n");
		return rtnl_zero();
	}

	/* -1 if odd, +1 if even */
	t = (i+j) & 0x1 ? rtnl(-1, 1) : rtnl(1, 1);

	C = rtnl_mul(t, rtnl_mtx_det(n));
	rtnl_mtx_free(n);

	return C;
}



Rational rtnl_mtx_det(const RationalMatrix *m)
{
	unsigned int i, j;
	Rational det;

	assert(m->rows == m->cols);  /* Otherwise determinant doesn't exist */

	if ( m->rows == 2 ) {
		Rational a, b;
		a = rtnl_mul(rtnl_mtx_get(m, 0, 0), rtnl_mtx_get(m, 1, 1));
		b = rtnl_mul(rtnl_mtx_get(m, 0, 1), rtnl_mtx_get(m, 1, 0));
		return rtnl_sub(a, b);
	}

	i = 0;  /* Fixed */
	det = rtnl_zero();
	for ( j=0; j<m->cols; j++ ) {
		Rational a;
		a = rtnl_mul(rtnl_mtx_get(m, i, j), cofactor(m, i, j));
		det = rtnl_add(det, a);
	}

	return det;
}
