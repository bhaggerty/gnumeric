/*
 * go-data-slicer.h : The definition of a content for a data slicer
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

#include <gnumeric-config.h>
#include "go-data-slicer-impl.h"
#include "go-data-cache.h"
#include "gnm-data-cache-source.h"
#include "go-data-slicer-index.h"
#include "go-data-slicer-cache-overlay.h"
#include "go-data-slicer-tuple.h"
#include "go-val.h"
#include "value.h"
#include "numbers.h"
#include "position.h"

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <glib/gprintf.h>

#define GO_DATA_SLICER_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), GO_DATA_SLICER_TYPE, GODataSlicerClass))
#define IS_GO_DATA_SLICER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GO_DATA_SLICER_TYPE))
#define GO_DATA_SLICER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GO_DATA_SLICER_TYPE, GODataSlicerClass))

enum {
	PROP_0,
	
	PROP_CACHE,	/* GODataCache * */
	PROP_NAME,	/* GOString */
	PROP_AGGFUNC    /*The AggregateFunction chosen for this Slicer*/
};

/*
 * Put a value at a particular set of coordinates in the view
 */
static void
go_data_slicer_put_value_at(GODataSlicer *self, int x, int y, GOVal * value) {
    GnmCellPos key;
    /*Check to make sure the coordinates are within range*/
    g_return_if_fail(x <= (int)(self->col_field->tuples->len));
    g_return_if_fail(y <= (int)(self->row_field->tuples->len));
     
    /*Use GnmCellPos hash function to hash position*/
    key.col = x;
    key.row = y;
    /*Perform insertion*/
    g_hash_table_insert(self->view, &key, value);
}

static gint
go_data_slicer_uint_cmp(guint * a, guint * b) {
     if (*a < *b) {
          return -1;
     } else if (*a > *b) {
          return 1;
     } else {
          return 0;
     }
}

/*
 * Return a (potentially partial) aggregate value based on a partial aggregate 
 * value and a contribution.
 *
 * All the dirt that has to do with different aggregate functions and types
 * gets dealt with here.
 */
static GOVal *
go_data_slicer_contribute_to_aggregate_val(GODataSlicer *self, GOVal * original, const GOVal * contribution) {
     GnmValueType toriginal, tcontribution;
    
    toriginal = VALUE_IS_EMPTY (original) ? VALUE_EMPTY : original->type;
    tcontribution = VALUE_IS_EMPTY (contribution) ? VALUE_EMPTY : contribution->type;
    
     switch (self->aggregate_function) {
          case SUM:
               /*Note that VALUE_FLOAT encompasses all numeric types*/
               if (toriginal== VALUE_FLOAT && tcontribution == VALUE_FLOAT) {                   
                    original->v_float.val += contribution->v_float.val;
               } else {
                    /*FIXME: Not sure what to do if the value isn't numeric*/
                    g_warn_if_reached();
               }
               break;
          case COUNT:
               /*Note that VALUE_FLOAT encompasses all numeric types*/
               if (toriginal == VALUE_FLOAT) {
                   original->v_float.val += 1.0f; /*count ignores any data fields*/
               } else {
                    /*FIXME: Not sure what to do if the value isn't numeric*/
                    g_warn_if_reached();
               }
               break;
          default:
               g_warn_if_reached();
               break;
               
     }
     return original;
}

static void
go_data_slicer_init (GODataSlicer *self)
{
	self->name  = NULL;
	self->cache = NULL;
	self->cache_overlay = NULL;	
	self->col_field = NULL;
	self->row_field = NULL;
    self->data_field = NULL;
	self->page_filters = g_ptr_array_new ();
	self->view = g_hash_table_new((GHashFunc)gnm_cellpos_hash, (GEqualFunc)gnm_cellpos_equal);
}


