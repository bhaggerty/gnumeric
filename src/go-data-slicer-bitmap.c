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

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "go-data-slicer-bitmap.h"

#ifdef GO_DEBUG_SLICERS
#include <glib/gprintf.h>
#endif

#define DEFAULT_NUM_BLOCKS 8
#define DEFAULT_NUM_UNCOMPRESSED_BLOCKS 5

enum
{
	PROP_0,

	PROP_NUM_BLOCKS,
	PROP_NUM_UNCOMPRESSED_BLOCKS
};


/**
 * private clone
 *
 */
static GODataSlicerBitmap * 
go_data_slicer_bitmap_clone(GODataSlicerBitmap *self) {
	/*TODO: Implement private method*/
	return NULL;
}

/**
 * private block_is_compressed
 *
 * Examines the block array's bitmap to determine if a particular block is
 * marked as 'compressed'.  Achieves this by calculating the position of the
 * blocknum-th bit in the block_map array.
 *
 * @param blocknum - the block to check for compression
 * @return TRUE if the block is compressed, FALSE otherwise.
 */
static gboolean 
go_data_slicer_bitmap_block_is_compressed(GODataSlicerBitmap *self, guint blocknum) {
	/*The guint8 'bin' the blocknum-th bit resides in*/
	guint8 blockmap_block = g_array_index(self->block_map, guint8, (blocknum-1)/8);
	/*A mask which extracts the bit from the 'bin'*/
	guint8 mask = 0x1 << (7-((blocknum-1)%8));
	/*Check to see if the bit is set by setting it and seeing if the 'bin' changes*/
	if (blockmap_block == (blockmap_block | mask)) {
		return TRUE;
	} else {
		return FALSE;
	}
}

/**
 * private block_set_compressed_flag
 *
 * Examines the block array's bitmap to mark a particular block as being
 * 'compressed' or not 'compressed'.  Achieves this by calculating the 
 * position of the blocknum-th bit in the block_map array and setting it to 
 * the appropriate value (0 or 1).  Does not actually perform compression
 * (resizing of the block array).
 * 
 * @param blocknum - the block to mark as compressed in block_map
 * @param is_compressed - the value to set at the bit position in block_map
 */
static void 
go_data_slicer_bitmap_block_set_compressed_flag(GODataSlicerBitmap *self, guint blocknum, gboolean is_compressed) {
	/*The guint8 'bin' the blocknum-th bit resides in*/
	guint8 blockmap_block = g_array_index(self->block_map, guint8, (blocknum-1)/8);
	/*A mask which extracts the bit from the 'bin'*/
	guint8 mask = 0x1 << (7-(((blocknum-1)%8)));
	/*Set the bit*/
	if (is_compressed) {
		blockmap_block = blockmap_block | mask;
	} else {
		blockmap_block = blockmap_block & (~mask);
	}
	g_array_append_val(self->block_map, blockmap_block);
	g_array_remove_index_fast (self->block_map, (blocknum-1)/8); /*removes an element and replaces it with the last element*/
}

/**
 * private get_block_index
 *
 * Converts a blocknum, which is a virtual block number that includes empty
 * blocks, into the actual array index for that block in blocks (which does
 * not store empty blocks).
 *
 * PRECONDITION: block is not compressed
 *
 * @param blocknum - the block number to convert
 * @return the index of blocknum in blocks
 */
static guint
go_data_slicer_bitmap_get_block_index(GODataSlicerBitmap *self, guint blocknum) {
	int i; guint8 mask;
	guint index = 0;
	/*start by figuring out which 8bit bin in block_map this blocknum's bit resides*/
	guint8 blockmap_block_num = (blocknum-1)/8;

	/*count the number of uncompressed blocks which appear before this block -
	  note that this is equivalent to counting 1s in block_map.  Start by
	  counting up to (but not including) blockmap_block_num*/
	for (i=0;i<blockmap_block_num;i++) {
		index += PreCompute8[g_array_index(self->block_map, guint8, i)];
	}

	/*now we just have to count the 1s which appear BEFORE the bit in its own
	  block.  start by masking out the bit we care about and all bits following
	  it.*/
	mask = ~(-0x1 << (7-(((blocknum-1)%8))));
	index += PreCompute8[(g_array_index(self->block_map, guint8, blockmap_block_num) & mask)];

	return index;
}


G_DEFINE_TYPE (GODataSlicerBitmap, go_data_slicer_bitmap, G_TYPE_OBJECT);

static void
go_data_slicer_bitmap_init (GODataSlicerBitmap *self)
{
	/* set up member variables */
	self->block_map = g_array_sized_new(FALSE,TRUE,sizeof(guint8), (DEFAULT_NUM_BLOCKS%8 == 0 ? DEFAULT_NUM_BLOCKS/8 : (DEFAULT_NUM_BLOCKS/8 + 1)));
	self->blocks = g_array_sized_new(FALSE, TRUE, sizeof(guint32), DEFAULT_NUM_UNCOMPRESSED_BLOCKS);
	
	/* hook up instance methods */
	self->is_member = go_data_slicer_bitmap_is_member;
	self->set_member = go_data_slicer_bitmap_set_member;
	self->set_block = go_data_slicer_bitmap_set_block;
	self->intersect_with = go_data_slicer_bitmap_intersect_with;
}

static void
go_data_slicer_bitmap_finalize (GObject *object)
{
	GODataSlicerBitmap *self = GO_DATA_SLICER_BITMAP(object);
	/*deinitalization code here */
	g_array_unref(self->block_map);
	g_array_unref(self->blocks);
	G_OBJECT_CLASS (go_data_slicer_bitmap_parent_class)->finalize (object);
}

