/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnm-data-cache-source.c : GODataCacheSource from a Sheet
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
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
#include "gnm-data-cache-source.h"
#include "go-data-cache-source.h"
#include "go-data-cache.h"


#include <gnumeric.h>
#include <ranges.h>
#include <sheet.h>
#include <expr-name.h>

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#include "cell.h"
#include "go-data-cache-impl.h"
#include "go-data-cache-field-impl.h"


struct _GnmDataCacheSource {
	GObject		base;

	Sheet	  *src_sheet;
	GnmRange   src_range;	/* treated as cache if src_name is non-NULL */
	GOString  *src_name;	/* optionally NULL */
};
typedef GObjectClass GnmDataCacheSourceClass;

enum {
	PROP_0,
	PROP_SHEET,	/* GnmSheet * */
	PROP_RANGE,	/* GnmRange */
	PROP_NAME	/* char *, optionally NULL */
};

typedef struct {
	gboolean has_blank;
	GHashTable *hash;
	GODateConventions const *date_conv;
	Sheet *src_sheet;
} UniqueCollection;

/* Distinguish between the same value with different formats */
static gboolean
formatted_value_equal (GnmValue const *a, GnmValue const *b)
{
	return value_equal (a, b) && (VALUE_FMT(a) == VALUE_FMT(b));
}

/**
 * go_data_cache_max_unique_for_indexed:
 * @cacheFieldSize : The size of index used in number of bits. Should be 8, 16, or 32.
 * @numRows : The number of rows in the cell range.
 *
 * Finds the limit of indexed elements such that if number of indexed elements greater
 * than limit, this cache field such be set as inline. Return An integer whoses values 
 * represents the max number of indexed elements in a column.
 */
static int 
go_data_cache_max_unique_for_indexed(int cacheFieldSize, int numRows) {
	// Memory saved by using indices divided by size of GnmValue pointer.
	return numRows * ((8 * sizeof(GnmValue *)) - cacheFieldSize) / (8 * sizeof(GnmValue *));
}

/**
 * go_data_cache_collect_content:
 * @iter : *iter
 * @userData : gpointer
 *
 * Adds the GnmVal value from GnmCellIter iter to the given hash table if it is not 
 * already in it. To be used in cunjunction with sheet_foreach_cell() in order to 
 * create an array and a hash table of unique values for a specified cell range.
 * Returns a GHashTable and a GOValArray with unique values.
 */
static GnmValue*
go_data_cache_collect_content (GnmCellIter const *iter, gpointer userData)
{
	// Seperate userData into respective arguments.
	// uc is a container for the hash table.
	UniqueCollection * uc = ((UniqueCollection **) userData)[0];
	// index is the array of unique values.
	GOValArray * indexed = ((GOValArray **) userData)[1];
	// intArr is the numrows and limit.
	int * intArr = ((int **) userData)[2];
	
	int numRows = intArr[0];
	unsigned int maxUniqueForIndexed = intArr[1];
	
	guint hashSize;
	GnmValue *vClone;
	gpointer *value;
	GnmValue *v;

	GnmCell const *cell = (iter->pp.sheet == uc->src_sheet) ? iter->cell
		: sheet_cell_get (uc->src_sheet,
			iter->pp.eval.col, iter->pp.eval.row);
	if (gnm_cell_is_blank (cell)) {
		uc->has_blank = TRUE;
	}
	
	v = cell->value;
	// Check that the GnmValue v is not already in the hash table.
	if (g_hash_table_lookup (uc->hash, v) == NULL) {
		hashSize = g_hash_table_size (uc->hash);
		//int numRows = intArr[0];

		// Find the limits for varying index sizes.
		if (hashSize + 1 == (1<<8)) {
			intArr[1] = go_data_cache_max_unique_for_indexed(16, numRows);
		} else if (hashSize + 1 == (1<<16)) {
			intArr[1] = go_data_cache_max_unique_for_indexed(32, numRows);
		}

		// If size of hash is larger than limit, we can stop finding unique
		// values since this cache field will be set as inline.
		maxUniqueForIndexed = intArr[1];
		if (hashSize + 1 > maxUniqueForIndexed) {
			return VALUE_TERMINATE;
		}

		vClone = value_dup (v);
		g_ptr_array_add(indexed, vClone);	
		value = (gpointer *) g_malloc(sizeof(gpointer));
		*value = GUINT_TO_POINTER(hashSize);
		g_hash_table_replace (uc->hash, vClone, value);
	}
	return NULL;
}

