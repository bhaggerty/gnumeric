/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * gnumeric
 * Copyright (C) 2010 Sean McIntyre <s.mcintyre@utoronto.ca> 
 *                    David Algar   <david.algar@utoronto.ca>
 * 
 * gnumeric is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * gnumeric is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 * GODataSlicerTuple                                                           *
 *                                                                             *
 * Represents a tuple of values from a record in the cache.  In particular, it *
 * belongs to a SlicerIndex which dictates which Cache Fields from a record    *
 * are tupelized, and contains a record number to indicate the record in which *
 * those values are contained.  Can be compared to another tuple from the same *
 * SlicerIndex.                                                                *
 *******************************************************************************/

#ifndef _GO_DATA_SLICER_TUPLE_H
#define _GO_DATA_SLICER_TUPLE_H

#include <glib-object.h>
#include "go-data-cache-impl.h"

G_BEGIN_DECLS

#define GO_DATA_SLICER_TUPLE_TYPE            (go_data_slicer_tuple_get_type ())
#define GO_DATA_SLICER_TUPLE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GO_DATA_SLICER_TUPLE_TYPE, GODataSlicerTuple))
#define GO_DATA_SLICER_TUPLE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GO_DATA_SLICER_TUPLE_TYPE, GODataSlicerTupleClass))
#define IS_GO_DATA_SLICER_TUPLE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GO_DATA_SLICER_TUPLE_TYPE))
#define IS_GO_DATA_SLICER_TUPLE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GO_DATA_SLICER_TUPLE_TYPE))
#define GO_DATA_SLICER_TUPLE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GO_DATA_SLICER_TUPLE_TYPE, GODataSlicerTupleClass))

typedef struct _GODataSlicerTupleClass GODataSlicerTupleClass;
typedef struct _GODataSlicerTuple GODataSlicerTuple;

struct _GODataSlicerTupleClass
{
	GObjectClass parent_class;
};

struct _GODataSlicerTuple
{
	GObject parent_instance;
	GODataCache	*cache;
	GPtrArray *tuple_template;
	unsigned int record_num;
	
	gint (*compare_to) (const GODataSlicerTuple * self, const GODataSlicerTuple * other);
};

GType go_data_slicer_tuple_get_type (void) G_GNUC_CONST;

/**
 * comapare_to
 *
 * Compares this tuple to another tuple with the same tuple template.
 *
 * @param self - this tuple
 * @param other - the tuple to compare this tuple to
 * @return 1 if this tuple should appear before other, -1 if this tuple should appear after other, and 0 otherwise (equality).
 */
gint go_data_slicer_tuple_compare_to (const GODataSlicerTuple * self, const GODataSlicerTuple * other);

G_END_DECLS

#endif /* _GO_DATA_SLICER_TUPLE_H_ */
