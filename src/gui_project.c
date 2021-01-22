/*
 * gui_project.c
 *
 * GUI project persistence
 *
 * Copyright © 2020 Deutsches Elektronen-Synchrotron DESY,
 *                  a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2020 Thomas White <taw@physics.org>
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "gui_project.h"
#include "crystfel_gui.h"
#include "gui_backend_local.h"
#include "gui_backend_slurm.h"

static double parse_float(const char *val)
{
	float v;

	if (sscanf(val, "%f", &v) != 1) {
		ERROR("Invalid float value '%s'\n", val);
		return NAN;
	}

	return v;
}


static int parse_int(const char *val)
{
	int v;

	if (sscanf(val, "%i", &v) != 1) {
		ERROR("Invalid int value '%s'\n", val);
		return 0;
	}

	return v;
}


static const char *str_matchtype(enum match_type_id mt)
{
	switch ( mt ) {
	case MATCH_EVERYTHING : return "everything";
	case MATCH_H5 : return "hdf5";
	case MATCH_CHEETAH_LCLS_H5 : return "lcls-cheetah-hdf5";
	case MATCH_CHEETAH_CXI : return "cheetah-cxi";
	case MATCH_CBF : return "cbf";
	case MATCH_CBFGZ : return "cbfgz";
	}
	return NULL;
}


enum match_type_id decode_matchtype(const char *type_id)
{
	if ( strcmp(type_id, "everything") == 0 ) return MATCH_EVERYTHING;
	if ( strcmp(type_id, "hdf5") == 0 ) return MATCH_H5;
	if ( strcmp(type_id, "lcls-cheetah-hdf5") == 0 ) return MATCH_CHEETAH_LCLS_H5;
	if ( strcmp(type_id, "cheetah-cxi") == 0 ) return MATCH_CHEETAH_CXI;
	if ( strcmp(type_id, "cbf") == 0 ) return MATCH_CBF;
	if ( strcmp(type_id, "cbfgz") == 0 ) return MATCH_CBFGZ;
	ERROR("Unknown match type id '%s'\n", type_id);
	return MATCH_EVERYTHING;
}


int match_filename(const char *fn, enum match_type_id mt)
{
	const char *ext = NULL;
	const char *ext2 = NULL;

	if ( mt == MATCH_EVERYTHING ) return 1;

	ext = filename_extension(fn, &ext2);
	if ( ext == NULL ) return 0;

	if ( mt == MATCH_H5 ) return (strcmp(ext, ".h5")==0);
	if ( mt == MATCH_CHEETAH_LCLS_H5 ) {
		return ((strcmp(ext, ".h5")==0)
		        && (strncmp(fn, "LCLS", 4)==0));
	}
	if ( mt == MATCH_CHEETAH_CXI ) return strcmp(ext, ".cxi")==0;
	if ( mt == MATCH_CBF ) return strcmp(ext, ".cbf")==0;
	if ( mt == MATCH_CBFGZ  ) {
		if ( ext2 != NULL ) {
			return strcmp(ext2, ".cbf.gz")==0;
		}
	}

	return 0;
}


/* "tols" is in frac (not %) and radians */
static void parse_tols(const char *text, float *tols)
{
	int r;

	r = sscanf(text, "%f,%f,%f,%f,%f,%f",
	           &tols[0], &tols[1], &tols[2],
	           &tols[3], &tols[4], &tols[5]);

	if ( r != 6 ) {
		STATUS("Invalid tolerances '%s'\n", text);
	} else {
		int i;
		for ( i=0; i<3; i++ ) tols[i] /= 100.0;
		for ( i=3; i<6; i++ ) tols[i] = deg2rad(tols[i]);
	}
}


static int find_backend(const char *name, struct crystfelproject *proj)
{
	int i;

	for ( i=0; i<proj->n_backends; i++ ) {
		if ( strcmp(proj->backends[i].name, name) == 0 ) {
			return i;
		}
	}

	ERROR("Couldn't find backend '%s'\n", name);
	return 0;
}


static void parse_peaksearch_opt(const char *key, const char *val,
                                 struct crystfelproject *proj)
{
	if ( strcmp(key, "peak_search_params.method") == 0 ) {
		proj->peak_search_params.method = parse_peaksearch(val);
		if ( proj->peak_search_params.method == PEAK_ERROR ) {
			ERROR("Unrecognised peak search method '%s'\n",
			      val);
		}
	}

	if ( strcmp(key, "peak_search_params.threshold") == 0 ) {
		proj->peak_search_params.threshold = parse_float(val);
	}

