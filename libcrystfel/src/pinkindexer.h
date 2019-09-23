/*
 * pinkindexer.h
 *
 * Interface to PinkIndexer
 *
 * Copyright © 2017-2019 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2017-2019 Yaroslav Gevorkov <yaroslav.gevorkov@desy.de>
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

#ifndef LIBCRYSTFEL_SRC_PINKINDEXER_H_
#define LIBCRYSTFEL_SRC_PINKINDEXER_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

typedef struct pinkIndexer_options PinkIndexerOptions;
extern struct argp pinkIndexer_argp;

struct pinkIndexer_options {
	unsigned int considered_peaks_count;
	unsigned int angle_resolution;
	unsigned int refinement_type;
	float maxResolutionForIndexing_1_per_A;
	float tolerance;
	int multi;
	int thread_count;
	int min_peaks;
	int no_check_indexed;
	float reflectionRadius; /* In m^-1 */
};

#include <stddef.h>
#include "index.h"

extern int run_pinkIndexer(struct image *image, void *ipriv);

extern void *pinkIndexer_prepare(IndexingMethod *indm, UnitCell *cell,
                                 struct pinkIndexer_options *pinkIndexer_opts,
                                 struct detector *det, struct beam_params *beam);

extern void pinkIndexer_cleanup(void *pp);

extern const char *pinkIndexer_probe(UnitCell *cell);

#endif /* LIBCRYSTFEL_SRC_PINKINDEXER_H_ */