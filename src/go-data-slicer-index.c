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
#include <glib/gprintf.h>

enum
{
	PROP_0,

    PROP_CACHE,     /*The cache associated with the slicer this slicer index belongs to*/
    PROP_TEMPLATE,  /*an array of CacheField objects representing which values in a cache record belong to tuples in this SlicerIndex*/
    PROP_COMPLETED  /*a flag which represents whether or not all cache rows that will be added to this SlicerIndex have been added*/
};

static GODataSlicerIndexedTuple *
go_data_slicer_index_get_indexed_tuple_by_tuple_record_num(const GODataSlicerIndex *self, unsigned int tuple_record_num) {         
          
    guint lower = 0;
    guint upper = MIN(tuple_record_num, self->tuples->len-1);   
    guint pivot = upper/2;
    GODataSlicerIndexedTuple * pivot_tuple;

    g_return_val_if_fail(self->completed==TRUE,NULL);
     
    /*We know that the tuples array is sorted by record_num in ascending order
      so we can binary search for the desired tuple to set its flag*/    
    pivot_tuple = g_ptr_array_index(self->tuples,pivot);
    while (pivot_tuple->tuple->record_num != tuple_record_num) {
	/*g_printf("L:%u,P:%u,U:%u,D:%u,C:%u\n", lower, pivot, upper,tuple_record_num,pivot_tuple->tuple->record_num);*/
        if (tuple_record_num < pivot_tuple->tuple->record_num) {
             upper = pivot-1;
             pivot = (upper+lower)/2;
             pivot_tuple = g_ptr_array_index(self->tuples,pivot);
        } else if (tuple_record_num > pivot_tuple->tuple->record_num) {
             lower = pivot+1;
             pivot = (upper+lower)/2;
             pivot_tuple = g_ptr_array_index(self->tuples,pivot);
        } else if ((upper == lower) && (tuple_record_num != pivot_tuple->tuple->record_num)) {
             return NULL;
        } else {
             break;
        }
    }
    return (GODataSlicerIndexedTuple*) pivot_tuple;
}


G_DEFINE_TYPE (GODataSlicerIndex, go_data_slicer_index, G_TYPE_OBJECT);