static void
go_data_slicer_bitmap_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GODataSlicerBitmap *self;
	guint new_size;
	g_return_if_fail (IS_GO_DATA_SLICER_BITMAP (object));

	self = GO_DATA_SLICER_BITMAP(object);

	switch (prop_id)
	{
	case PROP_NUM_BLOCKS:
		/*Only allow block array to be expanded*/
		new_size = g_value_get_uint (value);
		if (new_size > self->block_map->len) {
			g_array_set_size(self->block_map, new_size);
		}
		break;
	case PROP_NUM_UNCOMPRESSED_BLOCKS:
		/*Only allow block_map array to be expanded by multiples of 8*/
		new_size = g_value_get_uint (value);
	    new_size = new_size % 8 == 0 ? new_size/8 : (new_size/8) + 1;
		if (new_size > self->block_map->len) {
			g_array_set_size(self->blocks, new_size);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_bitmap_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{	
	GODataSlicerBitmap *self;
	g_return_if_fail (IS_GO_DATA_SLICER_BITMAP (object));

	self = GO_DATA_SLICER_BITMAP(object);
	
	switch (prop_id)
	{
	case PROP_NUM_BLOCKS:
		g_value_set_int(value, (self->block_map->len) * 32);
		break;
	case PROP_NUM_UNCOMPRESSED_BLOCKS:
		g_value_set_int(value, self->blocks->len);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
go_data_slicer_bitmap_class_init (GODataSlicerBitmapClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	/*GObjectClass* parent_class = G_OBJECT_CLASS (klass);*/

	object_class->finalize = go_data_slicer_bitmap_finalize;
	object_class->set_property = go_data_slicer_bitmap_set_property;
	object_class->get_property = go_data_slicer_bitmap_get_property;

	g_object_class_install_property (object_class,
	                                 PROP_NUM_BLOCKS,
	                                 g_param_spec_int ("NUM_BLOCKS",
	                                                      "num_blocks",
	                                                      "Total number of blocks in this bitmap (compressed or uncompressed)",
	                                                      1,G_MAXUINT,DEFAULT_NUM_BLOCKS,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (object_class,
	                                 PROP_NUM_UNCOMPRESSED_BLOCKS,
	                                 g_param_spec_int  ("NUM_UNCOMPRESSED_BLOCKS",
	                                                      "num_uncompressed_blocks",
	                                                      "Total number of uncompressed blocks in this bitmap.",
	                                                      1,G_MAXUINT,DEFAULT_NUM_UNCOMPRESSED_BLOCKS,
	                                                      G_PARAM_READWRITE));
}

void
go_data_slicer_bitmap_set_member (GODataSlicerBitmap * self, guint bitnum, gboolean is_member)
{
	guint blocknum = (bitnum-1)/32;
	guint block_index = go_data_slicer_bitmap_get_block_index(self, blocknum); /*where the new block should go, or where it is already*/
	guint32 mask = 0x1 << (31-((blocknum-1)%32));
	/*Check to see if the block is compressed*/
	if (go_data_slicer_bitmap_block_is_compressed(self, blocknum)) {
		/*decompress block*/
		g_array_insert_val(self->blocks, block_index, mask);
		go_data_slicer_bitmap_block_set_compressed_flag(self, blocknum, TRUE);
	} else {
		/*set the appropriate block to new value*/
		guint32 new_value = g_array_index(self->blocks, guint32, block_index) | mask; /*calculate new block*/
		g_array_append_val(self->blocks, new_value);
		g_array_remove_index_fast (self->blocks, block_index); /*removes an element and replaces it with the last element*/
	}
}

void 
go_data_slicer_bitmap_set_block (GODataSlicerBitmap * self, guint blocknum, guint32 value) {
	guint block_index = go_data_slicer_bitmap_get_block_index(self, blocknum); /*where the new block should go, or where it is already*/
	/*Check to see if the block is compressed*/
	if (go_data_slicer_bitmap_block_is_compressed(self, blocknum)) {
		/*decompress block*/
		g_array_insert_val(self->blocks, block_index, value);
		go_data_slicer_bitmap_block_set_compressed_flag(self, blocknum, TRUE);
	} else {
		/*set the appropriate block to value*/
		g_array_append_val(self->blocks, value);
		g_array_remove_index_fast (self->blocks, block_index); /*removes an element and replaces it with the last element*/
	}
}

gboolean
go_data_slicer_bitmap_is_member (GODataSlicerBitmap * self, guint bitnum)
{
	/*Find out what block bitnum should be in*/
	guint blocknum = (bitnum-1)/32;

	if (blocknum >= self->blocks->len) {
		/*if the block doesnt exist...*/
		return FALSE;
	} else if (go_data_slicer_bitmap_block_is_compressed(self, blocknum)) {
		/*if the block is compressed, it must be all zeroes*/
		return FALSE;
	} else {
		/*otherwise, look up the value*/
		guint32 mask = 0x1 << (31-((blocknum-1)%32));
		guint32 block = g_array_index(self->block_map, guint32, blocknum);
		if (block == (block | mask)) {
			return TRUE;
		} else {
			return FALSE;
		}
	}
	return FALSE;
}

GODataSlicerBitmap *
go_data_slicer_bitmap_intersect_with (GODataSlicerBitmap * self, GODataSlicerBitmap * other)
{
	/* TODO: Add public function implementation here, using clone (see above) to
	   quickly and efficiently make a new bitmap to represent the result (clone
	   one of the two intersecting bitmaps and store the result in its bitmap,
	   since the size of the result can only decrease)*/
	return NULL;
}
