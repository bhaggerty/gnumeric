/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * gnumeric
 * Copyright (C) Sean McIntyre 2010 <s.mcintyre@utoronto.ca>
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

#ifndef _GO_DATA_SLICER_FIELD_H_
#define _GO_DATA_SLICER_FIELD_H_

#include <gnumeric-config.h>
#include "go-data-cache-field-impl.h"
#include "go-data-cache-impl.h"
#include <go-val.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <glib/gprintf.h>
#include <string.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GO_DATA_SLICER_FIELD_TYPE             (go_data_slicer_field_get_type ())
#define GO_DATA_SLICER_FIELD(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GO_DATA_SLICER_FIELD_TYPE, GODataSlicerField))
#define GO_DATA_SLICER_FIELD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GO_DATA_SLICER_FIELD_TYPE, GODataSlicerFieldClass))
#define IS_GO_DATA_SLICER_FIELD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GO_DATA_SLICER_FIELD_TYPE))
#define IS_GO_DATA_SLICER_FIELD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GO_DATA_SLICER_FIELD_TYPE))
#define GO_DATA_SLICER_FIELD_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GO_DATA_SLICER_FIELD_TYPE, GODataSlicerFieldClass))

typedef struct _GODataSlicerFieldClass GODataSlicerFieldClass;
/*typedef struct _GODataSlicerField GODataSlicerField; declared in goffice-data*/

struct _GODataSlicerFieldClass
{
	GObjectClass parent_class;
};

struct _GODataSlicerField
{
	GObject parent_instance;

	GPtrArray * cache_fields;

	GOVal const * (*get_val) (GODataSlicerField const *self, unsigned int record_num);
	void (*dump_val) (GODataSlicerField const *self, unsigned int record_num);	
	void (*dump_cols) (GODataSlicerField const *self);
};

GType go_data_slicer_field_get_type (void) G_GNUC_CONST;

/**
 * get_val:
 *
 * @self:           this GODataSlicerField
 * @record_num:     the record number within the cache to retrieve a value from,
 *					taking into account all bucketing/grouping.  
 * 
 * Returns the single representative value for that record, from the perspective
 * of this slicer field.
 *
 * Returns: the representative value at record_num in the cache for this 
 *          SlicerField's primary CacheField, taking into account all 
 *          bucketing/grouping
 */
GOVal const	 * go_data_slicer_field_get_val   (GODataSlicerField const *self, unsigned int record_num);

/**
 * dump_val:
 *
 * @self:           this GODataSlicerField
 * @record_num:     the record number within the cache to retrieve a value from,
 *					taking into account all bucketing/grouping.
 *
 * Prints the value of this Slicer Field at a particular record num.  This
 * 'value' is really the value of all underlying Cache Fields, taking into
 * account bucketing.
 *
 */
void go_data_slicer_field_dump_val (GODataSlicerField const *self, unsigned int record_num);

/**
 * dump_cols:
 *
 * @self:           this GODataSlicerField
 *
 * Dumps a string representation of the cache columns which contribute to
 * this SlicerField to the screen.
 */
void go_data_slicer_field_dump_cols (GODataSlicerField const *self);

G_END_DECLS

#endif /* _GO_DATA_SLICER_FIELD_H_ */