static void 
dumpHashTableForEach(gpointer key, gpointer value, gpointer userData) {
	value_dump((GnmValue *)key);
	printf("Value: %d\n", GPOINTER_TO_INT(*((gpointer *)value)));
}

void 
go_data_cache_dump_hash_table(GHashTable* ht) {
	printf("HASH TABLE DUMP\n");
	g_hash_table_foreach(ht, (GHFunc) &dumpHashTableForEach, NULL);
}

/**
 * go_data_cache_create_all_fields:
 * @cache : The GODataCache to store the values in.
 * @sheet : The sheet containing values to be stored in the cache.
 * @hashedIdx : The array of hash tables that contains the unique values for each indexed cache field.
 * @cellRange : The range of cells in respect to the sheet to store in the cache.
 * 
 * Creates the cache fields with elements in the range of cellRange from given Sheet sheet
 * and attaches them to the given GODataCache cache.
 */
void 
go_data_cache_create_all_fields(GODataCache * cache, Sheet * sheet, GPtrArray *hashedIdx, GnmRange * cellRange) {
	int numRows;
	UniqueCollection uc;
	int intArr[2];
	GOValArray * indexed;
	gpointer * userData;
	GnmValue * terminated;
	GODataCacheField * tempCacheField;
	int col;
	
	for (col = cellRange->start.col; col <= cellRange->end.col; col++) {
		numRows = cellRange->end.row - cellRange->start.row + 1;
		uc.has_blank = FALSE;
		uc.hash = g_hash_table_new_full ((GHashFunc)value_hash, (GEqualFunc)formatted_value_equal,
			(GDestroyNotify)value_release, (GDestroyNotify)g_free);
		uc.src_sheet = sheet;
		uc.date_conv = workbook_date_conv (uc.src_sheet->workbook);
		
		// Set up arguments to pass to unique filter.
		intArr[0] = numRows;
		// Find limit for 8 bit indices here to save time computing later.
		intArr[1] = go_data_cache_max_unique_for_indexed(8, numRows);
		indexed = g_ptr_array_new();
		userData = (gpointer *) g_malloc(sizeof(gpointer) * 3);
		
		// UniqueCollection containing hash table of unique values.
		((UniqueCollection **)userData)[0] = &uc;
		// GOValArray of unique values.
		((GOValArray **)userData)[1] = indexed;
		// Array containing number of rows and limit.
		((int **)userData)[2] = intArr;
		
		// Apply the unique filter to each cell in the range.
		terminated = sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_HIDDEN,
				col, cellRange->start.row, col, cellRange->end.row,
				(CellIterFunc)&go_data_cache_collect_content, userData);
		g_free(userData);

		if(terminated != NULL) {
			indexed = NULL;
		}

		//go_data_cache_dump_hash_table(uc.hash);
		g_ptr_array_add(hashedIdx, uc.hash);

		// Create the data cache field.
		tempCacheField = g_object_new(GO_DATA_CACHE_FIELD_TYPE, NULL);	

		// Configure the data cache field.
		go_data_cache_field_set_vals (tempCacheField, FALSE, indexed);

		// Add the cache field to the cache.
		go_data_cache_add_field(cache, tempCacheField);
	}
}

/**
 * go_data_cache_build_cache:
 * @cache : The GODataCache with its cache fields already added.
 * @sheet : The sheet containing elements to add to the cache.
 * @cellRange : The range of cells in respect to the sheet to store in the cache.
 *
 * Builds the GODataCache cache given the Sheet sheet. At this point cache should
 * have its cache fields already added to it.

 */
