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


/*******************************************************************************
 * GODataSlicerIndex                                                           *
 * An interface which describes the behaviour of a dictionary which maps keys  *
 * in the form of GODataSlicerTuples to sparse bitmaps which indicate the      *
 * records of the GODataCache which contain that tuple.                        *
 *                                                                             * 
 * This is an interface rather than an implementation so that the particular   *
 * data structure which underlies it may be easily swapped or improved in      *
 * the future.  We will be using a B+ tree to implement this interface.        *
 ******************************************************************************/

#ifndef GO_DATA_SLICER_INDEX_H
#define GO_DATA_SLICER_INDEX_H

#include <glib-object.h>
#include "go-data-slicer-bitmap.h"

G_BEGIN_DECLS

#define GO_DATA_SLICER_INDEX_TYPE	  (go_data_slicer_index_get_type ())
#define GO_DATA_SLICER_INDEX(o)		  (G_TYPE_CHECK_INSTANCE_CAST ((o), GO_DATA_SLICER_INDEX_TYPE, GODataSlicerIndex))
#define IS_GO_DATA_SLICER_INDEX(o)	  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GO_DATA_SLICER_INDEX_TYPE))
#define GO_DATA_SLICER_INDEX_CLASS(k)	  (G_TYPE_CHECK_CLASS_CAST ((k), GO_DATA_SLICER_INDEX_TYPE, GODataSlicerIndexInterface))
#define IS_GO_DATA_SLICER_INDEX_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GO_DATA_SLICER_INDEX_TYPE))
#define GO_DATA_SLICER_INDEX_GET_INTERFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GO_DATA_SLICER_INDEX_TYPE, GODataSlicerIndexInterface))

typedef struct _GODataSlicerIndex GODataSlicerIndex; /* dummy object */
typedef struct _GODataSlicerIndexInterface GODataSlicerIndexInterface;

/**Resolve circular dependency between this and go-data-slicer-tuple***********/
G_END_DECLS
#include "go-data-slicer-tuple.h"
G_BEGIN_DECLS
/******************************************************************************/

struct _GODataSlicerIndexInterface {
	GTypeInterface		   base;

	GPtrArray * (*get_tuple_template) (const GODataSlicerIndex *self);
	void (*set_tuple_template) (GODataSlicerIndex *self, GPtrArray *tuple_template);
	void (*index_record) (GODataSlicerIndex *self, unsigned int record_num);
	GODataSlicerBitmap * (*retrieve_bitmap) (const GODataSlicerIndex *self, const GODataSlicerTuple *tuple);
    void (*retrieve_all_bitmaps) (const GODataSlicerIndex *self, GPtrArray *tuples, GPtrArray *bitmaps);
};

GType go_data_slicer_index_get_type (void);


/**
 * get_tuple_template
 * 
 * Returns an array of pointers to go-data-cache-field objects which
 * represent the ordered set of fields which tuples in this index draw values
 * from.
 *
 * @param self - this GODataSlicerIndex
 * @return a GPtrArray of go-data-cache-fields representing this SlicerIndex's tuple template.
 */
GPtrArray * 
go_data_slicer_index_get_tuple_template (const GODataSlicerIndex *self);

/**
 * set_tuple_template
 * 
 * Sets the tuple template of this SlicerIndex to tuple_template.
 * PRECONDITION: the go-data-cache-fields in tuple_template belong to this
 * SlicerIndex's cache.
 *
 * @param self - this GODataSlicerIndex
 * @param tuple_template - a GPtrArray of go-data-cache-fields
 */
void 
go_data_slicer_index_set_tuple_template (GODataSlicerIndex *self, GPtrArray *tuple_template);

/**
 * index_record
 *
 * Given a record number from the cache this SlicerIndex is associated with,
 * process the record to form a key tuple (by choosing fields according to this
 * this SlicerIndex's tuple template), and then perform an insertion for that
 * record.  Create the tuple and associated bitmap if necessary, otherwise, find
 * it and change the bit for record_num to a 1.
 *
 * @param self - this GODataSlicerIndex
 * @param record_num - the record number within the cache to process.  Must be less than the total number of records in the cache.
 */
void 
go_data_slicer_index_index_record (GODataSlicerIndex *self, unsigned int record_num);

/**
 * retrieve_bitmap
 * 
 * Given a tuple which is compatible with this SlicerIndex (same length and 
 * field types), return the bitmap which is associated with tuple.
 *
 * @param self - this GODataSlicerIndex
 * @param tuple - the GODataSlicerTuple to search for
 * @return the bitmap associated with tuple, or NULL if the tuple does not exist
 */
GODataSlicerBitmap * 
go_data_slicer_index_retrieve_bitmap (const GODataSlicerIndex *self, const GODataSlicerTuple *tuple);

/**
 * retrieve_all_bitmaps
 *
 * Returns all bitmaps and tuples indexed by this SlicerIndex in sorted order
 * (by tuple).
 *
 * @param self - this GODataSlicerIndex
 * @param tuples - an empty GPtrArray to fill with tuples in sorted order
 * @param bitmaps - an empty GPtrArray to fill with bitmaps such that the bitmap for tuple i in tuples is at position i in bitmaps.
 */
void
go_data_slicer_index_retrieve_all_bitmaps (const GODataSlicerIndex *self, GPtrArray *tuples, GPtrArray *bitmaps);

G_END_DECLS

#endif /* GO_DATA_SLICER_INDEX_H */