	if ( strcmp(key, "peak_search_params.min_sq_gradient") == 0 ) {
		proj->peak_search_params.min_sq_gradient = parse_float(val);
	}

	if ( strcmp(key, "peak_search_params.min_snr") == 0 ) {
		proj->peak_search_params.min_snr = parse_float(val);
	}

	if ( strcmp(key, "peak_search_params.local_bg_radius") == 0 ) {
		proj->peak_search_params.local_bg_radius = parse_int(val);
	}

	if ( strcmp(key, "peak_search_params.min_res") == 0 ) {
		proj->peak_search_params.min_res = parse_int(val);
	}

	if ( strcmp(key, "peak_search_params.min_sig") == 0 ) {
		proj->peak_search_params.min_sig = parse_float(val);
	}

	if ( strcmp(key, "peak_search_params.max_res") == 0 ) {
		proj->peak_search_params.max_res = parse_int(val);
	}

	if ( strcmp(key, "peak_search_params.min_pix_count") == 0 ) {
		proj->peak_search_params.min_pix_count = parse_int(val);
	}

	if ( strcmp(key, "peak_search_params.max_pix_count") == 0 ) {
		proj->peak_search_params.max_pix_count = parse_int(val);
	}

	if ( strcmp(key, "peak_search_params.min_peak_over_neighbour") == 0 ) {
		proj->peak_search_params.min_peak_over_neighbour = parse_float(val);
	}

	if ( strcmp(key, "peak_search_params.pk_inn") == 0 ) {
		proj->peak_search_params.pk_inn = parse_float(val);
	}

	if ( strcmp(key, "peak_search_params.pk_mid") == 0 ) {
		proj->peak_search_params.pk_mid = parse_float(val);
	}

	if ( strcmp(key, "peak_search_params.pk_out") == 0 ) {
		proj->peak_search_params.pk_out = parse_float(val);
	}

	if ( strcmp(key, "peak_search_params.half_pixel_shift") == 0 ) {
		proj->peak_search_params.half_pixel_shift = parse_int(val);
	}

	if ( strcmp(key, "peak_search_params.revalidate") == 0 ) {
		proj->peak_search_params.revalidate = parse_int(val);
	}
}


static void parse_indexing_opt(const char *key, const char *val,
                               struct crystfelproject *proj)
{
	if ( strcmp(key, "indexing.cell_file") == 0 ) {
		proj->indexing_params.cell_file = strdup(val);
	}

	if ( strcmp(key, "indexing.methods") == 0 ) {
		proj->indexing_params.indexing_methods = strdup(val);
	}

	if ( strcmp(key, "indexing.multi_lattice") == 0 ) {
		proj->indexing_params.multi = parse_int(val);
	}

	if ( strcmp(key, "indexing.no_refine") == 0 ) {
		proj->indexing_params.no_refine = parse_int(val);
	}

	if ( strcmp(key, "indexing.no_retry") == 0 ) {
		proj->indexing_params.no_retry = parse_int(val);
	}

	if ( strcmp(key, "indexing.no_peak_check") == 0 ) {
		proj->indexing_params.no_peak_check = parse_int(val);
	}

	if ( strcmp(key, "indexing.no_cell_check") == 0 ) {
		proj->indexing_params.no_cell_check = parse_int(val);
	}

	if ( strcmp(key, "indexing.cell_tolerance") == 0 ) {
		parse_tols(val, proj->indexing_params.tols);
	}

	if ( strcmp(key, "indexing.min_peaks") == 0 ) {
		proj->indexing_params.min_peaks = parse_int(val);
	}
}


static void parse_integration_opt(const char *key, const char *val,
                                  struct crystfelproject *proj)
{
	if ( strcmp(key, "integration.method") == 0 ) {
		proj->indexing_params.integration_method = strdup(val);
	}

	if ( strcmp(key, "integration.overpredict") == 0 ) {
		proj->indexing_params.overpredict = parse_int(val);
	}

	if ( strcmp(key, "integration.push_res") == 0 ) {
		proj->indexing_params.push_res = parse_float(val);
	}

	if ( strcmp(key, "integration.ir_inn") == 0 ) {
		proj->indexing_params.ir_inn = parse_float(val);
	}

	if ( strcmp(key, "integration.ir_mid") == 0 ) {
		proj->indexing_params.ir_mid = parse_float(val);
	}

	if ( strcmp(key, "integration.ir_out") == 0 ) {
		proj->indexing_params.ir_out = parse_float(val);
	}
}


static void add_metadata_to_copy(struct index_params *ip,
                                 const char *header)
{
	char **n;

	n = realloc(ip->metadata_to_copy,
	            (ip->n_metadata+1)*sizeof(char *));
	if ( n == NULL ) return;
	ip->metadata_to_copy = n;

	ip->metadata_to_copy[ip->n_metadata++] = strdup(header);
}


