/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnm-data-cache-source.h : GODataCacheSource from a GnmSheet
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
#ifndef GNM_DATA_CACHE_SOURCE_H
#define GNM_DATA_CACHE_SOURCE_H

#include <goffice/goffice.h>
#include <glib-object.h>

#include <gnumeric.h>
#include "goffice-data.h"

G_BEGIN_DECLS

#define GNM_DATA_CACHE_SOURCE_TYPE	(gnm_data_cache_source_get_type ())
#define GNM_DATA_CACHE_SOURCE(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_DATA_CACHE_SOURCE_TYPE, GnmDataCacheSource))
#define IS_GNM_DATA_CACHE_SOURCE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_DATA_CACHE_SOURCE_TYPE))

GType gnm_data_cache_source_get_type (void);

typedef struct _GnmDataCacheSource GnmDataCacheSource;

GODataCacheSource *gnm_data_cache_source_new (Sheet *src_sheet,
					      GnmRange const *src_range, char const *src_name);

Sheet		*gnm_data_cache_source_get_sheet (GnmDataCacheSource const *src);
void		 gnm_data_cache_source_set_sheet (GnmDataCacheSource *src, Sheet *sheet);
GnmRange const	*gnm_data_cache_source_get_range (GnmDataCacheSource const *src);
void		 gnm_data_cache_source_set_range (GnmDataCacheSource *src, GnmRange const *r);
char const	*gnm_data_cache_source_get_name  (GnmDataCacheSource const *src);
void		 gnm_data_cache_source_set_name  (GnmDataCacheSource *src, char const *name);

void go_data_cache_build_cache(GODataCache * cache, Sheet *sheet, GnmRange * cellRange);
void go_data_cache_create_all_fields(GODataCache * cache, Sheet * sheet, GPtrArray *hashedIdx, GnmRange * cellRange);
void go_data_cache_dump_hash_table(GHashTable* ht);

G_END_DECLS

#endif /* GNM_DATA_CACHE_SOURCE_H */
