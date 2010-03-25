/*
 * go-data-slicer-index.c :
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

#include <gnumeric-config.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include "go-data-slicer-index.h"
#include "go-data-slicer-tuple.h"

enum
{
	PROP_0,

    PROP_CACHE,     /*The cache associated with the slicer this slicer index belongs to*/
    PROP_TEMPLATE,  /*an array of CacheField objects representing which values in a cache record belong to tuples in this SlicerIndex*/
    PROP_COMPLETED  /*a flag which represents whether or not all cache rows that will be added to this SlicerIndex have been added*/
};

G_DEFINE_TYPE (GODataSlicerIndex, go_data_slicer_index, G_TYPE_OBJECT);

static void
go_data_slicer_index_init (GODataSlicerIndex *self)
{
    self->completed = FALSE;
	self->cache = NULL;
	self->tuple_template = NULL;
    self->tuples = NULL;
    self->tuples_tree = NULL;
	/* hook up instance methods */
	self->index_record = go_data_slicer_index_index_record;
	self->complete_index = go_data_slicer_index_complete_index;
    self->tuple_set_enabled = go_data_slicer_index_tuple_set_enabled;
    self->disable_all_tuples = go_data_slicer_index_disable_all_tuples;
    self->get_tuple_index = go_data_slicer_index_get_tuple_index;
    self->get_all_tuples = go_data_slicer_index_get_all_tuples;
}

static void
go_data_slicer_index_dispose (GObject *object) 
{
    GODataSlicerIndex *self;
    g_return_if_fail (IS_GO_DATA_SLICER_INDEX (object));	
	self = GO_DATA_SLICER_INDEX(object); /*Cast object into our type*/     

    /*unref stuff we built
      Note that we dont have to unref tuples twice since we didn't ref them
      when we put them in the tree*/
    g_ptr_array_foreach(self->tuples, (GFunc)g_object_unref, NULL);

    /*unref stuff from slicer*/
    g_ptr_array_foreach(self->tuple_template, (GFunc)g_object_unref, NULL);     
     
    /*unref stuff from cache*/
    g_object_unref(self->cache);

    /*nuke structures*/
    g_tree_destroy(self->tuples_tree);
    g_ptr_array_free(self->tuples, TRUE);
     
	G_OBJECT_CLASS (go_data_slicer_index_parent_class)->dispose(object);
}

static void
go_data_slicer_index_finalize (GObject *self)
{	
	G_OBJECT_CLASS (go_data_slicer_index_parent_class)->finalize (self);
}

static void
go_data_slicer_index_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GODataSlicerIndex *self;
	g_return_if_fail (IS_GO_DATA_SLICER_INDEX (object));	
	self = GO_DATA_SLICER_INDEX(object); /*Cast object into our type*/
	
	switch (prop_id)
	{
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
    case PROP_COMPLETED:
        self->completed = g_value_get_boolean (value);
        break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_index_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GODataSlicerIndex *self;
	g_return_if_fail (IS_GO_DATA_SLICER_INDEX (object));	
	self = GO_DATA_SLICER_INDEX(object); /*Cast object into our type*/

	
	switch (prop_id)
	{
	case PROP_CACHE:
		g_value_set_object (value, self->cache);
		break;	
	case PROP_TEMPLATE:
		g_value_set_pointer(value,self->tuple_template);
		break;
    case PROP_COMPLETED:
        g_value_set_boolean (value,self->completed);
        break;              
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_index_class_init (GODataSlicerIndexClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = go_data_slicer_index_finalize;
	object_class->dispose = go_data_slicer_index_dispose;
	object_class->set_property = go_data_slicer_index_set_property;
	object_class->get_property = go_data_slicer_index_get_property;

	g_object_class_install_property (object_class, PROP_CACHE,
		 g_param_spec_object ("cache", NULL, NULL,
			GO_DATA_CACHE_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_TEMPLATE,
	     g_param_spec_pointer ("tuple_template", NULL, NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class, PROP_COMPLETED,
         g_param_spec_boolean ("completed", NULL, NULL, FALSE, G_PARAM_READWRITE));
}

void 
go_data_slicer_index_index_record (GODataSlicerIndex *self, unsigned int record_num) {
	 g_return_if_fail(IS_GO_DATA_SLICER_INDEX(self));
	 /*TODO: implement - BE SURE TO EITHER MAKE A NEW TUPLE AND THEN DO NOT g_object_ref IT WHEN PUTTING IT IN THE TREE!*/
}

void
go_data_slicer_index_complete_index (GODataSlicerIndex *self) {
     /*TODO: implement*/  
}

void
go_data_slicer_index_tuple_set_enabled (GODataSlicerIndex *self, unsigned int tuple_record_num, gboolean is_enabled) {
     /*TODO: implement*/     
}

void
go_data_slicer_index_disable_all_tuples (GODataSlicerIndex *self) {
     /*TODO: implement*/     
}

guint
go_data_slicer_index_get_tuple_index (const GODataSlicerIndex *self, unsigned int tuple_record_num) {
     /*TODO: implement*/     
}

GPtrArray *
go_data_slicer_index_get_all_tuples (const GODataSlicerIndex *self) {
     /*TODO: implement*/
}
