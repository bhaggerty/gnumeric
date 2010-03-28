/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-data-cache.h : The definition of a content for a data slicer
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
#include "go-data-cache-impl.h"
#include "go-data-cache-source.h"
#include "go-data-cache-field-impl.h"
#include "value.h"
#include "cell.h"
#include "go-val.h"
#include "go-data-cache-field.h"
#include "sheet.h"
#include "workbook.h"

#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#define GO_DATA_CACHE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST ((k), GO_DATA_CACHE_TYPE, GODataCacheClass))
#define IS_GO_DATA_CACHE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GO_DATA_CACHE_TYPE))
#define GO_DATA_CACHE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GO_DATA_CACHE_TYPE, GODataCacheClass))

enum {
	PROP_0,
	PROP_REFRESHED_BY,	/* char  * */
	PROP_REFRESHED_ON,	/* GOVal * */
	PROP_REFRESH_UPGRADES,	/* bool    */
	PROP_XL_REFRESH_VER,	/* unsigned int */
	PROP_XL_CREATED_VER	/* unsigned int */
};

/*****************************************************************/

static void
go_data_cache_records_set_size (GODataCache *cache, unsigned int n)
{
	int expand = n - cache->records_allocated;
	if (0 == expand)
		return;

	cache->records = g_realloc (cache->records, n * cache->record_size);
	if (expand > 0)
		memset (cache->records + cache->records_allocated * cache->record_size, 0,
			expand * cache->record_size);
	cache->records_allocated = n;
}

static guint8 *
go_data_cache_records_fetch_index (GODataCache *cache, unsigned i)
{
	if (cache->records_allocated <= i)
		go_data_cache_records_set_size (cache, i+128);
	if (cache->records_len <= i)
		cache->records_len = i + 1;

	return go_data_cache_records_index (cache, i);
}

static void
go_data_cache_records_init (GODataCache *cache, unsigned int n, unsigned int record_size)
{
	cache->record_size = record_size;
	cache->records_len = 0;
	go_data_cache_records_set_size (cache, n);
}

/*****************************************************************/

static GObjectClass *parent_klass;
static void
go_data_cache_init (GODataCache *cache)
{
	cache->fields = g_ptr_array_new ();
	cache->data_source = NULL;
	cache->records = NULL;
	cache->records_len = cache->records_allocated = 0;

	cache->refreshed_by	= NULL;
	cache->refreshed_on	= NULL;
	cache->refresh_upgrades	= TRUE;

	cache->XL_created_ver	= 1;
	cache->XL_refresh_ver	= 1;
}

static void
go_data_cache_finalize (GObject *obj)
{
	GODataCache *cache = (GODataCache *)obj;
	unsigned int i, j;

	if (NULL != cache->records) {
		GODataCacheField const *f;
		gpointer p;

		for (i = cache->fields->len ; i-- > 0 ; ) {
			f = g_ptr_array_index (cache->fields, i);
			if (GO_DATA_CACHE_FIELD_TYPE_INLINE == f->ref_type) {
				for (j = cache->records_len ; j-- > 0 ; ) {
					p = go_data_cache_records_index (cache, j) + f->offset;
					go_val_free (*((GOVal **)p));
				}
			}
		}
		g_free (cache->records);
		cache->records = NULL;
		cache->records_len = cache->records_allocated = 0;
	}

	for (i = cache->fields->len ; i-- > 0 ; )
		g_object_unref (g_ptr_array_index (cache->fields, i));
	g_ptr_array_free (cache->fields, TRUE);
	cache->fields = NULL;

	if (NULL != cache->data_source) {
		g_object_unref (cache->data_source);
		cache->data_source = NULL;
	}

	g_free (cache->refreshed_by);
	go_val_free (cache->refreshed_on);

	(parent_klass->finalize) (obj);
}

