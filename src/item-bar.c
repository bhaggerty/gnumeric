/* vim: set sw=8: */
/*
 * A canvas item implementing row/col headers with support for outlining.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg   (jody@gnome.org)
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "item-bar.h"
#include "gnm-pane-impl.h"

#include "style-color.h"
#include "sheet.h"
#include "sheet-control-gui.h"
#include "sheet-control-gui-priv.h"
#include "application.h"
#include "selection.h"
#include "wbc-gtk-impl.h"
#include "gui-util.h"
#include "parse-util.h"
#include "commands.h"

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#define GNUMERIC_ITEM "BAR"

#include <string.h>

struct _ItemBar {
	GocItem	 base;

	GnmPane		*pane;
	GdkGC           *text_gc, *filter_gc, *lines, *shade;
	GdkCursor       *normal_cursor;
	GdkCursor       *change_cursor;
	PangoFont	*normal_font, *bold_font;
	int             normal_font_ascent, bold_font_ascent;
	GtkWidget       *tip;			/* Tip for scrolling */
	gboolean	 dragging;
	gboolean	 is_col_header;
	gboolean	 has_resize_guides;
	int		 indent, cell_width, cell_height;
	int		 start_selection;	/* Where selection started */
	int		 colrow_being_resized;
	int		 colrow_resize_size;
	int		 resize_start_pos;

	struct {
		PangoItem	 *item;
		PangoGlyphString *glyphs;
	} pango;
};

typedef GocItemClass ItemBarClass;
static GocItemClass *parent_class;

enum {
	ITEM_BAR_PROP_0,
	ITEM_BAR_PROP_PANE,
	ITEM_BAR_PROP_IS_COL_HEADER
};

static int
ib_compute_pixels_from_indent (Sheet const *sheet, gboolean const is_cols)
{
	double const scale =
		sheet->last_zoom_factor_used *
		gnm_app_display_dpi_get (is_cols) / 72.;
	int const indent = is_cols
		? sheet->cols.max_outline_level
		: sheet->rows.max_outline_level;
	if (!sheet->display_outlines || indent <= 0)
		return 0;
	return (int)(5 + (indent + 1) * 14 * scale + 0.5);
}

static void
ib_fonts_unref (ItemBar *ib)
{
	if (ib->normal_font != NULL) {
		g_object_unref (ib->normal_font);
		ib->normal_font = NULL;
	}

	if (ib->bold_font != NULL) {
		g_object_unref (ib->bold_font);
		ib->bold_font = NULL;
	}
}

/**
 * item_bar_calc_size :
 * @ib : #ItemBar
 *
 * Scale fonts and sizes by the pixels_per_unit of the associated sheet.
 *
 * Returns : the size of the fixed dimension.
 **/
int
item_bar_calc_size (ItemBar *ib)
{
	SheetControlGUI	* const scg = ib->pane->simple.scg;
	Sheet const *sheet = scg_sheet (scg);
	double const zoom_factor = sheet->last_zoom_factor_used;
	PangoContext *context;
	PangoFontDescription const *src_desc = wbcg_get_font_desc (scg_wbcg (scg));
	PangoFontDescription *desc;
	int size = pango_font_description_get_size (src_desc);
	PangoLayout *layout;
	PangoRectangle ink_rect, logical_rect;
	gboolean const char_label = ib->is_col_header && !sheet->convs->r1c1_addresses;
	GList *item_list;
	PangoAttrList *attr_list;

	ib_fonts_unref (ib);

	context = gtk_widget_get_pango_context (GTK_WIDGET (ib->pane));
	desc = pango_font_description_copy (src_desc);
	pango_font_description_set_size (desc, zoom_factor * size);
	layout = pango_layout_new (context);

	/*
	 * Figure out how tall the label can be.
	 * (Note that we avoid J/Q/Y which may go below the line.)
	 */
	pango_layout_set_text (layout,
			       char_label ? "AHW" : "0123456789",
			       -1);
	ib->normal_font = pango_context_load_font (context, desc);
	pango_layout_set_font_description (layout, desc);
	pango_layout_get_extents (layout, &ink_rect, NULL);
	ib->normal_font_ascent = PANGO_PIXELS (ink_rect.height + ink_rect.y);

	/*
	 * Use the size of the bold header font to size the free dimensions.
	 * Add 2 pixels above and below.
	 */
	pango_font_description_set_weight (desc, PANGO_WEIGHT_BOLD);
	ib->bold_font = pango_context_load_font (context, desc);
	pango_layout_set_font_description (layout, desc);
	pango_layout_get_extents (layout, &ink_rect, &logical_rect);
	ib->cell_height = 2 + 2 + PANGO_PIXELS (logical_rect.height);
	ib->bold_font_ascent = PANGO_PIXELS (ink_rect.height + ink_rect.y);

	/* 5 pixels left and right plus the width of the widest string I can think of */
	if (char_label)
		pango_layout_set_text (layout, "WWWWWWWWWW", strlen (col_name (gnm_sheet_get_last_col (sheet))));
	else
		pango_layout_set_text (layout, "8888888888", strlen (row_name (gnm_sheet_get_last_row (sheet))));
	pango_layout_get_extents (layout, NULL, &logical_rect);
	ib->cell_width = 5 + 5 + PANGO_PIXELS (logical_rect.width);

	attr_list = pango_attr_list_new ();
	pango_attr_list_insert (attr_list, pango_attr_font_desc_new (desc));
	item_list = pango_itemize (context, "A", 0, 1, attr_list, NULL);
	pango_attr_list_unref (attr_list);

	ib->pango.item = item_list->data;
	item_list->data = NULL;

	if (item_list->next != NULL)
	  g_warning ("Leaking pango items");

	g_list_free (item_list);

	ib->indent = ib_compute_pixels_from_indent (sheet, ib->is_col_header);

	pango_font_description_free (desc);
	g_object_unref (layout);

	return ib->indent +
		(ib->is_col_header ? ib->cell_height : ib->cell_width);
}

