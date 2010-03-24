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

#include <gnumeric-config.h>
#include <go-val.h>
#include "go-data-slicer-tuple.h"
#include "go-data-cache-field-impl.h"
#include "go-data-cache-field.h"
#include "go-data-cache-impl.h"
#include "go-data-cache.h"

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

enum
{
	PROP_0,

	PROP_RECORDNUM,
	PROP_CACHE,
	PROP_INDEX
};



G_DEFINE_TYPE (GODataSlicerTuple, go_data_slicer_tuple, G_TYPE_OBJECT);

static void
go_data_slicer_tuple_init (GODataSlicerTuple *self)
{
	self->cache = NULL;
	self->slicer_index = NULL;
	/* hook up instance methods */
	self->compare_to = go_data_slicer_tuple_compare_to;
	
}

static void
go_data_slicer_tuple_finalize (GObject *self)
{
	G_OBJECT_CLASS (go_data_slicer_tuple_parent_class)->finalize (self);
}

static void
go_data_slicer_tuple_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GODataSlicerTuple *self;
	g_return_if_fail (IS_GO_DATA_SLICER_TUPLE (object));	
	self = GO_DATA_SLICER_TUPLE(object); /*Cast object into our type*/
	
	switch (prop_id)
	{
	case PROP_RECORDNUM:
		self->record_num = g_value_get_int (value);
		break;
	case PROP_CACHE:
		self->cache = g_value_get_object (value);
		break;
	case PROP_INDEX:
		self->slicer_index = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_tuple_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GODataSlicerTuple *self;
	g_return_if_fail (IS_GO_DATA_SLICER_TUPLE (object));
	self = GO_DATA_SLICER_TUPLE(object); /*Cast object into our type*/

	
	switch (prop_id)
	{
	case PROP_RECORDNUM:
		g_value_set_int(value, self->record_num);
		break;
	case PROP_CACHE:
		g_value_set_object (value, self->cache);
		break;	
	case PROP_INDEX:
		g_value_set_object(value,self->slicer_index);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_tuple_class_init (GODataSlicerTupleClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = go_data_slicer_tuple_finalize;
	object_class->set_property = go_data_slicer_tuple_set_property;
	object_class->get_property = go_data_slicer_tuple_get_property;

	g_object_class_install_property (object_class, PROP_CACHE,
		 g_param_spec_object ("cache", NULL, NULL,
			GO_DATA_CACHE_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	
	g_object_class_install_property (object_class,
	                                 PROP_RECORDNUM,
	                                 g_param_spec_int    ("record_num",
	                                                      "record_num",
	                                                      "The record in the cache this tuple draws values from.",
	                                                      -1,
	                                                      UINT_MAX,
	                                                      -1,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

int go_data_slicer_tuple_compare_to (const GODataSlicerTuple * self, const GODataSlicerTuple * other) {
	guint i, comparison;
	GPtrArray * tuple_template;	
	g_warn_if_fail(self->slicer_index == other->slicer_index);

	/*Get the set of columns (types and values) for this (and others') tuple*/
	tuple_template = GO_DATA_SLICER_INDEX_GET_INTERFACE(self->slicer_index)->get_tuple_template(self->slicer_index);
	/*Iterate over tuple values, comparing left-to-right*/
	comparison = 0;
	for (i=0;i<tuple_template->len;i++) {
		GODataCacheField * column = g_ptr_array_index(tuple_template, i);
		comparison = go_val_cmp(go_data_cache_field_get_val(column,self->record_num), go_data_cache_field_get_val(column, other->record_num));
		if (comparison != 0) return comparison;
	}
	return comparison;
}