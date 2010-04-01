/*
 * go-data-slicer.h :
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
 *               2010 Sean McIntyre <s.mcintyre@utoronto.ca>
 *                    David Algar   <david.algar@utoronto.ca>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

/**
 * SECTION: GODataSlicer
 * @short_description: Data Slicer main class
 * @see_also: #GoDataCache #GODataSlicerIndex #GODataSlicerCacheOverlay
 *
 * This class implements the 'slicing' functionality of our Slicer        
 * implementation.  Note that the methods in this header file are arranged in  
 * the order in which they should logically be called by any visual            
 * representation of the Slicer.
 */

#ifndef GO_DATA_SLICER_H
#define GO_DATA_SLICER_H

#include <gnumeric.h>
#include "goffice-data.h"	/* remove after move to goffice */
#include <glib-object.h>
#include <go-val.h>

#define NUM_AGGREGATE_FUNCTIONS 2
typedef enum {
     SUM,
     COUNT
} AggregateFunction;

G_BEGIN_DECLS

#define GO_DATA_SLICER_TYPE	(go_data_slicer_get_type ())
#define GO_DATA_SLICER(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_SLICER_TYPE, GODataSlicer))
#define IS_GO_DATA_SLICER(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_SLICER_TYPE))

GType go_data_slicer_get_type (void);

/**
 * create_cache:
 * @self:       This GODataSlicer
 * @sheet:      The sheet which will serve as the source for a new cache
 * @range:      The range of cells from the sheet to use to construct a new cache
 */
void go_data_slicer_create_cache(GODataSlicer *self, Sheet * sheet, GnmRange * range);

GODataCache *go_data_slicer_get_cache (GODataSlicer const *self);
void	     go_data_slicer_set_cache (GODataSlicer *self, GODataCache *cache);



/**
 * set_col_field_index, set_row_field_index, add_page_field_index:
 * @self:            This GODataSlicer
 * @tuple_template:  an array of column indices which correspond to 
 *                   GODataCacheField objects in the cache, representing
 *                   the columns of the Cache to index as a tuple in the
 *                   given SlicerIndex.
 *
 * Methods for telling the Slicer which columns in the cache records should be
 * indexed as Tuples by which SlicerIndex (the one for row fields, the one
 * for column fields, or as a page field).
 *
 * Returns: an integer.
 *
 * PRECONDITION: Cache has been set.
 */
void go_data_slicer_set_col_field_index (GODataSlicer *self, GArray * tuple_template);
void go_data_slicer_set_row_field_index (GODataSlicer *self, GArray * tuple_template);
void go_data_slicer_add_page_field_index (GODataSlicer *self, GArray * tuple_template);

/**
 * set_data_field_index:
 * @self:            This GODataSlicer
 * @column_number:   the column index which corresponds to a 
 *                   GODataCacheField in the cache.
 *
 * Somewhat identical to above, except that a data field may only consist of 
 * one column.
 *
 */

void go_data_slicer_set_data_field_index(GODataSlicer *self, guint column_number);

/**
 * index_cache:
 *
 * @self:           This GODataSlicer
 *
 * Iterates over the records in self->cache to produce a Cache overlay while
 * filling up the various SlicerIndex objects.  This is the first pass over
 * the cache's records and never has to be done again.
 * 
 * PRECONDITION: all SlicerIndexes are instantiated (in particular those for 
 * row and column fields, since they are necessary).
 *
 */
void
go_data_slicer_index_cache (GODataSlicer *self);

/**
 * filter_set_enabled:
 *
 * @param self - this GODataSlicer
 * @param page_filter_num - the index of the page field in self->page_filters
 * @param tuple_record_num - the record_num value of the tuple to enable/disable
 * @param is_enabled - whether the tuple should be enabled or disabled
 *
 * Enables or disables rows in the CacheOverlay on the condition that a
 * particular tuple belonging to a particular page field appears in that row.
 *
 */
void
go_data_slicer_filter_set_enabled (GODataSlicer *self, guint page_filter_num, guint tuple_record_num, gboolean is_enabled);

/**
 * slice_cache:
 *
 * @self:           This GODataSlicer
 *
 * Iterates over the CacheOverlay's records to produce all aggregate values and
 * totals, with respect to all page field filters.  This is the 'second' pass
 * over the cache's records and is performed each time the page field filters
 * are altered since it (re)generates the slicer values and totals (storing
 * them in self->view).
 *
 */
void
go_data_slicer_slice_cache (GODataSlicer *self);


/**
 * get_all_row_tuples
 *
 * @self:           This GODataSlicer
 *
 * Returns all enabled row field IndexedTuples.
 * For use along the top of the resultant Slicer representation.
 * Since they are IndexedTuples, one can use each relative_position value
 * to query self->view and retrieve values for the resultant Slicer 
 * representation.
 *
 * Returns: an array of pointers to GODataSlicerTuples
 */
GPtrArray *
go_data_slicer_get_all_row_tuples(GODataSlicer *self);

/**
 * get_all_column_tuples:
 *
 * @self:           This GODataSlicer
 *
 * Returns all enabled column field IndexedTuples.
 * For use along the left side of the resultant Slicer representation.
 * Since they are IndexedTuples, one can use each relative_position value
 * to query self->view and retrieve values for the resultant Slicer 
 * representation.
 *
 * Returns: an array of pointers to GODataSlicerTuples
 */
GPtrArray *
go_data_slicer_get_all_column_tuples(GODataSlicer *self);

/**
 * get_page_filter_tuples:
 *
 * @self:           This GODataSlicer
 *
 * Returns all page field filter IndexedTuples for a particular page filter.
 * For use in a drop-down box of some sort.
 *
 * Returns: an array of pointers to GODataSlicerTuples
 */
GPtrArray *
go_data_slicer_get_page_filter_tuples(GODataSlicer *self, guint page_filter_num);

/**
 * get_value_at:
 *
 * @self:           This GODataSlicer
 * @x:              x-coordinate for the value/total desired
 * @y:              y-coordinate for the value/total desired
 *
 * After having sliced the cache, self->view represents a sparse matrix of
 * values indexed by location in the resultant Slicer representation.  This
 * function returns the aggregate value/total which should be displayed in
 * cell (x,y) for some x and some y.
 *
 * Returns:     A pointer to a GOVal
 */
GOVal *
go_data_slicer_get_value_at(GODataSlicer *self, int x, int y);

/**
 * dump_slicer
 *
 * @self: This GODataSlicer
 * 
 * Prints a string representation of this GODataSlicer to stdout
 */
void
go_data_slicer_dump_slicer(GODataSlicer *self);

G_END_DECLS

#endif /* GO_DATA_SLICER_H */