static void parse_stream_opt(const char *key, const char *val,
                             struct index_params *ip)
{
	if ( strcmp(key, "stream.exclude_blanks") == 0 ) {
		ip->exclude_nonhits = parse_int(val);
	}
	if ( strcmp(key, "stream.exclude_peaks") == 0 ) {
		ip->exclude_peaks = parse_int(val);
	}
	if ( strcmp(key, "stream.exclude_refls") == 0 ) {
		ip->exclude_refls = parse_int(val);
	}
	if ( strcmp(key, "stream.metadata") == 0 ) {
		add_metadata_to_copy(ip, val);
	}
}


static void parse_merging_opt(const char *key, const char *val,
                              struct crystfelproject *proj)
{
	if ( strcmp(key, "merging.model") == 0 ) {
		proj->merging_params.model = strdup(val);
	}

	if ( strcmp(key, "merging.symmetry") == 0 ) {
		proj->merging_params.symmetry = strdup(val);
	}

	if ( strcmp(key, "merging.scale") == 0 ) {
		proj->merging_params.scale = parse_int(val);
	}

	if ( strcmp(key, "merging.bscale") == 0 ) {
		proj->merging_params.bscale = parse_int(val);
	}

	if ( strcmp(key, "merging.postref") == 0 ) {
		proj->merging_params.postref = parse_int(val);
	}

	if ( strcmp(key, "merging.niter") == 0 ) {
		proj->merging_params.niter = parse_int(val);
	}

	if ( strcmp(key, "merging.polarisation") == 0 ) {
		proj->merging_params.polarisation = strdup(val);
	}

	if ( strcmp(key, "merging.deltacchalf") == 0 ) {
		proj->merging_params.deltacchalf = parse_int(val);
	}

	if ( strcmp(key, "merging.min_measurements") == 0 ) {
		proj->merging_params.min_measurements = parse_int(val);
	}

	if ( strcmp(key, "merging.max_adu") == 0 ) {
		proj->merging_params.max_adu = parse_float(val);
	}

	if ( strcmp(key, "merging.custom_split") == 0 ) {
		proj->merging_params.custom_split = strdup(val);
	}

	if ( strcmp(key, "merging.twin_sym") == 0 ) {
		proj->merging_params.twin_sym = strdup(val);
	}

	if ( strcmp(key, "merging.min_res") == 0 ) {
		proj->merging_params.min_res = parse_float(val);
	}

	if ( strcmp(key, "merging.push_res") == 0 ) {
		proj->merging_params.push_res = parse_float(val);
	}
}


static void handle_var(const char *key, const char *val,
                       struct crystfelproject *proj)
{
	if ( strcmp(key, "indexing.new_job_title") == 0 ) {
		free(proj->indexing_new_job_title);
		proj->indexing_new_job_title = strdup(val);
	}

	if ( strcmp(key, "merging.new_job_title") == 0 ) {
		free(proj->merging_new_job_title);
		proj->merging_new_job_title = strdup(val);
	}

	if ( strcmp(key, "indexing.backend") == 0 ) {
		proj->indexing_backend_selected = find_backend(val, proj);
	}

	if ( strcmp(key, "merging.backend") == 0 ) {
		proj->merging_backend_selected = find_backend(val, proj);
	}

	if ( strcmp(key, "show_peaks") == 0 ) {
		proj->show_peaks = parse_int(val);
	}

	if ( strcmp(key, "show_refls") == 0 ) {
		proj->show_refls = parse_int(val);
	}

	if ( strcmp(key, "label_refls") == 0 ) {
		proj->label_refls = parse_int(val);
	}

	if ( strcmp(key, "geom") == 0 ) {
		proj->geom_filename = strdup(val);
	}

	if ( strcmp(key, "data_folder") == 0 ) {
		proj->data_top_folder = strdup(val);
	}

	if ( strncmp(key, "stream.", 7) == 0 ) {
		parse_stream_opt(key, val, &proj->indexing_params);
	} else if ( strcmp(key, "stream") == 0 ) {
		proj->stream_filename = strdup(val);
	}

	if ( strcmp(key, "search_pattern") == 0 ) {
		proj->data_search_pattern = decode_matchtype(val);
	}

	if ( strncmp(key, "peak_search_params.", 19) == 0 ) {
		parse_peaksearch_opt(key, val, proj);
	}

