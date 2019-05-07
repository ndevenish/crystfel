/*
 * stream.h
 *
 * Stream tools
 *
 * Copyright © 2013-2018 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2010-2018 Thomas White <taw@physics.org>
 *   2014      Valerio Mariani
 *   2011      Andrew Aquila
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

#ifndef STREAM_H
#define STREAM_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/**
 * \file stream.h
 * Stream functions (for indexing results)
 */

struct image;
struct hdfile;
struct event;
struct imagefile;
#include "cell.h"

#define GEOM_START_MARKER "----- Begin geometry file -----"
#define GEOM_END_MARKER "----- End geometry file -----"
#define CELL_START_MARKER "----- Begin unit cell -----"
#define CELL_END_MARKER "----- End unit cell -----"
#define CHUNK_START_MARKER "----- Begin chunk -----"
#define CHUNK_END_MARKER "----- End chunk -----"
#define PEAK_LIST_START_MARKER "Peaks from peak search"
#define PEAK_LIST_END_MARKER "End of peak list"
#define CRYSTAL_START_MARKER "--- Begin crystal"
#define CRYSTAL_END_MARKER "--- End crystal"
#define REFLECTION_START_MARKER "Reflections measured after indexing"
/* REFLECTION_END_MARKER is over in reflist-utils.h because it is also
 * used to terminate a standalone list of reflections */

/**
 * An opaque structure representing a stream being read or written
 */
typedef struct _stream Stream;

/**
 * A bitfield of things that can be read from a stream.  Use this (and
 * \ref read_chunk_2) to read the stream faster if you don't need the entire
 * contents of the stream.
 *
 * Using either or both of \p STREAM_READ_REFLECTIONS and \p STREAM_READ_UNITCELL
 * implies \p STREAM_READ_CRYSTALS.
 **/
typedef enum {

	/** Read the unit cell */
	STREAM_READ_UNITCELL = 1,

	/** Read the integrated reflections */
	STREAM_READ_REFLECTIONS = 2,

	/** Read the peak search results */
	STREAM_READ_PEAKS = 4,

	/** Read the general information about crystals */
	STREAM_READ_CRYSTALS = 8,

} StreamReadFlags;

struct stuff_from_stream
{
	char **fields;
	int n_fields;
};

#ifdef __cplusplus
extern "C" {
#endif

extern Stream *open_stream_for_read(const char *filename);
extern Stream *open_stream_for_write(const char *filename);
extern Stream *open_stream_for_write_2(const char *filename,
                                const char* geom_filename, int argc,
                                char *argv[]);
extern Stream *open_stream_for_write_3(const char *filename,
                                const char* geom_filename, UnitCell *cell,
                                int argc, char *argv[]);
extern Stream *open_stream_for_write_4(const char *filename,
                                const char *geom_filename, UnitCell *cell,
                                int argc, char *argv[], const char *indm_str);
extern Stream *open_stream_fd_for_write(int fd);
extern int get_stream_fd(Stream *st);
extern void close_stream(Stream *st);

extern void free_stuff_from_stream(struct stuff_from_stream *sfs);

extern int read_chunk(Stream *st, struct image *image);
extern int read_chunk_2(Stream *st, struct image *image,
                           StreamReadFlags srf);
extern int stream_has_old_indexers(Stream *st);

extern int write_chunk(Stream *st, struct image *image, struct imagefile *imfile,
                       int include_peaks, int include_reflections,
                       struct event *ev);

extern int write_chunk_2(Stream *st, struct image *image,
                         struct imagefile *imfile,
                         int include_peaks, int include_reflections,
                         struct event *ev);

extern void write_command(Stream *st, int argc, char *argv[]);
extern void write_geometry_file(Stream *st, const char *geom_filename);
extern int rewind_stream(Stream *st);
extern int is_stream(const char *filename);
extern char *stream_audit_info(Stream *st);

#ifdef __cplusplus
}
#endif

#endif	/* STREAM_H */
