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

/**
 * SECTION: GODataSlicerCacheOverlay
 * @short_description: An overlay of the Cache
 * @see_also: #GoDataCache
 *
 * This class complements the Cache by providing each record with additional   
 * information - in particular, the unique numerical identifiers of the        
 * various tuples which are found in that record.  It doesn't do anything      
 * else.  Records are zero-indexed as in the Cache.  
 */

#ifndef _GODATASLICERCACHEOVERLAY_H_
#define _GODATASLICERCACHEOVERLAY_H_

#include "goffice-data.h"	/* remove after move to goffice */
#include <goffice/goffice.h>
#include <go-val.h>
#include <glib-object.h>
#include "go-data-cache-impl.h"

G_BEGIN_DECLS

#define GO_DATA_SLICER_CACHE_OVERLAY_TYPE             (go_data_slicer_cache_overlay_get_type ())
#define GO_DATA_SLICER_CACHE_OVERLAY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GO_DATA_SLICER_CACHE_OVERLAY_TYPE, GODataSlicerCacheOverlay))
#define GO_DATA_SLICER_CACHE_OVERLAY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GO_DATA_SLICER_CACHE_OVERLAY_TYPE, GODataSlicerCacheOverlayClass))
#define IS_GO_DATA_SLICER_CACHE_OVERLAY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GO_DATA_SLICER_CACHE_OVERLAY_TYPE))
#define IS_GO_DATA_SLICER_CACHE_OVERLAY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GO_DATA_SLICER_CACHE_OVERLAY_TYPE))
#define GO_DATA_SLICER_CACHE_OVERLAY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GO_DATA_SLICER_CACHE_OVERLAY_TYPE, GODataSlicerCacheOverlayClass))

typedef struct {
	guint col_tuple; /*The record_num of the column tuple which appears in this cache overlay record.*/
	guint row_tuple; /*The record_num of the row tuple which appears in this cache overlay record.*/
	GArray * filter_tuples; /*The record_num values of the filter tuples which appears in this 
	                          cache overlay record - one (possibly NULL) for each Page Filter 
						      defined in the Slicer, in the same order.*/
} GODataSlicerCacheOverlayRecord;

typedef struct _GODataSlicerCacheOverlayClass GODataSlicerCacheOverlayClass;
typedef struct _GODataSlicerCacheOverlay GODataSlicerCacheOverlay;

struct _GODataSlicerCacheOverlayClass
{
	GObjectClass parent_class;
};

struct _GODataSlicerCacheOverlay
{
	GObject parent_instance;

	GODataCache * cache; /*The cache to be overlayed by this CacheOverlay*/
	GArray * records; /*An array of GODataSlicerCacheOverlayRecords - one for each record in the Cache*/

	const GODataSlicerCacheOverlayRecord *  (*get_record) (const GODataSlicerCacheOverlay *self, const guint record_num);
	void (*append_record) (GODataSlicerCacheOverlay *self, GODataSlicerCacheOverlayRecord * record);
};

GType go_data_slicer_cache_overlay_get_type (void) G_GNUC_CONST;


/**
 * get_record:
 *
 * @self:			This CacheOverlay
 * @record_num:		the record_num of the record desired
 *
 * Returns a particular record in this CacheOverlay

 * Returns: the record
 */
const GODataSlicerCacheOverlayRecord *  
go_data_slicer_cache_overlay_get_record (const GODataSlicerCacheOverlay *self, const guint record_num);

/**
 * append_record:
 *
 * @self:			This CacheOverlay
 * @record:			the record to insert
 *
 * Inserts a new record into this CacheOverlay
 */
void
go_data_slicer_cache_overlay_append_record (GODataSlicerCacheOverlay *self, GODataSlicerCacheOverlayRecord * record);

G_END_DECLS

#endif /* _GODATASLICERCACHEOVERLAY_H_ */
