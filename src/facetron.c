/*
 * facetron.c
 *
 * Profile fitting for coherent nanocrystallography
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
#include <assert.h>

#include "utils.h"
#include "hdf5-file.h"
#include "symmetry.h"
#include "reflections.h"
#include "stream.h"
#include "geometry.h"
#include "peaks.h"
#include "thread-pool.h"


static void show_help(const char *s)
{
	printf("Syntax: %s [options]\n\n", s);
	printf(
"Post-refinement and profile fitting for coherent nanocrystallography.\n"
"\n"
"  -h, --help                 Display this help message.\n"
"\n"
"  -i, --input=<filename>     Specify the name of the input 'stream'.\n"
"                              (must be a file, not e.g. stdin)\n"
"  -o, --output=<filename>    Output filename.  Default: facetron.hkl.\n"
"  -g. --geometry=<file>      Get detector geometry from file.\n"
"  -x, --prefix=<p>           Prefix filenames from input file with <p>.\n"
"      --basename             Remove the directory parts of the filenames.\n"
"      --no-check-prefix      Don't attempt to correct the --prefix.\n"
"  -y, --symmetry=<sym>       Merge according to symmetry <sym>.\n"
"  -n, --iterations=<n>       Run <n> cycles of post-refinement.\n"
"\n"
"  -j <n>                     Run <n> analyses in parallel.\n");
}


struct refine_args
{
	const char *sym;
	ReflItemList *obs;
	double *i_full;
	struct image *image;
};


static void refine_image(int mytask, void *tasks)
{
	struct refine_args *all_args = tasks;
	struct refine_args *pargs = &all_args[mytask];
	/* Do, er, something. */
}


struct integrate_args
{
	const char *sym;
	ReflItemList *obs;
	double *i_full;
	unsigned int *cts;
	pthread_mutex_t *list_lock;
	struct image *image;
};


static void integrate_image(int mytask, void *tasks)
{
	struct integrate_args *all_args = tasks;
	struct integrate_args *pargs = &all_args[mytask];
	struct reflhit *spots;
	int j, n;
	struct hdfile *hdfile;
	struct image *image = pargs->image;

	hdfile = hdfile_open(image->filename);
	if ( hdfile == NULL ) {
		ERROR("Couldn't open '%s'\n", image->filename);
		return;
	} else if ( hdfile_set_image(hdfile, "/data/data0") ) {
		ERROR("Couldn't select path\n");
		hdfile_close(hdfile);
		return;
	}

	if ( hdf5_read(hdfile, pargs->image, 0) ) {
		ERROR("Couldn't read '%s'\n", image->filename);
		hdfile_close(hdfile);
		return;
	}

	/* Figure out which spots should appear in this pattern */
	spots = find_intersections(image, image->indexed_cell,
	                           image->div, image->bw, &n, 0);

	/* For each reflection, estimate the partiality */
	for ( j=0; j<n; j++ ) {

		signed int h, k, l;
		float i_partial;
		double p;
		float xc, yc;

		h = spots[j].h;
		k = spots[j].k;
		l = spots[j].l;

		/* Calculated partiality of this spot in this pattern */
		p = partiality(image, h, k, l);

		/* Don't attempt to use spots with very small
		 * partialities, since it won't be accurate. */
		if ( p < 0.1 ) continue;

		/* Actual measurement of this reflection from this
		 * pattern? */
		/* FIXME: Coordinates aren't whole numbers */
		if ( integrate_peak(image, spots[j].x, spots[j].y,
		                    &xc, &yc, &i_partial, 1, 1) ) continue;

		pthread_mutex_lock(pargs->list_lock);
		integrate_intensity(pargs->i_full, h, k, l, i_partial);
		integrate_count(pargs->cts, h, k, l, 1);
		if ( !find_item(pargs->obs, h, k, l) ) {
			add_item(pargs->obs, h, k, l);
		}
		pthread_mutex_unlock(pargs->list_lock);

	}

	free(image->data);
	if ( image->flags != NULL ) free(image->flags);
	hdfile_close(hdfile);
	free(spots);

	/* Muppet proofing */
	image->data = NULL;
	image->flags = NULL;
}


static void refine_all(struct image *images, int n_total_patterns,
                       struct detector *det, const char *sym,
                       ReflItemList *obs, double *i_full, int nthreads)
{
	struct refine_args *tasks;
	int i;

	tasks = malloc(n_total_patterns * sizeof(struct refine_args));
	for ( i=0; i<n_total_patterns; i++ ) {

		tasks[i].sym = sym;
		tasks[i].obs = obs;
		tasks[i].i_full = i_full;
		tasks[i].image = &images[i];

	}

	munch_threads(n_total_patterns, nthreads, "Refining",
	              refine_image, tasks);

	free(tasks);
}


static void estimate_full(struct image *images, int n_total_patterns,
                          struct detector *det, const char *sym,
                          ReflItemList *obs, double *i_full, int nthreads)
{
	int i;
	unsigned int *cts;
	struct integrate_args *tasks;
	pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

	cts = new_list_count();
	clear_items(obs);

	tasks = malloc(n_total_patterns * sizeof(struct integrate_args));
	for ( i=0; i<n_total_patterns; i++ ) {

		tasks[i].sym = sym;
		tasks[i].obs = obs;
		tasks[i].i_full = i_full;
		tasks[i].cts = cts;
		tasks[i].list_lock = &list_lock;
		tasks[i].image = &images[i];

	}

	munch_threads(n_total_patterns, nthreads, "Integrating",
	              integrate_image, tasks);

	free(tasks);

	/* Divide the totals to get the means */
	for ( i=0; i<num_items(obs); i++ ) {

		struct refl_item *it;
		double total;

		it = get_item(obs, i);
		total = lookup_intensity(i_full, it->h, it->k, it->l);
		total /= lookup_count(cts, it->h, it->k, it->l);
		set_intensity(i_full, it->h, it->k, it->l, total);

	}

	free(cts);
}