static void
go_data_slicer_index_init (GODataSlicerIndex *self)
{
    self->completed = FALSE;
	self->cache = NULL;
	self->tuple_template = NULL;
    self->tuples = g_ptr_array_new();
    self->tuples_tree = g_tree_new((GCompareFunc)go_data_slicer_tuple_compare_to);
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
    guint i;
    GODataSlicerIndexedTuple * indexed_tuple;
    GODataSlicerIndex *self;
    g_return_if_fail (IS_GO_DATA_SLICER_INDEX (object));	
	self = GO_DATA_SLICER_INDEX(object); /*Cast object into our type*/     

    /*unref stuff we built
      Note that we dont have to unref tuples twice since we didn't ref them
      when we put them in the tree - the array 'holds' the reference.*/
    for (i=0;i<self->tuples->len;i++) {
         indexed_tuple = (GODataSlicerIndexedTuple *) g_ptr_array_index(self->tuples,i);
         g_object_unref(indexed_tuple->tuple);
	 g_free(indexed_tuple);
    }

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

unsigned int
go_data_slicer_index_index_record (GODataSlicerIndex *self, unsigned int record_num) {
     GODataSlicerIndexedTuple * new_indexed_tuple;
     GODataSlicerIndexedTuple * existing_tuple;
     GODataSlicerTuple * new_tuple;

     g_warn_if_fail(self->completed == FALSE);
     
     /*Create a tuple to represent the record record_num*/     
     new_tuple = g_object_new(GO_DATA_SLICER_TUPLE_TYPE, "cache", self->cache, "tuple_template", self->tuple_template, "record_num", record_num, NULL);
     /*Search for tuple in the tree*/
     existing_tuple = g_tree_lookup(self->tuples_tree, new_tuple);
     if (existing_tuple) {
          /*won't be needing the new_tuple*/
          g_object_unref (new_tuple);
          /*if it's found, return its record_num*/
          return existing_tuple->tuple->record_num;
     } else {
          /*if it isn't found, create it and perform insertions*/
	  new_indexed_tuple = g_malloc(sizeof(GODataSlicerIndexedTuple));
          new_indexed_tuple->enabled = TRUE;  /*enable tuples by default*/
          new_indexed_tuple->relative_position = 0;
          new_indexed_tuple->tuple = new_tuple;
          g_ptr_array_add(self->tuples, new_indexed_tuple);
          g_tree_insert(self->tuples_tree, new_tuple, new_indexed_tuple);
          return new_tuple->record_num;
     }
}

static void
tuples_tree_traversal_function (gconstpointer key, gconstpointer value, gpointer user_data) {
     GPtrArray * accumulator = (GPtrArray *) user_data;
     GODataSlicerIndexedTuple * indexed_tuple = (GODataSlicerIndexedTuple *) value;
     g_ptr_array_add(accumulator, indexed_tuple);
}

void
go_data_slicer_index_complete_index (GODataSlicerIndex *self) {
    guint i;
    GODataSlicerIndexedTuple * tuple;
    GPtrArray * accumulator;

    g_return_if_fail(self->completed == FALSE);
     
    accumulator = g_ptr_array_sized_new(g_tree_nnodes(self->tuples_tree));
    g_tree_foreach(self->tuples_tree, (GTraverseFunc) tuples_tree_traversal_function, accumulator);

    for(i=0;i<accumulator->len;i++) {
         tuple = (GODataSlicerIndexedTuple *) g_ptr_array_index(accumulator,i);
         tuple->relative_position = i;
    }

    self->completed = TRUE; /*Complete this slicer index, making it immutable*/
    g_ptr_array_free(accumulator, TRUE); /*Free up the accumulator array*/
}

void
go_data_slicer_index_tuple_set_enabled (GODataSlicerIndex *self, unsigned int tuple_record_num, gboolean is_enabled) {
	GODataSlicerIndexedTuple tuple;
	void * result = go_data_slicer_index_get_indexed_tuple_by_tuple_record_num(self,tuple_record_num);
	if (result != NULL) {
		tuple = *((GODataSlicerIndexedTuple *)result);
		tuple.enabled = is_enabled;
	}
}

void
go_data_slicer_index_disable_all_tuples (GODataSlicerIndex *self) {
    guint i;
    GODataSlicerIndexedTuple indexed_tuple;
    
    for (i=0;i<self->tuples->len;i++) {
         indexed_tuple = *((GODataSlicerIndexedTuple *)g_ptr_array_index(self->tuples, i));
         indexed_tuple.enabled = FALSE;
    }
}

guint
go_data_slicer_index_get_tuple_index (const GODataSlicerIndex *self, unsigned int tuple_record_num) {
    GODataSlicerIndexedTuple * tuple;     
    g_warn_if_fail(self->completed == TRUE);

    tuple = (GODataSlicerIndexedTuple *) go_data_slicer_index_get_indexed_tuple_by_tuple_record_num(self, tuple_record_num);
    return tuple->relative_position;
}

GPtrArray *
go_data_slicer_index_get_all_tuples (const GODataSlicerIndex *self) {     
    guint i;
    GODataSlicerIndexedTuple * indexed_tuple;
    GPtrArray * accumulator = g_ptr_array_sized_new(g_tree_nnodes(self->tuples_tree));
    GPtrArray * result = g_ptr_array_new();
     
    g_return_val_if_fail(self->completed == TRUE, NULL);

    g_tree_foreach(self->tuples_tree, (GTraverseFunc) tuples_tree_traversal_function, accumulator);

    for(i=0;i<accumulator->len;i++) {
        indexed_tuple = (GODataSlicerIndexedTuple *) g_ptr_array_index(accumulator,i);
        if (indexed_tuple->enabled) {
             g_ptr_array_add(result, indexed_tuple);
             /*Increase reference count to that tuple since someone else will be using it*/
             g_object_ref(indexed_tuple->tuple);
        }
    }
    return result;
}