	if ( strncmp(key, "indexing.", 9) == 0 ) {
		int i;
		parse_indexing_opt(key, val,  proj);
		for ( i=0; i<proj->n_backends; i++ ) {
			struct crystfel_backend *be;
			be = &proj->backends[i];
			be->read_indexing_opt(be->indexing_opts_priv,
			                      key, val);
		}
	}

	if ( strncmp(key, "integration.", 12) == 0 ) {
		parse_integration_opt(key, val, proj);
	}

	if ( strncmp(key, "merging.", 9) == 0 ) {
		int i;
		parse_merging_opt(key, val, proj);
		for ( i=0; i<proj->n_backends; i++ ) {
			struct crystfel_backend *be;
			be = &proj->backends[i];
			be->read_merging_opt(be->merging_opts_priv,
			                     key, val);
		}
	}
}


void clear_project_files(struct crystfelproject *proj)
{
	int i;

	if ( proj->filenames != NULL ) {
		for ( i=0; i<proj->n_frames; i++ ) {
			free(proj->filenames[i]);
			free(proj->events[i]);
		}
		free(proj->filenames);
		free(proj->events);
	}
	proj->n_frames = 0;
	proj->max_frames = 0;
	proj->filenames = NULL;
	proj->events = NULL;
}


void add_file_to_project(struct crystfelproject *proj,
                         const char *filename, const char *event)
{
	if ( proj->n_frames == proj->max_frames ) {
		int n_max = proj->max_frames + 1024;
		char **n_filenames;
		char **n_events;
		n_filenames = realloc(proj->filenames,
		                      n_max*sizeof(char *));
		n_events = realloc(proj->events,
		                   n_max*sizeof(char *));
		if ( (n_filenames == NULL) || (n_events == NULL) ) {
			ERROR("Failed to allocate new filename\n");
			return;
		}
		proj->max_frames = n_max;
		proj->filenames = n_filenames;
		proj->events = n_events;
	}

	proj->filenames[proj->n_frames] = strdup(filename);
	proj->events[proj->n_frames] = safe_strdup(event);
	proj->n_frames++;
}


static char **add_stream(char *new_stream,
                         char **streams,
                         int *pn_streams)
{
	int i = *pn_streams;
	char **new_streams = realloc(streams,
	                             (i+1)*sizeof(struct gui_indexing_result));
	if ( new_streams == NULL ) return streams;

	new_streams[i] = new_stream;
	*pn_streams = i+1;
	return new_streams;
}


static void read_parameters(FILE *fh, struct crystfelproject *proj)
{
	char *rval;
	char line[1024];

	do {

		char *sp;

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) break;
		chomp(line);
		if ( line[0] == '\0' ) continue;

		if ( strcmp(line, "-----") == 0 ) break;

		sp = strchr(line, ' ');
		if ( sp == NULL ) {
			ERROR("Unrecognised line in crystfel.project "
			      "file: '%s'\n", line);
			continue;
		}
		sp[0] = '\0';
		handle_var(line, sp+1, proj);

	} while ( rval != NULL );
}


static void add_result(struct crystfelproject *proj,
                       char *results_name,
                       char **streams,
                       int n_streams,
                       int selected,
                       char *hkl)
{
	if ( (n_streams > 0) && (hkl == NULL) ) {
		add_indexing_result(proj, results_name,
		                    streams, n_streams);

		if ( selected ) {
			select_result(proj, results_name);
		}

	} else if ( (hkl != NULL) && (n_streams == 0) ) {
		add_merge_result(proj,
		                 results_name,
		                 hkl);

	} else {
		ERROR("Bad results %s (%i %s)\n",
		      results_name, n_streams, hkl);
	}

}


static void read_results(FILE *fh, struct crystfelproject *proj)
{
	char *rval;
	char line[1024];
	char **streams = NULL;
	int n_streams = 0;
	char *results_name = NULL;
	char *hkl = NULL;
	int selected = 0;
	int first = 1;

	do {

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) break;
		chomp(line);
		if ( line[0] == '\0' ) continue;

		if ( strncmp(line, "Result ", 7) == 0 ) {

			if ( !first ) {
				add_result(proj, results_name,
				           streams, n_streams, selected,
				           hkl);
			}
			first = 0;

			n_streams = 0;
			selected = 0;
			streams = NULL;
			hkl = NULL;

			results_name = strdup(line+7);
		}

		if ( strncmp(line, "   Selected", 11) == 0 ) {
			selected = 1;
		}

		if ( strncmp(line, "   Stream ", 10) == 0 ) {
			streams = add_stream(strdup(line+10),
			                     streams,
			                     &n_streams);
		}

		if ( strncmp(line, "   HKL ", 7) == 0 ) {
			hkl = strdup(line+7);
		}

		if ( strcmp(line, "-----") == 0 ) {
			add_result(proj, results_name,
			           streams, n_streams, selected,
			           hkl);
			break;
		}

	} while ( rval != NULL );
}


