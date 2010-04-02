/*
 * go-data-slicer-index.h :
 *
 * Copyright (C) 2010 Sean McIntyre <s.mcintyre@utoronto.ca> 
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
 * SECTION: GODataSlicerIndex
 * @short_description: An index used by the slicer
 * @see_also: #GoDataCache
 *
 * Organizes a set of tuples which represent a row field, a cache field, or a  
 * page field.  Maintains various representations of these tuples, including   
 * a representation sorted by tuple value as well as one sorted by each        
 * tuple's unique numerical index.  These representations allow the slicer in  
 * such tasks as sorting tuples, and identifying/eliminating duplicates. 
 */

#ifndef GO_DATA_SLICER_INDEX_H
#define GO_DATA_SLICER_INDEX_H

#include <glib-object.h>
#include "go-data-slicer-bitmap.h"
#include "go-data-slicer-tuple.h"

G_BEGIN_DECLS

#define GO_DATA_SLICER_INDEX_TYPE	  (go_data_slicer_index_get_type ())
#define GO_DATA_SLICER_INDEX(o)		  (G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_SLICER_INDEX_TYPE, GODataSlicerIndex))
#define IS_GO_DATA_SLICER_INDEX(o)	  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_SLICER_INDEX_TYPE))
#define GO_DATA_SLICER_INDEX_CLASS(k)	  (G_TYPE_CHECK_CLASS_CAST ((k), GO_DATA_SLICER_INDEX_TYPE, GODataSlicerIndexInterface))
#define IS_GO_DATA_SLICER_INDEX_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GO_DATA_SLICER_INDEX_TYPE))
#define GO_DATA_SLICER_INDEX_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GO_DATA_SLICER_INDEX_TYPE, GODataSlicerIndexClass))

typedef struct {
    gboolean enabled;           /*Whether or not this tuple is enabled with respect to filters*/
	guint relative_position;    /*The position of this tuple relative to other tuples in this SlicerIndex when sorted by tuple values*/
    GODataSlicerTuple * tuple;  /*The tuple*/
} GODataSlicerIndexedTuple;

typedef struct _GODataSlicerIndex GODataSlicerIndex;
typedef struct _GODataSlicerIndexClass GODataSlicerIndexClass;


struct _GODataSlicerIndexClass {
     GObjectClass parent_class;
};

struct _GODataSlicerIndex {
	GObject parent_instance;

    gboolean completed;  /*a flag which represents whether or not all cache rows that will be added to this SlicerIndex have been added*/
   	GODataCache	*cache;  /*The cache associated with the slicer this slicer index belongs to*/
    GPtrArray *tuple_template; /*an array of CacheField objects representing which values in a cache record belong to tuples in this SlicerIndex*/
    GPtrArray *tuples; /*an array of IndexedTuples sorted by the RECORDNUM property*/
    GTree *tuples_tree; /*a tree which essentially indexes the tuples array by tuple value rather than RECORDNUM*/

	unsigned int (*index_record) (GODataSlicerIndex *self, unsigned int record_num);
    void (*complete_index) (GODataSlicerIndex *self);
    void (*tuple_set_enabled) (GODataSlicerIndex *self, unsigned int tuple_record_num, gboolean is_enabled);
    void (*disable_all_tuples) (GODataSlicerIndex *self);
    guint (*get_tuple_index) (const GODataSlicerIndex *self, unsigned int tuple_record_num);
    GPtrArray * (*get_all_tuples) (const GODataSlicerIndex *self, gboolean only_enabled);
};

GType go_data_slicer_index_get_type (void);


/**
 * index_record:
 *
 * @self:           this GODataSlicerIndex
 * @record_num:     the record number within the cache to process.  Must be less than the total number of records in the cache.\
 *
 * Given a record number from the cache this SlicerIndex is associated with,
 * process the record to form a key tuple (by choosing fields according to this
 * this SlicerIndex's tuple template), and then perform an insertion for that
 * record.
 *
 * Returns: the new (or existing) tuple's unique record_num value 
 *          (either record_num which was passed, or if the tuple already existed, it's record_num)
 */
guint 
go_data_slicer_index_index_record (GODataSlicerIndex *self, unsigned int record_num);

/**
 * complete_index:
 *
 * @self:            this GODataSlicerIndex
 *
 * Used to make this index (essentially) immutable when all cache rows have 
 * been processed.  At this point, the various indexes of tuples will be 
 * finished up (mostly numbering stuff).
 * 
 */
void
go_data_slicer_index_complete_index (GODataSlicerIndex *self);


/**
 * get_tuple_index
 *
 * @self - this GODataSlicerIndex
 * @tuple_record_num - the record_num value of the tuple which should be searched for
 *
 * Given a tuple, identified by its record_num, return its position relative to
 * other tuples in this SlicerIndex sorted by tuple value.
 *
 * Returns: the tuple's position relative to other tuples in this SlicerIndex when sorted by tuple values
 */
unsigned int
go_data_slicer_index_get_tuple_index (const GODataSlicerIndex *self, unsigned int tuple_record_num);

/**
 * tuple_set_enabled:
 * 
 * @self:               this GODataSlicerIndex
 * @tuple_record_num:   the record_num value of the tuple which should be altered
 * @is_enabled:         true, if the tuple should be enabled, false otherwise.
 *
 * Given a tuple, identified by its record_num, enable or disable it based on
 * filters (as instructed by the Slicer).
 */
void
go_data_slicer_index_tuple_set_enabled (GODataSlicerIndex *self, unsigned int tuple_record_num, gboolean is_enabled);

/**
 * disable_all_tuples
 * 
 * @tuple_record_num:        the record_num value of the tuple which should be searched for
 *
 * Disable all tuples in this SlicerIndex, as though they had been disabled 
 * by filters.  They will be re-enabled by the Slicer as they are encountered
 * (in the situation where, for example, some filters are changed and the
 * whole Slicer has to recalculate all of its values).
 */
void
go_data_slicer_index_disable_all_tuples (GODataSlicerIndex *self);

/**
 * get_all_tuples:
 *
 * @tuple_record_num:   the record_num value of the tuple which should be searched for
 * @only_enabled        Whether or not to return only enabled tuples
 * Return all IndexedTuples in this SlicerIndex, sorted by tuple values.
 * Will not return those which are disabled by Page Filters if only_enabled == TRUE
 *
 * IF YOU ARE USING THIS FUNCTION: Be sure to decrease the ref count of each tuple
 * when you are finished with them.
 *
 * Returns: a GPtrArray of tuples, sorted by value
 */
GPtrArray *
go_data_slicer_index_get_all_tuples (const GODataSlicerIndex *self, gboolean only_enabled);

G_END_DECLS

#endif /* GO_DATA_SLICER_INDEX_H */