PangoFont *
item_bar_normal_font (ItemBar const *ib)
{
	return ib->normal_font;
}

int
item_bar_indent	(ItemBar const *ib)
{
	return ib->indent;
}

static void
item_bar_update_bounds (GocItem *item)
{
	ItemBar *ib = ITEM_BAR (item);
	item->x0 = 0;
	item->y0 = 0;
	if (ib->is_col_header) {
		item->x1 = G_MAXINT64/2;
		item->y1 = (ib->cell_height + ib->indent);
	} else {
		item->x1 = (ib->cell_width  + ib->indent);
		item->y1 = G_MAXINT64/2;
	}
}

static void
item_bar_realize (GocItem *item)
{
	ItemBar *ib;
	GdkDisplay *display;

	if (parent_class->realize)
		(*parent_class->realize)(item);

	ib = ITEM_BAR (item);

	display = gtk_widget_get_display (GTK_WIDGET (item->canvas));
	ib->normal_cursor = gdk_cursor_new_for_display (display, GDK_LEFT_PTR);
	if (ib->is_col_header)
		ib->change_cursor = gdk_cursor_new_for_display (display, GDK_SB_H_DOUBLE_ARROW);
	else
		ib->change_cursor = gdk_cursor_new_for_display (display, GDK_SB_V_DOUBLE_ARROW);
	item_bar_calc_size (ib);
}

static void
item_bar_unrealize (GocItem *item)
{
	ItemBar *ib = ITEM_BAR (item);

	gdk_cursor_unref (ib->change_cursor);
	gdk_cursor_unref (ib->normal_cursor);

	parent_class->unrealize (item);
}

static void
ib_draw_cell (ItemBar const * const ib, cairo_t *cr,
	      ColRowSelectionType const type,
	      char const * const str, GocRect *rect)
{
	GtkLayout	*canvas = GTK_LAYOUT (ib->base.canvas);
	GtkWidget   *widget = GTK_WIDGET (canvas);
	PangoFont	*font;
	PangoRectangle   size;
	GOColor color;
	int shadow, ascent;

	switch (type) {
	default:
	case COL_ROW_NO_SELECTION:
		shadow = GTK_SHADOW_OUT;
		font   = ib->normal_font;
		color = GO_COLOR_FROM_GDK (widget->style->bg[GTK_STATE_ACTIVE]);
		ascent = ib->normal_font_ascent;
		break;

	case COL_ROW_PARTIAL_SELECTION:
		shadow = GTK_SHADOW_OUT;
		font   = ib->bold_font;
		color = GO_COLOR_FROM_GDK (widget->style->dark[GTK_STATE_PRELIGHT]);
		ascent = ib->bold_font_ascent;
		break;

	case COL_ROW_FULL_SELECTION:
		shadow = GTK_SHADOW_IN;
		font   = ib->bold_font;
		color = GO_COLOR_FROM_GDK (widget->style->dark[GTK_STATE_NORMAL]);
		ascent = ib->bold_font_ascent;
		break;
	}
	/* When we are really small leave out the shadow and the text */
	cairo_save (cr);
	cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (color));
	if (rect->width <= 2 || rect->height <= 2) {
		cairo_rectangle (cr, rect->x, rect->y, rect->width, rect->height);
		cairo_fill (cr);
		cairo_restore (cr);
		return;
	}

	cairo_rectangle (cr, rect->x + 1, rect->y + 1, rect->width - 2, rect->height - 2);
	cairo_fill_preserve (cr);
	cairo_restore (cr);

	/* The widget parameters could be NULL, but if so some themes would emit a warning.
	 * (Murrine is known to do this: http://bugzilla.gnome.org/show_bug.cgi?id=564410). */
	gtk_paint_shadow (widget->style, canvas->bin_window, GTK_STATE_NORMAL, shadow,
			  NULL, widget, "GnmItemBarCell",
			  rect->x, rect->y, rect->width + 1, rect->height + 1);

	g_return_if_fail (font != NULL);
	g_object_unref (ib->pango.item->analysis.font);
	ib->pango.item->analysis.font = g_object_ref (font);
	pango_shape (str, strlen (str), &(ib->pango.item->analysis), ib->pango.glyphs);
	pango_glyph_string_extents (ib->pango.glyphs, font, NULL, &size);

	cairo_save (cr);
	cairo_clip (cr);
	cairo_set_source_rgb (cr, 0., 0., 0.);
	cairo_translate (cr,
					 rect->x + (rect->width - PANGO_PIXELS (size.width)) / 2,
					 rect->y  + (rect->height - PANGO_PIXELS (size.height)) / 2 + ascent);
	pango_cairo_show_glyph_string (cr, font, ib->pango.glyphs);
	cairo_restore (cr);
}