void 
go_data_cache_build_cache(GODataCache * cache, Sheet *sheet, GnmRange * cellRange){
	GODataCacheField * f;
	GnmValue *val;
	gpointer value;
	unsigned int idx;
	unsigned int i, j;
	unsigned int numRows = cellRange->end.row - cellRange->start.row + 1;
	
	// Create all the cache fields first.
	GPtrArray *hashedIdx = g_ptr_array_new ();
	go_data_cache_create_all_fields(cache, sheet, hashedIdx, cellRange);
	
	go_data_cache_import_start(cache, numRows);
	
	for (i = 0; i < cache->fields->len; i++) {
		f = g_ptr_array_index(cache->fields, i);
		// If the cache field is inline.
		if (f->ref_type == GO_DATA_CACHE_FIELD_TYPE_INLINE) {
			for (j = 0; j < numRows; j++) {	
				val = (sheet_cell_get(sheet,i,j))->value;
				go_data_cache_set_val(cache, i, j, value_dup(val));
			}
		// If the cache field is indexed.
		} else {
			GHashTable *hIdx = g_ptr_array_index(hashedIdx, i);
			for (j = 0; j < numRows; j++) {
				val = (sheet_cell_get(sheet,i,j))->value;
				value = g_hash_table_lookup(hIdx, val);
				// If the cell has no value.
				if (value == NULL) {
					go_data_cache_set_val(cache, i, j, NULL);
				} else {
					idx = GPOINTER_TO_INT(*((gpointer *)value));
					go_data_cache_set_index(cache, i, j, idx);
				}
	
			}
		}		
	}
	// May want to change to actual number of allocated records rather than numRows.
	go_data_cache_import_done(cache, numRows);
}

static GODataCache *
gdcs_allocate (GODataCacheSource const *src)
{
	GnmDataCacheSource *gdcs = (GnmDataCacheSource *)src;
	GODataCache *res;

	g_return_val_if_fail (gdcs->src_sheet != NULL, NULL);

	if (NULL != gdcs->src_name) {
		GnmParsePos pp;
		GnmEvalPos ep;
		GnmNamedExpr *nexpr = expr_name_lookup (
			parse_pos_init_sheet (&pp, gdcs->src_sheet), gdcs->src_name->str);
		if (NULL != nexpr) {
			GnmValue *v = expr_name_eval (nexpr,
				eval_pos_init_sheet (&ep, gdcs->src_sheet),
				GNM_EXPR_EVAL_PERMIT_NON_SCALAR	| GNM_EXPR_EVAL_PERMIT_EMPTY);

			if (NULL != v) {
				value_release (v);
			}
		}
	}
	
	res = g_object_new (GO_DATA_CACHE_TYPE, NULL);
	go_data_cache_build_cache(res, gdcs->src_sheet, &(gdcs->src_range));
	return res;
}

static GError *
gdcs_validate (GODataCacheSource const *src)
{
	return NULL;
}

static gboolean
gdcs_needs_update (GODataCacheSource const *src)
{
	return FALSE;
}

static void
gnm_data_cache_source_init (GnmDataCacheSource *src)
{
	src->src_sheet = NULL;
	range_init_invalid (&src->src_range);
}

