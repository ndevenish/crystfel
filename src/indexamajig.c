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
#include "peaks.h"
#include "diffraction.h"
#include "diffraction-gpu.h"
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
"\n"
"      --verbose           Be verbose about indexing.\n"
"      --gpu               Use the GPU to speed up the simulation.\n"
"\n"
"      --write-drx         Write 'xfel.drx' for visualisation of reciprocal\n"
"                           space.  Implied by any indexing method other than\n"
"                           'none'.  Beware: the units in this file are\n"
"                           reciprocal Angstroms.\n"
"      --dump-peaks        Write the results of the peak search to stdout.\n"
"      --near-bragg        Output a list of reflection intensities to stdout.\n"
"      --simulate          Simulate the diffraction pattern using the indexed\n"
"                           unit cell.\n"
"      --filter-cm         Perform common-mode noise subtraction on images\n"
"                           before proceeding.  Intensities will be extracted\n"
"                           from the image as it is after this processing.\n"
"      --filter-noise      Apply an aggressive noise filter which sets all\n"
"                           pixels in each 3x3 region to zero if any of them\n"
"                           have negative values.  Intensity measurement will\n"
"                           be performed on the image as it was before this.\n"
"      --no-match          Don't attempt to match the indexed cell to the\n"
"                           model, just proceed with the one generated by the\n"
"                           auto-indexing procedure.\n"
);
}


static struct image *get_simage(struct image *template, int alternate)
{
	struct image *image;
	struct panel panels[2];

	image = malloc(sizeof(*image));

	/* Simulate a diffraction pattern */
	image->twotheta = NULL;
	image->data = NULL;
	image->det = template->det;

	/* View head-on (unit cell is tilted) */
	image->orientation.w = 1.0;
	image->orientation.x = 0.0;
	image->orientation.y = 0.0;
	image->orientation.z = 0.0;

	/* Detector geometry for the simulation
	 * - not necessarily the same as the original. */
	image->width = 1024;
	image->height = 1024;
	image->det.n_panels = 2;

	if ( alternate ) {

		/* Upper */
		panels[0].min_x = 0;
		panels[0].max_x = 1023;
		panels[0].min_y = 512;
		panels[0].max_y = 1023;
		panels[0].cx = 523.6;
		panels[0].cy = 502.5;
		panels[0].clen = 56.4e-2;  /* 56.4 cm */
		panels[0].res = 13333.3;   /* 75 microns/pixel */

		/* Lower */
		panels[1].min_x = 0;
		panels[1].max_x = 1023;
		panels[1].min_y = 0;
		panels[1].max_y = 511;
		panels[1].cx = 520.8;
		panels[1].cy = 772.1;
		panels[1].clen = 56.7e-2;  /* 56.7 cm */
		panels[1].res = 13333.3;   /* 75 microns/pixel */

		image->det.panels = panels;

	} else {

		/* Copy pointer to old geometry */
		image->det.panels = template->det.panels;

	}

	image->lambda = ph_en_to_lambda(eV_to_J(1.8e3));

	image->molecule = copy_molecule(template->molecule);
	free(image->molecule->cell);
	image->molecule->cell = cell_new_from_cell(template->indexed_cell);

	return image;
}


static void simulate_and_write(struct image *simage,
                               struct gpu_context **gctx)
{
	/* Set up GPU if necessary */
	if ( (gctx != NULL) && (*gctx == NULL) ) {
		*gctx = setup_gpu(0, simage, simage->molecule);
	}

	if ( (gctx != NULL) && (*gctx != NULL) ) {
		get_diffraction_gpu(*gctx, simage, 24, 24, 40);
	} else {
		get_diffraction(simage, 24, 24, 40, 0, 0);
	}
	if ( simage->molecule == NULL ) {
		ERROR("Couldn't open molecule.pdb\n");
		return;
	}
	record_image(simage, 0);

	hdf5_write("simulated.h5", simage->data, simage->width, simage->height,
		   H5T_NATIVE_FLOAT);
}