int
item_bar_group_size (ItemBar const *ib, int max_outline)
{
	return (max_outline > 0) ? (ib->indent - 2) / (max_outline + 1) : 0;
}

static gboolean
item_bar_draw_region (GocItem const *item, cairo_t *cr, double x_0, double y_0, double x_1, double y_1)
{
	double scale = item->canvas->pixels_per_unit;
	int x0, x1, y0, y1;
	ItemBar const         *ib = ITEM_BAR (item);
	GnmPane	 const	      *pane = ib->pane;
	SheetControlGUI const *scg    = pane->simple.scg;
	Sheet const           *sheet  = scg_sheet (scg);
	SheetView const	      *sv     = scg_view (scg);
	GtkWidget *canvas = GTK_WIDGET (item->canvas);
	ColRowInfo const *cri, *next = NULL;
	int pixels;
	gboolean prev_visible;
	gboolean const draw_below = sheet->outline_symbols_below != FALSE;
	gboolean const draw_right = sheet->outline_symbols_right != FALSE;
	int prev_level;
	GocRect rect;
	GocPoint points[3];
	gboolean const has_object = scg->wbcg->new_object != NULL || scg->selected_objects != NULL;
	gboolean const rtl = sheet->text_is_rtl != FALSE;
	int shadow;
	int first_line_offset = 1;
	GtkStyle *style = gtk_widget_get_style (canvas);
	GOColor color = GO_COLOR_FROM_GDK (style->text[GTK_STATE_NORMAL]);
	goc_canvas_c2w (item->canvas, x_0, y_0, &x0, &y0);
	goc_canvas_c2w (item->canvas, x_1, y_1, &x1, &y1);

	if (ib->is_col_header) {
		int const inc = item_bar_group_size (ib, sheet->cols.max_outline_level);
		int const base_pos = .2 * inc;
		int const len = (inc > 4) ? 4 : inc;
		int end, total, col = pane->first.col;
		gboolean const char_label = !sheet->convs->r1c1_addresses;

		/* shadow type selection must be keep in sync with code in ib_draw_cell */
		goc_canvas_c2w (item->canvas, pane->first_offset.x / scale, 0, &total, NULL);
		end = x1;
		rect.y = ib->indent;
		rect.height = ib->cell_height;
		shadow = (col > 0 && !has_object && sv_selection_col_type (sv, col-1) == COL_ROW_FULL_SELECTION)
			? GTK_SHADOW_IN : GTK_SHADOW_OUT;

		if (col > 0) {
			cri = sheet_col_get_info (sheet, col-1);
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
		} else {
			prev_visible = TRUE;
			prev_level = 0;
		}

		do {
			if (col >= gnm_sheet_get_max_cols (sheet))
				return TRUE;

			/* DO NOT enable resizing all until we get rid of
			 * resize_start_pos.  It will be wrong if things ahead
			 * of it move
			 */
			cri = sheet_col_get_info (sheet, col);
			if (col != -1 && ib->colrow_being_resized == col)
			/* || sv_is_colrow_selected (sheet, col, TRUE))) */
				pixels = ib->colrow_resize_size;
			else
				pixels = cri->size_pixels;

			if (cri->visible) {
				int left, level, i = 0, pos = base_pos;

				if (rtl) {
					left = (total -= pixels);
					rect.x = total;
				} else {
					rect.x = left = total;
					total += pixels;
				}

				rect.width = pixels;
				ib_draw_cell (ib, cr,
					       has_object
						       ? COL_ROW_NO_SELECTION
						       : sv_selection_col_type (sv, col),
					       char_label
						       ? col_name (col)
						       : row_name (col),
					       &rect);

				if (len > 0) {
					cairo_save (cr);
					cairo_set_line_width (cr, 2.0);
					cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
					cairo_set_line_join (cr, CAIRO_LINE_JOIN_MITER);
					cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (color));
					if (!draw_right) {
						next = sheet_col_get_info (sheet, col + 1);
						prev_level = next->outline_level;
						prev_visible = next->visible;
						points[0].x = rtl ? (total + pixels) : left;
					} else
						points[0].x = total;

					/* draw the start or end marks and the vertical lines */
					points[1].x = points[2].x = left + pixels/2;
					for (level = cri->outline_level; i++ < level ; pos += inc) {
						points[0].y = points[1].y = pos;
						points[2].y		  = pos + len;
						if (i > prev_level) {
							cairo_move_to (cr, points[0].x, points[0].y);
							cairo_line_to (cr, points[1].x, points[1].y);
							cairo_line_to (cr, points[2].x, points[2].y);
						} else if (rtl) {
							cairo_move_to (cr, left - first_line_offset, pos);
							cairo_line_to (cr, total + pixels, pos);
						} else {
							cairo_move_to (cr, left - first_line_offset, pos);
							cairo_line_to (cr, total, pos);
						}
					}
					first_line_offset = 0;

					if (draw_right ^ rtl) {
						if (prev_level > level) {
							int safety = 0;
							int top = pos - base_pos + 2; /* inside cell's shadow */
							int size = inc < pixels ? inc : pixels;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							gtk_paint_shadow (canvas->style, GTK_LAYOUT (canvas)->bin_window,
								 GTK_STATE_NORMAL,
								 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 left, top+safety, size, size);
							if (size > 9) {
								if (!prev_visible) {
									top++;
									left++;
									cairo_move_to (cr, left+size/2, top+3);
									cairo_line_to (cr, left+size/2, top+size-4);
								}
								cairo_move_to (cr, left+3,	    top+size/2);
								cairo_line_to (cr, left+size-4, top+size/2);
							}
						} else if (level > 0) {
							cairo_move_to (cr, left+pixels/2, pos);
							cairo_line_to (cr, left+pixels/2, pos+len);
						}
					} else {
						if (prev_level > level) {
							int safety = 0;
							int top = pos - base_pos + 2; /* inside cell's shadow */
							int size = inc < pixels ? inc : pixels;
							int right;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							right = (rtl ? (total + pixels) : total) - size;
							gtk_paint_shadow (canvas->style, GTK_LAYOUT (canvas)->bin_window,
								 GTK_STATE_NORMAL,
								 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 right, top+safety, size, size);
							if (size > 9) {
								if (!prev_visible) {
									top++;
									right++;
									cairo_move_to (cr, right+size/2, top+3);
									cairo_line_to (cr, right+size/2, top+size-4);
								}
								cairo_move_to (cr, right+3,	    top+size/2);
								cairo_line_to (cr, right+size-4, top+size/2);
							}
						} else if (level > 0) {
								cairo_move_to (cr, left+pixels/2, pos);
								cairo_line_to (cr, left+pixels/2, pos+len);
						}
					}
					cairo_stroke (cr);
					cairo_restore (cr);
				}
			}
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
			++col;
		} while ((rtl && end <= total) || (!rtl && total <= end));
	} else {
		int const inc = item_bar_group_size (ib, sheet->rows.max_outline_level);
		int base_pos = .2 * inc;
		int const len = (inc > 4) ? 4 : inc;
		int const end = y1;
		int const dir = rtl ? -1 : 1;

		int total = pane->first_offset.y - item->canvas->scroll_y1 * scale;
		int row = pane->first.row;

		if (rtl) {
			base_pos = ib->indent + ib->cell_width - base_pos;
			/* Move header bar 1 pixel to the left. */
			rect.x = -1;
		} else
			rect.x = ib->indent;
		rect.width = ib->cell_width;

		if (row > 0) {
			cri = sheet_row_get_info (sheet, row-1);
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
		} else {
			prev_visible = TRUE;
			prev_level = 0;
		}

		do {
			if (row >= gnm_sheet_get_max_rows (sheet))
				return TRUE;

			/* DO NOT enable resizing all until we get rid of
			 * resize_start_pos.  It will be wrong if things ahead
			 * of it move
			 */
			cri = sheet_row_get_info (sheet, row);
			if (row != -1 && ib->colrow_being_resized == row)
			/* || sv_is_colrow_selected (sheet, row, FALSE))) */
				pixels = ib->colrow_resize_size;
			else
				pixels = cri->size_pixels;

			if (cri->visible) {
				int level, i = 0, pos = base_pos;
				int top = total;

				total += pixels;
				rect.y = top;
				rect.height = pixels;
				ib_draw_cell (ib, cr,
					      has_object
						      ? COL_ROW_NO_SELECTION
						      : sv_selection_row_type (sv, row),
					      row_name (row), &rect);

				if (len > 0) {
					cairo_save (cr);
					cairo_set_line_width (cr, 2.0);
					cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
					cairo_set_line_join (cr, CAIRO_LINE_JOIN_MITER);
					cairo_set_source_rgba (cr, GO_COLOR_TO_CAIRO (color));
					if (!draw_below) {
						next = sheet_row_get_info (sheet, row + 1);
						points[0].y = top;
					} else
						points[0].y = total;

					/* draw the start or end marks and the vertical lines */
					points[1].y = points[2].y = top + pixels/2;
					for (level = cri->outline_level; i++ < level ; pos += inc * dir) {
						points[0].x = points[1].x = pos;
						points[2].x		  = pos + len * dir;
						if (draw_below && i > prev_level) {
							cairo_move_to (cr, points[0].x, points[0].y);
							cairo_line_to (cr, points[1].x, points[1].y);
							cairo_line_to (cr, points[2].x, points[2].y);
						} else if (!draw_below && i > next->outline_level) {
							cairo_move_to (cr, points[0].x, points[0].y);
							cairo_line_to (cr, points[1].x, points[1].y);
							cairo_line_to (cr, points[2].x, points[2].y);
						} else {
							cairo_move_to (cr, pos, top - first_line_offset);
							cairo_line_to (cr, pos, total);
						}
					}
					first_line_offset = 0;

					if (draw_below) {
						if (prev_level > level) {
							int left, safety = 0;
							int size = inc < pixels ? inc : pixels;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							/* inside cell's shadow */
							left = pos - dir * (.2 * inc - 2);
							if (rtl)
								left -= size;
							gtk_paint_shadow (canvas->style, GTK_LAYOUT (canvas)->bin_window,
								 GTK_STATE_NORMAL,
								 prev_visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 left+safety, top, size, size);
							if (size > 9) {
								if (!prev_visible) {
									left += dir;
									top++;
									cairo_move_to (cr, left+size/2, top+3);
									cairo_line_to (cr, left+size/2, top+size-4);
								}
								cairo_move_to (cr, left+3,	    top+size/2);
								cairo_line_to (cr, left+size-4, top+size/2);
							}
						} else if (level > 0)
							cairo_move_to (cr, pos,      top+pixels/2);
							cairo_line_to (cr, pos+len,  top+pixels/2);
					} else {
						if (next->outline_level > level) {
							int left, safety = 0;
							int size = inc < pixels ? inc : pixels;
							int bottom;

							if (size > 15)
								size = 15;
							else if (size < 6)
								safety = 6 - size;

							/* inside cell's shadow */
							left = pos - dir * (.2 * inc - 2);
							if (rtl)
								left -= size;
							bottom = total - size;
							gtk_paint_shadow (canvas->style, GTK_LAYOUT (canvas)->bin_window,
								 GTK_STATE_NORMAL,
								 next->visible ? GTK_SHADOW_OUT : GTK_SHADOW_IN,
								 NULL, NULL, "GnmItemBarCell",
								 left+safety*dir, bottom, size, size);
							if (size > 9) {
								if (!next->visible) {
									left += dir;
									top++;
									cairo_move_to (cr, left+size/2, bottom+3);
									cairo_line_to (cr, left+size/2, bottom+size-4);
								}
								cairo_move_to (cr, left+3,	    bottom+size/2);
								cairo_line_to (cr, left+size-4, bottom+size/2);
							}
						} else if (level > 0)
							cairo_move_to (cr, pos,      top+pixels/2);
							cairo_line_to (cr, pos+len,  top+pixels/2);
					}
					cairo_stroke (cr);
					cairo_restore (cr);
				}
			}
			prev_visible = cri->visible;
			prev_level = cri->outline_level;
			++row;
		} while (total <= end);
	}
	return TRUE;
}