static void read_frames(FILE *fh, struct crystfelproject *proj)
{
	char *rval;
	char line[1024];

	do {

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) break;

		chomp(line);

		if ( line[0] == '\0' ) continue;

		char *ev = NULL;
		size_t n = strlen(line)-1;
		for ( ; n>0; n-- ) {
			if ( line[n] == ' ' ) {
				line[n] = '\0';
				ev = &line[n+1];
				break;
			}
		}
		add_file_to_project(proj, line, ev);

	} while ( rval != NULL );
}


int load_project(struct crystfelproject *proj)
{
	FILE *fh;

	fh = fopen("crystfel.project", "r");
	if ( fh == NULL ) return 1;

	default_project(proj);

	read_parameters(fh, proj);
	read_results(fh, proj);
	read_frames(fh, proj);

	fclose(fh);

	return 0;
}


int save_project(struct crystfelproject *proj)
{
	int i;
	FILE *fh;

	fh = fopen("crystfel.project", "w");
	if ( fh == NULL ) {
		STATUS("Couldn't save project.\n");
		return 1;
	}

	if ( proj->geom_filename != NULL ) {
		fprintf(fh, "geom %s\n", proj->geom_filename);
	}
	if ( proj->data_top_folder != NULL ) {
		fprintf(fh, "data_folder %s\n", proj->data_top_folder);
	}
	fprintf(fh, "search_pattern %s\n",
	        str_matchtype(proj->data_search_pattern));
	if ( proj->stream_filename != NULL ) {
		fprintf(fh, "stream %s\n", proj->stream_filename);
	}

	fprintf(fh, "peak_search_params.method %s\n",
	        str_peaksearch(proj->peak_search_params.method));
	fprintf(fh, "peak_search_params.threshold %f\n",
	        proj->peak_search_params.threshold);
	fprintf(fh, "peak_search_params.min_sq_gradient %f\n",
	        proj->peak_search_params.min_sq_gradient);
	fprintf(fh, "peak_search_params.min_snr %f\n",
	        proj->peak_search_params.min_snr);
	fprintf(fh, "peak_search_params.min_pix_count %i\n",
	        proj->peak_search_params.min_pix_count);
	fprintf(fh, "peak_search_params.max_pix_count %i\n",
	        proj->peak_search_params.max_pix_count);
	fprintf(fh, "peak_search_params.local_bg_radius %i\n",
	        proj->peak_search_params.local_bg_radius);
	fprintf(fh, "peak_search_params.min_res %i\n",
	        proj->peak_search_params.min_res);
	fprintf(fh, "peak_search_params.max_res %i\n",
	        proj->peak_search_params.max_res);
	fprintf(fh, "peak_search_params.min_snr_biggest_pix %f\n",
	        proj->peak_search_params.min_snr_biggest_pix);
	fprintf(fh, "peak_search_params.min_snr_peak_pix %f\n",
	        proj->peak_search_params.min_snr_peak_pix);
	fprintf(fh, "peak_search_params.min_peak_over_neighbour %f\n",
	        proj->peak_search_params.min_peak_over_neighbour);
	fprintf(fh, "peak_search_params.pk_inn %f\n",
	        proj->peak_search_params.pk_inn);
	fprintf(fh, "peak_search_params.pk_mid %f\n",
	        proj->peak_search_params.pk_mid);
	fprintf(fh, "peak_search_params.pk_out %f\n",
	        proj->peak_search_params.pk_out);
	fprintf(fh, "peak_search_params.half_pixel_shift %i\n",
	        proj->peak_search_params.half_pixel_shift);
	fprintf(fh, "peak_search_params.revalidate %i\n",
	        proj->peak_search_params.revalidate);

	if ( proj->indexing_params.cell_file != NULL ) {
		fprintf(fh, "indexing.cell_file %s\n",
		        proj->indexing_params.cell_file);
	}

	if ( proj->indexing_params.indexing_methods != NULL ) {
		fprintf(fh, "indexing.methods %s\n",
		        proj->indexing_params.indexing_methods);
	}
	fprintf(fh, "indexing.multi_lattice %i\n",
	        proj->indexing_params.multi);
	fprintf(fh, "indexing.no_refine %i\n",
	        proj->indexing_params.no_refine);
	fprintf(fh, "indexing.no_retry %i\n",
	        proj->indexing_params.no_retry);
	fprintf(fh, "indexing.no_peak_check %i\n",
	        proj->indexing_params.no_peak_check);
	fprintf(fh, "indexing.no_cell_check %i\n",
	        proj->indexing_params.no_cell_check);

	/* Values in file are in percent and degrees */
	/* Values in "tol" are in frac (not %) and radians */
	fprintf(fh, "indexing.cell_tolerance %f,%f,%f,%f,%f,%f\n",
	        proj->indexing_params.tols[0]*100.0,
	        proj->indexing_params.tols[1]*100.0,
	        proj->indexing_params.tols[2]*100.0,
	        rad2deg(proj->indexing_params.tols[3]),
	        rad2deg(proj->indexing_params.tols[4]),
	        rad2deg(proj->indexing_params.tols[5]));
	fprintf(fh, "indexing.min_peaks %i\n",
	        proj->indexing_params.min_peaks);

	if ( proj->indexing_new_job_title != NULL ) {
		fprintf(fh, "indexing.new_job_title %s\n",
		        proj->indexing_new_job_title);
	}

	fprintf(fh, "indexing.backend %s\n",
	        proj->backends[proj->indexing_backend_selected].name);
	for ( i=0; i<proj->n_backends; i++ ) {
		struct crystfel_backend *be;
		be = &proj->backends[i];
		be->write_indexing_opts(be->indexing_opts_priv, fh);
	}

	fprintf(fh, "integration.method %s\n",
	        proj->indexing_params.integration_method);
	fprintf(fh, "integration.overpredict %i\n",
	        proj->indexing_params.overpredict);
	fprintf(fh, "integration.push_res %f\n",
	        proj->indexing_params.push_res);
	fprintf(fh, "integration.ir_inn %f\n",
	        proj->indexing_params.ir_inn);
	fprintf(fh, "integration.ir_mid %f\n",
	        proj->indexing_params.ir_mid);
	fprintf(fh, "integration.ir_out %f\n",
	        proj->indexing_params.ir_out);

	fprintf(fh, "stream.exclude_blanks %i\n",
	        proj->indexing_params.exclude_nonhits);
	fprintf(fh, "stream.exclude_peaks %i\n",
	        proj->indexing_params.exclude_peaks);
	fprintf(fh, "stream.exclude_refls %i\n",
	        proj->indexing_params.exclude_refls);
	if ( proj->indexing_params.metadata_to_copy != NULL ) {
		int i;
		for ( i=0; i<proj->indexing_params.n_metadata; i++ ) {
			fprintf(fh, "stream.metadata %s\n",
			        proj->indexing_params.metadata_to_copy[i]);
		}
	}

	fprintf(fh, "merging.model %s\n",
	        proj->merging_params.model);
	fprintf(fh, "merging.symmetry %s\n",
	        proj->merging_params.symmetry);
	fprintf(fh, "merging.scale %i\n",
	        proj->merging_params.scale);
	fprintf(fh, "merging.bscale %i\n",
	        proj->merging_params.bscale);
	fprintf(fh, "merging.postref %i\n",
	        proj->merging_params.postref);
	fprintf(fh, "merging.niter %i\n",
	        proj->merging_params.niter);
	fprintf(fh, "merging.polarisation %s\n",
	        proj->merging_params.polarisation);
	fprintf(fh, "merging.deltacchalf %i\n",
	        proj->merging_params.deltacchalf);
	fprintf(fh, "merging.min_measurements %i\n",
	        proj->merging_params.min_measurements);
	fprintf(fh, "merging.max_adu %f\n",
	        proj->merging_params.max_adu);
	fprintf(fh, "merging.custom_split %s\n",
	        proj->merging_params.custom_split);
	fprintf(fh, "merging.twin_sym %s\n",
	        proj->merging_params.twin_sym);
	fprintf(fh, "merging.min_res %f\n",
	        proj->merging_params.min_res);
	fprintf(fh, "merging.push_res %f\n",
	        proj->merging_params.push_res);

	fprintf(fh, "merging.backend %s\n",
	        proj->backends[proj->merging_backend_selected].name);
	for ( i=0; i<proj->n_backends; i++ ) {
		struct crystfel_backend *be;
		be = &proj->backends[i];
		be->write_merging_opts(be->merging_opts_priv, fh);
	}

	fprintf(fh, "show_peaks %i\n", proj->show_peaks);
	fprintf(fh, "show_refls %i\n", proj->show_refls);
	fprintf(fh, "label_refls %i\n", proj->label_refls);

	fprintf(fh, "-----\n");
	for ( i=0; i<proj->n_results; i++ ) {
		int j;
		fprintf(fh, "Result %s\n", proj->results[i].name);
		for ( j=0; j<proj->results[i].n_streams; j++ ) {
			fprintf(fh, "   Stream %s\n",
			        proj->results[i].streams[j]);
		}
		if ( strcmp(selected_result(proj),
		            proj->results[i].name) == 0 )
		{
			fprintf(fh, "   Selected\n");
		}
	}
	for ( i=0; i<proj->n_merge_results; i++ ) {
		fprintf(fh, "Result %s\n", proj->merge_results[i].name);
		fprintf(fh, "   HKL %s\n", proj->merge_results[i].hkl);
	}

	fprintf(fh, "-----\n");
	for ( i=0; i<proj->n_frames; i++ ) {
		if ( proj->events[i] != NULL ) {
			fprintf(fh, "%s %s\n",
			        proj->filenames[i], proj->events[i]);
		} else {
			fprintf(fh, "%s\n", proj->filenames[i]);
		}
	}

	fclose(fh);

	proj->unsaved = 0;
	return 0;
}