int main(int argc, char *argv[])
{
	int c;
	struct gpu_context *gctx = NULL;
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
	int config_cmfilter = 0;
	int config_noisefilter = 0;
	int config_nomatch = 0;
	int config_gpu = 0;
	int config_verbose = 0;
	int config_alternate = 0;
	IndexingMethod indm;
	char *indm_str = NULL;
	struct image image;

	/* Long options */
	const struct option longopts[] = {
		{"help",               0, NULL,               'h'},
		{"input",              1, NULL,               'i'},
		{"gpu",                0, &config_gpu,         1},
		{"no-index",           0, &config_noindex,     1},
		{"dump-peaks",         0, &config_dumpfound,   1},
		{"near-bragg",         0, &config_nearbragg,   1},
		{"write-drx",          0, &config_writedrx,    1},
		{"indexing",           1, NULL,               'z'},
		{"simulate",           0, &config_simulate,    1},
		{"filter-cm",          0, &config_cmfilter,    1},
		{"filter-noise",       0, &config_noisefilter, 1},
		{"no-match",           0, &config_nomatch,     1},
		{"verbose",            0, &config_verbose,     1},
		{"alternate",          0, &config_alternate,   1},
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
		       " try to index anything.\n"
		       "If that isn't what you wanted, re-run with"
		       " --indexing=<method>.\n");
		indm = INDEXING_NONE;
	} else if ( strcmp(indm_str, "none") == 0 ) {
		indm = INDEXING_NONE;
	} else if ( strcmp(indm_str, "dirax") == 0) {
		indm = INDEXING_DIRAX;
	} else {
		ERROR("Unrecognised indexing method '%s'\n", indm_str);
		return 1;
	}
	free(indm_str);

	image.molecule = load_molecule();
	if ( image.molecule == NULL ) {
		ERROR("You must provide molecule.pdb in the working "
		      "directory.\n");
		return 1;
	}

	n_images = 0;
	n_hits = 0;
	do {

		char line[1024];
		struct hdfile *hdfile;
		struct image *simage;

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) continue;
		chomp(line);

		image.features = NULL;
		image.data = NULL;
		image.indexed_cell = NULL;

		#include "geometry-lcls.tmp"

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

		if ( config_cmfilter ) {
			filter_cm(&image);
		}

		/* Perform 'fine' peak search */
		search_peaks(&image);

		if ( image_feature_count(image.features) < 5 ) goto done;

		if ( config_dumpfound ) dump_peaks(&image);

		/* Not indexing nor writing xfel.drx?
		 * Then there's nothing left to do. */
		if ( (!config_writedrx) && (indm == INDEXING_NONE) ) {
			goto done;
		}

		/* Calculate orientation matrix (by magic) */
		if ( config_writedrx || (indm != INDEXING_NONE) ) {
			index_pattern(&image, indm, config_nomatch,
			              config_verbose);
		}

		/* No cell at this point?  Then we're done. */
		if ( image.indexed_cell == NULL ) goto done;

		n_hits++;

		simage = get_simage(&image, config_alternate);

		/* Measure intensities if requested */
		if ( config_nearbragg ) {
			/* Use original data (temporarily) */
			simage->data = image.data;
			output_intensities(simage, image.indexed_cell);
			simage->data = NULL;
		}

		/* Simulate if requested */
		if ( config_simulate ) {
			if ( config_gpu ) {
				simulate_and_write(simage, &gctx);
			} else {
				simulate_and_write(simage, NULL);
			}
		}

		/* Finished with alternate image */
		if ( simage->twotheta != NULL ) free(simage->twotheta);
		if ( simage->data != NULL ) free(simage->data);
		free_molecule(simage->molecule);
		free(simage);

		/* Only free cell if found */
		free(image.indexed_cell);

done:
		free(image.data);
		free(image.det.panels);
		image_feature_list_free(image.features);
		hdfile_close(hdfile);
		H5close();

	} while ( rval != NULL );

	fclose(fh);
	free_molecule(image.molecule);

	STATUS("There were %i images.\n", n_images);
	STATUS("%i hits were found.\n", n_hits);

	if ( gctx != NULL ) {
		cleanup_gpu(gctx);
	}

	return 0;
}