static double
item_bar_distance (GocItem *item, double x, double y,
		GocItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

/**
 * is_pointer_on_division :
 * @ib : #ItemBar
 * @x  : in world coords
 * @y  : in world coords
 * @the_total :
 * @the_element :
 * @minor_pos :
 *
 * NOTE : this could easily be optimized.  We need not start at 0 every time.
 *        We could potentially use the routines in gnm-pane.
 *
 * Returns non-NULL if point (@x,@y) is on a division
 **/
static ColRowInfo const *
is_pointer_on_division (ItemBar const *ib, gint64 x, gint64 y,
			gint64 *the_total, int *the_element, gint64 *minor_pos)
{
	Sheet *sheet = scg_sheet (ib->pane->simple.scg);
	ColRowInfo const *cri;
	gint64 major, minor, total = 0;
	int i;

	if (ib->is_col_header) {
		major = x;
		minor = y;
		i = ib->pane->first.col;
		total = ib->pane->first_offset.x;
	} else {
		major = y;
		minor = x;
		i = ib->pane->first.row;
		total = ib->pane->first_offset.y;
	}
	if (NULL != minor_pos)
		*minor_pos = minor;
	if (NULL != the_element)
		*the_element = -1;
	for (; total < major; i++) {
		if (ib->is_col_header) {
			if (i >= gnm_sheet_get_max_cols (sheet))
				return NULL;
			cri = sheet_col_get_info (sheet, i);
		} else {
			if (i >= gnm_sheet_get_max_rows (sheet))
				return NULL;
			cri = sheet_row_get_info (sheet, i);
		}

		if (cri->visible) {
			WBCGtk *wbcg = scg_wbcg (ib->pane->simple.scg);
			total += cri->size_pixels;

			if (wbc_gtk_get_guru (wbcg) == NULL &&
			    !wbcg_is_editing (wbcg) &&
			    (total - 4 < major) && (major < total + 4)) {
				if (the_total)
					*the_total = total;
				if (the_element)
					*the_element = i;
				return (minor >= ib->indent) ? cri : NULL;
			}
		}

		if (total > major) {
			if (the_element)
				*the_element = i;
			return NULL;
		}
	}
	return NULL;
}

/* x & y in world coords */
static void
ib_set_cursor (ItemBar *ib, gint64 x, gint64 y)
{
	GdkWindow *window = GTK_WIDGET (ib->base.canvas)->window;
	GdkCursor *cursor = ib->normal_cursor;

	/* We might be invoked before we are realized */
	if (NULL == window)
		return;
	if (NULL != is_pointer_on_division (ib, x, y, NULL, NULL, NULL))
		cursor = ib->change_cursor;
	gdk_window_set_cursor (window, cursor);
}

static void
colrow_tip_setlabel (ItemBar *ib, gboolean const is_cols, int size_pixels)
{
	if (ib->tip != NULL) {
		char *buffer;
		double const scale = 72. / gnm_app_display_dpi_get (!is_cols);
		if (is_cols)
			buffer = g_strdup_printf (_("Width: %.2f pts (%d pixels)"),
						  scale*size_pixels, size_pixels);
		else
			buffer = g_strdup_printf (_("Height: %.2f pts (%d pixels)"),
						  scale*size_pixels, size_pixels);
		gtk_label_set_text (GTK_LABEL (ib->tip), buffer);
		g_free(buffer);
	}
}

static void
item_bar_resize_stop (ItemBar *ib, int new_size)
{
	if (new_size != 0 && ib->colrow_being_resized >= 0)
		scg_colrow_size_set (ib->pane->simple.scg,
				     ib->is_col_header,
				     ib->colrow_being_resized, new_size);
	ib->colrow_being_resized = -1;
	ib->has_resize_guides = FALSE;
	scg_size_guide_stop (ib->pane->simple.scg);

	if (ib->tip != NULL) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ib->tip));
		ib->tip = NULL;
	}
}