void default_project(struct crystfelproject *proj)
{
	proj->unsaved = 0;
	proj->geom_filename = NULL;
	proj->n_frames = 0;
	proj->max_frames = 0;
	proj->filenames = NULL;
	proj->events = NULL;
	proj->peak_params = NULL;
	proj->data_top_folder = NULL;
	proj->data_search_pattern = 0;
	proj->stream_filename = NULL;
	proj->dtempl = NULL;
	proj->cur_image = NULL;
	proj->indexing_opts = NULL;
	proj->n_running_tasks = 0;
	proj->indexing_new_job_title = NULL;
	proj->merging_new_job_title = NULL;

	proj->indexing_backend_selected = 0;
	proj->merging_backend_selected = 0;
	proj->n_backends = 0;
	proj->backends = malloc(2*sizeof(struct crystfel_backend));
	/* FIXME: Crappy error handling */
	if ( proj->backends == NULL ) {
		ERROR("Couldn't allocate space for backends\n");
	}

	if ( make_local_backend(&proj->backends[proj->n_backends++]) ) {
		ERROR("Local backend setup failed\n");
	}

	#ifdef HAVE_SLURM
	if ( make_slurm_backend(&proj->backends[proj->n_backends++]) ) {
		ERROR("SLURM backend setup failed\n");
	}
	#endif

	/* Default parameter values */
	proj->show_peaks = 1;
	proj->show_refls = 1;
	proj->label_refls = 1;

	proj->peak_search_params.method = PEAK_ZAEF;
	proj->peak_search_params.threshold = 800.0;
	proj->peak_search_params.min_sq_gradient = 100000;
	proj->peak_search_params.min_snr = 5.0;
	proj->peak_search_params.min_pix_count = 2;
	proj->peak_search_params.max_pix_count = 200;
	proj->peak_search_params.local_bg_radius = 3;
	proj->peak_search_params.min_res = 0;
	proj->peak_search_params.max_res = 1200;
	proj->peak_search_params.min_snr_biggest_pix = 7.0;
	proj->peak_search_params.min_snr_peak_pix = 6.0;
	proj->peak_search_params.min_sig = 11.0;
	proj->peak_search_params.min_peak_over_neighbour = -INFINITY;
	proj->peak_search_params.pk_inn = 4.0;
	proj->peak_search_params.pk_mid = 5.0;
	proj->peak_search_params.pk_out = 7.0;
	proj->peak_search_params.half_pixel_shift = 1;
	proj->peak_search_params.revalidate = 1;

	proj->indexing_params.cell_file = NULL;
	proj->indexing_params.indexing_methods = NULL;
	proj->indexing_params.multi = 1;
	proj->indexing_params.no_refine = 0;
	proj->indexing_params.no_retry = 0;
	proj->indexing_params.no_peak_check = 0;
	proj->indexing_params.no_cell_check = 0;
	proj->indexing_params.tols[0] = 0.05; /* frac (not %) */
	proj->indexing_params.tols[1] = 0.05; /* frac (not %) */
	proj->indexing_params.tols[2] = 0.05; /* frac (not %) */
	proj->indexing_params.tols[3] = deg2rad(1.5); /* rad */
	proj->indexing_params.tols[4] = deg2rad(1.5); /* rad */
	proj->indexing_params.tols[5] = deg2rad(1.5); /* rad */
	proj->indexing_params.min_peaks = 0;
	proj->indexing_params.integration_method = strdup("rings");
	proj->indexing_params.overpredict = 0;
	proj->indexing_params.push_res = INFINITY;
	proj->indexing_params.ir_inn = 4.0;
	proj->indexing_params.ir_mid = 5.0;
	proj->indexing_params.ir_out = 7.0;
	proj->indexing_params.exclude_nonhits = 0;
	proj->indexing_params.exclude_peaks = 0;
	proj->indexing_params.exclude_refls = 0;
	proj->indexing_params.metadata_to_copy = NULL;
	proj->indexing_params.n_metadata = 0;

	proj->merging_params.model = strdup("unity");
	proj->merging_params.symmetry = strdup("1");
	proj->merging_params.scale = 1;
	proj->merging_params.bscale = 1;
	proj->merging_params.postref = 0;
	proj->merging_params.niter = 3;
	proj->merging_params.polarisation = strdup("horiz");
	proj->merging_params.deltacchalf = 1;
	proj->merging_params.min_measurements = 2;
	proj->merging_params.max_adu = INFINITY;
	proj->merging_params.custom_split = NULL;
	proj->merging_params.twin_sym = NULL;
	proj->merging_params.min_res = INFINITY;
	proj->merging_params.push_res = INFINITY;

	proj->results = NULL;
	proj->n_results = 0;

	proj->merge_results = NULL;
	proj->n_merge_results = 0;

	proj->fom_res_min = 0.0;
	proj->fom_res_max = INFINITY;
	proj->fom_nbins = 20;
	proj->fom_min_snr = 0.0;
	proj->fom_use_min_snr = 0;
}