static GObjectClass *parent_klass;
static void
go_data_slicer_finalize (GObject *obj)
{
	GODataSlicer *self = (GODataSlicer *)obj;
	go_data_slicer_set_cache (self, NULL);

    g_object_unref(self->cache_overlay);
	g_object_unref(self->col_field);
	g_object_unref(self->row_field);
    g_object_unref(self->data_field);
	g_ptr_array_foreach(self->page_filters,(GFunc)g_object_unref, NULL);
    g_ptr_array_foreach(self->disabled_values, (GFunc)g_tree_destroy, NULL);
	g_ptr_array_free(self->page_filters, TRUE);
	g_hash_table_destroy(self->view);
	
	go_string_unref (self->name); self->name   = NULL;

	(parent_klass->finalize) (obj);
}

static void
go_data_slicer_set_property (GObject *obj, guint property_id,
				  GValue const *value, GParamSpec *pspec)
{
	GODataSlicer *ds = (GODataSlicer *)obj;

	switch (property_id) {
	case PROP_CACHE : go_data_slicer_set_cache (ds, g_value_get_object (value)); break;
	case PROP_NAME :  go_string_unref (ds->name); ds->name = g_value_dup_boxed (value); break;
	case PROP_AGGFUNC : ds->aggregate_function = g_value_get_uint (value); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_slicer_get_property (GObject *obj, guint property_id,
				    GValue *value, GParamSpec *pspec)
{
	GODataSlicer const *ds = (GODataSlicer const *)obj;
	switch (property_id) {
	case PROP_CACHE : g_value_set_object (value, ds->cache); break;
	case PROP_NAME  : g_value_set_boxed (value, ds->name); break;
	case PROP_AGGFUNC  : g_value_set_uint(value, ds->aggregate_function); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_slicer_class_init (GODataSlicerClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	gobject_class->set_property	= go_data_slicer_set_property;
	gobject_class->get_property	= go_data_slicer_get_property;
	gobject_class->finalize		= go_data_slicer_finalize;

	g_object_class_install_property (gobject_class, PROP_CACHE,
		 g_param_spec_object ("cache", NULL, NULL,
			GO_DATA_CACHE_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_boxed ("name", NULL, NULL, go_string_get_type (),
			GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property(gobject_class, PROP_AGGFUNC,
	         g_param_spec_uint ("aggregate_function", NULL, NULL, 0, NUM_AGGREGATE_FUNCTIONS-1,
	                            COUNT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
	parent_klass = g_type_class_peek_parent (klass);
}

GSF_CLASS (GODataSlicer, go_data_slicer,
	   go_data_slicer_class_init, go_data_slicer_init,
	   G_TYPE_OBJECT)


void go_data_slicer_create_cache(GODataSlicer *self, Sheet * sheet, GnmRange * range) {
     GODataCache * cache = g_object_new(GO_DATA_CACHE_TYPE, NULL);
     go_data_cache_build_cache(cache, sheet, range);
     self->cache = cache;

     /*Create overlay*/
     self->cache_overlay = g_object_new(GO_DATA_SLICER_CACHE_OVERLAY_TYPE, "num_records", go_data_cache_num_items(cache), "cache", self->cache, NULL);
}

/**
 * go_data_slicer_get_cache :
 * @self : #GODataSlicer
 *
 * Does not add a reference.
 *
 * Returns : the #GODataCache associated with @self
 **/
GODataCache *
go_data_slicer_get_cache (GODataSlicer const *self)
{
	g_return_val_if_fail (IS_GO_DATA_SLICER (self), NULL);
	return self->cache;
}

/**
 * go_data_slicer_set_cache:
 * @ds : #GODataSlicer
 * @cache : #GODataCache
 *
 * Assign @cache to @self, and adds a reference to @cache
 **/
void
go_data_slicer_set_cache (GODataSlicer *self, GODataCache *cache)
{
	g_return_if_fail (IS_GO_DATA_SLICER (self));

	if (NULL != cache) {
		g_object_ref (G_OBJECT (cache));
	}
	
	if (NULL != self->cache) {
		g_object_unref (self->cache);
		if (NULL != self->cache_overlay) {
			g_object_unref(self->cache_overlay);
		}
	}
	
	self->cache = cache;
	self->cache_overlay = g_object_new(GO_DATA_SLICER_CACHE_OVERLAY_TYPE, "num_records", go_data_cache_num_items(self->cache), "cache", self->cache, NULL);
}

/* 
 * Helper private method for set_col_field_index, set_row_field_index and 
 * add_page_field_index.  See their comments in go-data-slicer.h for
 * details
 */
static GODataSlicerIndex *
go_data_slicer_create_index (GODataSlicer *self, GArray * tuple_template) {
    guint i, index;
    GPtrArray * cache_fields;    

    /*Convert integer tuple template into a GODataCacheField tuple template*/
    cache_fields = g_ptr_array_sized_new(tuple_template->len);
    for (i=0;i<tuple_template->len;i++) {
         index = g_array_index(tuple_template, guint, i);
         if (index < go_data_cache_num_fields (self->cache)) {
             g_ptr_array_add(cache_fields, go_data_cache_get_field(self->cache, index));
         } else {
             g_warn_if_reached();
         }
    }

    /*Construct a new SlicerIndex for column fields*/
    return g_object_new(GO_DATA_SLICER_INDEX_TYPE, "cache", self->cache, "tuple_template", cache_fields, NULL);
}

void go_data_slicer_set_col_field_index (GODataSlicer *self, GArray * tuple_template) {
    g_return_if_fail (IS_GO_DATA_SLICER (self));
    g_return_if_fail (self->cache != NULL);
    self->col_field = go_data_slicer_create_index (self, tuple_template);
}

void go_data_slicer_set_row_field_index (GODataSlicer *self, GArray * tuple_template) {
    g_return_if_fail (IS_GO_DATA_SLICER (self));
    g_return_if_fail (self->cache != NULL);     
    self->row_field = go_data_slicer_create_index (self, tuple_template);
}

void go_data_slicer_add_page_field_index (GODataSlicer *self, GArray * tuple_template) {
    g_return_if_fail (IS_GO_DATA_SLICER (self));
    g_return_if_fail (self->cache != NULL);     
    g_ptr_array_add(self->page_filters, go_data_slicer_create_index (self, tuple_template));
    g_ptr_array_add(self->disabled_values, g_tree_new((GCompareFunc)go_data_slicer_uint_cmp));
}

void go_data_slicer_set_data_field_index(GODataSlicer *self, guint column_number) {
    g_return_if_fail (IS_GO_DATA_SLICER (self));
    g_return_if_fail (self->cache != NULL);     
    self->data_field = go_data_cache_get_field (self->cache, column_number);
}

void
go_data_slicer_index_cache (GODataSlicer *self) {
     /*Create the cache overlay*/
     guint i,j,tuple_val;
     GODataSlicerIndex * page;
     g_return_if_fail (IS_GO_DATA_SLICER (self));
     g_return_if_fail(self->cache);
     g_return_if_fail(self->cache_overlay);
     g_return_if_fail(self->col_field);
     g_return_if_fail(self->row_field);
     
     /*Iterate over and process cache records to populate the overlay*/
     for (i=0;i<go_data_cache_num_items(self->cache);i++) {          
          /*set up new overlay record*/
          GODataSlicerCacheOverlayRecord * overlay_record = g_malloc(sizeof(GODataSlicerCacheOverlayRecord));          
          overlay_record->filter_tuples = g_array_sized_new(FALSE,TRUE,sizeof(guint),self->page_filters->len);
          
          /*record column tuple*/
          overlay_record->col_tuple = self->col_field->index_record(self->col_field, i);
          /*record row tuple*/
          overlay_record->row_tuple = self->row_field->index_record(self->row_field, i);
          /*record any page field tuples*/
          for (j=0;j<self->page_filters->len;j++) {
               page = (GODataSlicerIndex*)g_ptr_array_index(self->page_filters, i);
               tuple_val = page->index_record(page, i);
               g_array_append_val(overlay_record->filter_tuples,tuple_val);
          }

          /*commit overlay_record to overlay*/
          self->cache_overlay->append_record(self->cache_overlay, overlay_record);
     }
     
     /*complete indexes*/
     self->col_field->complete_index(self->col_field);
     self->row_field->complete_index(self->row_field);
     for (j=0;j<self->page_filters->len;i++) {
          page = (GODataSlicerIndex*)g_ptr_array_index(self->page_filters, i);
          page->complete_index(page);
     }

     /*compute dimensions of view*/
     /*number of tuples - 1, since the view coords are zero-indexed, +1 for totals.*/
     self->max_x = self->col_field->tuples->len;
     self->max_y = self->row_field->tuples->len;

     self->indexed = TRUE;
}

void
go_data_slicer_filter_set_enabled (GODataSlicer *self, guint page_filter_num, guint tuple_record_num, gboolean is_enabled) {
     GODataSlicerIndex * index;
     GTree * disabled;
     
     g_return_if_fail (IS_GO_DATA_SLICER (self));
     g_return_if_fail (page_filter_num < self->page_filters->len);

     /*Find the SlicerIndex associated with the desired page filter*/
     index = (GODataSlicerIndex *) g_ptr_array_index(self->page_filters, page_filter_num);

     /*Enable/disable the desired tuple*/
     index->tuple_set_enabled(index, tuple_record_num, is_enabled);

     /*Record here that the tuple has been disabled for easy lookup during slice_cache*/
     disabled = (GTree *)g_ptr_array_index(self->disabled_values, page_filter_num);
     if (is_enabled) {
          g_tree_remove(disabled,&tuple_record_num);
     } else {
          g_tree_insert(disabled,&tuple_record_num,&tuple_record_num);
     }
}

void
go_data_slicer_slice_cache (GODataSlicer *self) {
    guint i,j, filter_tuple_num;
    guint x,y;
    gboolean ignoreRecord;
    GTree * disabled;
    GOVal * old_data;     
    const GOVal * contribution;
    const GODataSlicerCacheOverlayRecord * record;

    /*Check to make sure we have what we need to slice*/
    g_return_if_fail (IS_GO_DATA_SLICER (self));
    g_return_if_fail(self->indexed);
    switch(self->aggregate_function) {
         case SUM:
              g_return_if_fail(self->data_field);
              break;
         case COUNT:
              /*Good to go*/ 
              break;
         default:
              g_warn_if_reached();
              break;
    }

    /*SLICE*/

    /*disable all tuples in the column and row fields*/
    self->col_field->disable_all_tuples(self->col_field);
    self->row_field->disable_all_tuples(self->row_field);
     
    /*make a single pass through the cache to compute aggregate values and totals*/
    for (i=0;i<go_data_cache_num_items(self->cache);i++) {	
         /*Get overlay record for cache row*/
         record = self->cache_overlay->get_record(self->cache_overlay,i);
	
         /*see if this record is affected by ANY page filters. if so, ignore it*/
         ignoreRecord = FALSE;
         for (j=0;j<self->page_filters->len;j++) {
              /* check each filter one by one, comparing the filter value found
               * in the current record to the tree of disabled values.*/
              disabled = (GTree *)g_ptr_array_index(self->disabled_values, j);
              filter_tuple_num = g_array_index(record->filter_tuples,guint,j);
              if (g_tree_lookup(disabled, &j)) {
                   /*ignore the record completely*/
                   ignoreRecord = TRUE;
                   break;
              }
         }
	
         if (!ignoreRecord) {
              /*enable row and column field tuples*/
              self->col_field->tuple_set_enabled(self->col_field, record->col_tuple, TRUE);
              self->row_field->tuple_set_enabled(self->row_field, record->row_tuple, TRUE);
              /*Compute coordinates*/
              x = self->col_field->get_tuple_index(self->col_field,record->col_tuple);
              y = self->row_field->get_tuple_index(self->row_field,record->row_tuple);
              /*Contribution - zero if there is no data field*/
              contribution = self->data_field == NULL ? go_val_new_float(0) : go_data_cache_field_get_val (self->data_field, i); 
              /*Let data contribute to row/col value*/
              old_data = go_data_slicer_get_value_at(self,x,y);		 		 		 		 		 
              go_data_slicer_put_value_at(self, x, y, go_data_slicer_contribute_to_aggregate_val(self, old_data, contribution));		 		 
              /*Let data contribute to row subtotal*/
              old_data = go_data_slicer_get_value_at(self,self->max_x,y);
              go_data_slicer_put_value_at(self, self->max_x, y,  go_data_slicer_contribute_to_aggregate_val(self, old_data, contribution));      	      
              /*Let data contribute to col subtotal*/
              old_data = go_data_slicer_get_value_at(self,x,self->max_y);
              go_data_slicer_put_value_at(self, x, self->max_y,  go_data_slicer_contribute_to_aggregate_val(self, old_data, contribution)); 
              /*Let data contribute to overall total*/
              old_data = go_data_slicer_get_value_at(self,self->max_x,self->max_y);
              go_data_slicer_put_value_at(self, self->max_x, self->max_y,  go_data_slicer_contribute_to_aggregate_val(self, old_data, contribution));
         }
    }
}


GPtrArray *
go_data_slicer_get_all_row_tuples(GODataSlicer *self) {
    g_return_val_if_fail (IS_GO_DATA_SLICER (self), NULL);
    return self->row_field->get_all_tuples(self->row_field);
}


GPtrArray *
go_data_slicer_get_all_column_tuples(GODataSlicer *self) {
    g_return_val_if_fail (IS_GO_DATA_SLICER (self), NULL);
    return self->col_field->get_all_tuples(self->col_field);     
}


GPtrArray *
go_data_slicer_get_page_filter_tuples(GODataSlicer *self, guint page_filter_num) {
    g_return_val_if_fail (IS_GO_DATA_SLICER (self), NULL);    
    g_return_val_if_fail (page_filter_num < self->page_filters->len, NULL);
    return g_ptr_array_index(self->page_filters, page_filter_num);
}

GOVal *
go_data_slicer_get_value_at(GODataSlicer *self, int x, int y) {
    GnmCellPos key;
    GOVal * result;    
    g_return_val_if_fail (IS_GO_DATA_SLICER (self), NULL);
    /*Check to make sure the coordinates are within range*/
    g_return_val_if_fail(x <= (int)(self->col_field->tuples->len), NULL);
    g_return_val_if_fail(y <= (int)(self->row_field->tuples->len), NULL);

    /*Use GnmCellPos hash function to hash position*/
    key.col = x;
    key.row = y;    

    /*Perform lookup*/
    result = g_hash_table_lookup(self->view, &key);
    if (result == NULL) {
        result = go_val_new_float(0);
    }
    return result;
}

/*Prints something akin to an OpenOffice Data Pilot table*/
void
go_data_slicer_dump_slicer(GODataSlicer *self) {

     guint i,j,row_tuple_in_order_num=-1, col_tuple_in_order_num=-1;
     GOVal * value;
     
     GPtrArray * row_tuples = self->row_field->get_all_tuples(self->row_field);
     GPtrArray * col_tuples = self->col_field->get_all_tuples(self->col_field);

     /*Need loops to go one index further than the number of tuples to print subtotals*/
     for (i=0;i<=row_tuples->len;i++) {

          if (i==col_tuples->len) {
              row_tuple_in_order_num++;
          } else {
              row_tuple_in_order_num = ((GODataSlicerIndexedTuple *)(g_ptr_array_index(row_tuples,i)))->relative_position;               
          }

          for (j=0;j<=col_tuples->len;j++) {
               if (j==col_tuples->len) {
                    col_tuple_in_order_num++;
               } else {
                    col_tuple_in_order_num = ((GODataSlicerIndexedTuple *)(g_ptr_array_index(col_tuples,j)))->relative_position;                    
               }
               value = go_data_slicer_get_value_at(self, col_tuple_in_order_num, row_tuple_in_order_num);
               g_printf("%10f ", value->v_float.val );
          }
          g_printf("\n");
     }
}