static gboolean
cb_extend_selection (GnmPane *pane, GnmPaneSlideInfo const *info)
{
	ItemBar * const ib = info->user_data;
	gboolean const is_cols = ib->is_col_header;
	scg_colrow_select (pane->simple.scg,
		is_cols, is_cols ? info->col : info->row, GDK_SHIFT_MASK);
	return TRUE;
}

static gint
outline_button_press (ItemBar const *ib, int element, int pixel)
{
	SheetControlGUI *scg = ib->pane->simple.scg;
	Sheet * const sheet = scg_sheet (scg);
	int inc, step;

	if (ib->is_col_header) {
		if (sheet->cols.max_outline_level <= 0)
			return TRUE;
		inc = (ib->indent - 2) / (sheet->cols.max_outline_level + 1);
	} else {
		if (sheet->rows.max_outline_level <= 0)
			return TRUE;
		inc = (ib->indent - 2) / (sheet->rows.max_outline_level + 1);
	}

	step = pixel / inc;

	cmd_selection_outline_change (scg_wbc (scg), ib->is_col_header,
				      element, step);
	return TRUE;
}

static gboolean
item_bar_button_pressed (GocItem *item, int button, double x_, double y_)
{
	ColRowInfo const *cri;
	GocCanvas	* const canvas = item->canvas;
	ItemBar		* const ib = ITEM_BAR (item);
	GnmPane		* const pane = ib->pane;
	SheetControlGUI	* const scg = pane->simple.scg;
	SheetControl	* const sc = (SheetControl *) pane->simple.scg;
	Sheet		* const sheet = sc_sheet (sc);
	WBCGtk * const wbcg = scg_wbcg (scg);
	gboolean const is_cols = ib->is_col_header;
	gint64 minor_pos, start;
	int element;
	GdkEventButton *event = (GdkEventButton *) goc_canvas_get_cur_event (item->canvas);
	gint64 x = x_ * item->canvas->pixels_per_unit, y = y_ * item->canvas->pixels_per_unit;

	if (button > 3)
		return FALSE;

	if (wbc_gtk_get_guru (wbcg) == NULL)
		scg_mode_edit (scg);

	cri = is_pointer_on_division (ib, x, y,
		&start, &element, &minor_pos);
	if (element < 0)
		return FALSE;
	if (minor_pos < ib->indent)
		return outline_button_press (ib, element, minor_pos);

	if (button == 3) {
		if (wbc_gtk_get_guru (wbcg) != NULL)
			return TRUE;
		/* If the selection does not contain the current row/col
		 * then clear the selection and add it.
		 */
		if (!sv_is_colrow_selected (sc_view (sc), element, is_cols))
			scg_colrow_select (scg, is_cols,
					   element, event->state);

		scg_context_menu (scg, event, is_cols, !is_cols);
		return TRUE;
	} else if (cri != NULL) {
		/*
		 * Record the important bits.
		 *
		 * By setting colrow_being_resized to a non -1 value,
		 * we know that we are being resized (used in the
		 * other event handlers).
		 */
		ib->colrow_being_resized = element;
		ib->resize_start_pos = (is_cols && sheet->text_is_rtl)
			? start : (start - cri->size_pixels);
		ib->colrow_resize_size = cri->size_pixels;

		if (ib->tip == NULL) {
			GtkWidget *cw = GTK_WIDGET (canvas);
			int wx, wy;
			ib->tip = gnumeric_create_tooltip (cw);
			colrow_tip_setlabel (ib, is_cols, ib->colrow_resize_size);
			/* Position above the current point for both
			 * col and row headers.  trying to put it
			 * beside for row headers often ends up pushing
			 * the tip under the cursor which can have odd
			 * effects on the event stream.  win32 was
			 * different from X. */

			gnm_canvas_get_position (canvas, &wx, &wy,x, y);
			gnumeric_position_tooltip (ib->tip,
						   wx, wy, TRUE);
			gtk_widget_show_all (gtk_widget_get_toplevel (ib->tip));
		}
	} else {
		if (wbc_gtk_get_guru (wbcg) != NULL &&
		    !wbcg_entry_has_logical (wbcg))
			return TRUE;

		/* If we're editing it is possible for this to fail */
		if (!scg_colrow_select (scg, is_cols, element, event->state))
			return TRUE;

		ib->start_selection = element;
		gnm_pane_slide_init (pane);
	}
	gnm_simple_canvas_grab (item,
		GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
		ib->change_cursor, event->time);
	return TRUE;
}

