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

/*******************************************************************************
 * GODataSlicerBitmap														   *
 *																			   *
 * A sparse bitmap which, in our implementation, will be used to map Tuples to *
 * cache records.  This bitmap has the ability to compress empty 32 bit blocks *
 * of data to save space.  Thus it has both a 'true' length and a 'virtual'    *
 * length.   Bits are one-indexed in the bitmap and its block_map.			   *
 *******************************************************************************/

#ifndef _GO_DATA_SLICER_BITMAP_H_
#define _GO_DATA_SLICER_BITMAP_H_

#include <glib-object.h>

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
						  i.e. whether or not it appears in the blocks array.*/
	GArray * blocks;    /*contains all uncompressed blocks of 32 bits*/

	gboolean (*is_member) (GODataSlicerBitmap * self, guint bitnum);
	void (*set_member) (GODataSlicerBitmap * self, guint bitnum, gboolean is_member);
	void (*set_block) (GODataSlicerBitmap * self, guint blocknum, guint32 value);	
	GODataSlicerBitmap * (*intersect_with) (GODataSlicerBitmap * self, GODataSlicerBitmap * other);
};

GType go_data_slicer_bitmap_get_type (void) G_GNUC_CONST;

/**
 * is_member
 * 
 * @param self - this bitmap
 * @param bitnum - the bit to check
 * @return true if bit bitnum is 1, false otherwise.
 */
gboolean go_data_slicer_bitmap_is_member (GODataSlicerBitmap * self, guint bitnum);

/**
 * set_member
 *
 * @param self - this bitmap
 * @param bitnum - the bit to set
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
 * @param blocknum - the block to set
 * @param value - the bits to store in the block
 */
void go_data_slicer_bitmap_set_block (GODataSlicerBitmap * self, guint blocknum, guint32 value);

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

G_END_DECLS

#endif /* _GO_DATA_SLICER_BITMAP_H_ */