static void
go_data_cache_set_property (GObject *obj, guint property_id,
			    GValue const *value, GParamSpec *pspec)
{
	GODataCache *cache = (GODataCache *)obj;

	switch (property_id) {
	case PROP_REFRESHED_BY :
		g_free (cache->refreshed_by);
		cache->refreshed_by = g_strdup (g_value_get_string (value));
		break;
	case PROP_REFRESHED_ON :
		go_val_free (cache->refreshed_on);
		cache->refreshed_on = g_value_dup_boxed (value);
		break;
	case PROP_REFRESH_UPGRADES : cache->refresh_upgrades = g_value_get_boolean (value); break;
	case PROP_XL_REFRESH_VER   : cache->XL_refresh_ver   = g_value_get_uint (value); break;
	case PROP_XL_CREATED_VER   : cache->XL_created_ver   = g_value_get_uint (value); break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_cache_get_property (GObject *obj, guint property_id,
			    GValue *value, GParamSpec *pspec)
{
	GODataCache const *cache = (GODataCache const *)obj;
	switch (property_id) {
	case PROP_REFRESHED_BY : g_value_set_string (value, cache->refreshed_by); break;
	case PROP_REFRESHED_ON : g_value_set_boxed (value, cache->refreshed_on); break;
	case PROP_REFRESH_UPGRADES : g_value_set_boolean (value, cache->refresh_upgrades); break;
	case PROP_XL_REFRESH_VER   : g_value_set_uint (value, cache->XL_refresh_ver); break;
	case PROP_XL_CREATED_VER   : g_value_set_uint (value, cache->XL_created_ver); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
go_data_cache_class_init (GODataCacheClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass *)klass;
	gobject_class->set_property	= go_data_cache_set_property;
	gobject_class->get_property	= go_data_cache_get_property;
	gobject_class->finalize		= go_data_cache_finalize;

	g_object_class_install_property (gobject_class, PROP_REFRESHED_BY,
		 g_param_spec_string ("refreshed-by", NULL, NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, PROP_REFRESHED_ON,
		 g_param_spec_boxed ("refreshed-on", NULL, NULL,
			GO_VAL_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, PROP_REFRESH_UPGRADES,
		 g_param_spec_boolean ("refresh-upgrades", NULL, NULL,
			TRUE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, PROP_XL_REFRESH_VER,
		 g_param_spec_uint ("refresh-version", NULL, NULL,
			0, G_MAXUINT, 1, GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, PROP_XL_CREATED_VER,
		 g_param_spec_uint ("created-version", NULL, NULL,
			0, G_MAXUINT, 1, GSF_PARAM_STATIC | G_PARAM_READWRITE));

	parent_klass = g_type_class_peek_parent (klass);
}

GSF_CLASS (GODataCache, go_data_cache,
	   go_data_cache_class_init, go_data_cache_init,
	   G_TYPE_OBJECT)

GODataCacheSource *
go_data_cache_get_source (GODataCache const *cache)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE (cache), NULL);
	return cache->data_source;
}

/**
 * go_data_cache_set_source :
 * @cache : #GODataCache
 * @src   : #GODataCacheSource
 *
 * Absorbs the reference to @src.
 **/
void
go_data_cache_set_source (GODataCache *cache, GODataCacheSource *src)
{
	g_return_if_fail (IS_GO_DATA_CACHE (cache));
	g_return_if_fail (NULL == src || IS_GO_DATA_CACHE_SOURCE (src));

	if (cache->data_source)
		g_object_unref (G_OBJECT (cache->data_source));
	cache->data_source = src;
}

void
go_data_cache_add_field (GODataCache *cache, GODataCacheField *field)
{
	g_return_if_fail (IS_GO_DATA_CACHE (cache));
	g_return_if_fail (IS_GO_DATA_CACHE_FIELD (field));
	g_return_if_fail (field->indx < 0);
	g_return_if_fail (field->cache == NULL);
	g_return_if_fail (NULL == cache->records);

	field->indx  = cache->fields->len;
	field->cache = cache;
	g_ptr_array_add (cache->fields, field);
}

/**
 * go_data_cache_import_start :
 * @cache :#GODataCache
 * @n : num records
 *
 * Validate the field setup and initialize the storage.
 **/
void
go_data_cache_import_start (GODataCache *cache, unsigned int n)
{
	GODataCacheField *f;
	unsigned int i, offset = 0;

	g_return_if_fail (IS_GO_DATA_CACHE (cache));
	g_return_if_fail (NULL == cache->records);

	for (i = 0 ; i < cache->fields->len ; i++) {
		f = g_ptr_array_index (cache->fields, i);
		f->offset = offset;
		if (NULL == f->indexed || 0 == f->indexed->len) {
			if (NULL != f->grouped &&
			    f->group_parent >= 0 && f->group_parent != f->indx)
				f->ref_type = GO_DATA_CACHE_FIELD_TYPE_NONE;
			else {
				offset += sizeof (GOVal *);
				f->ref_type = GO_DATA_CACHE_FIELD_TYPE_INLINE;
			}
		} else if (f->indexed->len < ((1<<8) - 1)) {
			offset += sizeof (guint8);
			f->ref_type = GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8;
		} else if (f->indexed->len < ((1<<16) - 1)) {
			offset += sizeof (guint16);
			f->ref_type = GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16;
		} else {
			offset += sizeof (guint32);
			f->ref_type = GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32;
		}
	}

	for (i = 0 ; i < cache->fields->len ; i++) {
		f = g_ptr_array_index (cache->fields, i);
		if (f->group_parent >= 0) {
			GODataCacheField *base = g_ptr_array_index (cache->fields, f->group_parent);
			g_return_if_fail (base->ref_type != GO_DATA_CACHE_FIELD_TYPE_NONE);
			f->offset = base->offset;
		}
	}
	go_data_cache_records_init (cache, n, offset);
}

void
go_data_cache_dump_value (GOVal const *v)
{
	if (NULL == v) {
		g_print ("<MISSING>");
	} else {
		GOFormat const *fmt = go_val_get_fmt (v);

		if (NULL != fmt) {
			char *str = format_value (fmt, v, NULL, -1, NULL);
			g_print ("'%s'", str);
			g_free (str);
		} else
			g_print ("'%s'", value_peek_string (v));
	}
}

void
go_data_cache_set_val (GODataCache *cache,
		       int field, unsigned int record_num, GOVal *v)
{
	GODataCacheField *f;
	gpointer p;

	g_return_if_fail (IS_GO_DATA_CACHE (cache));
	g_return_if_fail (NULL != cache->records);
	g_return_if_fail (0 <= field && (unsigned int )field < cache->fields->len);

	f = g_ptr_array_index (cache->fields, field);

#ifdef GO_DEBUG_SLICERS
	g_print ("\t[%d] ", field);
	go_data_cache_dump_value (v);
#endif

	p = go_data_cache_records_fetch_index (cache, record_num) + f->offset;
	switch (f->ref_type) {
	case GO_DATA_CACHE_FIELD_TYPE_NONE :
		g_warning ("attempt to set a value for grouped/calculated field #%d : '%s'",
			   f->indx, f->name->str);
		return;

	case GO_DATA_CACHE_FIELD_TYPE_INLINE	 : *((GOVal **)p)  = v; return;

	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8  : *((guint8 *)p)  = 0; break;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16 : *((guint16 *)p) = 0; break;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32 : *((guint32 *)p) = 0; break;

	default :
		g_warning ("unknown field type %d", f->ref_type);
	}
	go_val_free (v);
	g_warning ("Attempt to store a value in an indexed field");
}

void
go_data_cache_set_index (GODataCache *cache,
			 int field, unsigned int record_num, unsigned int idx)
{
	GODataCacheField *f;
	gpointer p;

	g_return_if_fail (IS_GO_DATA_CACHE (cache));
	g_return_if_fail (NULL != cache->records);
	g_return_if_fail (0 <= field && (unsigned int )field < cache->fields->len);

	f = g_ptr_array_index (cache->fields, field);

	g_return_if_fail (NULL != f->indexed);
	g_return_if_fail (idx < f->indexed->len);

#ifdef GO_DEBUG_SLICERS
	g_print ("\t(%d) %d=", field, idx);
	go_data_cache_dump_value (cache, g_ptr_array_index (f->indexed, idx));
#endif

	p = go_data_cache_records_fetch_index (cache, record_num) + f->offset;
	switch (f->ref_type) {
	case GO_DATA_CACHE_FIELD_TYPE_NONE :
		g_warning ("attempt to get value from grouped/calculated field #%d : '%s'",
			   f->indx, f->name->str);
		return;
	case GO_DATA_CACHE_FIELD_TYPE_INLINE	 : *((GOVal **)p)  = go_val_new_empty (); break;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8  : *((guint8 *)p)  = idx+1; break;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16 : *((guint16 *)p) = idx+1; break;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32 : *((guint32 *)p) = idx+1; break;

	default :
		g_warning ("unknown field type %d", f->ref_type);
	}
}

/**
 * go_data_cache_import_done :
 * @cache : #GODataCache
 * @actual_records : count
 *
 * Tidy up after an import, and tighten up the amount of memory used to store
 * the records.
 **/
void
go_data_cache_import_done (GODataCache *cache, unsigned int actual_records)
{
	g_return_if_fail (IS_GO_DATA_CACHE (cache));

	if (actual_records < cache->records_allocated)
		go_data_cache_records_set_size (cache, actual_records);
}

unsigned int
go_data_cache_num_items (GODataCache const *cache)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE (cache), 0);
	return cache->records_allocated;
}

unsigned int
go_data_cache_num_fields (GODataCache const *cache)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE (cache), 0);
	return cache->fields->len;
}
GODataCacheField *
go_data_cache_get_field (GODataCache const *cache, int i)
{
	g_return_val_if_fail (IS_GO_DATA_CACHE (cache), NULL);
	g_return_val_if_fail (0 <= i && (unsigned int)i < cache->fields->len, NULL);
	return g_ptr_array_index (cache->fields, i);
}

int
go_data_cache_get_index (GODataCache const *cache,
			 GODataCacheField const *field, unsigned int record_num)
{
	gpointer p;

	g_return_val_if_fail (IS_GO_DATA_CACHE (cache), -1);

	p = go_data_cache_records_index (cache, record_num) + field->offset;
	switch (field->ref_type) {
	case GO_DATA_CACHE_FIELD_TYPE_NONE   : break;
	case GO_DATA_CACHE_FIELD_TYPE_INLINE : break;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8  : return *(guint8 *)p - 1;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16 : return *(guint16 *)p - 1;
	case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32 : return *(guint32 *)p - 1;
	default :
		g_warning ("unknown field type %d", field->ref_type);
	}
	return -1;
}


/* Distinguish between the same value with different formats */
static gboolean
formatted_value_equal (GnmValue const *a, GnmValue const *b)
{
	return value_equal (a, b) && (VALUE_FMT(a) == VALUE_FMT(b));
}


typedef struct {
	gboolean has_blank;
	GHashTable *hash;
	GODateConventions const *date_conv;
	Sheet *src_sheet;
} UniqueCollection;

/**
 * Finds the limit of indexed elements such that if number of indexed elements greater
 * than limit, this cache field such be set as inline.
 * @param cacheFieldSize The size of index used in number of bits.
 * @param numRows The number of rows in the cell range.
 * @return An integer whoses values represents the max number of indexed elements in a column.
 */
static int 
go_data_cache_max_unique_for_indexed(int cacheFieldSize, int numRows) {
	// Memory saved by using indices divided by size of GnmValue pointer.
	return numRows * ((8 * sizeof(GnmValue *)) - cacheFieldSize) / (8 * sizeof(GnmValue *));
}

/**
 * Adds the GnmVal value from GnmCellIter iter to the given hash table if it is not 
 * already in it.
 * To be used in cunjunction with sheet_foreach_cell() in order to create
 * an array and a hash table of unique values for a specified cell range.
 * @param iter
 * @param userData
 * @return A GHashTable and a GOValArray with unique values.
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

	GnmCell const *cell = (iter->pp.sheet == uc->src_sheet) ? iter->cell
		: sheet_cell_get (uc->src_sheet,
			iter->pp.eval.col, iter->pp.eval.row);
	if (gnm_cell_is_blank (cell))
		uc->has_blank = TRUE;
	else {
		
		GnmValue *v = cell->value;
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
 * Creates the cache fields with elements in the range of cellRange from given Sheet sheet
 * and attaches them to the given GODataCache cache.
 * @param cache The GODataCache to store the values in.
 * @param sheet The sheet containing values to be stored in the cache.
 * @param hashedIdx The array of hash tables that contains the unique values for each indexed cache field.
 * @param cellRange The range of cells in respect to the sheet to store in the cache.
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
 * Builds the GODataCache cache given the Sheet sheet. At this point cache should
 * have its cache fields already added to it.
 * @param cache The GODataCache with its cache fields already added.
 * @param sheet The sheet containing elements to add to the cache.
 * @param cellRange The range of cells in respect to the sheet to store in the cache.
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


typedef struct {
	GODataCache const *cache;
	GArray const *field_order;
} GODataCacheCompare;
static gint
cb_go_data_cache_cmp (int const *a, int const * b,
		      GODataCacheCompare const *info)
{
	GODataCacheField const *f, *base;
	GOVal const *va, *vb;
	gpointer pa, pb;
	unsigned int idxa, idxb, i;
	unsigned int const n = info->field_order->len;
	int res;

	for (i = 0 ; i < n ; i++) {
		f = g_ptr_array_index (info->cache->fields, g_array_index (info->field_order, unsigned int, i));
		base = (f->group_parent < 0) ? f : g_ptr_array_index (info->cache->fields, f->group_parent);
		pa = go_data_cache_records_index (info->cache, *a) + base->offset;
		pb = go_data_cache_records_index (info->cache, *b) + base->offset;
		if (base->ref_type != GO_DATA_CACHE_FIELD_TYPE_INLINE) {
			switch (base->ref_type) {
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8  :
				idxa = *(guint8 *)pa;
				idxb = *(guint8 *)pb;
				break;
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16 :
				idxa = *(guint16 *)pa;
				idxb = *(guint16 *)pb;
				break;
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32 :
				idxa = *(guint32 *)pa;
				idxb = *(guint32 *)pb;
				break;
			default :
				g_assert_not_reached ();
			}
#warning TODO : compare indicies directly, and pre-order the indexed values
			va = (idxa > 0) ? g_ptr_array_index (base->indexed, idxa-1) : NULL;
			vb = (idxb > 0) ? g_ptr_array_index (base->indexed, idxb-1) : NULL;
		} else {
			va = *((GOVal **)pa);
			vb = *((GOVal **)pb);
		}

		if (f->bucketer.type != GO_VAL_BUCKET_NONE)
			res = go_val_bucketer_apply (&f->bucketer, va) - go_val_bucketer_apply (&f->bucketer, vb);
		else
			res = go_val_cmp (&va, &vb);
		if (res != 0)
			return res;
	}
	return 0;
}

/**
 * go_data_cache_permute:
 * @cache : #GODataCache
 * @field_order : #GArray of unsigned int
 * @permutation : #GArray of unsigned int that will be re-ordered according to the fields.
 *
 **/
void
go_data_cache_permute (GODataCache const *cache,
		       GArray const *field_order,
		       GArray *permutation)
{
	GODataCacheCompare info;

	g_return_if_fail (IS_GO_DATA_CACHE (cache));
	g_return_if_fail (field_order);
	g_return_if_fail (permutation);

	info.cache = cache;
	info.field_order = field_order;
	g_array_sort_with_data (permutation,
		(GCompareDataFunc) cb_go_data_cache_cmp, &info);
}

void
go_data_cache_dump (GODataCache *cache,
		    GArray const *field_order,
		    GArray const *permutation)
{
	GODataCacheField const *f, *base;
	unsigned int iter, i, j, idx, num_fields;
	gboolean index_val;
	gpointer p;
	GOVal *v;

	g_return_if_fail (IS_GO_DATA_CACHE (cache));

	num_fields = field_order ? field_order->len :  cache->fields->len;
	for (iter = 0 ; iter < cache->records_len ; iter++) {

		if (NULL == permutation)
			i = iter;
		else if ((i = g_array_index (permutation, unsigned int, iter)) >= 0)
			g_print ("[%d]", i);
		else
			break;
		g_print ("%d)", iter + 1);

		for (j = 0 ; j < num_fields ; j++) {
			f = g_ptr_array_index (cache->fields, field_order ? g_array_index (field_order, unsigned int, j) :j);
			base = (f->group_parent < 0) ? f : g_ptr_array_index (cache->fields, f->group_parent);
			p = go_data_cache_records_index (cache, i) + base->offset;
			index_val = TRUE;
			switch (base->ref_type) {
			case GO_DATA_CACHE_FIELD_TYPE_NONE :
				continue;
			case GO_DATA_CACHE_FIELD_TYPE_INLINE	 :
				v = *((GOVal **)p);
				index_val = FALSE;
				break;
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I8  : idx = *(guint8 *)p; break;
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I16 : idx = *(guint16 *)p; break;
			case GO_DATA_CACHE_FIELD_TYPE_INDEXED_I32 : idx = *(guint32 *)p; break;

			default :
				g_warning ("unknown field type %d", base->ref_type);
				continue;
			}

			if (index_val) {
				if (idx-- == 0)
					continue;
				g_return_if_fail (base->indexed != NULL && idx < base->indexed->len);

				v = g_ptr_array_index (base->indexed, idx);
				g_print ("\t(%d) %d=", j, idx);
			} else
				g_print ("\t[%d] ", j);

			if (f->bucketer.type != GO_VAL_BUCKET_NONE) {
				int res = go_val_bucketer_apply (&f->bucketer, v);
				go_data_cache_dump_value (g_ptr_array_index (f->grouped, res));
			}
			go_data_cache_dump_value (v);
		}
		g_print ("\n");
	}
}