static gboolean
item_bar_2button_pressed (GocItem *item, int button, double x, double y)
{
	ItemBar		* const ib = ITEM_BAR (item);
	if (button > 3)
		return FALSE;

	if (button != 3)
		item_bar_resize_stop (ib, -1);
	return TRUE;
}

static gboolean
item_bar_enter_notify (GocItem *item, double x_, double y_)
{
	ItemBar		* const ib = ITEM_BAR (item);
	gint64 x = x_ * item->canvas->pixels_per_unit, y = y_ * item->canvas->pixels_per_unit;
	ib_set_cursor (ib, x, y);
	return TRUE;
}

static gboolean
item_bar_motion (GocItem *item, double x_, double y_)
{
	ColRowInfo const *cri;
	GocCanvas	* const canvas = item->canvas;
	ItemBar		* const ib = ITEM_BAR (item);
	GnmPane		* const pane = ib->pane;
	SheetControlGUI	* const scg = pane->simple.scg;
	SheetControl	* const sc = (SheetControl *) pane->simple.scg;
	Sheet		* const sheet = sc_sheet (sc);
	gboolean const is_cols = ib->is_col_header;
	gint64 pos;
	gint64 x = x_ * item->canvas->pixels_per_unit, y = y_ * item->canvas->pixels_per_unit;

	if (ib->colrow_being_resized != -1) {
		int new_size;
		if (!ib->has_resize_guides) {
			ib->has_resize_guides = TRUE;
			scg_size_guide_start (ib->pane->simple.scg,
				ib->is_col_header, ib->colrow_being_resized, 1);
		}

		cri = sheet_colrow_get_info (sheet,
			ib->colrow_being_resized, is_cols);
		pos = is_cols ? x: y;
		new_size = pos - ib->resize_start_pos;
		if (is_cols && sheet->text_is_rtl)
			new_size += cri->size_pixels;

		/* Ensure we always have enough room for the margins */
		if (is_cols) {
			if (new_size <= (GNM_COL_MARGIN + GNM_COL_MARGIN)) {
				new_size = GNM_COL_MARGIN + GNM_COL_MARGIN + 1;
				pos = pane->first_offset.x +
					scg_colrow_distance_get (scg, TRUE,
						pane->first.col,
						ib->colrow_being_resized);
				pos += new_size;
			}
		} else {
			if (new_size <= (GNM_ROW_MARGIN + GNM_ROW_MARGIN)) {
				new_size = GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
				pos = pane->first_offset.y +
					scg_colrow_distance_get (scg, FALSE,
						pane->first.row,
						ib->colrow_being_resized);
				pos += new_size;
			}
		}

		ib->colrow_resize_size = new_size;
		colrow_tip_setlabel (ib, is_cols, new_size);
		scg_size_guide_motion (scg, is_cols, pos);

		/* Redraw the ItemBar to show nice incremental progress */
		goc_canvas_invalidate (canvas, 0, 0, G_MAXINT/2,  G_MAXINT/2);

	} else if (ib->start_selection != -1) {
		gnm_pane_handle_motion (ib->pane,
			canvas, x, y,
			GNM_PANE_SLIDE_AT_COLROW_BOUND |
				(is_cols ? GNM_PANE_SLIDE_X : GNM_PANE_SLIDE_Y),
			cb_extend_selection, ib);
	} else
		ib_set_cursor (ib, x, y);
	return TRUE;
}

