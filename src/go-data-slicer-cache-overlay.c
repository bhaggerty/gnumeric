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

#include "go-data-slicer-cache-overlay.h"

enum
{
	PROP_0,

	PROP_NUMRECORDS
};

G_DEFINE_TYPE (GODataSlicerCacheOverlay, go_data_slicer_cache_overlay, G_TYPE_OBJECT);

static void
go_data_slicer_cache_overlay_init (GODataSlicerCacheOverlay *self)
{
	self->cache = NULL;
	self->records = NULL;
	/* hook up instance methods */
	self->get_record = go_data_slicer_cache_overlay_get_record;
	self->append_record = go_data_slicer_cache_overlay_append_record;
}

static void 
go_data_slicer_cache_overlay_dispose(GObject *object) {

	GODataSlicerCacheOverlay *self;
	g_return_if_fail (IS_GO_DATA_SLICER_CACHE_OVERLAY (object));		
	self = GO_DATA_SLICER_CACHE_OVERLAY(object); /*Cast object into our type*/
	
	/*unref stuff from cache*/
    g_object_unref(self->cache);

	/*destroy records and array*/
	g_array_free(self->records, TRUE);
	self->records = NULL;

	G_OBJECT_CLASS (go_data_slicer_cache_overlay_parent_class)->dispose (object);
}

static void
go_data_slicer_cache_overlay_finalize (GObject *object)
{
	G_OBJECT_CLASS (go_data_slicer_cache_overlay_parent_class)->finalize (object);
}


static void
go_data_slicer_cache_overlay_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GODataSlicerCacheOverlay *self;
	g_return_if_fail (IS_GO_DATA_SLICER_CACHE_OVERLAY (object));		
	self = GO_DATA_SLICER_CACHE_OVERLAY(object); /*Cast object into our type*/
	
	switch (prop_id)
	{
	case PROP_NUMRECORDS:
		if (!self->records) {
			self->records = g_array_sized_new(FALSE,FALSE,sizeof(GODataSlicerCacheOverlayRecord),g_value_get_uint(value));
		} else {
			g_array_set_size(self->records,g_value_get_uint(value));
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_cache_overlay_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GODataSlicerCacheOverlay *self;
	g_return_if_fail (IS_GO_DATA_SLICER_CACHE_OVERLAY (object));		
	self = GO_DATA_SLICER_CACHE_OVERLAY(object); /*Cast object into our type*/
	
	switch (prop_id)
	{
	case PROP_NUMRECORDS:
		g_value_set_uint(value, self->records->len);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_cache_overlay_class_init (GODataSlicerCacheOverlayClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	/*GObjectClass* parent_class = G_OBJECT_CLASS (klass);*/

	object_class->finalize = go_data_slicer_cache_overlay_finalize;
	object_class->dispose = go_data_slicer_cache_overlay_dispose;
	object_class->set_property = go_data_slicer_cache_overlay_set_property;
	object_class->get_property = go_data_slicer_cache_overlay_get_property;

	g_object_class_install_property (object_class,
	                                 PROP_NUMRECORDS,
	                                 g_param_spec_uint    ("num_records",
	                                                      NULL,
	                                                      "The number of records this CacheOverlay will store",
	                                                      0,
	                                                      UINT_MAX,
	                                                      0,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

const GODataSlicerCacheOverlayRecord *
go_data_slicer_cache_overlay_get_record (const GODataSlicerCacheOverlay *self, const guint record_num) {
	g_return_val_if_fail(record_num < self->records->len, NULL);
	return &g_array_index(self->records, GODataSlicerCacheOverlayRecord, record_num);
}

void
go_data_slicer_cache_overlay_append_record (GODataSlicerCacheOverlay *self, GODataSlicerCacheOverlayRecord * record) {
	g_warn_if_fail(self->records->len == go_data_cache_num_items (self->cache));
	g_array_append_val(self->records, record);
}