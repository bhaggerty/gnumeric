/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * gnumeric
 * Copyright (C) 2010 Sean McIntyre <s.mcintyre@utoronto.ca> 
 *                    David Algar   <david.algar@utoronto.ca>
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

/*******************************************************************************
 * GODataSlicerBitmap														   *
 *																			   *
 * A sparse bitmap which, in our implementation, will be used to map Tuples to *
 * cache records.  This bitmap has the ability to compress empty 32 bit blocks *
 * of data to save space.  Thus it has both a 'true' length and a 'virtual'    *
 * length.																       *
 *******************************************************************************/

#ifndef _GO_DATA_SLICER_BITMAP_H_
#define _GO_DATA_SLICER_BITMAP_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GO_DATA_SLICER_BITMAP_TYPE             (go_data_slicer_bitmap_get_type ())
#define GO_DATA_SLICER_BITMAP(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GO_DATA_SLICER_BITMAP_TYPE, GODataSlicerBitmap))
#define GO_DATA_SLICER_BITMAP_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GO_DATA_SLICER_BITMAP_TYPE, GODataSlicerBitmapClass))
#define IS_GO_DATA_SLICER_BITMAP(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GO_DATA_SLICER_BITMAP_TYPE))
#define IS_GO_DATA_SLICER_BITMAP_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GO_DATA_SLICER_BITMAP_TYPE))
#define GO_DATA_SLICER_BITMAP_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GO_DATA_SLICER_BITMAP_TYPE, GODataSlicerBitmapClass))

typedef struct _GODataSlicerBitmapClass GODataSlicerBitmapClass;
typedef struct _GODataSlicerBitmap GODataSlicerBitmap;

struct _GODataSlicerBitmapClass
{
	GObjectClass parent_class;
};

struct _GODataSlicerBitmap
{
	GObject parent_instance;
	GArray * block_map; /*A bitmap with a single bit for each block, 
					      representing whether or not it is 'compressed',
						  i.e. whether or not it appears in the blocks array.
						  0 indicates that the block is compressed, and 1
	                      indicates that the block is uncompressed.*/
	GArray * blocks;    /*contains all uncompressed blocks of 32 bits*/

	guint max_blocks;   /*The number of 32-bit blocks the memory size of this bitmap can accomodate*/
	guint max_uncompressed_blocks;  /*The number of 32-bit blocks which can be uncompressed and used to store data without changing the memory size of this bitmap*/

	gboolean (*is_member) (GODataSlicerBitmap * self, guint bitnum);
	void (*set_member) (GODataSlicerBitmap * self, guint bitnum, gboolean is_member);
	void (*set_block) (GODataSlicerBitmap * self, guint blocknum, guint32 value);
	guint32 (*get_block) (GODataSlicerBitmap * self, guint blocknum);
	GODataSlicerBitmap * (*intersect_with) (GODataSlicerBitmap * self, GODataSlicerBitmap * other);	
};

GType go_data_slicer_bitmap_get_type (void);

/**
 * is_member
 * 
 * @param self - this bitmap
 * @param bitnum - the bit to check (zero-indexed)
 * @return true if bit bitnum is 1, false otherwise.
 */
gboolean go_data_slicer_bitmap_is_member (GODataSlicerBitmap * self, guint bitnum);

/**
 * set_member
 *
 * @param self - this bitmap
 * @param bitnum - the bit to set (zero-indexed)
 * @param is_member - the value to assign to bitnum
 */
void go_data_slicer_bitmap_set_member (GODataSlicerBitmap * self, guint bitnum, gboolean is_member);

/**
 * set_block
 *
 * Sets an entire block of this bitmap to a particular value, 
 * compressing/decompressing the block as necessary.
 *
 * @param self - this bitmap
 * @param blocknum - the block to set (zero-indexed)
 * @param value - the bits to store in the block
 */
void go_data_slicer_bitmap_set_block (GODataSlicerBitmap * self, guint blocknum, guint32 value);

/**
 * get_block
 *
 * Gets an entire virtual block of this bitmap
 *
 * @param self - this bitmap
 * @param blocknum - the block to get (zero-indexed)
 * @return the virtual block's value
 */
guint32 go_data_slicer_bitmap_get_block (GODataSlicerBitmap * self, guint blocknum);

/**
 * intersect_with
 *
 * Performs an intersection (bitwise AND) between this bitmap and another.
 *
 * @param self - this bitmap
 * @param other - another bitmap with the same number of blocks
 * @return - a new bitmap representing the result of the intersection.
 */
GODataSlicerBitmap * go_data_slicer_bitmap_intersect_with (GODataSlicerBitmap * self, GODataSlicerBitmap * other);

#ifdef GO_DEBUG_SLICERS
void go_data_slicer_bitmap_dump_bitmap (GODataSlicerBitmap * self);
#endif

G_END_DECLS

#endif /* _GO_DATA_SLICER_BITMAP_H_ */
