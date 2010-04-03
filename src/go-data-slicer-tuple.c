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
#include "value.h"

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <glib/gprintf.h>

enum
{
	PROP_0,

	PROP_RECORDNUM,
	PROP_CACHE,
	PROP_TEMPLATE	
};



G_DEFINE_TYPE (GODataSlicerTuple, go_data_slicer_tuple, G_TYPE_OBJECT);

static void
go_data_slicer_tuple_init (GODataSlicerTuple *self)
{
	self->cache = NULL;
	self->tuple_template = NULL;
	/* hook up instance methods */
	self->compare_to = go_data_slicer_tuple_compare_to;
	
}

static void
go_data_slicer_tuple_dispose (GObject *object) 
{
	GODataSlicerTuple *self;
	g_return_if_fail (IS_GO_DATA_SLICER_TUPLE (object));		
	self = GO_DATA_SLICER_TUPLE(object); /*Cast object into our type*/

	/*unref stuff from slicer*/
    g_ptr_array_foreach(self->tuple_template, (GFunc)g_object_unref, NULL);  
	
	/*unref stuff from cache*/
    g_object_unref(self->cache);
	
	G_OBJECT_CLASS (go_data_slicer_tuple_parent_class)->dispose(object);
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
		self->record_num = g_value_get_uint (value);
		break;
	case PROP_CACHE:
        if (self->cache) {
            g_object_unref(self->cache); /*decrease reference count of old cache*/
        }
		self->cache = g_value_get_object (value);
        g_object_ref(self->cache); /*increase reference count of new cache*/
		break;
	case PROP_TEMPLATE:
		if (self->tuple_template) {
            g_ptr_array_foreach(self->tuple_template, (GFunc)g_object_unref, NULL); /*decrease reference count of old template stuff*/            
        }
		self->tuple_template = g_value_get_pointer (value);
        g_ptr_array_foreach(self->tuple_template, (GFunc)g_object_ref, NULL); /*increase reference count of new template stuff*/            
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
		g_value_set_uint(value, self->record_num);
		break;
	case PROP_CACHE:
		g_value_set_object (value, self->cache);
		break;	
	case PROP_TEMPLATE:
		g_value_set_pointer(value,self->tuple_template);
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
	object_class->dispose = go_data_slicer_tuple_dispose;
	object_class->set_property = go_data_slicer_tuple_set_property;
	object_class->get_property = go_data_slicer_tuple_get_property;

	g_object_class_install_property (object_class, PROP_CACHE,
		 g_param_spec_object ("cache", NULL, NULL,
			GO_DATA_CACHE_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_TEMPLATE,
	     g_param_spec_pointer ("tuple_template", NULL, NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
			
	
	g_object_class_install_property (object_class,
	                                 PROP_RECORDNUM,
	                                 g_param_spec_uint    ("record_num",
	                                                      NULL,
	                                                      "The record in the cache this tuple draws values from.",
	                                                      0,
	                                                      UINT_MAX,
	                                                      0,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

gint go_data_slicer_tuple_compare_to (const GODataSlicerTuple * self, const GODataSlicerTuple * other) {
	guint i;
	gint comparison;
	const GOVal * selfVal;
	const GOVal * otherVal;	
	g_warn_if_fail(self->tuple_template == other->tuple_template);	
	g_warn_if_fail(self->cache == other->cache);
	
	/*Iterate over tuple values, comparing left-to-right*/
	comparison = 0; /*Assume equality and return appropriate value if any inequality is discovered*/
	for (i=0;i<self->tuple_template->len;i++) {
		GODataSlicerField * column = g_ptr_array_index(self->tuple_template, i);		
		
		/*Retrieve and compare values*/
		selfVal = go_data_slicer_field_get_val(column,self->record_num);
		otherVal = go_data_slicer_field_get_val(column, other->record_num);
		/*If one of the values is NULL*/
		if (selfVal == NULL && otherVal != NULL) {
			return -1;
		} else if (otherVal == NULL && selfVal != NULL) {
			return 1;
		}
		
		/*Otherwise, perform comparison*/
		comparison = go_val_cmp(&selfVal, &otherVal);
		if (comparison != 0) return comparison;
	}
	return comparison;
}

void go_data_slicer_tuple_dump_tuple(GODataSlicerTuple * tuple) {
	guint i;
	GODataSlicerField * column;
	
	g_printf("[");
	for (i=0;i<tuple->tuple_template->len;i++) {
		column = g_ptr_array_index(tuple->tuple_template, i);
		go_data_slicer_field_dump_val (column, tuple->record_num);
		if (i != tuple->tuple_template->len-1) g_printf(" ");
	}
	g_printf("]");
}
