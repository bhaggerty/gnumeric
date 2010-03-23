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


#include "go-data-slicer-index.h"

GType
go_data_slicer_index_get_type (void)
{
	static GType go_data_slicer_index_type = 0;

	if (!go_data_slicer_index_type) {
		static GTypeInfo const go_data_slicer_index_info = {
			sizeof (GODataSlicerIndexInterface),	/* class_size */
			NULL,		/* base_init */
			NULL,		/* base_finalize */
		};

		go_data_slicer_index_type = g_type_register_static (G_TYPE_INTERFACE,
			"GODataSlicerIndex", &go_data_slicer_index_info, 0);
	}

	return go_data_slicer_index_type;
}

GPtrArray * 
go_data_slicer_index_get_tuple_template (const GODataSlicerIndex *self) {
	 g_return_val_if_fail (IS_GO_DATA_SLICER_INDEX(self), NULL);
	 return GO_DATA_SLICER_INDEX_GET_INTERFACE (self)->get_tuple_template (self);
}

void 
go_data_slicer_index_set_tuple_template (GODataSlicerIndex *self, GPtrArray *tuple_template) {
	 g_return_if_fail(IS_GO_DATA_SLICER_INDEX(self));
	 GO_DATA_SLICER_INDEX_GET_INTERFACE (self)->set_tuple_template (self, tuple_template);
}

void 
go_data_slicer_index_index_record (GODataSlicerIndex *self, unsigned int record_num) {
	 g_return_if_fail(IS_GO_DATA_SLICER_INDEX(self));
	 return GO_DATA_SLICER_INDEX_GET_INTERFACE (self)->index_record (self, record_num);
}

GODataSlicerBitmap * 
go_data_slicer_index_retrieve_bitmap (const GODataSlicerIndex *self, const GODataSlicerTuple *tuple) {
	 g_return_val_if_fail (IS_GO_DATA_SLICER_INDEX(self), NULL);
	 return GO_DATA_SLICER_INDEX_GET_INTERFACE (self)->retrieve_bitmap (self, tuple);
}

void
go_data_slicer_index_retrieve_all_bitmaps (const GODataSlicerIndex *self, GPtrArray *tuples, GPtrArray *bitmaps) {
    g_return_if_fail (IS_GO_DATA_SLICER_INDEX(self));
    GO_DATA_SLICER_INDEX_GET_INTERFACE (self)->retrieve_all_bitmaps (self, tuples, bitmaps);
}