int main(int argc, char *argv[])
{
	int c;
	char *infile = NULL;
	char *outfile = NULL;
	char *geomfile = NULL;
	char *prefix = NULL;
	char *sym = NULL;
	FILE *fh;
	int nthreads = 1;
	int config_basename = 0;
	int config_checkprefix = 1;
	struct detector *det;
	double *i_full;
	ReflItemList *obs;
	int i;
	int n_total_patterns;
	struct image *images;
	int n_iter = 10;

	/* Long options */
	const struct option longopts[] = {
		{"help",               0, NULL,               'h'},
		{"input",              1, NULL,               'i'},
		{"output",             1, NULL,               'o'},
		{"geometry",           1, NULL,               'g'},
		{"prefix",             1, NULL,               'x'},
		{"basename",           0, &config_basename,    1},
		{"no-check-prefix",    0, &config_checkprefix, 0},
		{"symmetry",           1, NULL,               'y'},
		{"iterations",         1, NULL,               'n'},
		{0, 0, NULL, 0}
	};

	/* Short options */
	while ((c = getopt_long(argc, argv, "hi:g:x:j:y:o:",
	                        longopts, NULL)) != -1)
	{

		switch (c) {
		case 'h' :
			show_help(argv[0]);
			return 0;

		case 'i' :
			infile = strdup(optarg);
			break;

		case 'g' :
			geomfile = strdup(optarg);
			break;

		case 'x' :
			prefix = strdup(optarg);
			break;

		case 'j' :
			nthreads = atoi(optarg);
			break;

		case 'y' :
			sym = strdup(optarg);
			break;

		case 'o' :
			outfile = strdup(optarg);
			break;

		case 'n' :
			n_iter = atoi(optarg);
			break;

		case 0 :
			break;

		default :
			return 1;
		}

	}

	/* Sanitise input filename and open */
	if ( infile == NULL ) {
		infile = strdup("-");
	}
	if ( strcmp(infile, "-") == 0 ) {
		fh = stdin;
	} else {
		fh = fopen(infile, "r");
	}
	if ( fh == NULL ) {
		ERROR("Failed to open input file '%s'\n", infile);
		return 1;
	}
	free(infile);

	/* Sanitise output filename */
	if ( outfile == NULL ) {
		outfile = strdup("facetron.hkl");
	}

	/* Sanitise prefix */
	if ( prefix == NULL ) {
		prefix = strdup("");
	} else {
		if ( config_checkprefix ) {
			prefix = check_prefix(prefix);
		}
	}

	/* Get detector geometry */
	det = get_detector_geometry(geomfile);
	if ( det == NULL ) {
		ERROR("Failed to read detector geometry from '%s'\n", geomfile);
		return 1;
	}
	free(geomfile);

	/* Prepare for iteration */
	i_full = new_list_intensity();
	obs = new_items();

	n_total_patterns = count_patterns(fh);
	STATUS("There are %i patterns to process\n", n_total_patterns);

	images = malloc(n_total_patterns * sizeof(struct image));
	if ( images == NULL ) {
		ERROR("Couldn't allocate memory for images.\n");
		return 1;
	}

	/* Fill in what we know about the images so far */
	rewind(fh);
	for ( i=0; i<n_total_patterns; i++ ) {

		UnitCell *cell;
		char *filename;
		char *fnamereal;

		if ( find_chunk(fh, &cell, &filename) == 1 ) {
			ERROR("Couldn't get all of the filenames and cells"
			      " from the input stream.\n");
			return 1;
		}

		images[i].indexed_cell = cell;

		/* Mangle the filename now */
		if ( config_basename ) {
			char *tmp;
			tmp = strdup(basename(filename));
			free(filename);
			filename = tmp;
		}
		fnamereal = malloc(1024);
		snprintf(fnamereal, 1023, "%s%s", prefix, filename);

		images[i].filename = fnamereal;
		images[i].div = 0.5e-3;
		images[i].bw = 0.001;
		images[i].orientation.w = 1.0;
		images[i].orientation.x = 0.0;
		images[i].orientation.y = 0.0;
		images[i].orientation.z = 0.0;
		images[i].det = det;

		/* Muppet proofing */
		images[i].data = NULL;
		images[i].flags = NULL;

		free(filename);

		progress_bar(i, n_total_patterns-1, "Loading pattern data");

	}
	fclose(fh);
	free(prefix);

	/* Make initial estimates */
	estimate_full(images, n_total_patterns, det, sym, obs, i_full,
	              nthreads);

	/* Iterate */
	for ( i=0; i<n_iter; i++ ) {

		STATUS("Post refinement iteration %i of %i\n", i+1, n_iter);

		/* Refine the geometry of all patterns to get the best fit */
		refine_all(images, n_total_patterns, det, sym, obs, i_full,
		           nthreads);

		/* Re-estimate all the full intensities */
		estimate_full(images, n_total_patterns, det, sym, obs, i_full,
		              nthreads);

	}

	/* Output results */
	write_reflections(outfile, obs, i_full, NULL, NULL, NULL);

	/* Clean up */
	free(i_full);
	delete_items(obs);
	free(sym);
	free(outfile);
	free(det->panels);
	free(det);
	for ( i=0; i<n_total_patterns; i++ ) {
		cell_free(images[i].indexed_cell);
		free(images[i].filename);
	}
	free(images);

	return 0;
}