/* Assumes ownership of "name" and "streams" */
int add_indexing_result(struct crystfelproject *proj,
                        char *name,
                        char **streams,
                        int n_streams)
{
	int i;
	struct gui_indexing_result *new_results;

	new_results = realloc(proj->results,
	                      (proj->n_results+1)*sizeof(struct gui_indexing_result));
	if ( new_results == NULL ) return 1;

	new_results[proj->n_results].name = name;
	new_results[proj->n_results].streams = streams;
	new_results[proj->n_results].n_streams = n_streams;
	new_results[proj->n_results].indices = malloc(n_streams*sizeof(StreamIndex *));

	for ( i=0; i<n_streams; i++ ) {
		new_results[proj->n_results].indices[i] = NULL;
	}

	proj->results = new_results;
	proj->n_results++;

	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(proj->results_combo),
	                          name, name);

	return 0;
}


int add_merge_result(struct crystfelproject *proj,
                     char *name,
                     char *hkl)
{
	struct gui_merge_result *new_results;

	new_results = realloc(proj->merge_results,
	                      (proj->n_merge_results+1)*sizeof(struct gui_merge_result));
	if ( new_results == NULL ) return 1;

	new_results[proj->n_merge_results].name = name;
	new_results[proj->n_merge_results].hkl = hkl;

	proj->merge_results = new_results;
	proj->n_merge_results++;

	return 0;
}


