/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-data-slicer-impl.h :
 *
 * Copyright (C) 2008 Jody Goldberg (jody@gnome.org)
 *		 2010 Sean McIntyre <s.mcintyre@utoronto.ca>
 *		      David Algar   <david.algar@utoronto.ca>
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
#ifndef GO_DATA_SLICER_IMPL_H
#define GO_DATA_SLICER_IMPL_H

#include <goffice/goffice.h>
#include <glib-object.h>
#include <go-data-slicer.h>
#include <go-data-slicer-cache-overlay.h>
#include <go-data-slicer-index.h>
#include <go-data-cache.h>
#include <go-data-cache-field.h>

G_BEGIN_DECLS

struct _GODataSlicer {
	GObject		base;

	GOString	*name;
	gboolean	indexed;

	AggregateFunction aggregate_function;
	
	GODataCache	*cache;
	GODataSlicerCacheOverlay *cache_overlay;

	GODataSlicerIndex *col_field;
	GODataSlicerIndex *row_field;
	GODataCacheField *data_field;
	GPtrArray * page_filters;    /*An array of GODataSlicerIndexes, one for each page_filter*/
	GPtrArray * disabled_values; /*An array of GTrees, one for each page_filter, containing disabled tuple record_nums as keys/values*/

	GHashTable * view;  /*Sparse matrix of values which corresponds to final Slicer table*/
	guint max_x, max_y; /*maximum dimensions of view (enough to accomodate all values and totals, but not headers)*/
};
typedef struct {
	GObjectClass base;
} GODataSlicerClass;

G_END_DECLS

#endif /* GO_DATA_SLICER_IMPL_H */