static GObjectClass *parent_klass;
static void
gnm_data_cache_source_finalize (GObject *obj)
{
	GnmDataCacheSource *src = (GnmDataCacheSource *)obj;
	go_string_unref (src->src_name);
	(parent_klass->finalize) (obj);
}
static void
gnm_data_cache_source_set_property (GObject *obj, guint property_id,
				    GValue const *value, GParamSpec *pspec)
{
	GnmDataCacheSource *src = (GnmDataCacheSource *)obj;

	switch (property_id) {
	case PROP_SHEET :
		gnm_data_cache_source_set_sheet (src, g_value_get_object (value));
		break;
	case PROP_RANGE :
		gnm_data_cache_source_set_range (src, g_value_get_boxed (value));
		break;
	case PROP_NAME :
		gnm_data_cache_source_set_name (src, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_data_cache_source_get_property (GObject *obj, guint property_id,
				    GValue *value, GParamSpec *pspec)
{
	GnmDataCacheSource const *src = (GnmDataCacheSource const *)obj;
	switch (property_id) {
	case PROP_SHEET :
		g_value_set_object (value, gnm_data_cache_source_get_sheet (src));
		break;
	case PROP_RANGE :
		g_value_set_boxed (value, gnm_data_cache_source_get_range (src));
		break;
	case PROP_NAME :
		g_value_set_string (value, gnm_data_cache_source_get_name (src));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_data_cache_source_class_init (GnmDataCacheSourceClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	gobject_class->set_property	= gnm_data_cache_source_set_property;
	gobject_class->get_property	= gnm_data_cache_source_get_property;
	gobject_class->finalize		= gnm_data_cache_source_finalize;

	g_object_class_install_property (gobject_class, PROP_SHEET,
		 g_param_spec_object ("src-sheet", "sheet",
			"The source sheet.",
			GNM_SHEET_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_RANGE,
		 g_param_spec_boxed ("src-range", "range",
			"Optional named expression to generate a source range.",
			gnm_range_get_type (), GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_string ("src-name", "src-name",
			"Optional named expression to generate a source range.", NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));

	parent_klass = g_type_class_peek_parent (klass);
}

static void
gnm_data_cache_source_iface_init (GODataCacheSourceClass *iface)
{
	iface->allocate	    = gdcs_allocate;
	iface->validate	    = gdcs_validate;
	iface->needs_update = gdcs_needs_update;
}

GSF_CLASS_FULL (GnmDataCacheSource, gnm_data_cache_source, NULL, NULL,
		gnm_data_cache_source_class_init, NULL,
		gnm_data_cache_source_init, G_TYPE_OBJECT, 0,
		GSF_INTERFACE (gnm_data_cache_source_iface_init, GO_DATA_CACHE_SOURCE_TYPE))

/**
 * gnm_data_cache_source_new:
 * @src_sheet : #Sheet
 * @src_range : #GnmRange
 * @src_name : char *, optionally %NULL
 *
 * Allocates a new Allocates a new #GnmDataCacheSource
 *
 * Returns : #GODataCacheSource
 **/
GODataCacheSource *
gnm_data_cache_source_new (Sheet *src_sheet,
			   GnmRange const *src_range, char const *src_name)
{
	GnmDataCacheSource *res;

	g_return_val_if_fail (IS_SHEET (src_sheet), NULL);
	g_return_val_if_fail (src_range != NULL, NULL);

	res = g_object_new (GNM_DATA_CACHE_SOURCE_TYPE, NULL);
	res->src_sheet = src_sheet;
	res->src_range = *src_range;
	gnm_data_cache_source_set_name (res, src_name);

	return GO_DATA_CACHE_SOURCE (res);
}
Sheet *
gnm_data_cache_source_get_sheet (GnmDataCacheSource const *src)
{
	g_return_val_if_fail (IS_GNM_DATA_CACHE_SOURCE (src), NULL);
	return src->src_sheet;
}

void
gnm_data_cache_source_set_sheet (GnmDataCacheSource *src, Sheet *sheet)
{
	g_return_if_fail (IS_GNM_DATA_CACHE_SOURCE (src));
}

GnmRange const	*
gnm_data_cache_source_get_range (GnmDataCacheSource const *src)
{
	g_return_val_if_fail (IS_GNM_DATA_CACHE_SOURCE (src), NULL);
	return &src->src_range;
}

void
gnm_data_cache_source_set_range (GnmDataCacheSource *src, GnmRange const *r)
{
	g_return_if_fail (IS_GNM_DATA_CACHE_SOURCE (src));
	src->src_range = *r;
}

char const *
gnm_data_cache_source_get_name  (GnmDataCacheSource const *src)
{
	g_return_val_if_fail (IS_GNM_DATA_CACHE_SOURCE (src), NULL);
	return src->src_name ? src->src_name->str : NULL;
}

void
gnm_data_cache_source_set_name (GnmDataCacheSource *src, char const *name)
{
	GOString *new_val;

	g_return_if_fail (IS_GNM_DATA_CACHE_SOURCE (src));

	new_val = go_string_new (name);
	go_string_unref (src->src_name);
	src->src_name =  new_val;
}