static void update_result_index(struct gui_indexing_result *result)
{
	int i;

	for ( i=0; i<result->n_streams; i++ ) {

		/* FIXME: Skip if already up to date */

		stream_index_free(result->indices[i]);
		result->indices[i] = stream_make_index(result->streams[i]);

	}
}


struct gui_indexing_result *find_indexing_result_by_name(struct crystfelproject *proj,
                                                         const char *name)
{
	int i;

	for ( i=0; i<proj->n_results; i++ ) {
		if ( strcmp(proj->results[i].name, name) == 0 ) {
			return &proj->results[i];
		}
	}
	return NULL;
}


struct image *find_indexed_image(struct crystfelproject *proj,
                                 const char *results_name,
                                 const char *filename,
                                 const char *event)
{
	Stream *st;
	int i;
	int found = 0;
	struct image *image;
	struct gui_indexing_result *result;

	result = find_indexing_result_by_name(proj, results_name);
	if ( result == NULL ) return NULL;

	for ( i=0; i<result->n_streams; i++ ) {
		if ( stream_select_chunk(NULL,
		                         result->indices[i],
		                         filename,
		                         event) == 0 )
		{
			found = 1;
			break;
		}
	}

	if ( !found ) {
		update_result_index(result);
		for ( i=0; i<result->n_streams; i++ ) {
			if ( stream_select_chunk(NULL,
			                         result->indices[i],
			                         filename,
			                         event) == 0 )
				{
					found = 1;
					break;
				}
		}
	}

	if ( !found ) return NULL;

	st = stream_open_for_read(result->streams[i]);
	if ( stream_select_chunk(st, result->indices[i],
	                         filename, event) )
		{
		ERROR("Error selecting chunk.\n");
		return NULL;
	}

	image = stream_read_chunk(st, STREAM_REFLECTIONS
	                            | STREAM_PEAKS
	                            | STREAM_DATA_DETGEOM);

	stream_close(st);
	return image;
}
