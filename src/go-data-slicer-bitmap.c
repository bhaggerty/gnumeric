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

#define DEFAULT_MAX_BLOCKS 8
#define DEFAULT_MAX_UNCOMPRESSED_BLOCKS 5

static int PreCompute8 [256] =
{    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
     1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
     1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
     1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
     3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
     4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

enum
{
	PROP_0,

	PROP_NUM_BLOCKS,				/*The number of 32-bit blocks mapped by this bitmap*/
	PROP_MAX_BLOCKS,				/*The number of 32-bit blocks the memory size of this bitmap can accomodate*/
	PROP_NUM_UNCOMPRESSED_BLOCKS,   /*The number of 32-bit blocks which are uncompressed and contain data*/
	PROP_MAX_UNCOMPRESSED_BLOCKS    /*The number of 32-bit blocks which can be uncompressed and used to store data without changing the memory size of this bitmap*/
};

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
go_data_slicer_bitmap_block_is_compressed(const GODataSlicerBitmap *self, guint blocknum) {

	guint8 blockmap_block, mask;
	
	/*if blocknum is outside of the range of the bitmap, return FALSE*/
	if (blocknum >= self->block_map->len*8) {
		return FALSE;
	} else {
		/*Find the guint8 'bin' of block_map that the blocknum-th bit resides in*/
		blockmap_block = g_array_index(self->block_map, guint8, blocknum/8);
		mask = 0x1 << (7-(blocknum%8));
		/*Check to see if the bit is set by setting it and seeing if the 'bin' changes*/
		if (blockmap_block == (blockmap_block | mask)) {
			/*The bit was 1, so the block is uncompressed*/
			return FALSE;
		} else {
			/*The bit was 0, so the block is compressed*/
			return TRUE;
		}
	}
	
	return FALSE;
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
 * @param blocknum - the block to mark in block_map
 * @param is_compressed - the value to set at the bit position in block_map
 */
static void 
go_data_slicer_bitmap_block_set_compressed_flag(GODataSlicerBitmap *self, guint blocknum, gboolean is_compressed) {

	guint8 blockmap_block, mask;
	guint bin = blocknum/8;

	/*make the bin for the blocknum-th bit, if it doesn't exist*/
	if (bin >= self->block_map->len) {
		g_object_set(self, "num_blocks", blocknum, NULL);
	}

	/*The guint8 'bin' the blocknum-th bit resides in*/
	blockmap_block = g_array_index(self->block_map, guint8, bin);

	/*A mask which extracts the bit from the 'bin'*/
	mask = 0x1 << (7 - (blocknum%8));

	/*Set the bit*/
	if (is_compressed) {
		blockmap_block = blockmap_block & ~(mask);
	} else {
		blockmap_block = blockmap_block | mask;
	}

	g_array_append_val(self->block_map, blockmap_block);
	g_array_remove_index_fast (self->block_map, bin); /*removes an element and replaces it with the last element*/
}

/**
 * private get_block_index
 *
 * Converts a blocknum, which is a virtual block number that includes empty
 * blocks, into the actual array index for that block in self->blocks (which does
 * not store empty blocks).  If the block is compressed, this function will
 * return the real self->blocks index where it would be found if it were
 * uncompressed.
 *
 * PRECONDITION: block exists
 *
 * @param virt_blocknum - the block number to convert
 * @return the index of blocknum in blocks
 */
static guint
go_data_slicer_bitmap_get_block_index(const GODataSlicerBitmap *self, guint virt_blocknum) {
	int i; guint8 mask;
	guint result = 0;
	
	/*start by figuring out which 8bit bin in block_map this blocknum's bit resides*/
	guint8 blockmap_block_num = virt_blocknum/8;

	/*count the number of uncompressed blocks which appear before this block -
	  note that this is equivalent to counting 1s in block_map.  Start by
	  counting up to (but not including) blockmap_block_num*/
	for (i=0;i<blockmap_block_num;i++) {
		result += PreCompute8[g_array_index(self->block_map, guint8, i)];
	}

	/*now we just have to count the 1s which appear BEFORE the bit in its own
	  block.  start by masking out the bit we care about and all bits following
	  it.*/
	mask = -0x1 << ((7-(virt_blocknum%8)) + 1);
	result += PreCompute8[(g_array_index(self->block_map, guint8, blockmap_block_num) & mask)];

	return result;
}


G_DEFINE_TYPE (GODataSlicerBitmap, go_data_slicer_bitmap, G_TYPE_OBJECT);

static void
go_data_slicer_bitmap_init (GODataSlicerBitmap *self)
{
	/* set up member variables */
	self->max_blocks = DEFAULT_MAX_BLOCKS;
	self->max_uncompressed_blocks = DEFAULT_MAX_UNCOMPRESSED_BLOCKS;
	self->block_map = g_array_sized_new(FALSE,TRUE,sizeof(guint8), (DEFAULT_MAX_BLOCKS%8 == 0 ? DEFAULT_MAX_BLOCKS/8 : (DEFAULT_MAX_BLOCKS/8 + 1)));
	self->blocks = g_array_sized_new(FALSE, TRUE, sizeof(guint32), DEFAULT_MAX_UNCOMPRESSED_BLOCKS);

	
	/* hook up instance methods */
	self->is_member = go_data_slicer_bitmap_is_member;
	self->set_member = go_data_slicer_bitmap_set_member;
	self->set_block = go_data_slicer_bitmap_set_block;
	self->get_block = go_data_slicer_bitmap_get_block;	
	self->intersect_with = go_data_slicer_bitmap_intersect_with;
}

static void
go_data_slicer_bitmap_finalize (GObject *object)
{
	GODataSlicerBitmap *self = GO_DATA_SLICER_BITMAP(object);
	/*deinitalization code here */
	g_array_free(self->block_map, TRUE);
	g_array_free(self->blocks, TRUE);
	g_array_unref(self->block_map);
	g_array_unref(self->blocks);
	self->blocks = NULL;
	self->block_map = NULL;
	self->max_blocks = 0;
	self->max_uncompressed_blocks = 0;
	G_OBJECT_CLASS (go_data_slicer_bitmap_parent_class)->finalize (object);
}

static void
go_data_slicer_bitmap_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GODataSlicerBitmap *self;
	guint new_size,old_size,i;
	guint8 zero8 = 0;
	guint32 zero32 = 0;
	g_return_if_fail (IS_GO_DATA_SLICER_BITMAP (object));

	self = GO_DATA_SLICER_BITMAP(object);

	switch (prop_id)
	{
	case PROP_NUM_BLOCKS:
		/*Actually create some new compressed blocks*/
		/*Only allow block_map array to be expanded by multiples of 8*/
		new_size = g_value_get_uint (value);
		new_size = new_size % 8 == 0 ? new_size/8 : (new_size/8) + 1;
		for (i=self->block_map->len; i<new_size; i++) {
			g_array_append_val(self->block_map,zero8);
		}
		break;
	case PROP_MAX_BLOCKS:
		/*Allocate space for some new compressed blocks*/			
		/*Only allow block_map array to be expanded by multiples of 8*/
		new_size = g_value_get_uint (value);
	    new_size = new_size % 8 == 0 ? new_size/8 : (new_size/8) + 1;
		old_size = self->max_blocks % 8 == 0 ? self->max_blocks/8 : (self->max_blocks/8)+1;
		if (new_size > old_size) {
			g_array_set_size(self->block_map, new_size);
			self->max_blocks = g_value_get_uint (value);
		}
		break;			
	case PROP_NUM_UNCOMPRESSED_BLOCKS:
		/*Actually create some new uncompressed blocks*/
		/*Only allow block array to be expanded*/
		new_size = g_value_get_uint(value);
		if (new_size > self->blocks->len) {
			for (i=self->blocks->len; i<new_size; i++) {
				g_array_append_val(self->blocks,zero32);
			}
		}
		break;
	case PROP_MAX_UNCOMPRESSED_BLOCKS:
		/*Allocate space for some new uncompressed blocks*/
		/*Only allow block array to be expanded*/			
		new_size = g_value_get_uint(value);
		old_size = self->max_uncompressed_blocks;
		if (new_size > old_size) {
			g_array_set_size(self->blocks, new_size);
			self->max_uncompressed_blocks = g_value_get_uint(value);
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
		g_value_set_uint(value, (self->block_map->len) * 8);
		break;
	case PROP_MAX_BLOCKS:
		g_value_set_uint(value,self->max_blocks);
		break;
	case PROP_NUM_UNCOMPRESSED_BLOCKS:
		g_value_set_uint(value, self->blocks->len);
		break;
	case PROP_MAX_UNCOMPRESSED_BLOCKS:
		g_value_set_uint(value,self->max_uncompressed_blocks);			
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
	                                 g_param_spec_uint ("num_blocks",
	                                                      "num_blocks",
	                                                      "Total number of blocks in this bitmap (compressed or uncompressed)",
	                                                      0,G_MAXUINT,0,
	                                                      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_MAX_BLOCKS,
	                                 g_param_spec_uint ("max_blocks",
	                                                      "max_blocks",
	                                                      "Total number of blocks which can be accommodated by this bitmap (compressed or uncompressed)",
	                                                      1,G_MAXUINT,DEFAULT_MAX_BLOCKS,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (object_class,
	                                 PROP_NUM_UNCOMPRESSED_BLOCKS,
	                                 g_param_spec_uint  ("num_uncompressed_blocks",
	                                                      "num_uncompressed_blocks",
	                                                      "Total number of uncompressed blocks in this bitmap.",
	                                                      0,G_MAXUINT,0,
	                                                      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
	                                 PROP_MAX_UNCOMPRESSED_BLOCKS,
	                                 g_param_spec_uint  ("max_uncompressed_blocks",
	                                                      "max_uncompressed_blocks",
	                                                      "Total number of uncompressed blocks which can be accommodated by this bitmap.",
	                                                      0,G_MAXUINT,DEFAULT_MAX_UNCOMPRESSED_BLOCKS,
	                                                      G_PARAM_READABLE | G_PARAM_CONSTRUCT));	
}

void
go_data_slicer_bitmap_set_member (GODataSlicerBitmap * self, guint bitnum, gboolean is_member)
{
	gboolean was_compressed;
	guint32 mask, old_val;	
	guint virt_blocknum = bitnum/32; /*the virtual blocknum*/

	was_compressed = go_data_slicer_bitmap_block_is_compressed (self,virt_blocknum);

	if (was_compressed) {
		if (is_member) {
			/*the mask is the new uncompressed block*/
			mask = 0x1 << (31-bitnum%32);
			go_data_slicer_bitmap_set_block (self,virt_blocknum,mask);
		} else {
			go_data_slicer_bitmap_set_block (self,virt_blocknum,0x0);
		}
	} else {
		/*mask the existing block to get the new block*/
		mask = 0x1 << (31-bitnum%32);
		old_val = g_array_index(self->blocks, guint32, go_data_slicer_bitmap_get_block_index (self, virt_blocknum));

		if (is_member) {
			go_data_slicer_bitmap_set_block(self,virt_blocknum, old_val | mask);
		} else {
			go_data_slicer_bitmap_set_block(self,virt_blocknum, old_val & (~mask));
		}
	}
}

void 
go_data_slicer_bitmap_set_block (GODataSlicerBitmap * self, guint blocknum, guint32 value) {

	gboolean was_compressed;
	guint real_blocknum;

	/*Check to see if the block was already uncompressed*/
	was_compressed = go_data_slicer_bitmap_block_is_compressed (self,blocknum);
	
	if (value == 0) {		
		/*if the block was previously uncompressed, remove the data for
		 that block (compress it)*/
		if (!was_compressed) {
			/*convert virtual blocknum to real_blocknum*/
			real_blocknum = go_data_slicer_bitmap_get_block_index(self, blocknum);

			g_array_remove_index(self->blocks, real_blocknum);
		}

		/*mark this block as compressed in the block map*/
		go_data_slicer_bitmap_block_set_compressed_flag(self, blocknum, TRUE);
	} else {		
		/*convert virtual blocknum to real_blocknum*/
		real_blocknum = go_data_slicer_bitmap_get_block_index(self, blocknum);
		
		/*Set the block to its new value*/
		if (was_compressed) {
			/*insert the block*/
			g_array_insert_val(self->blocks, real_blocknum, value);
		} else {
			/*replace the block*/
			g_array_append_val(self->blocks, value);		
			g_array_remove_index_fast (self->blocks,real_blocknum);
		}

		/*mark this block as uncompressed in the block map*/
		go_data_slicer_bitmap_block_set_compressed_flag(self, blocknum, FALSE);		
	}
}

guint32 go_data_slicer_bitmap_get_block (const GODataSlicerBitmap * self, guint blocknum) {
	if (go_data_slicer_bitmap_block_is_compressed(self, blocknum)) {
		return 0x0;
	} else {
		return g_array_index(self->blocks,guint32,go_data_slicer_bitmap_get_block_index(self, blocknum));
	}
}

gboolean
go_data_slicer_bitmap_is_member (const GODataSlicerBitmap * self, guint bitnum)
{
	guint32 block, mask;

	/*Find out which virtual block bitnum should be in*/
	guint virt_blocknum = bitnum/32;

	if (virt_blocknum >= self->block_map->len*8) {
		/*if the block doesn't exist...*/
		return FALSE;
	} else if (go_data_slicer_bitmap_block_is_compressed(self, virt_blocknum)) {
		/*if the block is compressed, it must be all zeroes*/
		return FALSE;		
	} else {
		/*otherwise, look up the value*/
		
		/*get the uncompressed block*/
		mask = 0x1 << (31-(bitnum%32));
		block = self->get_block(self,virt_blocknum);
		if (block == (block | mask)) {
			return TRUE;
		} else {
			return FALSE;
		}
	}
}

GODataSlicerBitmap *
go_data_slicer_bitmap_intersect_with (const GODataSlicerBitmap * self, const GODataSlicerBitmap * other)
{
	guint i;
	/*Note that the size of the two arrays can only decrease during union*/
	GODataSlicerBitmap * result = g_object_new (GO_DATA_SLICER_BITMAP_TYPE, "max_blocks", self->block_map->len, "max_uncompressed_blocks", self->blocks->len, NULL);

	/*Loop over all virtual blocks*/
	for (i=0;i<self->block_map->len*8;i++) {
		/*if either block is compressed, the same block in the result will also be compressed*/
		if (go_data_slicer_bitmap_block_is_compressed(self,i) || go_data_slicer_bitmap_block_is_compressed(other,i)) {
			go_data_slicer_bitmap_block_set_compressed_flag(result,i,TRUE);
		} else {
			result->set_block(result,i,self->get_block(self,i) & other->get_block(other,i));
		}
	}

	return result;
}
