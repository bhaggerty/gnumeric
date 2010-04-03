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

enum
{
	PROP_0,

	PROP_INDEX,
	PROP_CACHE_FIELD
};



G_DEFINE_TYPE (GODataSlicerField, go_data_slicer_field, G_TYPE_OBJECT);

static void
go_data_slicer_field_init (GODataSlicerField *self)
{
	self->cache_field = NULL;

	self->get_val = go_data_slicer_field_get_val;
}

static void
go_data_slicer_field_finalize (GObject *object)
{
	GODataSlicerField * self = (GODataSlicerField *)object;	
	g_return_if_fail (IS_GO_DATA_SLICER_FIELD (object));
	g_object_unref(self->cache_field);

	G_OBJECT_CLASS (go_data_slicer_field_parent_class)->finalize (object);
}

static void
go_data_slicer_field_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GODataSlicerField * self = (GODataSlicerField *)object;
	g_return_if_fail (IS_GO_DATA_SLICER_FIELD (object));
	
	switch (prop_id)
	{
	case PROP_INDEX:
		/*read-only*/
		break;
	case PROP_CACHE_FIELD:
		if (self->cache_field) {
			g_object_unref(self->cache_field);
		}
		self->cache_field = g_value_get_object (value); 
		g_object_ref(self->cache_field); 
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

	switch (prop_id)
	{
	case PROP_INDEX:
		g_value_set_int(value, self->cache_field->indx);
		break;
	case PROP_CACHE_FIELD:
		g_value_set_object (value, self->cache_field);
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

	g_object_class_install_property (object_class, PROP_INDEX,
		 g_param_spec_int ("index", NULL,
			"Index of underlying cache field",
			-1, G_MAXINT, -1,
			G_PARAM_READABLE));
	
	g_object_class_install_property (object_class, PROP_CACHE_FIELD,
	                                  g_param_spec_object("cache_field", NULL, NULL,
	                              		GO_DATA_CACHE_FIELD_TYPE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

GOVal const	 *go_data_slicer_field_get_val   (GODataSlicerField const *self, unsigned int record_num) {
	return go_data_cache_field_get_val(self->cache_field, record_num);
}