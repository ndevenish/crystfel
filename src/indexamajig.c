/*
 * indexamajig.c
 *
 * Find hits, index patterns, output hkl+intensity etc.
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <hdf5.h>

#include "utils.h"
#include "hdf5-file.h"
#include "index.h"
#include "intensities.h"
#include "ewald.h"
#include "peaks.h"
#include "diffraction.h"
#include "detector.h"
#include "sfac.h"
#include "filters.h"


static void show_help(const char *s)
{
	printf("Syntax: %s [options]\n\n", s);
	printf(
"Process and index FEL diffraction images.\n"
"\n"
"  -h, --help              Display this help message.\n"
"\n"
"  -i, --input=<filename>  Specify file containing list of images to process.\n"
"                           '-' means stdin, which is the default.\n"
"      --indexing=<method> Use 'method' for indexing.  Choose from:\n"
"                           none     : no indexing\n"
"                           dirax    : invoke DirAx\n"
"      --write-drx         Write 'xfel.drx' for visualisation of reciprocal\n"
"                           space.  Implied by any indexing method other than"
"                           'none'.\n"
"      --dump-peaks        Write the results of the peak search to stdout.\n"
"      --near-bragg        Output a list of reflection intensities to stdout.\n"
"      --simulate          Simulate the diffraction pattern using the indexed\n"
"                           unit cell.\n"
"      --clean-image       Perform common-mode noise subtraction and\n"
"                           background removal on images before proceeding.\n"
"\n");
}


int main(int argc, char *argv[])
{
	int c;
	char *filename = NULL;
	FILE *fh;
	char *rval;
	int n_images;
	int n_hits;
	int config_noindex = 0;
	int config_dumpfound = 0;
	int config_nearbragg = 0;
	int config_writedrx = 0;
	int config_simulate = 0;
	int config_clean = 0;
	IndexingMethod indm;
	char *indm_str = NULL;

	/* Long options */
	const struct option longopts[] = {
		{"help",               0, NULL,               'h'},
		{"input",              1, NULL,               'i'},
		{"no-index",           0, &config_noindex,     1},
		{"dump-peaks",         0, &config_dumpfound,   1},
		{"near-bragg",         0, &config_nearbragg,   1},
		{"write-drx",          0, &config_writedrx,    1},
		{"indexing",           1, NULL,               'z'},
		{"simulate",           0, &config_simulate,    1},
		{"clean-image",        0, &config_clean,       1},
		{0, 0, NULL, 0}
	};

	/* Short options */
	while ((c = getopt_long(argc, argv, "hi:w", longopts, NULL)) != -1) {

		switch (c) {
		case 'h' : {
			show_help(argv[0]);
			return 0;
		}

		case 'i' : {
			filename = strdup(optarg);
			break;
		}

		case 'z' : {
			indm_str = strdup(optarg);
			break;
		}

		case 0 : {
			break;
		}

		default : {
			return 1;
		}
		}

	}

	if ( filename == NULL ) {
		filename = strdup("-");
	}
	if ( strcmp(filename, "-") == 0 ) {
		fh = stdin;
	} else {
		fh = fopen(filename, "r");
	}
	free(filename);
	if ( fh == NULL ) {
		ERROR("Failed to open input file\n");
		return 1;
	}

	if ( indm_str == NULL ) {
		STATUS("You didn't specify an indexing method, so I won't"
		       " try to index anything.  If that isn't what you\n"
		       " wanted, re-run with --indexing=<method>.\n");
		indm = INDEXING_NONE;
	} else if ( strcmp(indm_str, "none") == 0 ) {
		indm = INDEXING_NONE;
	} else if ( strcmp(indm_str, "dirax") == 0) {
		indm = INDEXING_DIRAX;
	} else {
		ERROR("Unrecognised indexing method '%s'\n", indm_str);
		return 1;
	}

	n_images = 0;
	n_hits = 0;
	do {

		char line[1024];
		struct hdfile *hdfile;
		struct image image;

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) continue;
		chomp(line);

		image.features = NULL;
		image.molecule = load_molecule();
		image.data = NULL;
		image.indexed_cell = NULL;

		/* Set up detector configuration */
		image.det.n_panels = 2;
		image.det.panels = malloc(2*sizeof(struct panel));
		/* Upper panel */
		image.det.panels[0].min_x = 0;
		image.det.panels[0].max_x = 1023;
		image.det.panels[0].min_y = 512;
		image.det.panels[0].max_y = 1023;
		image.det.panels[0].cx = 491.9;
		image.det.panels[0].cy = 440.7;
		/* Lower panel */
		image.det.panels[1].min_x = 0;
		image.det.panels[1].max_x = 1023;
		image.det.panels[1].min_y = 0;
		image.det.panels[1].max_y = 511;
		image.det.panels[1].cx = 492.0;
		image.det.panels[1].cy = 779.7;

		STATUS("Processing '%s'\n", line);

		n_images++;

		hdfile = hdfile_open(line);
		if ( hdfile == NULL ) {
			continue;
		} else if ( hdfile_set_first_image(hdfile, "/") ) {
			ERROR("Couldn't select path\n");
			continue;
		}

		hdf5_read(hdfile, &image);

		if ( config_clean ) {
			clean_image(&image);
		}

		/* Perform 'fine' peak search */
		search_peaks(&image);

		if ( image_feature_count(image.features) > 5 ) {

			if ( config_dumpfound ) dump_peaks(&image);

			/* Not indexing nor writing xfel.drx?
			 * Then there's nothing left to do. */
			if ( (!config_writedrx) && (indm == INDEXING_NONE) ) {
				goto done;
			}

			/* Calculate orientation matrix (by magic) */
			if ( config_writedrx || (indm != INDEXING_NONE) ) {
				index_pattern(&image, indm);
			}

			/* No cell at this point?  Then we're done. */
			if ( image.indexed_cell == NULL ) goto done;

			n_hits++;

			/* Simulation or intensity measurements both require
			 * Ewald sphere vectors */
			if ( config_nearbragg || config_simulate ) {

				/* Simulate a diffraction pattern */
				image.sfacs = NULL;
				image.qvecs = NULL;
				image.twotheta = NULL;
				image.hdr = NULL;

				/* View head-on (unit cell is tilted) */
				image.orientation.w = 1.0;
				image.orientation.x = 0.0;
				image.orientation.y = 0.0;
				image.orientation.z = 0.0;
				get_ewald(&image);

			}

			/* Measure intensities if requested */
			if ( config_nearbragg ) {
				output_intensities(&image, image.indexed_cell);
			}

			/* Simulate pattern if requested */
			if ( config_simulate ) {

				image.data = NULL;

				get_diffraction(&image, 8, 8, 8);
				if ( image.molecule == NULL ) {
					ERROR("Couldn't open molecule.pdb\n");
					return 1;
				}
				record_image(&image, 0, 0, 0);

				hdf5_write("simulated.h5", image.data,
				           image.width, image.height);

			}

		}

done:
		free(image.data);
		image_feature_list_free(image.features);
		hdfile_close(hdfile);
		H5close();

	} while ( rval != NULL );

	fclose(fh);

	STATUS("There were %i images.\n", n_images);
	STATUS("%i hits were found.\n", n_hits);

	return 0;
}