static gboolean
item_bar_button_released (GocItem *item, int button, double x, double y)
{
	ItemBar	*ib = ITEM_BAR (item);
	gnm_simple_canvas_ungrab (item, 0);
	if (ib->colrow_being_resized >= 0) {
		if (ib->has_resize_guides)
			item_bar_resize_stop (ib, ib->colrow_resize_size);
		else
			/*
			 * No need to resize, nothing changed.
			 * This will handle the case of a double click.
			 */
			item_bar_resize_stop (ib, 0);
	}
	ib->start_selection = -1;
	return TRUE;
}

static void
item_bar_set_property (GObject *obj, guint param_id,
		       GValue const *value, GParamSpec *pspec)
{
	ItemBar *ib = ITEM_BAR (obj);

	switch (param_id){
	case ITEM_BAR_PROP_PANE:
		ib->pane = g_value_get_object (value);
		break;
	case ITEM_BAR_PROP_IS_COL_HEADER:
		ib->is_col_header = g_value_get_boolean (value);
		goc_item_bounds_changed (GOC_ITEM (obj));
		break;
	}
}

static void
item_bar_dispose (GObject *obj)
{
	ItemBar *ib = ITEM_BAR (obj);

	ib_fonts_unref (ib);

	if (ib->tip) {
		gtk_object_destroy (GTK_OBJECT (ib->tip));
		ib->tip = NULL;
	}

	if (ib->pango.glyphs != NULL) {
		pango_glyph_string_free (ib->pango.glyphs);
		ib->pango.glyphs = NULL;
	}
	if (ib->pango.item != NULL) {
		pango_item_free (ib->pango.item);
		ib->pango.item = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
item_bar_init (ItemBar *ib)
{
	ib->base.x0 = 0;
	ib->base.y0 = 0;
	ib->base.x1 = 0;
	ib->base.y1 = 0;

	ib->dragging = FALSE;
	ib->is_col_header = FALSE;
	ib->cell_width = ib->cell_height = 1;
	ib->indent = 0;
	ib->start_selection = -1;

	ib->normal_font = NULL;
	ib->bold_font = NULL;
	ib->tip = NULL;

	ib->colrow_being_resized = -1;
	ib->has_resize_guides = FALSE;
	ib->pango.item = NULL;
	ib->pango.glyphs = pango_glyph_string_new ();
}

static void
item_bar_class_init (GObjectClass  *gobject_klass)
{
	GocItemClass *item_klass = (GocItemClass *) gobject_klass;

	parent_class = g_type_class_peek_parent (gobject_klass);

	gobject_klass->dispose = item_bar_dispose;
	gobject_klass->set_property = item_bar_set_property;
	g_object_class_install_property (gobject_klass, ITEM_BAR_PROP_PANE,
		g_param_spec_object ("pane", "pane",
			"The pane containing the associated grid",
			GNM_PANE_TYPE,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));
	g_object_class_install_property (gobject_klass, ITEM_BAR_PROP_IS_COL_HEADER,
		g_param_spec_boolean ("IsColHeader", "IsColHeader",
			"Is the item-bar a header for columns or rows",
			FALSE,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));

	item_klass->realize     = item_bar_realize;
	item_klass->unrealize   = item_bar_unrealize;
	item_klass->draw_region = item_bar_draw_region;
	item_klass->update_bounds  = item_bar_update_bounds;
	item_klass->distance	= item_bar_distance;
	item_klass->button_pressed = item_bar_button_pressed;
	item_klass->button_released = item_bar_button_released;
	item_klass->button2_pressed = item_bar_2button_pressed;
	item_klass->enter_notify = item_bar_enter_notify;
	item_klass->motion = item_bar_motion;
}

GSF_CLASS (ItemBar, item_bar,
	   item_bar_class_init, item_bar_init,
	   GOC_TYPE_ITEM)
