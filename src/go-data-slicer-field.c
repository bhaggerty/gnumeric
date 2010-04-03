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

#include "go-data-slicer-field.h"
#include <glib/gprintf.h>

enum
{
	PROP_0,

	PROP_CACHE_FIELDS
};



G_DEFINE_TYPE (GODataSlicerField, go_data_slicer_field, G_TYPE_OBJECT);

static void
go_data_slicer_field_init (GODataSlicerField *self)
{
	self->cache_fields = g_ptr_array_new();

	self->dump_cols = go_data_slicer_field_dump_cols;
	self->get_val = go_data_slicer_field_get_val;
	self->dump_val = go_data_slicer_field_dump_val;
}

static void
go_data_slicer_field_finalize (GObject *object)
{
	guint i;
	GODataSlicerField * self = (GODataSlicerField *)object;	
	g_return_if_fail (IS_GO_DATA_SLICER_FIELD (object));

	/*unref underlying cache fields*/
	for(i=0;i<self->cache_fields->len;i++) {
		g_object_unref(g_ptr_array_index(self->cache_fields, i));
	}

	G_OBJECT_CLASS (go_data_slicer_field_parent_class)->finalize (object);
}

static void
go_data_slicer_field_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	guint i;
	GODataSlicerField * self = (GODataSlicerField *)object;
	g_return_if_fail (IS_GO_DATA_SLICER_FIELD (object));
	
	switch (prop_id)
	{
	case PROP_CACHE_FIELDS:
		if (self->cache_fields) {
			/*unref underlying cache fields*/
			for(i=0;i<self->cache_fields->len;i++) {
				g_object_unref(g_ptr_array_index(self->cache_fields, i));
			}
		}
		self->cache_fields = (GPtrArray *) g_value_get_pointer (value);
		/*ref underlying cache fields*/
		for(i=0;i<self->cache_fields->len;i++) {
			g_object_ref(g_ptr_array_index(self->cache_fields, i));
		}
		break;		
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_field_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	const GODataSlicerField * self = (const GODataSlicerField *)object;
	g_return_if_fail (IS_GO_DATA_SLICER_FIELD (object));

	switch (prop_id) {
	case PROP_CACHE_FIELDS:
		g_value_set_pointer (value, self->cache_fields);
		break;	
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_field_class_init (GODataSlicerFieldClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = go_data_slicer_field_finalize;
	object_class->set_property = go_data_slicer_field_set_property;
	object_class->get_property = go_data_slicer_field_get_property;

	
	g_object_class_install_property (object_class, PROP_CACHE_FIELDS,
	                                  g_param_spec_pointer("cache_fields", NULL, NULL,
	                          				G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

GOVal const	 *go_data_slicer_field_get_val   (GODataSlicerField const *self, unsigned int record_num) {
	int parent;
	GODataCacheField * primary = (GODataCacheField *) g_ptr_array_index(self->cache_fields, 0);

	/*Find base field if this is a grouped field*/
	while (!go_data_cache_field_is_base(primary)) {
		g_object_get(primary, "group-base", &parent, NULL);
		primary = go_data_cache_get_field(primary->cache, parent);
	}
	
	return go_data_cache_field_get_val(primary, record_num);
}

void go_data_slicer_field_dump_cols (GODataSlicerField const *self) {
	guint i;
	GODataCacheField * field;

	if (self->cache_fields->len > 1) {
		g_printf("group(");
	} else {
		g_printf("(");
	}
	
	for (i=0;i<self->cache_fields->len;i++) {
		field = (GODataCacheField *) g_ptr_array_index(self->cache_fields, i);
		g_printf("%d", field->indx);
		if (i==0 && self->cache_fields->len > 1) g_printf("*");
		if (i != self->cache_fields->len-1) g_printf(" ");
	}
	g_printf(")");
}

void go_data_slicer_field_dump_val (GODataSlicerField const *self, unsigned int record_num) {
	guint i;
	GODataCacheField * field;
	const GOVal * value;
	GnmValueType tvalue;
	
	if (self->cache_fields->len > 1) {
		g_printf("group(");
	}
	for (i=0;i<self->cache_fields->len;i++) {
		field = (GODataCacheField *) g_ptr_array_index(self->cache_fields, i);

		value = go_data_cache_field_get_val(field,record_num);
		tvalue = VALUE_IS_EMPTY (value) ? VALUE_EMPTY : value->type;
		if (tvalue == VALUE_FLOAT) {		
			g_printf("%-4.1f", value->v_float.val);
		} else {
			/*TODO: Deal with the situation in which values are not numeric*/
			g_printf("--.-");
		}

		if (i==0 && self->cache_fields->len > 1) g_printf("*");
		if (i != self->cache_fields->len-1) g_printf(" ");
	}
	if (self->cache_fields->len > 1) {
		g_printf(")");
	}
}