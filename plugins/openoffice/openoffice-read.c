/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * openoffice-read.c : import open/star calc files
 *
 * Copyright (C) 2002-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2006 Luciano Miguel Wolf (luciano.wolf@indt.org.br)
 * Copyright (C) 2007 Morten Welinder (terra@gnome.org)
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

#include <gnumeric-config.h>
#include <gnumeric.h>

#include <gnm-plugin.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <ranges.h>
#include <cell.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <parse-util.h>
#include <style-color.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <style-border.h>
#include <gnm-format.h>
#include <print-info.h>
#include <command-context.h>
#include <gutils.h>
#include <xml-sax.h>
#include <sheet-object-cell-comment.h>
#include <style-conditions.h>


#include <goffice/goffice.h>

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-zip.h>
#include <gsf/gsf-opendoc-utils.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-utils.h>
#include <glib/gi18n-lib.h>

#include <sheet-object-graph.h>
#include <sheet-object-image.h>
#include <graph.h>

#include <string.h>
#include <errno.h>

GNM_PLUGIN_MODULE_HEADER;

#undef OO_DEBUG_OBJS

#define CXML2C(s) ((char const *)(s))
#define CC2XML(s) ((xmlChar const *)(s))

static inline gboolean
attr_eq (xmlChar const *a, char const *s)
{
	return !strcmp (CXML2C (a), s);
}

/* Filter Type */
typedef enum {
	OOO_VER_UNKNOWN	= -1,
	OOO_VER_1	=  0,
	OOO_VER_OPENDOC	=  1
} OOVer;
static struct {
	char const * const mime_type;
	int version;
} const OOVersions[] = {
	{ "application/vnd.sun.xml.calc",  OOO_VER_1 },
	{ "application/vnd.oasis.opendocument.spreadsheet",		OOO_VER_OPENDOC },
	{ "application/vnd.oasis.opendocument.spreadsheet-template",	OOO_VER_OPENDOC }
};

/* Formula Type */
typedef enum {
	FORMULA_OPENFORMULA = 0,
	FORMULA_OLD_OPENOFFICE,
	FORMULA_MICROSOFT,
	NUM_FORMULAE_SUPPORTED
} OOFormula;

#define OD_BORDER_THIN		1
#define OD_BORDER_MEDIUM	2.5
#define OD_BORDER_THICK		5

typedef enum {
	OO_PLOT_AREA,
	OO_PLOT_BAR,
	OO_PLOT_CIRCLE,
	OO_PLOT_LINE,
	OO_PLOT_RADAR,
	OO_PLOT_RADARAREA,
	OO_PLOT_RING,
	OO_PLOT_SCATTER,
	OO_PLOT_STOCK,
	OO_PLOT_CONTOUR,
	OO_PLOT_BUBBLE,
	OO_PLOT_GANTT,
	OO_PLOT_POLAR,
	OO_PLOT_SCATTER_COLOUR,
	OO_PLOT_XYZ_SURFACE,
	OO_PLOT_SURFACE,
	OO_PLOT_XL_SURFACE,
	OO_PLOT_UNKNOWN
} OOPlotType;

typedef enum {
	OO_STYLE_UNKNOWN,
	OO_STYLE_CELL,
	OO_STYLE_COL,
	OO_STYLE_ROW,
	OO_STYLE_SHEET,
	OO_STYLE_GRAPHICS,
	OO_STYLE_CHART,
	OO_STYLE_PARAGRAPH,
	OO_STYLE_TEXT
} OOStyleType;

typedef struct {
	GValue value;
	gchar const *name;
} OOProp;

typedef struct {
	gboolean grid;		/* graph has grid? */
	gboolean src_in_rows;	/* orientation of graph data: rows or columns */
	GSList	*axis_props;	/* axis properties */
	GSList	*plot_props;	/* plot properties */
	GSList	*other_props;	/* any other properties */
} OOChartStyle;

typedef struct {
	GogGraph	*graph;
	GogChart	*chart;
	GSList          *stock_series; /* used by Stcock plot */

	/* set in plot-area */
	GogPlot		*plot;
	Sheet		*src_sheet;
	GnmRange	 src_range;
	gboolean	 src_in_rows;
	int		 src_n_vectors;
	GnmRange	 src_abscissa;
	gboolean         src_abscissa_set;
	GnmRange	 src_label;
	gboolean         src_label_set;

	GogSeries	*series;
	unsigned	 series_count;	/* reset for each plotarea */
	unsigned	 domain_count;	/* reset for each series */
	unsigned	 data_pt_count;	/* reset for each series */

	GogObject	*axis;

	OOChartStyle		*cur_graph_style;
	GHashTable		*graph_styles;	/* contain links to OOChartStyle GSLists */
	GSList                  *these_plot_styles;  /* currently active styles */
	OOPlotType		 plot_type;
	SheetObjectAnchor	 anchor;	/* anchor to draw the frame (images or graphs) */
} OOChartInfo;

typedef enum {
	OO_PAGE_BREAK_NONE,
	OO_PAGE_BREAK_AUTO,
	OO_PAGE_BREAK_MANUAL
} OOPageBreakType;
typedef struct {
	gnm_float	 size_pts;
	int	 count;
	gboolean manual;
	OOPageBreakType break_before, break_after;
} OOColRowStyle;
typedef struct {
	GnmSheetVisibility visibility;
	gboolean is_rtl;
} OOSheetStyle;

typedef enum {
	ODF_ELAPSED_SET_SECONDS = 1 << 0,
	ODF_ELAPSED_SET_MINUTES = 1 << 1,
	ODF_ELAPSED_SET_HOURS   = 1 << 2
} odf_elapsed_set_t;

typedef struct {
	GOIOContext	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	OOVer		 ver;		/* Its an OOo v1.0 or v2.0? */
	GsfInfile	*zip;		/* Reference to the open file, to load graphs and images*/
	OOChartInfo	 chart;
	GnmParsePos	 pos;
	GnmCellPos	 extent_data;
	GnmCellPos	 extent_style;
	GnmComment      *cell_comment;

	int		 col_inc, row_inc;
	gboolean	 content_is_simple;
	gboolean	 content_is_error;

	GHashTable	*formats;

	struct {
		GHashTable	*cell;
		GHashTable	*col;
		GHashTable	*row;
		GHashTable	*sheet;
	} styles;
	struct {
		GnmStyle	*cells;
		OOColRowStyle	*col_rows;
		OOSheetStyle	*sheets;
	} cur_style;
	OOStyleType	 cur_style_type;

	gboolean	 h_align_is_valid, repeat_content;
	int              text_align, gnm_halign;

	struct {
		GnmStyle	*cells;
		OOColRowStyle	*rows;
		OOColRowStyle	*columns;
	} default_style;
	GSList		*sheet_order;
	int		 richtext_len;
	struct {
		GString	*accum;
		char	*name;
		int      magic;
		gboolean truncate_hour_on_overflow;
		int      elapsed_set; /* using a sum of odf_elapsed_set_t */
		guint      pos_seconds;
		guint      pos_minutes;
	} cur_format;
	GSList          *conditions;
	GSList          *cond_formats;
	GnmFilter	*filter;

	GnmConventions  *convs[NUM_FORMULAE_SUPPORTED];
	struct {
		GnmPageBreaks *h, *v;
	} page_breaks;

	gsf_off_t last_progress_update;
} OOParseState;

static void
maybe_update_progress (GsfXMLIn *xin)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GsfInput *input = gsf_xml_in_get_input (xin);
	gsf_off_t pos = gsf_input_tell (input);

	if (pos >= state->last_progress_update + 10000) {
		go_io_value_progress_update (state->context, pos);
		state->last_progress_update = pos;
	}
}

static GsfXMLInNode const * get_dtd (void);
static void oo_chart_style_free (OOChartStyle *pointer);

static gboolean oo_warning (GsfXMLIn *xin, char const *fmt, ...)
	G_GNUC_PRINTF (2, 3);

static gboolean
oo_warning (GsfXMLIn *xin, char const *fmt, ...)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char *msg;
	va_list args;

	va_start (args, fmt);
	msg = g_strdup_vprintf (fmt, args);
	va_end (args);

	if (IS_SHEET (state->pos.sheet)) {
		char *tmp;
		if (state->pos.eval.col >= 0 && state->pos.eval.row >= 0)
			tmp = g_strdup_printf ("%s!%s : %s",
				state->pos.sheet->name_quoted,
				cellpos_as_string (&state->pos.eval), msg);
		else
			tmp = g_strdup_printf ("%s : %s",
				state->pos.sheet->name_quoted, msg);
		g_free (msg);
		msg = tmp;
	}

	go_io_warning (state->context, "%s", msg);
	g_free (msg);

	return FALSE; /* convenience */
}

static gboolean
oo_attr_bool (GsfXMLIn *xin, xmlChar const * const *attrs,
	      int ns_id, char const *name, gboolean *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;
	*res = (g_ascii_strcasecmp (CXML2C (attrs[1]), "false") &&
		strcmp (CXML2C (attrs[1]), "0"));

	return TRUE;
}

static gboolean
oo_attr_int (GsfXMLIn *xin, xmlChar const * const *attrs,
	     int ns_id, char const *name, int *res)
{
	char *end;
	long tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	errno = 0; /* strtol sets errno, but does not clear it.  */
	tmp = strtol (CXML2C (attrs[1]), &end, 10);
	if (*end || errno != 0 || tmp < INT_MIN || tmp > INT_MAX)
		return oo_warning (xin, "Invalid integer '%s', for '%s'",
				   attrs[1], name);

	*res = tmp;
	return TRUE;
}

static gboolean
oo_attr_float (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name, gnm_float *res)
{
	char *end;
	double tmp;

	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	tmp = gnm_strto (CXML2C (attrs[1]), &end);
	if (*end)
		return oo_warning (xin, "Invalid attribute '%s', expected number, received '%s'",
				   name, attrs[1]);
	*res = tmp;
	return TRUE;
}

static GnmColor *magic_transparent;

static GnmColor *
oo_parse_color (GsfXMLIn *xin, xmlChar const *str, char const *name)
{
	guint r, g, b;

	g_return_val_if_fail (str != NULL, NULL);

	if (3 == sscanf (CXML2C (str), "#%2x%2x%2x", &r, &g, &b))
		return style_color_new_i8 (r, g, b);

	if (0 == strcmp (CXML2C (str), "transparent"))
		return style_color_ref (magic_transparent);

	oo_warning (xin, "Invalid attribute '%s', expected color, received '%s'",
		    name, str);
	return NULL;
}
static GnmColor *
oo_attr_color (GsfXMLIn *xin, xmlChar const * const *attrs,
	       int ns_id, char const *name)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return NULL;
	return oo_parse_color (xin, attrs[1], name);
}
/* returns pts */
static char const *
oo_parse_distance (GsfXMLIn *xin, xmlChar const *str,
		  char const *name, double *pts)
{
	double num;
	char *end = NULL;

	g_return_val_if_fail (str != NULL, NULL);

	if (0 == strncmp (CXML2C (str), "none", 4)) {
		*pts = 0;
		return CXML2C (str) + 4;
	}

	num = go_strtod (CXML2C (str), &end);
	if (CXML2C (str) != end) {
		if (0 == strncmp (end, "mm", 2)) {
			num = GO_CM_TO_PT (num/10.);
			end += 2;
		} else if (0 == strncmp (end, "m", 1)) {
			num = GO_CM_TO_PT (num*100.);
			end ++;
		} else if (0 == strncmp (end, "km", 2)) {
			num = GO_CM_TO_PT (num*100000.);
			end += 2;
		} else if (0 == strncmp (end, "cm", 2)) {
			num = GO_CM_TO_PT (num);
			end += 2;
		} else if (0 == strncmp (end, "pt", 2)) {
			end += 2;
		} else if (0 == strncmp (end, "pc", 2)) { /* pica 12pt == 1 pica */
			num /= 12.;
			end += 2;
		} else if (0 == strncmp (end, "ft", 2)) {
			num = GO_IN_TO_PT (num*12.);
			end += 2;
		} else if (0 == strncmp (end, "mi", 2)) {
			num = GO_IN_TO_PT (num*63360.);
			end += 2;
		} else if (0 == strncmp (end, "inch", 4)) {
			num = GO_IN_TO_PT (num);
			end += 4;
		} else if (0 == strncmp (end, "in", 2)) {
			num = GO_IN_TO_PT (num);
			end += 2;
		} else {
			oo_warning (xin, "Invalid attribute '%s', unknown unit '%s'",
				    name, str);
			return NULL;
		}
	} else {
		oo_warning (xin, "Invalid attribute '%s', expected distance, received '%s'",
			    name, str);
		return NULL;
	}

	*pts = num;
	return end;
}

/* returns pts */
static char const *
oo_attr_distance (GsfXMLIn *xin, xmlChar const * const *attrs,
		  int ns_id, char const *name, double *pts)
{
	g_return_val_if_fail (attrs != NULL, NULL);
	g_return_val_if_fail (attrs[0] != NULL, NULL);
	g_return_val_if_fail (attrs[1] != NULL, NULL);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return NULL;
	return oo_parse_distance (xin, attrs[1], name, pts);
}

typedef struct {
	char const * const name;
	int val;
} OOEnum;

static gboolean
oo_attr_enum (GsfXMLIn *xin, xmlChar const * const *attrs,
	      int ns_id, char const *name, OOEnum const *enums, int *res)
{
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (!gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), ns_id, name))
		return FALSE;

	for (; enums->name != NULL ; enums++)
		if (!strcmp (enums->name, CXML2C (attrs[1]))) {
			*res = enums->val;
			return TRUE;
		}
	return oo_warning (xin, "Invalid attribute '%s', unknown enum value '%s'",
			   name, attrs[1]);
}

static char const *
oo_cellref_parse (GnmCellRef *ref, char const *start, GnmParsePos const *pp)
{
	char const *tmp1, *tmp2, *ptr = start;
	GnmSheetSize const *ss;

	if (*ptr != '.') {
		char *name, *accum;

		/* ignore abs vs rel for sheets */
		if (*ptr == '$')
			ptr++;

		/* From the spec :
		 *	SheetName   ::= [^\. ']+ | "'" ([^'] | "''")+ "'" */
		if ('\'' == *ptr) {
			tmp1 = ++ptr;
two_quotes :
			/* missing close paren */
			if (NULL == (tmp1 = strchr (tmp1, '\'')))
				return start;

			/* two in a row is the escape for a single */
			if (tmp1[1] == '\'') {
				tmp1 += 2;
				goto two_quotes;
			}

			/* If a name is quoted the entire named must be quoted */
			if (tmp1[1] != '.')
				return start;

			accum = name = g_alloca (tmp1-ptr+1);
			while (ptr != tmp1)
				if ('\'' == (*accum++ = *ptr++))
					ptr++;
			*accum = '\0';
			ptr += 2;
		} else {
			if (NULL == (tmp1 = strchr (ptr, '.')))
				return start;
			name = g_alloca (tmp1-ptr+1);
			strncpy (name, ptr, tmp1-ptr);
			name[tmp1-ptr] = '\0';
			ptr = tmp1 + 1;
		}

		if (name[0] == 0)
			return start;

		/* OpenCalc does not pre-declare its sheets, but it does have a
		 * nice unambiguous format.  So if we find a name that has not
		 * been added yet add it.  Reorder below. */
		ref->sheet = workbook_sheet_by_name (pp->wb, name);
		if (ref->sheet == NULL) {
			if (strcmp (name, "#REF!") == 0) {
				g_warning ("Ignoring reference to sheet %s", name);
				ref->sheet = NULL;
			} else {
				Sheet *old_sheet = workbook_sheet_by_index (pp->wb, 0);
				ref->sheet = sheet_new (pp->wb, name,
							gnm_sheet_get_max_cols (old_sheet),
							gnm_sheet_get_max_rows (old_sheet));
				workbook_sheet_attach (pp->wb, ref->sheet);
			}
		}
	} else {
		ptr++; /* local ref */
		ref->sheet = NULL;
	}

	ss = gnm_sheet_get_size (eval_sheet (ref->sheet, pp->sheet));

	tmp1 = col_parse (ptr, ss, &ref->col, &ref->col_relative);
	if (!tmp1)
		return start;
	tmp2 = row_parse (tmp1, ss, &ref->row, &ref->row_relative);
	if (!tmp2)
		return start;

	if (ref->col_relative)
		ref->col -= pp->eval.col;
	if (ref->row_relative)
		ref->row -= pp->eval.row;
	return tmp2;
}

static char const *
oo_rangeref_parse (GnmRangeRef *ref, char const *start, GnmParsePos const *pp)
{
	char const *ptr = oo_cellref_parse (&ref->a, start, pp);
	if (*ptr == ':')
		ptr = oo_cellref_parse (&ref->b, ptr+1, pp);
	else
		ref->b = ref->a;
	return ptr;
}

static char const *
oo_expr_rangeref_parse (GnmRangeRef *ref, char const *start, GnmParsePos const *pp,
			G_GNUC_UNUSED GnmConventions const *convs)
{
	char const *ptr;
	if (*start == '[') {
		ptr = oo_rangeref_parse (ref, start+1, pp);
		if (*ptr == ']')
			return ptr + 1;
	}
	return start;
}

static GnmExpr const *
oo_func_map_in (GnmConventions const *convs, Workbook *scope,
		char const *name, GnmExprList *args);

static GnmConventions *
oo_conventions_new (void)
{
	GnmConventions *conv = gnm_conventions_new ();

	conv->decode_ampersands	= TRUE;
	conv->exp_is_left_associative = TRUE;

	conv->intersection_char	= '!';
	conv->decimal_sep_dot	= TRUE;
	conv->range_sep_colon	= TRUE;
	conv->arg_sep		= ';';
	conv->array_col_sep	= ';';
	conv->array_row_sep	= '|';
	conv->input.func	= oo_func_map_in;
	conv->input.range_ref	= oo_expr_rangeref_parse;
	conv->sheet_name_sep	= '.';

	return conv;
}

static void
oo_load_convention (OOParseState *state, OOFormula type)
{
	GnmConventions *convs;

	g_return_if_fail (state->convs[type] == NULL);

	switch (type) {
	case FORMULA_MICROSOFT:
		convs = gnm_xml_io_conventions ();
		convs->exp_is_left_associative = TRUE;
		break;
	case FORMULA_OLD_OPENOFFICE:
		convs = oo_conventions_new ();
		convs->sheet_name_sep	= '!'; /* Note that we are using this also as a marker*/
		                               /* in the function handlers */
		break;
	case FORMULA_OPENFORMULA:
	default:
		convs = oo_conventions_new ();
		break;
	}

	state->convs[type] = convs;
}

static GnmExprTop const *
oo_expr_parse_str (GsfXMLIn *xin, char const *str,
		   GnmParsePos const *pp, GnmExprParseFlags flags,
		   OOFormula type)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmExprTop const *texpr;
	GnmParseError  perr;

	if (state->convs[type] == NULL)
		oo_load_convention (state, type);
	parse_error_init (&perr);
	texpr = gnm_expr_parse_str (str, pp, flags,
				    state->convs[type], &perr);
	if (texpr == NULL) {
		oo_warning (xin, _("Unable to parse\n\t'%s'\nbecause '%s'"),
			    str, perr.err->message);
		parse_error_free (&perr);
	}
	return texpr;
}

/****************************************************************************/

static void
oo_date_convention (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:null-date table:date-value="1904-01-01"/> */
	OOParseState *state = (OOParseState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "date-value")) {
			if (!strncmp (CXML2C (attrs[1]), "1904", 4))
				workbook_set_1904 (state->pos.wb, TRUE);
		}
}
static void
oo_iteration (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:iteration table:status="enable"/> */
	OOParseState *state = (OOParseState *)xin->user_state;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "status"))
			workbook_iteration_enabled (state->pos.wb,
				strcmp (CXML2C (attrs[1]), "enable") == 0);
}

static void
oo_table_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	/* <table:table table:name="Result" table:style-name="ta1"> */
	OOParseState *state = (OOParseState *)xin->user_state;

	state->pos.eval.col = 0;
	state->pos.eval.row = 0;
	state->extent_data.col = state->extent_style.col = 0;
	state->extent_data.row = state->extent_style.row = 0;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "name")) {
			char const *name = CXML2C (attrs[1]);
			state->pos.sheet = workbook_sheet_by_name (state->pos.wb, name);
			if (NULL == state->pos.sheet) {
				state->pos.sheet = sheet_new (state->pos.wb, name, 256, 65536);
				workbook_sheet_attach (state->pos.wb, state->pos.sheet);
			}

			/* Store sheets in correct order in case we implicitly
			 * created one out of order */
			state->sheet_order = g_slist_prepend (
				state->sheet_order, state->pos.sheet);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))  {
			OOSheetStyle const *style = g_hash_table_lookup (state->styles.sheet, attrs[1]);
			if (style)
				g_object_set (state->pos.sheet,
					      "visibility", style->visibility,
					      "text-is-rtl", style->is_rtl,
					      NULL);
		}
	if (state->default_style.rows != NULL)
		sheet_row_set_default_size_pts (state->pos.sheet,
							state->default_style.rows->size_pts);
	if (state->default_style.columns != NULL)
		sheet_col_set_default_size_pts (state->pos.sheet,
						state->default_style.columns->size_pts);
}

static void
cb_find_default_colrow_style (gpointer *key, OOColRowStyle *val,
			   OOColRowStyle **cri)
{
	if (*cri == NULL || ((*cri)->count < val->count))
		*cri = val;
}

/* ODF defines default styles for cols/rows but many applications do not set it
 * and frequently ending up specifying a row style for real_extent..MAX
 * in order to make the styles work as above.  To avoid the miserable
 * performance of pretending to have 64k rows, we now need to go back and reset
 * the 'default'ness of any othewise empty rows, and assign the most common row
 * format as the default unless a default style is already set. */
static void
oo_col_reset_defaults (OOParseState *state)
{
	OOColRowStyle *cri = NULL;

	if (state->default_style.columns == NULL) {
		g_hash_table_foreach (state->styles.col,
				      (GHFunc)cb_find_default_colrow_style, &cri);
		if (NULL != cri && cri->size_pts > 0.)
			sheet_col_set_default_size_pts (state->pos.sheet,
							cri->size_pts);
	}
	colrow_reset_defaults (state->pos.sheet, TRUE,
			       state->extent_data.col);
}

static void
oo_row_reset_defaults (OOParseState *state)
{
	OOColRowStyle *cri = NULL;

	if (state->default_style.rows == NULL) {
		g_hash_table_foreach (state->styles.row,
				      (GHFunc)cb_find_default_colrow_style, &cri);
		if (NULL != cri && cri->size_pts > 0.)
			sheet_row_set_default_size_pts (state->pos.sheet,
							cri->size_pts);
	}
	colrow_reset_defaults (state->pos.sheet, FALSE,
			       state->extent_data.row);
}

static void
oo_table_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmRange r;
	int rows, cols;
	int max_cols, max_rows;

	maybe_update_progress (xin);

	if (NULL != state->page_breaks.h) {
		print_info_set_breaks (state->pos.sheet->print_info,
			state->page_breaks.h);
		state->page_breaks.h = NULL;
	}
	if (NULL != state->page_breaks.v) {
		print_info_set_breaks (state->pos.sheet->print_info,
			state->page_breaks.v);
		state->page_breaks.v = NULL;
	}

	max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
	max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

	/* default cell styles are applied only to cells that are specified
	 * which is a performance nightmare.  Instead we apply the styles to
	 * the entire column or row and clear the area beyond the extent here. */

	rows = state->extent_style.row;
	if (state->extent_data.row > rows)
		rows = state->extent_data.row;
	cols = state->extent_style.col;
	if (state->extent_data.col > cols)
		cols = state->extent_data.col;
	cols++; rows++;
	if (cols < max_cols) {
		range_init (&r, cols, 0,
			    max_cols - 1, max_rows - 1);
		sheet_style_set_range (state->pos.sheet, &r,
				       sheet_style_default (state->pos.sheet));
	}
	if (rows < max_rows) {
		range_init (&r, 0, rows,
			    max_cols - 1, max_rows - 1);
		sheet_style_set_range (state->pos.sheet, &r,
				       sheet_style_default (state->pos.sheet));
	}

	oo_col_reset_defaults (state);
	oo_row_reset_defaults (state);

	state->pos.eval.col = state->pos.eval.row = 0;
}

static void
oo_append_page_break (OOParseState *state, int pos, gboolean is_vert, gboolean is_manual)
{
	GnmPageBreaks *breaks;

	if (is_vert) {
		if (NULL == (breaks = state->page_breaks.v))
			breaks = state->page_breaks.v = gnm_page_breaks_new (TRUE);
	} else {
		if (NULL == (breaks = state->page_breaks.h))
			breaks = state->page_breaks.h = gnm_page_breaks_new (FALSE);
	}

	gnm_page_breaks_append_break (breaks, pos,
				      is_manual ? GNM_PAGE_BREAK_MANUAL : GNM_PAGE_BREAK_NONE);
}

static void
oo_set_page_break (OOParseState *state, int pos, gboolean is_vert, gboolean is_manual)
{
	GnmPageBreaks *breaks = (is_vert) ? state->page_breaks.v : state->page_breaks.h;

	switch (gnm_page_breaks_get_break (breaks, pos)) {
	case GNM_PAGE_BREAK_NONE:
		oo_append_page_break (state, pos, is_vert, is_manual);
		return;
	case GNM_PAGE_BREAK_MANUAL:
		return;
	case GNM_PAGE_BREAK_AUTO:
	default:
		if (is_manual)
			gnm_page_breaks_set_break (breaks, pos, GNM_PAGE_BREAK_MANUAL);
		break;
	}
}

static void
oo_col_row_style_apply_breaks (OOParseState *state, OOColRowStyle *cr_style,
			       int pos, gboolean is_vert)
{

	if (cr_style->break_before != OO_PAGE_BREAK_NONE)
		oo_set_page_break (state, pos, is_vert,
				      cr_style->break_before == OO_PAGE_BREAK_MANUAL);
	if (cr_style->break_after  != OO_PAGE_BREAK_NONE)
		oo_append_page_break (state, pos+1, is_vert,
				      cr_style->break_after  == OO_PAGE_BREAK_MANUAL);
}

static void
oo_update_data_extent (OOParseState *state, int cols, int rows)
{
	if (state->extent_data.col < (state->pos.eval.col + cols - 1))
		state->extent_data.col = state->pos.eval.col + cols - 1;
	if (state->extent_data.row < (state->pos.eval.row + rows - 1))
		state->extent_data.row = state->pos.eval.row + rows - 1;
}
static void
oo_update_style_extent (OOParseState *state, int cols, int rows)
{
	if (cols > 0 && state->extent_style.col < (state->pos.eval.col + cols - 1))
		state->extent_style.col = state->pos.eval.col + cols - 1;
	if (rows > 0 && state->extent_style.row < (state->pos.eval.row + rows - 1))
		state->extent_style.row = state->pos.eval.row + rows - 1;
}

static int
oo_extent_sheet_cols (Sheet *sheet, int cols)
{
	GOUndo   * goundo;
	int new_cols, new_rows;
	gboolean err;

	new_cols = cols;
	new_rows = gnm_sheet_get_max_rows (sheet);
	gnm_sheet_suggest_size (&new_cols, &new_rows);

	goundo = gnm_sheet_resize (sheet, new_cols, new_rows, NULL, &err);
	g_object_unref (G_OBJECT (goundo));

	return gnm_sheet_get_max_cols (sheet);
}


static void
oo_col_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOColRowStyle *col_info = NULL;
	GnmStyle *style = NULL;
	int	  i, repeat_count = 1;
	gboolean  hidden = FALSE;
	int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);

	maybe_update_progress (xin);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "default-cell-style-name"))
			style = g_hash_table_lookup (state->styles.cell, attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			col_info = g_hash_table_lookup (state->styles.col, attrs[1]);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &repeat_count))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "visibility"))
			hidden = !attr_eq (attrs[1], "visible");

	if (state->pos.eval.col + repeat_count > max_cols) {
		max_cols = oo_extent_sheet_cols (state->pos.sheet, state->pos.eval.col
						 + repeat_count);
		if (state->pos.eval.col + repeat_count > max_cols) {
			g_warning ("Ignoring column information beyond"
				   " the range we can handle.");
			repeat_count = max_cols - state->pos.eval.col - 1;
		}
	}

	if (hidden)
		colrow_set_visibility (state->pos.sheet, TRUE, FALSE, state->pos.eval.col,
			state->pos.eval.col + repeat_count - 1);

	/* see oo_table_end for details */
	if (NULL != style) {
		GnmRange r;
		r.start.col = state->pos.eval.col;
		r.end.col   = state->pos.eval.col + repeat_count - 1;
		r.start.row = 0;
		r.end.row  = gnm_sheet_get_last_row (state->pos.sheet);
		gnm_style_ref (style);
		sheet_style_set_range (state->pos.sheet, &r, style);
		oo_update_style_extent (state, repeat_count, -1);
	}
	if (col_info != NULL) {
		if (state->default_style.columns == NULL && repeat_count > max_cols/2) {
			int const last = state->pos.eval.col + repeat_count;
			state->default_style.columns = g_memdup (col_info, sizeof (*col_info));
			state->default_style.columns->count = repeat_count;
			sheet_col_set_default_size_pts (state->pos.sheet,
							state->default_style.columns->size_pts);
			if (col_info->break_before != OO_PAGE_BREAK_NONE)
				for (i = state->pos.eval.row ; i < last; i++ )
					oo_set_page_break (state, i, TRUE,
							   col_info->break_before
							   == OO_PAGE_BREAK_MANUAL);
			if (col_info->break_after!= OO_PAGE_BREAK_NONE)
				for (i = state->pos.eval.col ; i < last; i++ )
					oo_append_page_break (state, i+1, FALSE,
							      col_info->break_after
							      == OO_PAGE_BREAK_MANUAL);
		} else {
			int last = state->pos.eval.col + repeat_count;
			for (i = state->pos.eval.col ; i < last; i++ ) {
				/* I can not find a listing for the default but will
				 * assume it is TRUE to keep the files rational */
				if (col_info->size_pts > 0.)
					sheet_col_set_size_pts (state->pos.sheet, i,
								col_info->size_pts, col_info->manual);
				oo_col_row_style_apply_breaks (state, col_info, i, TRUE);
			}
			col_info->count += repeat_count;
		}
	}

	state->pos.eval.col += repeat_count;
}

static int
oo_extent_sheet_rows (Sheet *sheet, int rows)
{
	GOUndo   * goundo;
	int new_cols, new_rows;
	gboolean err;

	new_cols = gnm_sheet_get_max_cols (sheet);
	new_rows = rows;
	gnm_sheet_suggest_size (&new_cols, &new_rows);

	goundo = gnm_sheet_resize (sheet, new_cols, new_rows, NULL, &err);
	g_object_unref (G_OBJECT (goundo));

	return gnm_sheet_get_max_rows (sheet);
}

static void
oo_row_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOColRowStyle *row_info = NULL;
	GnmStyle *style = NULL;
	int	  i, repeat_count = 1;
	gboolean  hidden = FALSE;
	int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

	maybe_update_progress (xin);

	state->pos.eval.col = 0;

	if (state->pos.eval.row >= max_rows) {
		max_rows = oo_extent_sheet_rows (state->pos.sheet, state->pos.eval.row + 1);
		if (state->pos.eval.row >= max_rows) {
			oo_warning (xin, _("Content past the maximum number of rows (%i) supported."), max_rows);
			state->row_inc = 0;
			return;
		}
	}

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			row_info = g_hash_table_lookup (state->styles.row, attrs[1]);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-rows-repeated", &repeat_count))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "visibility"))
			hidden = !attr_eq (attrs[1], "visible");
	}

	if (state->pos.eval.row + repeat_count > max_rows) {
		max_rows = oo_extent_sheet_rows
			(state->pos.sheet,
			 state->pos.eval.row + repeat_count);
		if (state->pos.eval.row + repeat_count >= max_rows)
        	/* There are probably lots of empty lines at the end. */
			repeat_count = max_rows - state->pos.eval.row - 1;
	}

	if (hidden)
		colrow_set_visibility (state->pos.sheet, FALSE, FALSE, state->pos.eval.row,
			state->pos.eval.row+repeat_count - 1);

	/* see oo_table_end for details */
	if (NULL != style) {
		GnmRange r;
		r.start.row = state->pos.eval.row;
		r.end.row   = state->pos.eval.row + repeat_count - 1;
		r.start.col = 0;
		r.end.col  = gnm_sheet_get_last_col (state->pos.sheet);
		gnm_style_ref (style);
		sheet_style_set_range (state->pos.sheet, &r, style);
		oo_update_style_extent (state, -1, repeat_count);
	}

	if (row_info != NULL) {
		if (state->default_style.rows == NULL && repeat_count > max_rows/2) {
			int const last = state->pos.eval.row + repeat_count;
			state->default_style.rows = g_memdup (row_info, sizeof (*row_info));
			state->default_style.rows->count = repeat_count;
			sheet_row_set_default_size_pts (state->pos.sheet,
							state->default_style.rows->size_pts);
			if (row_info->break_before != OO_PAGE_BREAK_NONE)
				for (i = state->pos.eval.row ; i < last; i++ )
					oo_set_page_break (state, i, FALSE,
							   row_info->break_before
							   == OO_PAGE_BREAK_MANUAL);
			if (row_info->break_after!= OO_PAGE_BREAK_NONE)
				for (i = state->pos.eval.row ; i < last; i++ )
					oo_append_page_break (state, i+1, FALSE,
							      row_info->break_after
							      == OO_PAGE_BREAK_MANUAL);
		} else {
			int const last = state->pos.eval.row + repeat_count;
			for (i = state->pos.eval.row ; i < last; i++ ) {
				if (row_info->size_pts > 0.)
					sheet_row_set_size_pts (state->pos.sheet, i,
								row_info->size_pts, row_info->manual);
				oo_col_row_style_apply_breaks (state, row_info, i, FALSE);
			}
			row_info->count += repeat_count;
		}
	}

	state->row_inc = repeat_count;
}

static void
oo_row_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.row += state->row_inc;
}


static void
oo_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmExprTop const *texpr = NULL;
	GnmValue	*val = NULL;
	gboolean  has_date = FALSE, has_datetime = FALSE, has_time = FALSE;
	gboolean	 bool_val;
	gnm_float	 float_val = 0;
	int array_cols = -1, array_rows = -1;
	int merge_cols = 1, merge_rows = 1;
	GnmStyle *style = NULL;
	char const *expr_string;
	GnmRange tmp;
	int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
	int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);

	maybe_update_progress (xin);

	state->col_inc = 1;
	state->content_is_error = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "formula")) {
			OOFormula f_type = FORMULA_OPENFORMULA;

			if (attrs[1] == NULL) {
				oo_warning (xin, _("Missing expression"));
				continue;
			}

			expr_string = CXML2C (attrs[1]);
			if (state->ver == OOO_VER_OPENDOC) {
				if (strncmp (expr_string, "msoxl:", 6) == 0) {
					expr_string += 6;
					f_type = FORMULA_MICROSOFT;
				} else if (strncmp (expr_string, "oooc:", 5) == 0) {
					expr_string += 5;
					f_type = FORMULA_OLD_OPENOFFICE;
				} else if (strncmp (expr_string, "of:", 3) == 0)
					expr_string += 3;
				else if (strncmp (expr_string, "=", 1) == 0)
					/* They really should include a namespace */
					/* We assume that it is an OpenFormula expression */
					expr_string += 0;
				else {
					oo_warning (xin, _("Missing or unknown expression namespace: %s"), expr_string);
					continue;
				}
			} else if (state->ver == OOO_VER_1)
				f_type = FORMULA_OLD_OPENOFFICE;

			expr_string = gnm_expr_char_start_p (expr_string);
			if (expr_string == NULL)
				oo_warning (xin, _("Expression '%s' does not start with a recognized character"), attrs[1]);
			else if (*expr_string == '\0')
				/* Ick.  They seem to store error cells as
				 * having value date with expr : '=' and the
				 * message in the content.
				 */
				state->content_is_error = TRUE;
			else
				texpr = oo_expr_parse_str (xin, expr_string,
							   &state->pos, GNM_EXPR_PARSE_DEFAULT, f_type);
		} else if (oo_attr_bool (xin, attrs,
					 (state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
					 "boolean-value", &bool_val))
			val = value_new_bool (bool_val);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
			(state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
			"date-value")) {
			unsigned y, m, d, h, mi;
			gnm_float s;
			unsigned n = sscanf (CXML2C (attrs[1]), "%u-%u-%uT%u:%u:%" GNM_SCANF_g,
					     &y, &m, &d, &h, &mi, &s);

			if (n >= 3) {
				GDate date;
				g_date_set_dmy (&date, d, m, y);
				if (g_date_valid (&date)) {
					unsigned d_serial = go_date_g_to_serial (&date,
						workbook_date_conv (state->pos.wb));
					if (n >= 6) {
						double time_frac = h + ((double)mi / 60.) + ((double)s / 3600.);
						val = value_new_float (d_serial + time_frac / 24.);
						has_datetime = TRUE;
					} else {
						val = value_new_int (d_serial);
						has_date = TRUE;
					}
				}
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					       (state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
					       "time-value")) {
			unsigned h, m, s;
			if (3 == sscanf (CXML2C (attrs[1]), "PT%uH%uM%uS", &h, &m, &s)) {
				unsigned secs = h * 3600 + m * 60 + s;
				val = value_new_float (secs / (gnm_float)86400);
				has_time = TRUE;
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "string-value"))
			val = value_new_string (CXML2C (attrs[1]));
		else if (oo_attr_float (xin, attrs,
			(state->ver == OOO_VER_OPENDOC) ? OO_NS_OFFICE : OO_NS_TABLE,
			"value", &float_val))
			val = value_new_float (float_val);
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-matrix-columns-spanned", &array_cols))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-matrix-rows-spanned", &array_rows))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-spanned", &merge_cols))
			;
		else if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-rows-spanned", &merge_rows))
			;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "style-name")) {
			style = g_hash_table_lookup (state->styles.cell, attrs[1]);
			gnm_style_ref (style);
		}
	}

	if (state->pos.eval.col >= max_cols ||
	    state->pos.eval.row >= max_rows) {
		if (texpr)
			gnm_expr_top_unref (texpr);
		value_release (val);
		if (style)
			gnm_style_unref (style);
		return;
	}

	merge_cols = MIN (merge_cols, max_cols - state->pos.eval.col);
	merge_rows = MIN (merge_rows, max_rows - state->pos.eval.row);

	if ((state->ver == OOO_VER_1) && (has_datetime || has_date || has_time)) {
		GOFormat *format;

		if (has_datetime) {
			format = go_format_default_date_time ();
		} else if (has_date) {
			format = go_format_default_date ();
		} else {
			format = go_format_default_time ();
		}

		if (style == NULL) {
			style = gnm_style_new_default ();
/* 			gnm_style_ref(style); */
			gnm_style_set_format (style, format);
		} else if (!gnm_style_is_element_set (style, MSTYLE_FORMAT))
			gnm_style_set_format (style, format);
	}

	if (style != NULL) {
		if (state->col_inc > 1 || state->row_inc > 1) {
			range_init_cellpos_size (&tmp, &state->pos.eval,
				state->col_inc, state->row_inc);
			sheet_style_set_range (state->pos.sheet, &tmp, style);
			oo_update_style_extent (state, state->col_inc, state->row_inc);
		} else if (merge_cols > 1 || merge_rows > 1) {
			range_init_cellpos_size (&tmp, &state->pos.eval,
						 merge_cols, merge_rows);
			sheet_style_set_range (state->pos.sheet, &tmp, style);
			oo_update_style_extent (state, merge_cols, merge_rows);
		} else {
			sheet_style_set_pos (state->pos.sheet,
				state->pos.eval.col, state->pos.eval.row,
				style);
			oo_update_style_extent (state, 1, 1);
		}
	}

	state->content_is_simple = FALSE;
	if (texpr != NULL) {
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
						  state->pos.eval.col,
						  state->pos.eval.row);

		if (array_cols > 0 || array_rows > 0) {
			if (array_cols <= 0) {
				array_cols = 1;
				oo_warning (xin, _("Invalid array expression does not specify number of columns."));
			} else if (array_rows <= 0) {
				array_rows = 1;
				oo_warning (xin, _("Invalid array expression does not specify number of rows."));
			}
			gnm_cell_set_array_formula (state->pos.sheet,
				state->pos.eval.col, state->pos.eval.row,
				state->pos.eval.col + array_cols-1,
				state->pos.eval.row + array_rows-1,
				texpr);
			if (val != NULL)
				gnm_cell_assign_value (cell, val);
			oo_update_data_extent (state, array_cols, array_rows);
		} else {
			if (val != NULL)
				gnm_cell_set_expr_and_value (cell, texpr, val,
							     TRUE);
			else
				gnm_cell_set_expr (cell, texpr);
			gnm_expr_top_unref (texpr);
			oo_update_data_extent (state, 1, 1);
		}
	} else if (val != NULL) {
		GnmCell *cell = sheet_cell_fetch (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		/* has cell previously been initialized as part of an array */
		if (gnm_cell_is_nonsingleton_array (cell))
			gnm_cell_assign_value (cell, val);
		else
			gnm_cell_set_value (cell, val);
		oo_update_data_extent (state, 1, 1);
	} else if (!state->content_is_error)
		/* store the content as a string */
		state->content_is_simple = TRUE;

	if (merge_cols > 1 || merge_rows > 1) {
		range_init_cellpos_size (&tmp, &state->pos.eval,
					 merge_cols, merge_rows);
		gnm_sheet_merge_add (state->pos.sheet, &tmp, FALSE, NULL);
	}
}

static void
oo_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->col_inc > 1 || state->row_inc > 1) {
		GnmCell *cell = sheet_cell_get (state->pos.sheet,
			state->pos.eval.col, state->pos.eval.row);

		if (!gnm_cell_is_empty (cell)) {
			int i, j;
			GnmCell *next;
			for (j = 0; j < state->row_inc ; j++)
				for (i = 0; i < state->col_inc ; i++)
					if (j > 0 || i > 0) {
						next = sheet_cell_fetch (state->pos.sheet,
							state->pos.eval.col + i, state->pos.eval.row + j);
						gnm_cell_set_value (next, value_dup (cell->value));
					}
			oo_update_data_extent (state, state->col_inc, state->row_inc);
		}
	}
	state->pos.eval.col += state->col_inc;
}

static void
oo_cell_content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->content_is_simple || state->content_is_error) {
		int max_cols = gnm_sheet_get_max_cols (state->pos.sheet);
		int max_rows = gnm_sheet_get_max_rows (state->pos.sheet);
		GnmValue *v;
		GnmCell *cell;

		if (state->pos.eval.col >= max_cols ||
		    state->pos.eval.row >= max_rows)
			return;

		cell = sheet_cell_fetch (state->pos.sheet,
					 state->pos.eval.col,
					 state->pos.eval.row);

		if (state->content_is_simple)
			/* embedded newlines stored as a series of <p> */
			if (VALUE_IS_STRING (cell->value))
				v = value_new_string_str (go_string_new_nocopy (
					g_strconcat (cell->value->v_str.val->str, "\n",
						     xin->content->str, NULL)));
			else
				v = value_new_string (xin->content->str);
		else
			v = value_new_error (NULL, xin->content->str);

		/* Note that we could be looking at the result of an array calculation */
		gnm_cell_assign_value (cell, v);
		oo_update_data_extent (state, 1, 1);
	}
}

static void
oo_covered_cell_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->col_inc = 1;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int (xin, attrs, OO_NS_TABLE, "number-columns-repeated", &state->col_inc))
			;
#if 0
		/* why bother it is covered ? */
		else if (!strcmp (CXML2C (attrs[0]), OO_NS_TABLE, "style-name"))
			style = g_hash_table_lookup (state->styles.cell, attrs[1]);

	if (style != NULL) {
		gnm_style_ref (style);
		sheet_style_set_pos (state->pos.sheet,
		     state->pos.eval.col, state->pos.eval.row,
		     style);
	}
#endif
}

static void
oo_covered_cell_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	state->pos.eval.col += state->col_inc;
}

static void
oo_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const style_types [] = {
		{ "table-cell",	  OO_STYLE_CELL },
		{ "table-row",	  OO_STYLE_ROW },
		{ "table-column", OO_STYLE_COL },
		{ "table",	  OO_STYLE_SHEET },
		{ "graphics",	  OO_STYLE_GRAPHICS },
		{ "paragraph",	  OO_STYLE_PARAGRAPH },
		{ "text",	  OO_STYLE_TEXT },
		{ "chart",	  OO_STYLE_CHART },
		{ "graphic",	  OO_STYLE_GRAPHICS },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;
	char const *parent_name = NULL;
	GnmStyle *style;
	GOFormat *fmt = NULL;
	int tmp;
	OOChartStyle *cur_style;

	g_return_if_fail (state->cur_style_type == OO_STYLE_UNKNOWN);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "family", style_types, &tmp))
			state->cur_style_type = tmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "parent-style-name"))
			parent_name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "data-style-name")) {
			GOFormat *tmp = g_hash_table_lookup (state->formats, attrs[1]);
			if (tmp != NULL)
				fmt = tmp;
		}

	switch (state->cur_style_type) {
	case OO_STYLE_CELL:
		style = (parent_name != NULL)
			? g_hash_table_lookup (state->styles.cell, parent_name)
			: NULL;
		state->cur_style.cells = (style != NULL)
			? gnm_style_dup (style) : gnm_style_new_default ();
		state->h_align_is_valid = state->repeat_content = FALSE;
		state->text_align = -2;
		state->gnm_halign = -2;

		if (fmt != NULL)
			gnm_style_set_format (state->cur_style.cells, fmt);

		if (name != NULL)
			g_hash_table_replace (state->styles.cell,
				g_strdup (name), state->cur_style.cells);
		else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			 if (state->default_style.cells)
				 gnm_style_unref (state->default_style.cells);
			 state->default_style.cells = state->cur_style.cells;
		}
		break;

	case OO_STYLE_COL:
		state->cur_style.col_rows = g_new0 (OOColRowStyle, 1);
		state->cur_style.col_rows->size_pts = -1.;
		if (name)
			g_hash_table_replace (state->styles.col,
				g_strdup (name), state->cur_style.col_rows);
		else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			if (state->default_style.columns) {
				oo_warning (xin, _("Duplicate default column style encountered."));
				g_free (state->default_style.columns);
			}
			state->default_style.columns = state->cur_style.col_rows;
		}
		break;

	case OO_STYLE_ROW:
		state->cur_style.col_rows = g_new0 (OOColRowStyle, 1);
		state->cur_style.col_rows->size_pts = -1.;
		if (name)
			g_hash_table_replace (state->styles.row,
				g_strdup (name), state->cur_style.col_rows);
		else if (0 == strcmp (xin->node->id, "DEFAULT_STYLE")) {
			if (state->default_style.rows) {
				oo_warning (xin, _("Duplicate default row style encountered."));
				g_free (state->default_style.rows);
			}
			state->default_style.rows = state->cur_style.col_rows;
		}
		break;

	case OO_STYLE_SHEET:
		state->cur_style.sheets = g_new0 (OOSheetStyle, 1);
		if (name)
			g_hash_table_replace (state->styles.sheet,
				g_strdup (name), state->cur_style.sheets);
		break;

	case OO_STYLE_CHART:
		state->chart.plot_type = OO_PLOT_UNKNOWN;
		if (name != NULL){
			cur_style = g_new0(OOChartStyle, 1);
			cur_style->axis_props = NULL;
			cur_style->plot_props = NULL;
			cur_style->other_props = NULL;
			state->chart.cur_graph_style = cur_style;
			g_hash_table_replace (state->chart.graph_styles,
					      g_strdup (name),
					      state->chart.cur_graph_style);
		} else {
			state->chart.cur_graph_style = NULL;
		}
		break;
	default:
		break;
	}
}

static void
oo_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	switch (state->cur_style_type) {
	case OO_STYLE_CELL : state->cur_style.cells = NULL;
		break;
	case OO_STYLE_COL :
	case OO_STYLE_ROW : state->cur_style.col_rows = NULL;
		break;
	case OO_STYLE_SHEET : state->cur_style.sheets = NULL;
		break;
	case OO_STYLE_CHART : state->chart.cur_graph_style = NULL;
		break;

	default :
		break;
	}
	state->cur_style_type = OO_STYLE_UNKNOWN;
}

static void
oo_date_day (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = (attr_eq (attrs[1], "short"));

	g_string_append (state->cur_format.accum, is_short ? "d" : "dd");
}

static void
oo_date_month (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean as_text = FALSE;
	gboolean is_short = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
		else if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "textual", &as_text))
			;
	g_string_append (state->cur_format.accum, as_text
			 ? (is_short ? "mmm" : "mmmm")
			 : (is_short ? "m" : "mm"));
}
static void
oo_date_year (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
	g_string_append (state->cur_format.accum, is_short ? "yy" : "yyyy");
}
static void
oo_date_era (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_day_of_week (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
	g_string_append (state->cur_format.accum, is_short ? "ddd" : "dddd");
}
static void
oo_date_week_of_year (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_quarter (GsfXMLIn *xin, xmlChar const **attrs)
{
}
static void
oo_date_hours (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;
	gboolean truncate_hour_on_overflow = TRUE;
	gboolean truncate_hour_on_overflow_set = FALSE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
				       "truncate-on-overflow",
				       &truncate_hour_on_overflow))
			truncate_hour_on_overflow_set = TRUE;

	if (truncate_hour_on_overflow_set) {
		if (truncate_hour_on_overflow)
			g_string_append (state->cur_format.accum, is_short ? "h" : "hh");
		else {
			g_string_append (state->cur_format.accum, is_short ? "[h]" : "[hh]");
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_HOURS;
		}
	} else {
		if (state->cur_format.truncate_hour_on_overflow)
			g_string_append (state->cur_format.accum, is_short ? "h" : "hh");
		else {
			g_string_append (state->cur_format.accum, is_short ? "[h]" : "[hh]");
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_HOURS;
		}
	}
}

static void
oo_date_minutes (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;
	gboolean truncate_hour_on_overflow = TRUE;
	gboolean truncate_hour_on_overflow_set = FALSE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
				       "truncate-on-overflow",
				       &truncate_hour_on_overflow))
			truncate_hour_on_overflow_set = TRUE;
	state->cur_format.pos_minutes = state->cur_format.accum->len;

	if (truncate_hour_on_overflow_set) {
		if (truncate_hour_on_overflow)
			g_string_append (state->cur_format.accum, is_short ? "m" : "mm");
		else {
			g_string_append (state->cur_format.accum, is_short ? "[m]" : "[mm]");
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_MINUTES;
		}
	} else {
		if (state->cur_format.truncate_hour_on_overflow ||
		    0 != (state->cur_format.elapsed_set & ODF_ELAPSED_SET_HOURS))
			g_string_append (state->cur_format.accum, is_short ? "m" : "mm");
		else {
			g_string_append (state->cur_format.accum, is_short ? "[m]" : "[mm]");
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_MINUTES;
		}
	}
}

#define OO_DATE_SECONDS_PRINT_SECONDS	{				\
		g_string_append (state->cur_format.accum,		\
				 is_short ? "s" : "ss");		\
		if (digits > 0) {					\
			g_string_append_c (state->cur_format.accum,	\
					   '.');			\
			while (digits-- > 0)				\
				g_string_append_c			\
					(state->cur_format.accum, '0');	\
		}							\
	}


static void
oo_date_seconds (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean is_short = TRUE;
	int digits = 0;
	gboolean truncate_hour_on_overflow = TRUE;
	gboolean truncate_hour_on_overflow_set = FALSE;

	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "style"))
			is_short = attr_eq (attrs[1], "short");
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,
					     "decimal-places"))
			digits = atoi (attrs[1]);
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT,
				       "truncate-on-overflow",
				       &truncate_hour_on_overflow))
			truncate_hour_on_overflow_set = TRUE;

	state->cur_format.pos_seconds = state->cur_format.accum->len;

	if (truncate_hour_on_overflow_set) {
		if (truncate_hour_on_overflow) {
			OO_DATE_SECONDS_PRINT_SECONDS;
		} else {
			g_string_append_c (state->cur_format.accum, '[');
			OO_DATE_SECONDS_PRINT_SECONDS;
			g_string_append_c (state->cur_format.accum, ']');
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_SECONDS;
		}
	} else {
		if (state->cur_format.truncate_hour_on_overflow ||
		    0 != (state->cur_format.elapsed_set &
			  (ODF_ELAPSED_SET_HOURS | ODF_ELAPSED_SET_MINUTES))) {
			OO_DATE_SECONDS_PRINT_SECONDS;
		} else {
			g_string_append_c (state->cur_format.accum, '[');
			OO_DATE_SECONDS_PRINT_SECONDS;
			g_string_append_c (state->cur_format.accum, ']');
			state->cur_format.elapsed_set |= ODF_ELAPSED_SET_SECONDS;
		}
	}
}

#undef OO_DATE_SECONDS_PRINT_SECONDS

static void
oo_date_am_pm (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	if (state->cur_format.accum != NULL)
		g_string_append (state->cur_format.accum, "AM/PM");

}
static void
oo_date_text_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->cur_format.accum == NULL)
		return;

	if (xin->content->len == 1 && NULL != strchr (" /-(),",*xin->content->str))
		g_string_append (state->cur_format.accum, xin->content->str);
	else if (xin->content->len >0) {
		g_string_append_c (state->cur_format.accum, '"');
		g_string_append (state->cur_format.accum, xin->content->str);
		g_string_append_c (state->cur_format.accum, '"');
	}
}

static void
oo_date_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;
	int magic = GO_FORMAT_MAGIC_NONE;
	gboolean format_source_is_language = FALSE;
	gboolean truncate_hour_on_overflow = TRUE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "family") &&
			 !attr_eq (attrs[1], "data-style"))
			return;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT,
					     "format-magic"))
			magic = atoi (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "format-source"))
			format_source_is_language = attr_eq (attrs[1], "language");
		else if (oo_attr_bool (xin, attrs, OO_NS_NUMBER,
				       "truncate-on-overflow", &truncate_hour_on_overflow));

	g_return_if_fail (state->cur_format.accum == NULL);
	g_return_if_fail (name != NULL);

	/* We always save a magic number with source language, so if that is gone somebody may have changed formats */
	state->cur_format.magic = format_source_is_language ? magic : GO_FORMAT_MAGIC_NONE;
	state->cur_format.accum = (state->cur_format.magic == GO_FORMAT_MAGIC_NONE) ?  g_string_new (NULL) : NULL;
	state->cur_format.name = g_strdup (name);
	state->cur_format.truncate_hour_on_overflow = truncate_hour_on_overflow;
	state->cur_format.elapsed_set = 0;
	state->cur_format.pos_seconds = 0;
	state->cur_format.pos_minutes = 0;
}

static void
oo_date_style_end_rm_elapsed (GString *str, guint pos)
{
	guint end;
	g_return_if_fail (str->len > pos && str->str[pos] == '[');

	g_string_erase (str, pos, 1);
	end = strcspn (str->str + pos, "]");
	g_string_erase (str, pos + end, 1);
}

static void
oo_date_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	int elapsed = state->cur_format.elapsed_set;

	if (state->cur_format.magic != GO_FORMAT_MAGIC_NONE)
		g_hash_table_insert (state->formats, state->cur_format.name,
				     go_format_new_magic (state->cur_format.magic));
	else {
		g_return_if_fail (state->cur_format.accum != NULL);

		while (elapsed != 0 && elapsed != ODF_ELAPSED_SET_SECONDS
		       && elapsed != ODF_ELAPSED_SET_MINUTES
		       && elapsed != ODF_ELAPSED_SET_HOURS) {
			/*We need to fix the format string since several times are set as "elapsed". */
			if (0 != (elapsed & ODF_ELAPSED_SET_SECONDS)) {
				oo_date_style_end_rm_elapsed (state->cur_format.accum,
							      state->cur_format.pos_seconds);
				if (state->cur_format.pos_seconds < state->cur_format.pos_minutes)
					state->cur_format.pos_minutes -= 2;
				elapsed -= ODF_ELAPSED_SET_SECONDS;
			} else {
				oo_date_style_end_rm_elapsed (state->cur_format.accum,
							      state->cur_format.pos_minutes);
				elapsed -= ODF_ELAPSED_SET_MINUTES;
				break;
			}
		}

		g_hash_table_insert (state->formats, state->cur_format.name,
				     go_format_new_from_XL (state->cur_format.accum->str));
		g_string_free (state->cur_format.accum, TRUE);
	}
	state->cur_format.accum = NULL;
	state->cur_format.name = NULL;
}

/*****************************************************************************************************/

static void
odf_fraction (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean grouping = FALSE;
	gboolean no_int_part = FALSE;
	gboolean denominator_fixed = FALSE;
	int denominator = 0;
	int min_d_digits = 0;
	int max_d_digits = 3;
	int min_i_digits = 0;
	int min_n_digits = 0;


	if (state->cur_format.accum == NULL)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "grouping", &grouping)) {}
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "denominator-value")) {
			denominator_fixed = TRUE;
			denominator = atoi (CXML2C (attrs[1]));
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,
					       "min-denominator-digits"))
			min_d_digits = atoi (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT,
					       "max-denominator-digits"))
			max_d_digits = atoi (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,
					       "min-integer-digits"))
			min_i_digits = atoi (CXML2C (attrs[1]));
		else if  (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "no-integer-part", &no_int_part)) {}
		else if  (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,
					       "min-numerator-digits"))
			min_n_digits = atoi (CXML2C (attrs[1]));

	if (!no_int_part) {
		g_string_append_c (state->cur_format.accum, '#');
		while (min_i_digits-- > 0)
			g_string_append_c (state->cur_format.accum, '0');
		g_string_append_c (state->cur_format.accum, ' ');
	}
	g_string_append_c (state->cur_format.accum, '?');
	while (min_n_digits-- > 0)
		g_string_append_c (state->cur_format.accum, '0');
	g_string_append_c (state->cur_format.accum, '/');
	if (denominator_fixed) {
		int denom = denominator;
		int count = 0;
		while (denom > 0) {
			denom /= 10;
			count ++;
		}
		min_d_digits -= count;
		while (min_d_digits-- > 0)
			g_string_append_c (state->cur_format.accum, '0');
		g_string_append_printf (state->cur_format.accum, "%i", denominator);
	} else {
		max_d_digits -= min_d_digits;
		while (max_d_digits-- > 0)
			g_string_append_c (state->cur_format.accum, '?');
		while (min_d_digits-- > 0)
			g_string_append_c (state->cur_format.accum, '0');
	}
}

static void
odf_number (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean grouping = FALSE;
	int decimal_places = 0;
	gboolean decimal_places_specified = FALSE;
/* 	gnm_float display_factor = 1.; */
	int min_i_digits = 1;

	if (state->cur_format.accum == NULL)
		return;

	/* We are ignoring number:decimal-replacement */

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "grouping", &grouping)) {}
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "decimal-places")) {
			decimal_places = atoi (CXML2C (attrs[1]));
			decimal_places_specified = TRUE;
		} /* else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,  */
/* 					       "display-factor")) */
/* 			display_factor = gnm_strto (CXML2C (attrs[1]), NULL); */
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,
					       "min-integer-digits"))
			min_i_digits = atoi (CXML2C (attrs[1]));

	if (decimal_places_specified)
		go_format_generate_number_str (state->cur_format.accum,  min_i_digits, decimal_places,
					       grouping, FALSE, FALSE, NULL, NULL);
	else
		g_string_append (state->cur_format.accum, go_format_as_XL (go_format_general ()));
}

static void
odf_scientific (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GOFormatDetails details;
	gboolean engineering = FALSE;
/* 	int min_exp_digits = 1; */

	if (state->cur_format.accum == NULL)
		return;

	go_format_details_init (&details, GO_FORMAT_SCIENTIFIC);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_NUMBER, "grouping", &details.thousands_sep)) {}
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER, "decimal-places"))
		        details.num_decimals = atoi (CXML2C (attrs[1]));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,
					     "min-integer-digits"))
			details.min_digits = atoi (CXML2C (attrs[1]));
/* 		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_NUMBER,  */
/* 					     "min-exponent-digits")) */
/* 			min_exp_digits = atoi (CXML2C (attrs[1])); */
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "engineering",
				       &engineering));
	if (engineering)
		details.exponent_step = 3;
	go_format_generate_str (state->cur_format.accum, &details);
}

static void
odf_currency_symbol_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->cur_format.accum == NULL)
		return;
	if (0 == strcmp (xin->content->str, "$")) {
		g_string_append_c (state->cur_format.accum, '$');
		return;
	}
	g_string_append (state->cur_format.accum, "[$");
	g_string_append (state->cur_format.accum, xin->content->str);
	g_string_append_c (state->cur_format.accum, ']');
}


static void
odf_map (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *condition = NULL;
	char const *style_name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "condition"))
			condition = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "apply-style-name"))
			style_name = attrs[1];

	if (condition != NULL && style_name != NULL && g_str_has_prefix (condition, "value()")) {
		condition += 7;
		while (*condition == ' ') condition++;
		if (*condition == '>' || *condition == '<' || *condition == '=') {
			state->conditions = g_slist_prepend (state->conditions, g_strdup (condition));
			state->cond_formats = g_slist_prepend (state->cond_formats,
							       g_strdup (style_name));
			return;
		}
	}
}

static inline gboolean
attr_eq_ncase (xmlChar const *a, char const *s, int n)
{
	return !g_ascii_strncasecmp (CXML2C (a), s, n);
}

static void
odf_number_color (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "color")){
			char const *color = NULL;
			if (attr_eq_ncase (attrs[1], "#ff0000", 7))
				color = "[Red]";
			else if (attr_eq_ncase (attrs[1], "#000000", 7))
				color = "[Black]";
			else if (attr_eq_ncase (attrs[1], "#0000ff", 7))
				color = "[Blue]";
			else if (attr_eq_ncase (attrs[1], "#00ffff", 7))
				color = "[Cyan]";
			else if (attr_eq_ncase (attrs[1], "#00ff00", 7))
				color = "[Green]";
			else if (attr_eq_ncase (attrs[1], "#ff00ff", 7))
				color = "[Magenta]";
			else if (attr_eq_ncase (attrs[1], "#ffffff", 7))
				color = "[White]";
			else if (attr_eq_ncase (attrs[1], "#ffff00", 7))
				color = "[Yellow]";
			if (color != NULL)
				g_string_append (state->cur_format.accum, color);
		}
}

static void
odf_number_style (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "name"))
			name = CXML2C (attrs[1]);

	g_return_if_fail (state->cur_format.accum == NULL);
	g_return_if_fail (name != NULL);

	state->cur_format.accum = g_string_new (NULL);
	state->cur_format.name = g_strdup (name);
	state->conditions = NULL;
	state->cond_formats = NULL;
}

static void
odf_number_style_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	g_return_if_fail (state->cur_format.accum != NULL);

	if (state->conditions != NULL) {
		/* We have conditional formats */
		GSList *lc, *lf;
		char *accum;
		int parts = 0;

		accum = g_string_free (state->cur_format.accum, FALSE);
		if (strlen (accum) == 0) {
			g_free (accum);
			accum = NULL;
		}
		state->cur_format.accum = g_string_new (NULL);

		lc = state->conditions;
		lf = state->cond_formats;
		while (lc && lf) {
			char *cond = lc->data;
			if (cond != NULL && *cond == '>') {
				char *val = cond + strcspn (cond, "0123456789.");
				if ((*(cond+1) != '=') || (strtod (val, NULL) != 0.))
					g_string_append_printf
						(state->cur_format.accum,
						 (*(cond+1) == '=') ? "[>=%s]" : "[>%s]", val);
				g_string_append (state->cur_format.accum, go_format_as_XL
					 (g_hash_table_lookup (state->formats, lf->data)));
				parts++;
				g_free (lc->data);
				lc->data = NULL;
				break;
			}
			lc = lc->next;
			lf = lf->next;
		}

		if (parts == 0) {
			lc = state->conditions;
			lf = state->cond_formats;
			while (lc && lf) {
				char *cond = lc->data;
				if (cond != NULL && *cond == '=') {
					char *val = cond + strcspn (cond, "0123456789.");
					g_string_append_printf (state->cur_format.accum, "[=%s]", val);
					g_string_append (state->cur_format.accum, go_format_as_XL
							 (g_hash_table_lookup (state->formats, lf->data)));
					parts++;
					g_free (lc->data);
					lc->data = NULL;
					break;
				}
				lc = lc->next;
				lf = lf->next;
			}
		}

		if (parts == 0) {
			lc = state->conditions;
			lf = state->cond_formats;
			while (lc && lf) {
				char *cond = lc->data;
				if (cond != NULL && *cond == '<' && *(cond + 1) == '>') {
					char *val = cond + strcspn (cond, "0123456789.");
					g_string_append_printf (state->cur_format.accum, "[<>%s]", val);
					g_string_append (state->cur_format.accum, go_format_as_XL
							 (g_hash_table_lookup (state->formats, lf->data)));
					parts++;
					g_free (lc->data);
					lc->data = NULL;
					break;
				}
				lc = lc->next;
				lf = lf->next;
			}
		}

		if ((parts == 0) && (accum != NULL)) {
			g_string_append (state->cur_format.accum, accum);
			parts++;
		}

		lc = state->conditions;
		lf = state->cond_formats;
		while (lc && lf) {
			char *cond = lc->data;
			if (cond != NULL && *cond == '<' && *(cond + 1) != '>') {
				char *val = cond + strcspn (cond, "0123456789.");
				if (parts > 0)
					g_string_append_c (state->cur_format.accum, ';');
				if ((*(cond+1) != '=') || (strtod (val, NULL) != 0.))
					g_string_append_printf
						(state->cur_format.accum,
						 (*(cond+1) == '=') ? "[<=%s]" : "[<%s]", val);
				g_string_append (state->cur_format.accum, go_format_as_XL
					 (g_hash_table_lookup (state->formats, lf->data)));
				parts++;
			}
			lc = lc->next;
			lf = lf->next;
		}

		if (parts < 2) {
			lc = state->conditions;
			lf = state->cond_formats;
			while (lc && lf) {
				char *cond = lc->data;
				if (cond != NULL && *cond == '=') {
					char *val = cond + strcspn (cond, "0123456789.");
					if (parts > 0)
						g_string_append_c (state->cur_format.accum, ';');
					g_string_append_printf (state->cur_format.accum, "[=%s]", val);
					g_string_append (state->cur_format.accum, go_format_as_XL
							 (g_hash_table_lookup (state->formats, lf->data)));
					parts++;
					break;
				}
				lc = lc->next;
				lf = lf->next;
			}
		}

		if (parts < 2) {
			lc = state->conditions;
			lf = state->cond_formats;
			while (lc && lf) {
				char *cond = lc->data;
				if (cond != NULL && *cond == '<' && *(cond + 1) == '>') {
					char *val = cond + strcspn (cond, "0123456789.");
					if (parts > 0)
						g_string_append_c (state->cur_format.accum, ';');
					g_string_append_printf (state->cur_format.accum, "[<>%s]", val);
					g_string_append (state->cur_format.accum, go_format_as_XL
							 (g_hash_table_lookup (state->formats, lf->data)));
					parts++;
					break;
				}
				lc = lc->next;
				lf = lf->next;
			}
		}
		if (accum != NULL) {
			if (parts > 0)
				g_string_append_c (state->cur_format.accum, ';');
			g_string_append (state->cur_format.accum, accum);
			g_free (accum);
		}
	}

	g_hash_table_insert (state->formats, state->cur_format.name,
			     go_format_new_from_XL (state->cur_format.accum->str));
	g_string_free (state->cur_format.accum, TRUE);
	state->cur_format.accum = NULL;
	state->cur_format.name = NULL;
	go_slist_free_custom (state->conditions, g_free);
	state->conditions = NULL;
	go_slist_free_custom (state->cond_formats, g_free);
	state->cond_formats = NULL;
}

/*****************************************************************************************************/

static void
oo_set_gnm_border  (GsfXMLIn *xin, GnmStyle *style,
		    xmlChar const *str, GnmStyleElement location)
{
	GnmStyleBorderType border_style;
	GnmBorder   *old_border, *new_border;
	GnmStyleBorderLocation const loc =
		GNM_STYLE_BORDER_TOP + (int)(location - MSTYLE_BORDER_TOP);

	if (!strcmp ((char const *)str, "hair"))
		border_style = GNM_STYLE_BORDER_HAIR;
	else if (!strcmp ((char const *)str, "medium-dash"))
		border_style = GNM_STYLE_BORDER_MEDIUM_DASH;
	else if (!strcmp ((char const *)str, "dash-dot"))
		border_style = GNM_STYLE_BORDER_DASH_DOT;
	else if (!strcmp ((char const *)str, "medium-dash-dot"))
		border_style = GNM_STYLE_BORDER_MEDIUM_DASH_DOT;
	else if (!strcmp ((char const *)str, "dash-dot-dot"))
		border_style = GNM_STYLE_BORDER_DASH_DOT_DOT;
	else if (!strcmp ((char const *)str, "medium-dash-dot-dot"))
		border_style = GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT;
	else if (!strcmp ((char const *)str, "slanted-dash-dot"))
		border_style = GNM_STYLE_BORDER_SLANTED_DASH_DOT;
	else return;

	old_border = gnm_style_get_border (style, location);
	new_border = gnm_style_border_fetch (border_style,
					     style_color_ref(old_border->color),
					     gnm_style_border_get_orientation (loc));
	gnm_style_set_border (style, location, new_border);
}

static void
oo_parse_border (GsfXMLIn *xin, GnmStyle *style,
		 xmlChar const *str, GnmStyleElement location)
{
	double pts;
	char const *end = oo_parse_distance (xin, str, "border", &pts);
	GnmBorder *border = NULL;
	GnmColor *color = NULL;
	const char *border_color = NULL;
	GnmStyleBorderType border_style;
	GnmStyleBorderLocation const loc =
		GNM_STYLE_BORDER_TOP + (int)(location - MSTYLE_BORDER_TOP);

	if (end == NULL || end == CXML2C (str))
		return;
	while (*end == ' ')
		end++;
	/* "0.035cm solid #000000" */
	border_color = strchr (end, '#');
	if (border_color) {
		char *border_type = g_strndup (end, border_color - end);
		color = oo_parse_color (xin, CC2XML (border_color), "color");

		if (g_str_has_prefix (border_type, "none")||
		    g_str_has_prefix (border_type, "hidden"))
			border_style = GNM_STYLE_BORDER_NONE;
		else if (g_str_has_prefix (border_type, "solid") ||
			 g_str_has_prefix (border_type, "groove") ||
			 g_str_has_prefix (border_type, "ridge") ||
			 g_str_has_prefix (border_type, "inset") ||
			 g_str_has_prefix (border_type, "outset")) {
			if (pts <= OD_BORDER_THIN)
				border_style = GNM_STYLE_BORDER_THIN;
			else if (pts <= OD_BORDER_MEDIUM)
				border_style = GNM_STYLE_BORDER_MEDIUM;
			else
				border_style = GNM_STYLE_BORDER_THICK;
		} else if (g_str_has_prefix (border_type, "dashed"))
			border_style = GNM_STYLE_BORDER_DASHED;
		else if (g_str_has_prefix (border_type, "dotted"))
			border_style = GNM_STYLE_BORDER_DOTTED;
		else
			border_style = GNM_STYLE_BORDER_DOUBLE;

		border = gnm_style_border_fetch (border_style, color,
						 gnm_style_border_get_orientation (loc));
		border->width = pts;
		gnm_style_set_border (style, location, border);
		g_free (border_type);
	}
}

static void
odf_style_set_align_h (GnmStyle *style, gboolean h_align_is_valid, gboolean repeat_content,
		       int text_align, int gnm_halign)
{
	int alignment = HALIGN_GENERAL;
	if (h_align_is_valid)
		alignment = repeat_content ? HALIGN_FILL
			: ((text_align < 0) ? ((gnm_halign > -1) ? gnm_halign : HALIGN_LEFT)
			   : text_align);

	gnm_style_set_align_h (style, alignment);
}

static void
oo_style_prop_cell (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const h_alignments [] = {
		{ "start",	-1 },            /* see below, we may have a gnm:GnmHAlign attribute */
		{ "left",	HALIGN_LEFT },
		{ "center",	HALIGN_CENTER },
		{ "end",	HALIGN_RIGHT },   /* This really depends on the text direction */
		{ "right",	HALIGN_RIGHT },
		{ "justify",	HALIGN_JUSTIFY },
		{ "automatic",	HALIGN_GENERAL },
		{ NULL,	0 },
	};
	static OOEnum const v_alignments [] = {
		{ "bottom",	VALIGN_BOTTOM },
		{ "top",	VALIGN_TOP },
		{ "middle",	VALIGN_CENTER },
		{ "automatic",	-1 },            /* see below, we may have a gnm:GnmVAlign attribute */
		{ NULL,	0 },
	};
	static OOEnum const protections [] = {
		{ "none",			0 },
		{ "hidden-and-protected",	1 | 2 },
		{ "protected",			    2 },
		{ "formula-hidden",		1 },
		{ "protected formula-hidden",	1 | 2 },
		{ "formula-hidden protected",	1 | 2 },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmColor *color;
	GnmStyle *style = state->cur_style.cells;
	gboolean  btmp;
	int	  tmp;
	gboolean  v_alignment_is_fixed = FALSE;

	g_return_if_fail (style != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "background-color"))) {
			gnm_style_set_back_color (style, color);
			if (color == magic_transparent)
				gnm_style_set_pattern (style, 0);
			else
				gnm_style_set_pattern (style, 1);
		} else if ((color = oo_attr_color (xin, attrs, OO_NS_FO, "color")))
			gnm_style_set_font_color (style, color);
		else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "cell-protect", protections, &tmp)) {
			gnm_style_set_contents_locked (style, (tmp & 2) != 0);
			gnm_style_set_contents_hidden (style, (tmp & 1) != 0);
		} else if (oo_attr_enum (xin, attrs,
				       (state->ver >= OOO_VER_OPENDOC) ? OO_NS_FO : OO_NS_STYLE,
					 "text-align", h_alignments, &(state->text_align)))
			/* Note that style:text-align-source, style:text_align, style:repeat-content  */
			/* and gnm:GnmHAlign interact but can appear in any order and arrive from different */
			/* elements, so we can't use local variables                                  */
			odf_style_set_align_h (style, state->h_align_is_valid, state->repeat_content,
					       state->text_align, state->gnm_halign);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "text-align-source")) {
			state->h_align_is_valid = attr_eq (attrs[1], "fix");
			odf_style_set_align_h (style, state->h_align_is_valid, state->repeat_content,
					       state->text_align, state->gnm_halign);
		} else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "repeat-content", &(state->repeat_content)))
			odf_style_set_align_h (style, state->h_align_is_valid, state->repeat_content,
					       state->text_align, state->gnm_halign);
		else if (oo_attr_int (xin,attrs, OO_GNUM_NS_EXT, "GnmHAlign", &(state->gnm_halign)))
			odf_style_set_align_h (style, state->h_align_is_valid, state->repeat_content,
					       state->text_align, state->gnm_halign);
		else if (oo_attr_enum (xin, attrs,
				       (state->ver >= OOO_VER_OPENDOC) ? OO_NS_STYLE : OO_NS_FO,
				       "vertical-align", v_alignments, &tmp)) {
			if (tmp != -1) {
				gnm_style_set_align_v (style, tmp);
				v_alignment_is_fixed = TRUE;
			} else if (!v_alignment_is_fixed)
                                /* This should depend on the rotation */
				gnm_style_set_align_v (style, VALIGN_BOTTOM);
		} else if (oo_attr_int (xin,attrs, OO_GNUM_NS_EXT, "GnmVAlign", &tmp)) {
			if (!v_alignment_is_fixed) {
				gnm_style_set_align_v (style, tmp);
				v_alignment_is_fixed = TRUE;
			}
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "wrap-option"))
			gnm_style_set_wrap_text (style, attr_eq (attrs[1], "wrap"));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border-bottom"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border-left"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border-right"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border-top"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "border")) {
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "diagonal-bl-tr"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "diagonal-tl-br"))
			oo_parse_border (xin, style, attrs[1], MSTYLE_BORDER_REV_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "border-line-style-bottom"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_BOTTOM);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "border-line-style-top"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_TOP);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "border-line-style-left"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_LEFT);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "border-line-style-right"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_RIGHT);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "diagonal-bl-tr-line-style"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_GNUM_NS_EXT, "diagonal-tl-br-line-style"))
			oo_set_gnm_border (xin, style, attrs[1], MSTYLE_BORDER_REV_DIAGONAL);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "font-name"))
			gnm_style_set_font_name (style, CXML2C (attrs[1]));
		else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, "shrink-to-fit", &btmp))
			gnm_style_set_shrink_to_fit (style, btmp);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "direction"))
			gnm_style_set_text_dir (style, attr_eq (attrs[1], "rtl") ? GNM_TEXT_DIR_RTL : GNM_TEXT_DIR_LTR);
		else if (oo_attr_int (xin, attrs, OO_NS_STYLE, "rotation-angle", &tmp))
			gnm_style_set_rotation	(style, tmp);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "font-size")) {
			gnm_float size;
			if (1 == sscanf (CXML2C (attrs[1]), "%" GNM_SCANF_g "pt", &size))
				gnm_style_set_font_size (style, size);

		/* TODO : get specs on how these relate */
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "text-underline-style") ||
			   gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "text-underline-type") ||
			   gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "text-underline"))
			/* cheesy simple support for now */
			gnm_style_set_font_uline (style, attr_eq (attrs[1], "none") ? UNDERLINE_NONE : UNDERLINE_SINGLE);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "font-style"))
			gnm_style_set_font_italic (style, attr_eq (attrs[1], "italic"));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "font-weight")) {
			int weight = atoi (CXML2C (attrs[1]));
			if (attr_eq (attrs[1], "bold"))
				weight = 700;
			gnm_style_set_font_bold (style, weight >= PANGO_WEIGHT_SEMIBOLD);
		}
#if 0
		else if (!strcmp (attrs[0], OO_NS_FO, "font-weight")) {
				gnm_style_set_font_bold (style, TRUE);
				gnm_style_set_font_uline (style, TRUE);
			="normal"
		} else if (!strcmp (attrs[0], OO_NS_STYLE, "text-underline" )) {
			="italic"
				gnm_style_set_font_italic (style, TRUE);
		}
#endif
}

static OOPageBreakType
oo_page_break_type (GsfXMLIn *xin, xmlChar const *attr)
{
	/* Note that truly automatic of soft page breaks are stored */
	/* via text:soft-page-break tags                            */
	if (!strcmp (attr, "page"))
		return OO_PAGE_BREAK_MANUAL;
	if (!strcmp (attr, "column"))
		return OO_PAGE_BREAK_MANUAL;
	if (!strcmp (attr, "auto"))
		return OO_PAGE_BREAK_NONE;
	oo_warning (xin,
		_("Unknown break type '%s' defaulting to NONE"), attr);
	return OO_PAGE_BREAK_NONE;
}

static void
oo_style_prop_col_row (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const * const size_tag = (state->cur_style_type == OO_STYLE_COL)
		? "column-width" :  "row-height";
	char const * const use_optimal = (state->cur_style_type == OO_STYLE_COL)
		? "use-optimal-column-width" : "use-optimal-row-height";
	double pts;
	gboolean auto_size;

	g_return_if_fail (state->cur_style.col_rows != NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (NULL != oo_attr_distance (xin, attrs, OO_NS_STYLE, size_tag, &pts))
			state->cur_style.col_rows->size_pts = pts;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "break-before"))
			state->cur_style.col_rows->break_before =
				oo_page_break_type (xin, attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_FO, "break-after"))
			state->cur_style.col_rows->break_after =
				oo_page_break_type (xin, attrs[1]);
		else if (oo_attr_bool (xin, attrs, OO_NS_STYLE, use_optimal, &auto_size))
			state->cur_style.col_rows->manual = !auto_size;
}

static void
oo_style_prop_table (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const modes [] = {
		{ "lr-tb",	0 },
		{ "rl-tb",	1 },
		{ "tb-rl",	1 },
		{ "tb-lr",	0 },
		{ "lr",		0 },
		{ "rl",		1 },
		{ "tb",		0 },	/* what do tb and page imply in this context ? */
		{ "page",	0 },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	OOSheetStyle *style = state->cur_style.sheets;
	gboolean tmp_i;
	int tmp_b;

	g_return_if_fail (style != NULL);

	style->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
	style->is_rtl  = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_bool (xin, attrs, OO_NS_TABLE, "display", &tmp_b)) {
			if (!tmp_b)
				style->visibility = GNM_SHEET_VISIBILITY_HIDDEN;
		} else if (oo_attr_enum (xin, attrs, OO_NS_STYLE, "writing-mode", modes, &tmp_i))
			style->is_rtl = tmp_i;
}

static gboolean
odf_style_map_load_two_values (GsfXMLIn *xin, char *condition, GnmStyleCond *cond)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	condition = g_strstrip (condition);
	if (*(condition++) == '(') {
		guint len = strlen (condition);
		char *end = condition + len - 1;
		if (*end == ')') {
			GnmParsePos   pp;

			parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
			len -= 1;
			*end = '\0';
			while (1) {
				gchar * try = g_strrstr_len (condition, len, ",");
				GnmExprTop const *texpr;

				if (try == NULL || try == condition) return FALSE;

				texpr = oo_expr_parse_str
					(xin, try + 1, &pp,
					 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
					 FORMULA_OPENFORMULA);
				if (texpr != NULL) {
					cond->texpr[1] = texpr;
					*try = '\0';
					break;
				}
				len = try - condition - 1;
			}
			cond->texpr[0] = oo_expr_parse_str
				(xin, condition, &pp,
				 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
				 FORMULA_OPENFORMULA);
			return ((cond->texpr[0] != NULL) && (cond->texpr[1] != NULL));
		}
	}
	return FALSE;
}

static gboolean
odf_style_map_load_one_value (GsfXMLIn *xin, char *condition, GnmStyleCond *cond)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GnmParsePos   pp;

	parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
	cond->texpr[0] = oo_expr_parse_str
		(xin, condition, &pp,
		 GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
		 FORMULA_OPENFORMULA);
	return (cond->texpr[0] != NULL);
}


static void
oo_style_map (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *style_name = NULL;
	char const *condition = NULL, *full_condition;
	GnmStyle *style = NULL;
	GnmStyleCond cond;
	GnmStyleConditions *sc;
	gboolean success = FALSE;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "condition"))
			condition = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "apply-style-name"))
			style_name = attrs[1];
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_STYLE, "base-cell-address"))
			;
	if (style_name == NULL || condition == NULL)
		return;

	style = g_hash_table_lookup (state->styles.cell, style_name);

	g_return_if_fail (style != NULL);
	g_return_if_fail (state->cur_style.cells != NULL);

	full_condition = condition;
	cond.texpr[0] = NULL;
	cond.texpr[1] = NULL;

	if (g_str_has_prefix (condition, "cell-content()")) {
		condition += strlen ("cell-content()") - 1;
		while (*(++condition) == ' ');
		switch (*(condition++)) {
		case '<':
			if (*condition == '=') {
				condition++;
				cond.op = GNM_STYLE_COND_LTE;
			} else
				cond.op = GNM_STYLE_COND_LT;
			success = TRUE;
			break;
		case '>':
			if (*condition == '=') {
				condition++;
				cond.op = GNM_STYLE_COND_GTE;
			} else
				cond.op = GNM_STYLE_COND_GT;
			success = TRUE;
			break;
			break;
		case '=':
			cond.op = GNM_STYLE_COND_EQUAL;
			success = TRUE;
			break;
		case '!':
			if (*condition == '=') {
				condition++;
				cond.op = GNM_STYLE_COND_NOT_EQUAL;
				success = TRUE;
			}
			break;
		default:
			break;
		}
		if (success) {
			char *text = g_strdup (condition);
			success = odf_style_map_load_one_value (xin, text, &cond);
			g_free (text);
		}

	} else if (g_str_has_prefix (condition, "cell-content-is-between")) {
		char *text;
		cond.op = GNM_STYLE_COND_BETWEEN;
		condition += strlen ("cell-content-is-between");
		text = g_strdup (condition);
		success = odf_style_map_load_two_values (xin, text, &cond);
		g_free (text);
	} else if (g_str_has_prefix (condition, "cell-content-is-not-between")) {
		char *text;
		cond.op = GNM_STYLE_COND_NOT_BETWEEN;
		condition += strlen ("cell-content-is-not-between");
		text = g_strdup (condition);
		success = odf_style_map_load_two_values (xin, text, &cond);
		g_free (text);
	} else if (g_str_has_prefix (condition, "is-true-formula")) {
		char *text;
		cond.op = GNM_STYLE_COND_CUSTOM;
		condition += strlen ("is-true-formula");
		text = g_strdup (condition);
		success = odf_style_map_load_one_value (xin, text, &cond);
		g_free (text);
	}

	if (!success)
	{
		if (cond.texpr[0] != NULL)
			gnm_expr_top_unref (cond.texpr[0]);
		if (cond.texpr[1] != NULL)
			gnm_expr_top_unref (cond.texpr[1]);
		oo_warning (xin,
			    _("Unknown condition '%s' encountered, ignoring."),
			    full_condition);
		return;
	}

	cond.overlay = style;
	gnm_style_ref (style);

	if (gnm_style_is_element_set (state->cur_style.cells, MSTYLE_CONDITIONS) &&
	    (sc = gnm_style_get_conditions (state->cur_style.cells)) != NULL)
		gnm_style_conditions_insert (sc, &cond, -1);
	else {
		sc = gnm_style_conditions_new ();
		gnm_style_conditions_insert (sc, &cond, -1);
		gnm_style_set_conditions (state->cur_style.cells, sc);
	}

}

static OOProp *
oo_prop_new_bool (char const *name, gboolean val)
{
	OOProp *res = g_new0 (OOProp, 1);
	res->name = name;
	g_value_init (&res->value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&res->value, val);
	return res;
}
static OOProp *
oo_prop_new_int (char const *name, int val)
{
	OOProp *res = g_new0 (OOProp, 1);
	res->name = name;
	g_value_init (&res->value, G_TYPE_INT);
	g_value_set_int (&res->value, val);
	return res;
}
static OOProp *
oo_prop_new_string (char const *name, char const *val)
{
	OOProp *res = g_new0 (OOProp, 1);
	res->name = name;
	g_value_init (&res->value, G_TYPE_STRING);
	g_value_set_string (&res->value, val);
	return res;
}
static void
oo_prop_free (OOProp *prop)
{
	g_value_unset (&prop->value);
	g_free (prop);
}

static void
oo_prop_list_free (GSList *props)
{
	GSList *ptr;
	for (ptr = props; NULL != ptr; ptr = ptr->next)
		oo_prop_free (ptr->data);
	g_slist_free (props);
}

static void
oo_prop_list_apply (GSList *props, GObject *obj)
{
	GSList *ptr;
	OOProp *prop;
	GObjectClass *klass;

	if (NULL == obj)
		return;
	klass = G_OBJECT_GET_CLASS (obj);

	for (ptr = props; ptr; ptr = ptr->next) {
		prop = ptr->data;
		if (NULL != g_object_class_find_property (klass, prop->name))
			g_object_set_property (obj, prop->name, &prop->value);
	}
}

static void
oo_prop_list_has_three_dimensional (GSList *props, gboolean *threed)
{
	GSList *ptr;
	for (ptr = props; ptr; ptr = ptr->next) {
		OOProp *prop = ptr->data;
		if (0 == strcmp (prop->name, "three-dimensional") && g_value_get_boolean (&prop->value))
			*threed = TRUE;
	}
}

static gboolean
oo_style_have_three_dimensional (GSList *styles)
{
	GSList *l;
	gboolean is_three_dimensional = FALSE;
	for (l = styles; l != NULL; l = l->next) {
		OOChartStyle *style = l->data;
		oo_prop_list_has_three_dimensional (style->other_props,
						    &is_three_dimensional);
	}
	return is_three_dimensional;
}

static void
oo_prop_list_has_multi_series (GSList *props, gboolean *threed)
{
	GSList *ptr;
	for (ptr = props; ptr; ptr = ptr->next) {
		OOProp *prop = ptr->data;
		if (0 == strcmp (prop->name, "multi-series") && g_value_get_boolean (&prop->value))
			*threed = TRUE;
	}
}

static gboolean
oo_style_have_multi_series (GSList *styles)
{
	GSList *l;
	gboolean is_multi_series = FALSE;
	for (l = styles; l != NULL; l = l->next) {
		OOChartStyle *style = l->data;
		oo_prop_list_has_multi_series (style->other_props,
						    &is_multi_series);
	}
	return is_multi_series;
}

static void
od_style_prop_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	OOChartStyle *style = state->chart.cur_graph_style;
	gboolean btmp;
	int	  tmp;
	gboolean default_style_has_lines_set = FALSE;
	gboolean draw_stroke_set = FALSE;
	gboolean draw_stroke;

	g_return_if_fail (style != NULL);

	style->grid = FALSE;
	style->src_in_rows = FALSE;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (oo_attr_bool (xin, attrs, OO_NS_CHART, "logarithmic", &btmp)) {
			if (btmp)
				style->axis_props = g_slist_prepend (style->axis_props,
					oo_prop_new_string ("map-name", "Log"));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "vertical", &btmp)) {
			/* This is backwards from my intuition */
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_bool ("horizontal", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "reverse-direction", &btmp)) {
			style->axis_props = g_slist_prepend (style->axis_props,
				oo_prop_new_bool ("invert-axis", btmp));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "stacked", &btmp)) {
			if (btmp)
				style->plot_props = g_slist_prepend (style->plot_props,
					oo_prop_new_string ("type", "stacked"));
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "percentage", &btmp)) {
			if (btmp)
				style->plot_props = g_slist_prepend (style->plot_props,
					oo_prop_new_string ("type", "as_percentage"));
		} else if (oo_attr_int (xin, attrs, OO_NS_CHART, "overlap", &tmp)) {
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_int ("overlap-percentage", tmp));
		} else if (oo_attr_int (xin, attrs, OO_NS_CHART, "gap-width", &tmp))
			style->plot_props = g_slist_prepend (style->plot_props,
				oo_prop_new_int ("gap-percentage", tmp));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "symbol-type"))
			style->plot_props = g_slist_prepend
				(style->plot_props,
				 oo_prop_new_bool ("default-style-has-markers",
						   !attr_eq (attrs[1], "none")));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_DRAW, "stroke")) {
			draw_stroke = !attr_eq (attrs[1], "none");
			draw_stroke_set = TRUE;
		} else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "lines", &btmp)) {
			style->plot_props = g_slist_prepend
				(style->plot_props,
				 oo_prop_new_bool ("default-style-has-lines", btmp));
			default_style_has_lines_set = TRUE;
		} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "series-source"))
			style->src_in_rows = attr_eq (attrs[1], "rows");
		else if (oo_attr_bool (xin, attrs, OO_NS_CHART, "three-dimensional", &btmp))
			style->other_props = g_slist_prepend (style->other_props,
				oo_prop_new_bool ("three-dimensional", btmp));
		else if (oo_attr_bool (xin, attrs, OO_GNUM_NS_EXT, "multi-series", &btmp))
			style->other_props = g_slist_prepend (style->other_props,
				oo_prop_new_bool ("multi-series", btmp));
	}

	if (draw_stroke_set && !default_style_has_lines_set)
		style->plot_props = g_slist_prepend
			(style->plot_props,
			 oo_prop_new_bool ("default-style-has-lines", draw_stroke));
}

static void
oo_style_prop (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	switch (state->cur_style_type) {
	case OO_STYLE_CELL  : oo_style_prop_cell (xin, attrs); break;
	case OO_STYLE_COL   :
	case OO_STYLE_ROW   : oo_style_prop_col_row (xin, attrs); break;
	case OO_STYLE_SHEET : oo_style_prop_table (xin, attrs); break;
	case OO_STYLE_CHART : od_style_prop_chart (xin, attrs); break;

	default :
		break;
	}
}

static void
oo_named_expr (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *name      = NULL;
	char const *base_str  = NULL;
	char const *expr_str  = NULL;
	char *range_str = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "name"))
			name = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "base-cell-address"))
			base_str = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "expression"))
			expr_str = CXML2C (attrs[1]);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "cell-range-address"))
			expr_str = range_str = g_strconcat ("[", CXML2C (attrs[1]), "]", NULL);

	if (name != NULL && base_str != NULL && expr_str != NULL) {
		GnmParsePos   pp;
		GnmExprTop const *texpr;
		char *tmp = g_strconcat ("[", base_str, "]", NULL);

		parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
		texpr = oo_expr_parse_str (xin, tmp, &pp,
					   GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES, FORMULA_OPENFORMULA);
		g_free (tmp);

		if (texpr == NULL)
			;
		else if (GNM_EXPR_GET_OPER (texpr->expr) != GNM_EXPR_OP_CELLREF) {
			oo_warning (xin, _("expression '%s' @ '%s' is not a cellref"),
				    name, base_str);
			gnm_expr_top_unref (texpr);
		} else {
			GnmCellRef const *ref = &texpr->expr->cellref.ref;
			parse_pos_init (&pp, state->pos.wb, ref->sheet,
				ref->col, ref->row);

			gnm_expr_top_unref (texpr);
			texpr = oo_expr_parse_str (xin, expr_str,
						   &pp, GNM_EXPR_PARSE_DEFAULT, FORMULA_OPENFORMULA);
			if (texpr != NULL) {
				pp.sheet = NULL;
				expr_name_add (&pp, name, texpr, NULL, TRUE, NULL);
			}
		}
	}
	g_free (range_str);
}

static void
oo_db_range_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gboolean buttons = TRUE;
	GnmRangeRef ref;
	GnmRange r;

	g_return_if_fail (state->filter == NULL);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "target-range-address")) {
			char const *ptr = oo_cellref_parse (&ref.a, CXML2C (attrs[1]), &state->pos);
			if (':' == *ptr &&
			    '\0' == *oo_cellref_parse (&ref.b, ptr+1, &state->pos))
				state->filter = gnm_filter_new (ref.a.sheet, range_init_rangeref (&r, &ref));
			else
				oo_warning (xin, _("Invalid DB range '%s'"), attrs[1]);
		} else if (oo_attr_bool (xin, attrs, OO_NS_TABLE, "display-filter-buttons", &buttons))
			/* ignore this */;
}

static void
oo_db_range_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	
	if (state->filter != NULL) {
		gnm_filter_reapply (state->filter);
		state->filter = NULL;
	}
}

static void
oo_filter_cond (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const datatypes [] = {
		{ "text",	  VALUE_STRING },
		{ "number",	  VALUE_FLOAT },
		{ NULL,	0 },
	};
	static OOEnum const operators [] = {
		{ "=",			GNM_FILTER_OP_EQUAL },
		{ "!=",			GNM_FILTER_OP_NOT_EQUAL },
		{ "<",			GNM_FILTER_OP_LT },
		{ "<=",			GNM_FILTER_OP_LTE },
		{ ">",			GNM_FILTER_OP_GT },
		{ ">=",			GNM_FILTER_OP_GTE },

		{ "match",		GNM_FILTER_OP_MATCH },
		{ "!match",		GNM_FILTER_OP_NO_MATCH },
		{ "empty",		GNM_FILTER_OP_BLANKS },
		{ "!empty",		GNM_FILTER_OP_NON_BLANKS },
		{ "bottom percent",	GNM_FILTER_OP_BOTTOM_N_PERCENT },
		{ "bottom values",	GNM_FILTER_OP_BOTTOM_N },
		{ "top percent",	GNM_FILTER_OP_TOP_N_PERCENT },
		{ "top values",		GNM_FILTER_OP_TOP_N },

		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	int field_num = 0, type = -1, op = -1;
	char const *val_str = NULL;

	if (NULL == state->filter)
		return;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_int (xin, attrs, OO_NS_TABLE, "field-number", &field_num)) ;
		else if (oo_attr_enum (xin, attrs, OO_NS_TABLE, "data-type", datatypes, &type)) ;
		else if (oo_attr_enum (xin, attrs, OO_NS_TABLE, "operator", operators, &op)) ;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "value"))
			val_str = CXML2C (attrs[1]);

	if (field_num >= 0 && op >= 0) {
		GnmFilterCondition *cond = NULL;
		GnmValue *v = NULL;

		if (type >= 0 && val_str != NULL)
			v = value_new_from_string (type, val_str, NULL, FALSE);

		switch (op) {
		case GNM_FILTER_OP_EQUAL:
		case GNM_FILTER_OP_NOT_EQUAL:
		case GNM_FILTER_OP_LT:
		case GNM_FILTER_OP_LTE:
		case GNM_FILTER_OP_GT:
		case GNM_FILTER_OP_GTE:
		case GNM_FILTER_OP_MATCH:
		case GNM_FILTER_OP_NO_MATCH:
			if (NULL != v) {
				cond = gnm_filter_condition_new_single (op, v);
				v = NULL;
			}
			break;

		case GNM_FILTER_OP_BLANKS:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_BLANKS, NULL);
			break;
		case GNM_FILTER_OP_NON_BLANKS:
			cond = gnm_filter_condition_new_single (
				GNM_FILTER_OP_NON_BLANKS, NULL);
			break;

		case GNM_FILTER_OP_BOTTOM_N_PERCENT:
		case GNM_FILTER_OP_BOTTOM_N:
		case GNM_FILTER_OP_TOP_N_PERCENT:
		case GNM_FILTER_OP_TOP_N:
			if (v && VALUE_IS_NUMBER(v))
				cond = gnm_filter_condition_new_bucket (
					0 == (op & GNM_FILTER_OP_BOTTOM_MASK),
					0 == (op & GNM_FILTER_OP_PERCENT_MASK),
					value_get_as_float (v));
			break;
		}
		value_release (v);
		if (NULL != cond)
			gnm_filter_set_condition  (state->filter, field_num, cond, FALSE);
	}
}

static void
od_draw_frame (GsfXMLIn *xin, xmlChar const **attrs)
{
/* Note that in ODF spreadsheet files svg:height and svg:width should be ignored. We should be considering */
/* table:end-x and table:end-y together with table:end-cell-address */

	OOParseState *state = (OOParseState *)xin->user_state;
	GnmRange cell_base;
	double frame_offset[4];
	gchar const *aux = NULL;
	gdouble height = 0., width = 0., x = 0., y = 0., end_x = 0., end_y = 0.;
	ColRowInfo const *col, *row;
	GnmExprTop const *texpr = NULL;

	height = width = x = y = 0.;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2){
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "width"))
			aux = oo_parse_distance (xin, attrs[1], "width", &width);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "height"))
			aux = oo_parse_distance (xin, attrs[1], "height", &height);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "x"))
			aux = oo_parse_distance (xin, attrs[1], "x", &x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_SVG, "y"))
			aux = oo_parse_distance (xin, attrs[1], "y", &y);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-x"))
			aux = oo_parse_distance (xin, attrs[1], "end-x", &end_x);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-y"))
			aux = oo_parse_distance (xin, attrs[1], "end-y", &end_y);
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "end-cell-address")) {
			GnmParsePos   pp;
			char *end_str = g_strconcat ("[", CXML2C (attrs[1]), "]", NULL);
			parse_pos_init (&pp, state->pos.wb, NULL, 0, 0);
			texpr = oo_expr_parse_str (xin, end_str, &pp,
						   GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES,
						   FORMULA_OPENFORMULA);
			g_free (end_str);
		}
	}

	cell_base.start.col = cell_base.end.col = state->pos.eval.col;
	cell_base.start.row = cell_base.end.row = state->pos.eval.row;

	col = sheet_col_get_info (state->pos.sheet, state->pos.eval.col);
	row = sheet_row_get_info (state->pos.sheet, state->pos.eval.row);

	frame_offset[0] = x;
	frame_offset[1] = y;

	if (texpr == NULL || (GNM_EXPR_GET_OPER (texpr->expr) != GNM_EXPR_OP_CELLREF)) {
		frame_offset[2] = x+width;
		frame_offset[3] = y+height;
	} else {
		GnmCellRef const *ref = &texpr->expr->cellref.ref;
		cell_base.end.col = ref->col;
		cell_base.end.row = ref->row;
		frame_offset[2] = end_x;
		frame_offset[3] = end_y ;
	}

	frame_offset[0] /= col->size_pts;
	frame_offset[1] /= row->size_pts;
	frame_offset[2] /= col->size_pts;
	frame_offset[3] /= row->size_pts;

	if (texpr)
		gnm_expr_top_unref (texpr);
	sheet_object_anchor_init (&state->chart.anchor, &cell_base, frame_offset,
				  GOD_ANCHOR_DIR_DOWN_RIGHT);
}

static void
od_draw_object (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *name_start = NULL;
	gchar * name;
	gint name_len;
	GsfInput	*content = NULL;
	SheetObject *sog = sheet_object_graph_new (NULL);

	state->chart.graph = sheet_object_graph_get_gog (sog);
	sheet_object_set_anchor (sog, &state->chart.anchor);
	sheet_object_set_sheet (sog, state->pos.sheet);
	g_object_unref (sog);

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_XLINK, "href")) {
			name_start = CXML2C (attrs[1]);
			if (strncmp (CXML2C (attrs[1]), "./", 2) == 0)
				name_start += 2;
			if (strncmp (CXML2C (attrs[1]), "/", 1) == 0)
				name_start = NULL;
			break;
		}

	if (!name_start)
		return;
	name_len = strlen (name_start);
	if (*(name_start + name_len - 1) == '/') /* OOo does not append a / */
		name_len--;
	name = g_strndup (name_start, name_len);

#ifdef OO_DEBUG_OBJS
	g_print ("START %s\n", name);
#endif
	content = gsf_infile_child_by_vname (state->zip, name, "content.xml", NULL);

	if (content != NULL) {
		GsfXMLInDoc *doc =
			gsf_xml_in_doc_new (get_dtd (), gsf_ooo_ns);
		gsf_xml_in_doc_parse (doc, content, state);
		gsf_xml_in_doc_free (doc);
		g_object_unref (content);
	}
#ifdef OO_DEBUG_OBJS
	g_print ("END %s\n", name);
#endif
	g_free (name);

	if (state->cur_style_type == OO_STYLE_CHART)
		state->cur_style_type = OO_STYLE_UNKNOWN;
	state->chart.cur_graph_style = NULL;
	g_hash_table_remove_all (state->chart.graph_styles);
}

static void
od_draw_image (GsfXMLIn *xin, xmlChar const **attrs)
{
	GsfInput *input;

	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *file = NULL;

	SheetObjectImage *soi;
	SheetObject *so;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_XLINK, "href") &&
		    strncmp (CXML2C (attrs[1]), "Pictures/", 9) == 0) {
			file = CXML2C (attrs[1]) + 9;
			break;
		}

	if (!file)
		return;

	input = gsf_infile_child_by_vname (state->zip, "Pictures", file, NULL);

	if (input != NULL) {
		gsf_off_t len = gsf_input_size (input);
		guint8 const *data = gsf_input_read (input, len, NULL);
		soi = g_object_new (SHEET_OBJECT_IMAGE_TYPE, NULL);
		sheet_object_image_set_image (soi, "", (void *)data, len, TRUE);

		so = SHEET_OBJECT (soi);
		sheet_object_set_anchor (so, &state->chart.anchor);
		sheet_object_set_sheet (so, state->pos.sheet);
		g_object_unref (input);
	}
}

static void
oo_chart_title (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	GogObject *label = NULL;
	OOParseState *state = (OOParseState *)xin->user_state;

	label = gog_object_add_by_name ((GogObject *)state->chart.chart, "Title", NULL);
	gog_dataset_set_dim (GOG_DATASET (label), 0,
			     go_data_scalar_str_new (g_strdup (xin->content->str), TRUE),
			     NULL);
}

static void
oo_chart_axis (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const types[] = {
		{ "x",	GOG_AXIS_X },
		{ "y",	GOG_AXIS_Y },
		{ "z",	GOG_AXIS_Z },
		{ NULL,	0 },
	};
	GSList	*axes;

	OOParseState *state = (OOParseState *)xin->user_state;
	OOChartStyle *style = NULL;
	gchar const *style_name = NULL;
	GogAxisType  axis_type;
	int tmp;
	GSList *l;

	axis_type = GOG_AXIS_UNKNOWN;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "style-name"))
			style_name = CXML2C (attrs[1]);
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "dimension", types, &tmp))
			axis_type = tmp;

	axes = gog_chart_get_axes (state->chart.chart, axis_type);
	if (NULL != axes) {
		state->chart.axis = axes->data;
		g_slist_free (axes);
	}

	for (l = state->chart.these_plot_styles; l != NULL; l = l->next) {
		style = l->data;
		oo_prop_list_apply (style->axis_props, G_OBJECT (state->chart.axis));
	}

	if (NULL != (style = g_hash_table_lookup (state->chart.graph_styles, style_name))) {
		if (NULL != state->chart.axis)
			oo_prop_list_apply (style->axis_props, G_OBJECT (state->chart.axis));

		/* AAARRRGGGHH : why would they do this.  The axis style impact
		 * the plot ?? */
		if (NULL != state->chart.plot && (state->ver == OOO_VER_1))
			oo_prop_list_apply (style->plot_props, G_OBJECT (state->chart.plot));
	}
}

static int
gog_series_map_dim (GogSeries const *series, GogMSDimType ms_type)
{
	GogSeriesDesc const *desc = &series->plot->desc.series;
	unsigned i = desc->num_dim;

	if (ms_type == GOG_MS_DIM_LABELS)
		return -1;
	while (i-- > 0)
		if (desc->dim[i].ms_type == ms_type)
			return i;
	return -2;
}

static int
gog_series_map_dim_by_name (GogSeries const *series, char const *dim_name)
{
	GogSeriesDesc const *desc = &series->plot->desc.series;
	unsigned i = desc->num_dim;

	while (i-- > 0)
		if (desc->dim[i].name != NULL && strcmp (desc->dim[i].name, dim_name) == 0)
			return i;
	return -2;
}

/* If range == %NULL use an implicit range */
static void
oo_plot_assign_dim (GsfXMLIn *xin, xmlChar const *range, int dim_type, char const *dim_name)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	/* force relative to A1, not the containing cell */
	GnmExprTop const *texpr;
	GnmParsePos pp;
	GnmValue *v;
	int dim;
	gboolean set_default_labels = FALSE;
	gboolean set_default_series_name = FALSE;

	if (NULL == state->chart.series)
		return;
	if (dim_type < 0)
		dim = - (1 + dim_type);
	else if (dim_name == NULL)
		dim = gog_series_map_dim (state->chart.series, dim_type);
	else
		dim = gog_series_map_dim_by_name (state->chart.series, dim_name);
	if (dim < -1)
		return;

	if (NULL != range) {
		GnmRangeRef ref;
		char const *ptr = oo_rangeref_parse (&ref, CXML2C (range),
			parse_pos_init_sheet (&pp, state->pos.sheet));
		if (ptr == CXML2C (range))
			return;
		v = value_new_cellrange (&ref.a, &ref.b, 0, 0);
#ifdef OO_DEBUG_OBJS
		g_print ("%d = rangeref (%s)\n", dim, range);
#endif
	} else if (NULL != gog_dataset_get_dim (GOG_DATASET (state->chart.series), dim))
		return;	/* implicit does not overwrite existing */
	else if (state->chart.src_n_vectors <= 0) {
		oo_warning (xin,
			"Not enough data in the supplied range for all the requests");
		return;
	} else {
		v = value_new_cellrange_r (
			   state->chart.src_sheet,
			   &state->chart.src_range);

#ifdef OO_DEBUG_OBJS
		g_print ("%d = implicit (%s)\n", dim, range_as_string (&state->chart.src_range));
#endif

		state->chart.src_n_vectors--;
		if (state->chart.src_in_rows)
			state->chart.src_range.end.row = ++state->chart.src_range.start.row;
		else
			state->chart.src_range.end.col = ++state->chart.src_range.start.col;
	       
		set_default_labels = state->chart.src_abscissa_set;
		set_default_series_name = state->chart.src_label_set;
	}

	texpr = gnm_expr_top_new_constant (v);
	if (NULL != texpr)
		gog_series_set_dim (state->chart.series, dim,
			(dim_type != GOG_MS_DIM_LABELS)
			? gnm_go_data_vector_new_expr (state->pos.sheet, texpr)
			: gnm_go_data_scalar_new_expr (state->pos.sheet, texpr),
			NULL);
	if (set_default_labels) {
		v = value_new_cellrange_r (state->chart.src_sheet,
					   &state->chart.src_abscissa);
		texpr = gnm_expr_top_new_constant (v);
		if (NULL != texpr)
			gog_series_set_dim (state->chart.series, GOG_DIM_LABEL,
					    gnm_go_data_vector_new_expr 
					    (state->pos.sheet, texpr),
					    NULL);
	}
	if (set_default_series_name) {
		v = value_new_cellrange_r (state->chart.src_sheet,
					   &state->chart.src_label);
		texpr = gnm_expr_top_new_constant (v);
		if (NULL != texpr)
			gog_series_set_name (state->chart.series,
					     GO_DATA_SCALAR (gnm_go_data_scalar_new_expr 
							     (state->pos.sheet, texpr)),
					    NULL);
		if (state->chart.src_in_rows)
			state->chart.src_label.end.row = ++state->chart.src_label.start.row;
		else
			state->chart.src_label.end.col = ++state->chart.src_label.start.col;
		
	}
}

static void
oo_plot_area (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const labels[] = {
		{ "both",		2 | 1 },
		{ "column",		2 },
		{ "row",		    1 },
		{ "none",		    0 },
		{ NULL,	0 },
	};

	OOParseState *state = (OOParseState *)xin->user_state;
	gchar const *type = NULL;
	OOChartStyle	*style = NULL;
	xmlChar const   *source_range_str = NULL;
	int label_flags = 0;
	GSList *l;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					OO_NS_CHART, "style-name"))
			state->chart.these_plot_styles = g_slist_append
				(state->chart.these_plot_styles,
				 g_hash_table_lookup
				 (state->chart.graph_styles, CXML2C (attrs[1])));
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "cell-range-address"))
			source_range_str = attrs[1];
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "data-source-has-labels", labels, &label_flags))
			;

	state->chart.src_n_vectors = -1;
	state->chart.src_in_rows = TRUE;
	state->chart.src_abscissa_set = FALSE;
	state->chart.src_label_set = FALSE;
	state->chart.series = NULL;
	state->chart.series_count = 0;
	state->chart.stock_series = NULL;
	if (NULL != source_range_str) {
		GnmParsePos pp;
		GnmEvalPos  ep;
		GnmRangeRef ref;
		Sheet	   *dummy;
		char const *ptr = oo_rangeref_parse (&ref, CXML2C (source_range_str),
			parse_pos_init_sheet (&pp, state->pos.sheet));
		if (ptr != CXML2C (source_range_str)) {
			gnm_rangeref_normalize (&ref,
				eval_pos_init_sheet (&ep, state->pos.sheet),
				&state->chart.src_sheet, &dummy,
				&state->chart.src_range);

			if (label_flags & 1)
				state->chart.src_range.start.row++;
			if (label_flags & 2)
				state->chart.src_range.start.col++;

			for (l = state->chart.these_plot_styles; l != NULL; l = l->next) {
				style = l->data;
				state->chart.src_in_rows = style->src_in_rows;
			}
			if (state->chart.src_in_rows) {
				state->chart.src_n_vectors = range_height (&state->chart.src_range);
				state->chart.src_range.end.row  = state->chart.src_range.start.row;
				if (label_flags & 1) {
					state->chart.src_abscissa = state->chart.src_range;
					state->chart.src_abscissa.end.row = --state->chart.src_abscissa.start.row;
					state->chart.src_abscissa_set = TRUE;
				}
				if (label_flags & 2) {
					state->chart.src_label = state->chart.src_range;
					state->chart.src_label.end.col = --state->chart.src_label.start.col;
					state->chart.src_label.end.row = state->chart.src_label.start.row;
					state->chart.src_label_set = TRUE;
				}
			} else {
				state->chart.src_n_vectors = range_width (&state->chart.src_range);
				state->chart.src_range.end.col  = state->chart.src_range.start.col;
				if (label_flags & 2) {
					state->chart.src_abscissa = state->chart.src_range;
					state->chart.src_abscissa.end.col = --state->chart.src_abscissa.start.col;
					state->chart.src_abscissa_set = TRUE;
				}
				if (label_flags & 1) {
					state->chart.src_label = state->chart.src_range;
					state->chart.src_label.end.row = --state->chart.src_label.start.row;
					state->chart.src_label.end.col = state->chart.src_label.start.col;
					state->chart.src_label_set = TRUE;
				}
			}
		}
	}

	switch (state->chart.plot_type) {
	case OO_PLOT_AREA:	type = "GogAreaPlot";	break;
	case OO_PLOT_BAR:	type = "GogBarColPlot";	break;
	case OO_PLOT_CIRCLE:	type = "GogPiePlot";	break;
	case OO_PLOT_LINE:	type = "GogLinePlot";	break;
	case OO_PLOT_RADAR:	type = "GogRadarPlot";	break;
	case OO_PLOT_RADARAREA: type = "GogRadarAreaPlot";break;
	case OO_PLOT_RING:	type = "GogRingPlot";	break;
	case OO_PLOT_SCATTER:	type = "GogXYPlot";	break;
	case OO_PLOT_STOCK:	type = "GogMinMaxPlot";	break;  /* This is not quite right! */
	case OO_PLOT_CONTOUR:
		if (oo_style_have_multi_series (state->chart.these_plot_styles)) {
			type = "XLSurfacePlot";
			state->chart.plot_type = OO_PLOT_XL_SURFACE;
		} else if (oo_style_have_three_dimensional (state->chart.these_plot_styles)) {
			type = "GogSurfacePlot";
			state->chart.plot_type = OO_PLOT_SURFACE;
		} else
			type = "GogContourPlot";
		break;
	case OO_PLOT_BUBBLE:	type = "GogBubblePlot"; break;
	case OO_PLOT_GANTT:	type = "GogDropBarPlot"; break;
	case OO_PLOT_POLAR:	type = "GogPolarPlot"; break;
	case OO_PLOT_XYZ_SURFACE:
		if (oo_style_have_three_dimensional (state->chart.these_plot_styles))
			type = "GogXYZSurfacePlot";
		else
			type = "GogXYZContourPlot";
		break;
	case OO_PLOT_SURFACE: type = "GogSurfacePlot"; break;
	case OO_PLOT_SCATTER_COLOUR: type = "GogXYColorPlot";	break;
	case OO_PLOT_XL_SURFACE: type = "XLSurfacePlot";	break;
	default: return;
	}

	state->chart.plot = gog_plot_new_by_name (type);
	gog_object_add_by_name (GOG_OBJECT (state->chart.chart),
		"Plot", GOG_OBJECT (state->chart.plot));
	for (l = state->chart.these_plot_styles; l != NULL; l = l->next) {
		style = l->data;
		oo_prop_list_apply (style->plot_props, G_OBJECT (state->chart.plot));
	}
	if (state->chart.plot_type == OO_PLOT_GANTT) {
		GogObject *yaxis = gog_object_get_child_by_name (GOG_OBJECT (state->chart.chart),
								 "Y-Axis");
		if (yaxis != NULL) {
			GValue *val = g_value_init (g_new0 (GValue, 1), G_TYPE_BOOLEAN);
			g_value_set_boolean (val, TRUE);
			g_object_set_property (G_OBJECT (yaxis), "invert-axis", val);
			g_value_unset (val);
			g_free (val);
		}
	}
}

static void
odf_create_stock_plot (GsfXMLIn *xin)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	GSList *series_addresses = state->chart.stock_series;
	int len = g_slist_length (series_addresses);

	if (len > 3) {
		series_addresses = series_addresses->next;
		len--;
	}

	if (len-- > 0) {
		state->chart.series = gog_plot_new_series (state->chart.plot);
		oo_plot_assign_dim (xin, series_addresses->data, GOG_MS_DIM_LOW, NULL);
	}
	if (len-- > 0) {
		series_addresses = series_addresses->next;
		oo_plot_assign_dim (xin, series_addresses->data, GOG_MS_DIM_HIGH, NULL);
	}
}

static void
oo_plot_area_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	if (state->chart.plot_type == OO_PLOT_STOCK) {
		odf_create_stock_plot (xin);
		go_slist_free_custom (state->chart.stock_series, g_free);
		state->chart.stock_series = NULL;
	} else if (state->chart.series != NULL) {
		oo_plot_assign_dim (xin, NULL, GOG_MS_DIM_VALUES, NULL);
		state->chart.series = NULL;
	}
	state->chart.plot = NULL;
	g_slist_free (state->chart.these_plot_styles);
	state->chart.these_plot_styles = NULL;
}


static void
oo_plot_series (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

#ifdef OO_DEBUG_OBJS
	g_print ("<<<<< Start\n");
#endif
	state->chart.series_count++;
	state->chart.domain_count = 0;

	switch (state->chart.plot_type) {
	case OO_PLOT_STOCK:
		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
			if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "values-cell-range-address"))
				state->chart.stock_series = g_slist_append (state->chart.stock_series,
									    g_strdup (attrs[1]));
		break;
	case OO_PLOT_SURFACE:
	case OO_PLOT_CONTOUR:
		state->chart.series = gog_plot_new_series (state->chart.plot);
		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
			if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "values-cell-range-address")) {
				GnmRangeRef ref;
				GnmValue *v;
				GnmExprTop const *texpr;
				GnmParsePos pp;
				char const *ptr = oo_rangeref_parse (&ref, CXML2C (attrs[1]),
								     parse_pos_init_sheet (&pp, state->pos.sheet));
				if (ptr == CXML2C (attrs[1]))
					return;
				v = value_new_cellrange (&ref.a, &ref.b, 0, 0);
				texpr = gnm_expr_top_new_constant (v);
				if (NULL != texpr)
					gog_series_set_dim (state->chart.series, 2,
							    gnm_go_data_matrix_new_expr (state->pos.sheet, texpr), NULL);
			}
	default:
		if (state->chart.series == NULL)
			state->chart.series = gog_plot_new_series (state->chart.plot);
		for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
			if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "values-cell-range-address")) {
				int dim;
				switch (state->chart.plot_type) {
				case OO_PLOT_GANTT:
					dim = (state->chart.series_count % 2 == 1) ? GOG_MS_DIM_START : GOG_MS_DIM_END;
					break;
				case OO_PLOT_BUBBLE:
					dim = GOG_MS_DIM_BUBBLES;
					break;
				case OO_PLOT_SCATTER_COLOUR:
					dim = GOG_MS_DIM_EXTRA1;
					break;
				default:
					dim = GOG_MS_DIM_VALUES;
					break;
				}
				oo_plot_assign_dim (xin, attrs[1], dim, NULL);
			} else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "label-cell-address"))
				oo_plot_assign_dim (xin, attrs[1], GOG_MS_DIM_LABELS, NULL);
		break;
	}
}

static void
oo_plot_series_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	switch (state->chart.plot_type) {
	case OO_PLOT_STOCK:
	case OO_PLOT_CONTOUR:
		break;
	case OO_PLOT_GANTT:
		if ((state->chart.series_count % 2) != 0)
			break;
		/* else no break */
	default:
		oo_plot_assign_dim (xin, NULL, GOG_MS_DIM_VALUES, NULL);
		state->chart.series = NULL;
		break;
	}
#ifdef OO_DEBUG_OBJS
	g_print (">>>>> end\n");
#endif
}

static void
oo_series_domain (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	xmlChar const *src = NULL;
	int dim = GOG_MS_DIM_VALUES;
	char const *name = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_TABLE, "cell-range-address"))
			src = attrs[1];
	switch (state->chart.plot_type) {
	case OO_PLOT_BUBBLE:
	case OO_PLOT_SCATTER_COLOUR:
		dim = (state->chart.domain_count == 0) ? GOG_MS_DIM_VALUES : GOG_MS_DIM_CATEGORIES;
		break;
	case OO_PLOT_XYZ_SURFACE:
	case OO_PLOT_SURFACE:
		name = (state->chart.domain_count == 0) ? "Y" : "X";
		break;
	case OO_PLOT_CONTOUR:
		dim = (state->chart.domain_count == 0) ? -1 : GOG_MS_DIM_CATEGORIES;
		break;
	default:
		dim = GOG_MS_DIM_CATEGORIES;
		break;
	}
	oo_plot_assign_dim (xin, src, dim, name);
	state->chart.domain_count++;
}

static void
oo_series_pt (GsfXMLIn *xin, xmlChar const **attrs)
{
#if 0
	OOParseState *state = (OOParseState *)xin->user_state;
	/* <chart:data-point chart:repeated="3"/> */
#endif
}

static void
oo_chart (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const types[] = {
		{ "chart:area",		OO_PLOT_AREA },
		{ "chart:bar",		OO_PLOT_BAR },
		{ "chart:circle",	OO_PLOT_CIRCLE },
		{ "chart:line",		OO_PLOT_LINE },
		{ "chart:radar",	OO_PLOT_RADAR },
		{ "chart:filled-radar",	OO_PLOT_RADARAREA },
		{ "chart:ring",		OO_PLOT_RING },
		{ "chart:scatter",	OO_PLOT_SCATTER },
		{ "chart:stock",	OO_PLOT_STOCK },
		{ "chart:bubble",	OO_PLOT_BUBBLE },
		{ "chart:gantt",	OO_PLOT_GANTT },
		{ "chart:surface",	OO_PLOT_CONTOUR },
		{ "gnm:polar",  	OO_PLOT_POLAR },
		{ "gnm:xyz-surface", 	OO_PLOT_XYZ_SURFACE },
		{ "gnm:scatter-color", 	OO_PLOT_SCATTER_COLOUR },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	int tmp;
	OOPlotType type = OO_PLOT_SCATTER; /* arbitrary default */
	OOChartStyle	*style = NULL;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_CHART, "class", types, &tmp))
			type = tmp;
		else if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]),
					     OO_NS_CHART, "style-name"))
			state->chart.these_plot_styles = g_slist_append
				(state->chart.these_plot_styles,
				 g_hash_table_lookup
				 (state->chart.graph_styles, CXML2C (attrs[1])));
	state->chart.plot_type = type;
	state->chart.chart = GOG_CHART (gog_object_add_by_name (
		GOG_OBJECT (state->chart.graph), "Chart", NULL));
	state->chart.plot = NULL;
	state->chart.series = NULL;
	state->chart.axis = NULL;
	if (NULL != style)
		state->chart.src_in_rows = style->src_in_rows;

	/* if (NULL != style) we also need to save the style for later use in oo_plot_area */

}

static void
oo_legend (GsfXMLIn *xin, xmlChar const **attrs)
{
	static OOEnum const positions [] = {
		{ "top",	  GOG_POSITION_N },
		{ "bottom",	  GOG_POSITION_S },
		{ "start",	  GOG_POSITION_W },
		{ "end",	  GOG_POSITION_E },
		{ "top-start",	  GOG_POSITION_N | GOG_POSITION_W },
		{ "bottom-start", GOG_POSITION_S | GOG_POSITION_W },
		{ "top-end",	  GOG_POSITION_N | GOG_POSITION_E },
		{ "bottom-end",   GOG_POSITION_S | GOG_POSITION_E },
		{ NULL,	0 },
	};
	static OOEnum const alignments [] = {
		{ "start",	  GOG_POSITION_ALIGN_START },
		{ "center",	  GOG_POSITION_ALIGN_CENTER },
		{ "end",	  GOG_POSITION_ALIGN_END },
		{ NULL,	0 },
	};
	OOParseState *state = (OOParseState *)xin->user_state;
	GogObjectPosition pos = GOG_POSITION_W | GOG_POSITION_ALIGN_CENTER;
	GogObjectPosition align = GOG_POSITION_ALIGN_CENTER;
	GogObject *legend;
	int tmp;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (oo_attr_enum (xin, attrs, OO_NS_CHART, "legend-position", positions, &tmp))
			pos = tmp;
		else if (oo_attr_enum (xin, attrs, OO_NS_CHART, "legend-align", alignments, &tmp))
			align = tmp;

	legend = gog_object_add_by_name ((GogObject *)state->chart.chart, "Legend", NULL);
	gog_object_set_position_flags (legend, pos | align,
		GOG_POSITION_COMPASS | GOG_POSITION_ALIGNMENT);
}

static void
oo_chart_grid (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	if (state->chart.axis == NULL)
		return;
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, CXML2C (attrs[0]), OO_NS_CHART, "class")) {
			if (attr_eq (attrs[1], "major"))
				gog_object_add_by_name (state->chart.axis, "MajorGrid", NULL);
			else if (attr_eq (attrs[1], "minor"))
				gog_object_add_by_name (state->chart.axis, "MinorGrid", NULL);
		}
}

static void
oo_chart_wall (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;
/* 	GOStyle *style = NULL; */
/* 	GogObject *backplane; */

	/* backplane =  */gog_object_add_by_name (GOG_OBJECT (state->chart.chart), "Backplane", NULL);

/* 	g_object_get (G_OBJECT (backplane), "style", &style, NULL); */
}

static void
oo_chart_style_free (OOChartStyle *cstyle)
{
	oo_prop_list_free (cstyle->axis_props);
	oo_prop_list_free (cstyle->plot_props);
	oo_prop_list_free (cstyle->other_props);
	g_free (cstyle);
}

static void
odf_annotation_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->cell_comment = cell_set_comment (state->pos.sheet, &state->pos.eval,
						NULL, NULL, NULL);
}

static void
odf_annotation_content_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;
	char const *old = cell_comment_text_get (state->cell_comment);
	char *new;

	if (old != NULL && strlen (old) > 0)
		new = g_strconcat (old, "\n", xin->content->str, NULL);
	else
		new = g_strdup (xin->content->str);
	cell_comment_text_set (state->cell_comment, new);
	g_free (new);
}

static void
odf_annotation_author_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	cell_comment_author_set (state->cell_comment, xin->content->str);
}

static void
odf_annotation_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	OOParseState *state = (OOParseState *)xin->user_state;

	state->cell_comment = NULL;
}

static GsfXMLInNode const styles_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),

/* ooo-1.x */
GSF_XML_IN_NODE (START, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", GSF_XML_NO_CONTENT, NULL, NULL),

/* ooo-2.x */
GSF_XML_IN_NODE (START, OFFICE_FONTS, OO_NS_OFFICE, "font-face-decls", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-face", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE (START, OFFICE_STYLES, OO_NS_OFFICE, "styles", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, MARKER, OO_NS_DRAW, "marker", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
    GSF_XML_IN_NODE (STYLE, TABLE_CELL_PROPS, OO_NS_STYLE,	"table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (STYLE, TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (STYLE, PARAGRAPH_PROPS, OO_NS_STYLE,	"paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (PARAGRAPH_PROPS, PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE,		"properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, DEFAULT_STYLE, OO_NS_STYLE, "default-style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_CELL_PROPS, OO_NS_STYLE, "table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TEXT_PROP, OO_NS_STYLE,	   "text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_GRAPHIC_PROPS, OO_NS_STYLE,	   "graphic-properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_PARAGRAPH_PROPS, OO_NS_STYLE,  "paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_PARAGRAPH_PROPS, DEFAULT_PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_STYLE_PROP, OO_NS_STYLE,	   "properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
      GSF_XML_IN_NODE (DEFAULT_STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_COL_PROPS, OO_NS_STYLE, "table-column-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
    GSF_XML_IN_NODE (DEFAULT_STYLE, DEFAULT_TABLE_ROW_PROPS, OO_NS_STYLE, "table-row-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
       GSF_XML_IN_NODE (NUMBER_STYLE_NUMBER, NUMBER_EMBEDDED_TEXT, OO_NS_NUMBER, "embedded-text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT, &odf_fraction, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, &odf_scientific, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	"properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,			"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
    GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,			"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,		"currency-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE,	"properties", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER,	"currency-symbol", GSF_XML_CONTENT, NULL, &odf_currency_symbol_end),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
    GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),

  GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

static GsfXMLInNode const ooo1_content_dtd [] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "script", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-decls", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_DECL, OO_NS_STYLE, "font-decl", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
      GSF_XML_IN_NODE (STYLE, STYLE_PROP, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
        GSF_XML_IN_NODE (STYLE_PROP, STYLE_TAB_STOPS, OO_NS_STYLE, "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	  "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	  "text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_PROP, OO_NS_STYLE,	  "properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		  "map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER, "time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER, "boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER, "boolean", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER, "currency-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER, "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE, "map", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER, "currency-symbol", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER, "text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER, "number", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER, "text", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER, "text-style", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,		"text", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

  GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, TABLE_CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TABLE_CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", GSF_XML_NO_CONTENT, oo_date_convention, NULL),
      GSF_XML_IN_NODE (TABLE_CALC_SETTINGS, ITERATION, OO_NS_TABLE, "iteration", GSF_XML_NO_CONTENT, oo_iteration, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, VALIDATIONS, OO_NS_TABLE, "content-validations", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (VALIDATIONS, VALIDATION, OO_NS_TABLE, "content-validation", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (VALIDATION, VALIDATION_MSG, OO_NS_TABLE, "error-message", GSF_XML_NO_CONTENT, NULL, NULL),

    GSF_XML_IN_NODE (OFFICE_BODY, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &oo_table_start, &oo_table_end),
      GSF_XML_IN_NODE (TABLE, FORMS,	 OO_NS_OFFICE, "forms", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, &oo_col_start, NULL),
      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &oo_row_start, &oo_row_end),
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &oo_cell_start, &oo_cell_end),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_CONTROL, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &oo_cell_content_end),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
	  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd def */
	    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd def */
	GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &oo_covered_cell_start, &oo_covered_cell_end),
      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
      GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
    GSF_XML_IN_NODE (OFFICE_BODY, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
      GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_RANGE, OO_NS_TABLE, "named-range", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
    GSF_XML_IN_NODE (OFFICE_BODY, DB_RANGES, OO_NS_TABLE, "database-ranges", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE (DB_RANGES, DB_RANGE, OO_NS_TABLE, "database-range", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (DB_RANGE, TABLE_SORT, OO_NS_TABLE, "sort", GSF_XML_NO_CONTENT, NULL, NULL),
          GSF_XML_IN_NODE (TABLE_SORT, SORT_BY, OO_NS_TABLE, "sort-by", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

/****************************************************************************/

typedef GValue		OOConfigItem;
typedef GHashTable	OOConfigItemSet;
typedef GHashTable	OOConfigItemMapNamed;
typedef GPtrArray	OOConfigItemMapIndexed;

#if 0
static GHashTable *
oo_config_item_set ()
{
	return NULL;
}
#endif

static GsfXMLInNode const opencalc_settings_dtd [] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-settings", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (OFFICE, SETTINGS, OO_NS_OFFICE, "settings", GSF_XML_NO_CONTENT, NULL, NULL),
    GSF_XML_IN_NODE (SETTINGS, CONFIG_ITEM_SET, OO_NS_CONFIG, "config-item-set", GSF_XML_NO_CONTENT, NULL, NULL),
      GSF_XML_IN_NODE_FULL (CONFIG_ITEM_SET, CONFIG_ITEM,		OO_NS_CONFIG, "config-item",		GSF_XML_NO_CONTENT,  TRUE, FALSE, NULL, NULL, 0),
      GSF_XML_IN_NODE_FULL (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_INDEXED,	OO_NS_CONFIG, "config-item-map-indexed", GSF_XML_NO_CONTENT, TRUE, FALSE, NULL, NULL, 1),
      GSF_XML_IN_NODE_FULL (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_ENTRY,	OO_NS_CONFIG, "config-item-map-entry",	GSF_XML_NO_CONTENT,  TRUE, FALSE, NULL, NULL, 2),
      GSF_XML_IN_NODE_FULL (CONFIG_ITEM_SET, CONFIG_ITEM_MAP_NAMED,	OO_NS_CONFIG, "config-item-map-named",	GSF_XML_NO_CONTENT,  TRUE, FALSE, NULL, NULL, 3),

GSF_XML_IN_NODE_END
};

/****************************************************************************/
/* Generated based on:
 * http://www.oasis-open.org/committees/download.php/12572/OpenDocument-v1.0-os.pdf */
static GsfXMLInNode const opendoc_content_dtd [] =
{
	GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
	GSF_XML_IN_NODE (START, OFFICE, OO_NS_OFFICE, "document-content", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE, SCRIPT, OO_NS_OFFICE, "scripts", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE, OFFICE_FONTS, OO_NS_OFFICE, "font-face-decls", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_FONTS, FONT_FACE, OO_NS_STYLE, "font-face", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE, OFFICE_STYLES, OO_NS_OFFICE, "automatic-styles", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE, OO_NS_STYLE, "style", GSF_XML_NO_CONTENT, &oo_style, &oo_style_end),
	      GSF_XML_IN_NODE (STYLE, TABLE_CELL_PROPS, OO_NS_STYLE, "table-cell-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_COL_PROPS, OO_NS_STYLE, "table-column-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_ROW_PROPS, OO_NS_STYLE, "table-row-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, CHART_PROPS, OO_NS_STYLE, "chart-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TEXT_PROPS, OO_NS_STYLE, "text-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, TABLE_PROPS, OO_NS_STYLE, "table-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, PARAGRAPH_PROPS, OO_NS_STYLE, "paragraph-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	        GSF_XML_IN_NODE (PARAGRAPH_PROPS, PARA_TABS, OO_NS_STYLE,  "tab-stops", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE, GRAPHIC_PROPS, OO_NS_STYLE, "graphic-properties", GSF_XML_NO_CONTENT, &oo_style_prop, NULL),
	      GSF_XML_IN_NODE (STYLE, STYLE_MAP, OO_NS_STYLE, "map", GSF_XML_NO_CONTENT, &oo_style_map, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, NUMBER_STYLE, OO_NS_NUMBER, "number-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_NUMBER, OO_NS_NUMBER,	  "number", GSF_XML_NO_CONTENT, &odf_number, NULL),
                 GSF_XML_IN_NODE (NUMBER_STYLE_NUMBER, NUMBER_EMBEDDED_TEXT, OO_NS_NUMBER, "embedded-text", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_TEXT, OO_NS_NUMBER,	  "text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_STYLE_FRACTION, OO_NS_NUMBER, "fraction", GSF_XML_NO_CONTENT,  &odf_fraction, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_SCI_STYLE_PROP, OO_NS_NUMBER, "scientific-number", GSF_XML_NO_CONTENT, &odf_scientific, NULL),
	      GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_MAP, OO_NS_STYLE,		  "map", GSF_XML_NO_CONTENT, &odf_map, NULL),
              GSF_XML_IN_NODE (NUMBER_STYLE, NUMBER_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, DATE_STYLE, OO_NS_NUMBER, "date-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY, OO_NS_NUMBER,		"day", GSF_XML_NO_CONTENT,	&oo_date_day, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MONTH, OO_NS_NUMBER,		"month", GSF_XML_NO_CONTENT,	&oo_date_month, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_YEAR, OO_NS_NUMBER,		"year", GSF_XML_NO_CONTENT,	&oo_date_year, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_ERA, OO_NS_NUMBER,		"era", GSF_XML_NO_CONTENT,	&oo_date_era, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_DAY_OF_WEEK, OO_NS_NUMBER,	"day-of-week", GSF_XML_NO_CONTENT, &oo_date_day_of_week, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_WEEK_OF_YEAR, OO_NS_NUMBER,	"week-of-year", GSF_XML_NO_CONTENT, &oo_date_week_of_year, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_QUARTER, OO_NS_NUMBER,		"quarter", GSF_XML_NO_CONTENT, &oo_date_quarter, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_HOURS, OO_NS_NUMBER,		"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MINUTES, OO_NS_NUMBER,		"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_SECONDS, OO_NS_NUMBER,		"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_AM_PM, OO_NS_NUMBER,		"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_TEXT_PROP, OO_NS_STYLE,		"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	      GSF_XML_IN_NODE (DATE_STYLE, DATE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, TIME_STYLE, OO_NS_NUMBER,	"time-style", GSF_XML_NO_CONTENT, &oo_date_style, &oo_date_style_end),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_HOURS, OO_NS_NUMBER,	"hours", GSF_XML_NO_CONTENT,	&oo_date_hours, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_MINUTES, OO_NS_NUMBER,	"minutes", GSF_XML_NO_CONTENT, &oo_date_minutes, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_SECONDS, OO_NS_NUMBER,	"seconds", GSF_XML_NO_CONTENT, &oo_date_seconds, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_AM_PM, OO_NS_NUMBER,	"am-pm", GSF_XML_NO_CONTENT,	&oo_date_am_pm, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT,	NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	      GSF_XML_IN_NODE (TIME_STYLE, TIME_MAP, OO_NS_STYLE,	"map", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_BOOL, OO_NS_NUMBER,	"boolean-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_BOOL, BOOL_PROP, OO_NS_NUMBER,	"boolean", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_CURRENCY, OO_NS_NUMBER,      	"currency-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_STYLE_PROP, OO_NS_STYLE,"properties", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_MAP, OO_NS_STYLE,	"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_SYMBOL, OO_NS_NUMBER,	"currency-symbol", GSF_XML_CONTENT, NULL, &odf_currency_symbol_end),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT, OO_NS_NUMBER,	"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (STYLE_CURRENCY, CURRENCY_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_PERCENTAGE, OO_NS_NUMBER, "percentage-style", GSF_XML_NO_CONTENT, &odf_number_style, &odf_number_style_end),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_STYLE_PROP, OO_NS_NUMBER,	"number", GSF_XML_NO_CONTENT, &odf_number, NULL),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT, OO_NS_NUMBER,		"text", GSF_XML_CONTENT, NULL, &oo_date_text_end),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, &odf_map, NULL),
	      GSF_XML_IN_NODE (STYLE_PERCENTAGE, PERCENTAGE_TEXT_PROP, OO_NS_STYLE,	"text-properties", GSF_XML_NO_CONTENT, &odf_number_color, NULL),
	    GSF_XML_IN_NODE (OFFICE_STYLES, STYLE_TEXT, OO_NS_NUMBER,		"text-style", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_CONTENT, OO_NS_NUMBER,	"text-content", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_PROP, OO_NS_NUMBER,	"text", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (STYLE_TEXT, STYLE_TEXT_MAP, OO_NS_STYLE,		"map", GSF_XML_NO_CONTENT, NULL, NULL),

	GSF_XML_IN_NODE (OFFICE, OFFICE_BODY, OO_NS_OFFICE, "body", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE_BODY, SPREADSHEET, OO_NS_OFFICE, "spreadsheet", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, CONTENT_VALIDATIONS, OO_NS_TABLE, "content-validations", GSF_XML_NO_CONTENT, NULL, NULL),
 	      GSF_XML_IN_NODE (CONTENT_VALIDATIONS, CONTENT_VALIDATION, OO_NS_TABLE, "content-validation", GSF_XML_NO_CONTENT, NULL, NULL),
 	        GSF_XML_IN_NODE (CONTENT_VALIDATION, ERROR_MESSAGE, OO_NS_TABLE, "error-message", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (ERROR_MESSAGE, ERROR_MESSAGE_P, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (ERROR_MESSAGE_P, ERROR_MESSAGE_P_S, OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, CALC_SETTINGS, OO_NS_TABLE, "calculation-settings", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (CALC_SETTINGS, ITERATION, OO_NS_TABLE, "iteration", GSF_XML_NO_CONTENT, oo_iteration, NULL),
	      GSF_XML_IN_NODE (CALC_SETTINGS, DATE_CONVENTION, OO_NS_TABLE, "null-date", GSF_XML_NO_CONTENT, oo_date_convention, NULL),
	    GSF_XML_IN_NODE (SPREADSHEET, CHART, OO_NS_CHART, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE (OFFICE_BODY, OFFICE_CHART, OO_NS_OFFICE, "chart", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (OFFICE_CHART, CHART_CHART, OO_NS_CHART, "chart", GSF_XML_NO_CONTENT, &oo_chart, NULL),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_ROWS, OO_NS_TABLE, "table-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_ROWS, CHART_TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (CHART_TABLE_ROW, CHART_TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, NULL, NULL),
	              GSF_XML_IN_NODE (CHART_TABLE_CELL, CHART_CELL_P, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_COLS, OO_NS_TABLE, "table-columns", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_COLS, CHART_TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_HROWS, OO_NS_TABLE, "table-header-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HROWS, CHART_TABLE_HROW, OO_NS_TABLE, "table-header-row", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HROWS, CHART_TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */
	        GSF_XML_IN_NODE (CHART_TABLE, CHART_TABLE_HCOLS, OO_NS_TABLE, "table-header-columns", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HCOLS, CHART_TABLE_HCOL, OO_NS_TABLE, "table-header-column", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (CHART_TABLE_HCOLS, CHART_TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd Def */

	      GSF_XML_IN_NODE (CHART_CHART, CHART_TITLE, OO_NS_CHART, "title", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (CHART_TITLE, TITLE_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &oo_chart_title),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_LEGEND, OO_NS_CHART, "legend", GSF_XML_NO_CONTENT, &oo_legend, NULL),
	      GSF_XML_IN_NODE (CHART_CHART, CHART_PLOT_AREA, OO_NS_CHART, "plot-area", GSF_XML_NO_CONTENT, &oo_plot_area, &oo_plot_area_end),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_SERIES, OO_NS_CHART, "series", GSF_XML_NO_CONTENT, &oo_plot_series, &oo_plot_series_end),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DOMAIN, OO_NS_CHART, "domain", GSF_XML_NO_CONTENT, &oo_series_domain, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DATA_PT, OO_NS_CHART, "data-point", GSF_XML_NO_CONTENT, &oo_series_pt, NULL),
		  GSF_XML_IN_NODE (CHART_SERIES, SERIES_DATA_ERR, OO_NS_CHART, "error-indicator", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_WALL, OO_NS_CHART, "wall", GSF_XML_NO_CONTENT, &oo_chart_wall, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_FLOOR, OO_NS_CHART, "floor", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (CHART_PLOT_AREA, CHART_AXIS, OO_NS_CHART, "axis", GSF_XML_NO_CONTENT, &oo_chart_axis, NULL),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_GRID, OO_NS_CHART, "grid", GSF_XML_NO_CONTENT, &oo_chart_grid, NULL),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_AXIS_CAT,   OO_NS_CHART, "categories", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (CHART_AXIS, CHART_TITLE, OO_NS_CHART, "title", GSF_XML_NO_CONTENT, NULL, NULL),				/* 2nd Def */

	    GSF_XML_IN_NODE (SPREADSHEET, TABLE, OO_NS_TABLE, "table", GSF_XML_NO_CONTENT, &oo_table_start, &oo_table_end),
	      GSF_XML_IN_NODE (TABLE, FORMS, OO_NS_OFFICE, "forms", GSF_XML_NO_CONTENT, NULL, NULL),
	        GSF_XML_IN_NODE (FORMS, FORM, OO_NS_FORM, "form", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (FORM_PROPERTIES, FORM_PROPERTY, OO_NS_FORM, "property", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (FORM, FORM_BUTTON, OO_NS_FORM, "button", GSF_XML_NO_CONTENT, NULL, NULL),
	            GSF_XML_IN_NODE (FORM_BUTTON, FORM_PROPERTIES, OO_NS_FORM, "properties", GSF_XML_NO_CONTENT, NULL, NULL),			/* 2nd Def */
	            GSF_XML_IN_NODE (FORM_BUTTON, FORM_EVENT_LISTENERS, OO_NS_OFFICE, "event-listeners", GSF_XML_NO_CONTENT, NULL, NULL),
	              GSF_XML_IN_NODE (FORM_EVENT_LISTENERS, SCRIPT_LISTENER, OO_NS_SCRIPT, "event-listener", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_ROWS, OO_NS_TABLE, "table-rows", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, &oo_col_start, NULL),
	      GSF_XML_IN_NODE (TABLE, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, &oo_row_start, &oo_row_end),
	      GSF_XML_IN_NODE (TABLE, SOFTPAGEBREAK, OO_NS_TEXT, "soft-page-break", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROWS, TABLE_ROW, OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
	      GSF_XML_IN_NODE (TABLE_ROWS, SOFTPAGEBREAK, OO_NS_TEXT, "soft-page-break", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */

		GSF_XML_IN_NODE (TABLE_ROW, TABLE_CELL, OO_NS_TABLE, "table-cell", GSF_XML_NO_CONTENT, &oo_cell_start, &oo_cell_end),
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &oo_cell_content_end),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_ADDR, OO_NS_TEXT, "a", GSF_XML_SHARED_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		    GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		    GSF_XML_IN_NODE (CELL_TEXT, CELL_TEXT_LINE_BREAK,    OO_NS_TEXT, "line-break", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_LINE_BREAK,    OO_NS_TEXT, "line-break", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		      GSF_XML_IN_NODE (CELL_TEXT_SPAN, CELL_TEXT_SPAN_ADDR, OO_NS_TEXT, "a", GSF_XML_SHARED_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, NULL, NULL),		/* ignore for now */
		  GSF_XML_IN_NODE (TABLE_CELL, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),			/* ignore for now */
		    GSF_XML_IN_NODE (CELL_GRAPHIC, CELL_GRAPHIC, OO_NS_DRAW, "g", GSF_XML_NO_CONTENT, NULL, NULL),		/* 2nd def */
		    GSF_XML_IN_NODE (CELL_GRAPHIC, DRAW_POLYLINE, OO_NS_DRAW, "polyline", GSF_XML_NO_CONTENT, NULL, NULL),	/* 2nd def */
	          GSF_XML_IN_NODE (TABLE_CELL, DRAW_CONTROL, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (TABLE_CELL, DRAW_FRAME, OO_NS_DRAW, "frame", GSF_XML_NO_CONTENT, &od_draw_frame, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, DRAW_OBJECT, OO_NS_DRAW, "object", GSF_XML_NO_CONTENT, &od_draw_object, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, DRAW_IMAGE, OO_NS_DRAW, "image", GSF_XML_NO_CONTENT, &od_draw_image, NULL),
		    GSF_XML_IN_NODE (DRAW_FRAME, SVG_DESC, OO_NS_SVG, "desc", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_CELL, CELL_ANNOTATION, OO_NS_OFFICE, "annotation", GSF_XML_NO_CONTENT, &odf_annotation_start, &odf_annotation_end),
	            GSF_XML_IN_NODE (CELL_ANNOTATION, CELL_ANNOTATION_TEXT, OO_NS_TEXT, "p", GSF_XML_CONTENT, NULL, &odf_annotation_content_end),
  		      GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT, CELL_ANNOTATION_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
		      GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT, CELL_ANNOTATION_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_SHARED_CONTENT, NULL, NULL),
		        GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT_SPAN, CELL_ANNOTATION_TEXT_SPAN, OO_NS_TEXT, "span", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
		        GSF_XML_IN_NODE (CELL_ANNOTATION_TEXT_SPAN, CELL_ANNOTATION_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),/* 2nd def */
	            GSF_XML_IN_NODE (CELL_ANNOTATION, CELL_ANNOTATION_AUTHOR, OO_NS_DC, "creator", GSF_XML_CONTENT, NULL, &odf_annotation_author_end),
	            GSF_XML_IN_NODE (CELL_ANNOTATION, CELL_ANNOTATION_DATE, OO_NS_DC, "date", GSF_XML_NO_CONTENT, NULL, NULL),

		GSF_XML_IN_NODE (TABLE_ROW, TABLE_COVERED_CELL, OO_NS_TABLE, "covered-table-cell", GSF_XML_NO_CONTENT, &oo_covered_cell_start, &oo_covered_cell_end),
		  GSF_XML_IN_NODE (TABLE_COVERED_CELL, COVERED_CELL_TEXT, OO_NS_TEXT, "p", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (COVERED_CELL_TEXT, COVERED_CELL_TEXT_S,    OO_NS_TEXT, "s", GSF_XML_NO_CONTENT, NULL, NULL),
	          GSF_XML_IN_NODE (TABLE_COVERED_CELL, DRAW_CONTROL, OO_NS_DRAW, "control", GSF_XML_NO_CONTENT, NULL, NULL),

	      GSF_XML_IN_NODE (TABLE, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL_GROUP, OO_NS_TABLE, "table-column-group", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (TABLE_COL_GROUP, TABLE_COL, OO_NS_TABLE, "table-column", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */
	      GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW_GROUP, OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (TABLE, TABLE_ROW_GROUP,	      OO_NS_TABLE, "table-row-group", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_ROW_GROUP, TABLE_ROW,	    OO_NS_TABLE, "table-row", GSF_XML_NO_CONTENT, NULL, NULL), /* 2nd def */

	  GSF_XML_IN_NODE (SPREADSHEET, NAMED_EXPRS, OO_NS_TABLE, "named-expressions", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_EXPR, OO_NS_TABLE, "named-expression", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),
	    GSF_XML_IN_NODE (NAMED_EXPRS, NAMED_RANGE, OO_NS_TABLE, "named-range", GSF_XML_NO_CONTENT, &oo_named_expr, NULL),

	  GSF_XML_IN_NODE (SPREADSHEET, DB_RANGES, OO_NS_TABLE, "database-ranges", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (DB_RANGES, DB_RANGE, OO_NS_TABLE, "database-range", GSF_XML_NO_CONTENT, &oo_db_range_start, &oo_db_range_end),
	      GSF_XML_IN_NODE (DB_RANGE, FILTER, OO_NS_TABLE, "filter", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (FILTER, FILTER_COND, OO_NS_TABLE, "filter-condition", GSF_XML_NO_CONTENT, &oo_filter_cond, NULL),
	    GSF_XML_IN_NODE (DB_RANGE, TABLE_SORT, OO_NS_TABLE, "sort", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (TABLE_SORT, SORT_BY, OO_NS_TABLE, "sort-by", GSF_XML_NO_CONTENT, NULL, NULL),

GSF_XML_IN_NODE_END
};

static GsfXMLInNode const *get_dtd () { return opendoc_content_dtd; }

/****************************************************************************/

static GnmExpr const *
odf_func_address_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);

	if (argc == 4 && convs->sheet_name_sep == '!') {
		/* Openoffice was missing the A1 parameter */
		GnmExprList *new_args;
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("ADDRESS", scope, FALSE);

		new_args = g_slist_insert ((GSList *) args,
					   (gpointer) gnm_expr_new_constant (value_new_int (1)),
					   3);
		return gnm_expr_new_funcall (f, new_args);
	}
	return NULL;
}

static GnmExpr const *
odf_func_phi_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("NORMDIST", scope, FALSE);

	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_constant (value_new_int (0)));
	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_constant (value_new_int (1)));

	args = g_slist_append (args,
			       (gpointer) gnm_expr_new_funcall
			       (gnm_func_lookup_or_add_placeholder ("FALSE", scope, FALSE), NULL));

	return gnm_expr_new_funcall (f, args);
}

static GnmExpr const *
odf_func_gauss_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);
	GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("ERF", scope, FALSE);
	GnmFunc  *fs = gnm_func_lookup_or_add_placeholder ("SQRT", scope, FALSE);
	GnmExpr const * expr;

	if (argc != 1)
		return NULL;

	expr = gnm_expr_new_binary (gnm_expr_new_funcall1
				    (f, gnm_expr_new_binary ((gnm_expr_copy ((GnmExpr const *)(args->data))),
							     GNM_EXPR_OP_DIV,
									     gnm_expr_new_funcall1 (fs,
												    gnm_expr_new_constant
												    (value_new_int (2))))),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_constant (value_new_int (2)));
	gnm_expr_list_unref (args);
	return expr;
}

static GnmExpr const *
odf_func_floor_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);
	GnmExpr const *expr_x;
	GnmExpr const *expr_sig;
	GnmExpr const *expr_mode;
	GnmExpr const *expr_mode_zero;
	GnmExpr const *expr_mode_one;
	GnmExpr const *expr_if;
	GnmFunc  *fd_ceiling;
	GnmFunc  *fd_floor;
	GnmFunc  *fd_if;

	if (argc == 0 || argc > 3)
		return NULL;

	fd_ceiling = gnm_func_lookup_or_add_placeholder ("CEILING", scope, FALSE);
	fd_floor = gnm_func_lookup_or_add_placeholder ("FLOOR", scope, FALSE);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF", scope, FALSE);

	expr_x = g_slist_nth_data ((GSList *) args, 0);
	if (argc > 1)
		expr_sig = gnm_expr_copy (g_slist_nth_data ((GSList *) args, 1));
	else {
		GnmFunc  *fd_sign = gnm_func_lookup_or_add_placeholder ("SIGN", scope, FALSE);
		expr_sig = gnm_expr_new_funcall1 (fd_sign, gnm_expr_copy (expr_x));
	}

	expr_mode_zero = gnm_expr_new_funcall3
		(fd_if,
		 gnm_expr_new_binary
		 (gnm_expr_copy (expr_x),
		  GNM_EXPR_OP_LT,
		  gnm_expr_new_constant (value_new_int (0))),
		 gnm_expr_new_funcall2
		 (fd_ceiling,
		  gnm_expr_copy (expr_x),
		  gnm_expr_copy (expr_sig)),
		 gnm_expr_new_funcall2
		 (fd_floor,
		  gnm_expr_copy (expr_x),
		  gnm_expr_copy (expr_sig)));
	if (argc < 3) {
		gnm_expr_free (expr_sig);
		gnm_expr_list_unref (args);
		return expr_mode_zero;
	}

	expr_mode_one =
		gnm_expr_new_funcall2
		(fd_floor,
		 gnm_expr_copy (expr_x),
		 gnm_expr_copy (expr_sig));

	expr_mode = g_slist_nth_data ((GSList *) args, 2);
	if (GNM_EXPR_GET_OPER (expr_mode) == GNM_EXPR_OP_CONSTANT) {
		GnmValue const * val = expr_mode->constant.value;
		if (VALUE_IS_NUMBER (val)) {
			gnm_float value = value_get_as_float (val);
			if (value == 0.) {
				gnm_expr_free (expr_mode_one);
				gnm_expr_list_unref (args);
				gnm_expr_free (expr_sig);
				return expr_mode_zero;
			} else {
				gnm_expr_free (expr_mode_zero);
				gnm_expr_list_unref (args);
				gnm_expr_free (expr_sig);
				return expr_mode_one;
			}
		}
	}
	expr_if = gnm_expr_new_funcall3
		(fd_if,
		 gnm_expr_new_binary
		 (gnm_expr_new_constant (value_new_int (0)),
		  GNM_EXPR_OP_EQUAL,
		  gnm_expr_copy (expr_mode)),
		 expr_mode_zero,
		 expr_mode_one);

	gnm_expr_free (expr_sig);
	gnm_expr_list_unref (args);
	return expr_if;
}

static GnmExpr const *
odf_func_ceiling_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	guint argc = gnm_expr_list_length (args);
	switch (argc) {
	case 1: {
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("CEIL", scope, FALSE);
		return gnm_expr_new_funcall (f, args);
	}
	case 2: case 3: {
		GnmExpr const *expr_mode_zero;
		GnmExpr const *expr_mode_one;
		GnmExpr const *expr_if;
		GnmExpr const *expr_mode;
		GnmExpr const *expr_x = g_slist_nth_data ((GSList *) args, 0);
		GnmExpr const *expr_sig = g_slist_nth_data ((GSList *) args, 1);

		GnmFunc  *fd_ceiling = gnm_func_lookup_or_add_placeholder ("CEILING", scope, FALSE);
		GnmFunc  *fd_floor = gnm_func_lookup_or_add_placeholder ("FLOOR", scope, FALSE);
		GnmFunc  *fd_if = gnm_func_lookup_or_add_placeholder ("IF", scope, FALSE);

		expr_mode_zero = gnm_expr_new_funcall3
			(fd_if,
			 gnm_expr_new_binary
			 (gnm_expr_copy (expr_x),
			  GNM_EXPR_OP_LT,
			  gnm_expr_new_constant (value_new_int (0))),
			 gnm_expr_new_funcall2
			 (fd_floor,
			  gnm_expr_copy (expr_x),
			  gnm_expr_copy (expr_sig)),
			 gnm_expr_new_funcall2
			 (fd_ceiling,
			  gnm_expr_copy (expr_x),
			  gnm_expr_copy (expr_sig)));
		if (argc == 2) {
			gnm_expr_list_unref (args);
			return expr_mode_zero;
		}

		expr_mode_one =
			gnm_expr_new_funcall2
			(fd_ceiling,
			 gnm_expr_copy (expr_x),
			 gnm_expr_copy (expr_sig));

		expr_mode = g_slist_nth_data ((GSList *) args, 2);
		if (GNM_EXPR_GET_OPER (expr_mode) == GNM_EXPR_OP_CONSTANT) {
			GnmValue const * val = expr_mode->constant.value;
			if (VALUE_IS_NUMBER (val)) {
				gnm_float value = value_get_as_float (val);
				if (value == 0.) {
					gnm_expr_free (expr_mode_one);
					gnm_expr_list_unref (args);
					return expr_mode_zero;
				} else {
					gnm_expr_free (expr_mode_zero);
					gnm_expr_list_unref (args);
					return expr_mode_one;
				}
			}
		}
		expr_if = gnm_expr_new_funcall3
			(fd_if,
			 gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (0)),
			  GNM_EXPR_OP_EQUAL,
			  gnm_expr_copy (expr_mode)),
			 expr_mode_zero,
			 expr_mode_one);
		gnm_expr_list_unref (args);
		return expr_if;
	}
	default:
		break;
	}
	return NULL;
}

static GnmExpr const *
odf_func_chisqdist_handler (GnmConventions const *convs, Workbook *scope, GnmExprList *args)
{
	switch (gnm_expr_list_length (args)) {
	case 2: {
		GnmFunc  *f = gnm_func_lookup_or_add_placeholder ("R.PCHISQ", scope, FALSE);
		return gnm_expr_new_funcall (f, args);
	}
	case 3: {
		GSList * link = g_slist_nth ((GSList *) args, 2);
		GnmExpr const *expr = link->data;
		GnmFunc  *fd_if;
		GnmFunc  *fd_pchisq;
		GnmFunc  *fd_dchisq;
		GnmExpr  const *expr_pchisq;
		GnmExpr  const *expr_dchisq;

		args = (GnmExprList *) g_slist_remove_link ((GSList *) args, link);
		g_slist_free (link);

		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL) {
			if (go_ascii_strcase_equal (expr->func.func->name, "TRUE")) {
				fd_pchisq = gnm_func_lookup_or_add_placeholder ("R.PCHISQ", scope, FALSE);
				gnm_expr_free (expr);
				return gnm_expr_new_funcall (fd_pchisq, args);
			}
			if (go_ascii_strcase_equal (expr->func.func->name, "FALSE")) {
				fd_dchisq = gnm_func_lookup_or_add_placeholder ("R.DCHISQ", scope, FALSE);
				gnm_expr_free (expr);
				return gnm_expr_new_funcall (fd_dchisq, args);
			}
		}
		fd_if = gnm_func_lookup_or_add_placeholder ("IF", scope, FALSE);
		fd_pchisq = gnm_func_lookup_or_add_placeholder ("R.PCHISQ", scope, FALSE);
		fd_dchisq = gnm_func_lookup_or_add_placeholder ("R.DCHISQ", scope, FALSE);
		expr_pchisq = gnm_expr_new_funcall2
			(fd_pchisq, gnm_expr_copy (g_slist_nth_data ((GSList *) args, 0)),
			 gnm_expr_copy (g_slist_nth_data ((GSList *) args, 1)));
		expr_dchisq = gnm_expr_new_funcall (fd_dchisq, args);
		return gnm_expr_new_funcall3 (fd_if, expr, expr_pchisq, expr_dchisq);
	}
	default:
		break;
	}
	return NULL;
}


static GnmExpr const *
oo_func_map_in (GnmConventions const *convs, Workbook *scope,
		 char const *name, GnmExprList *args)
{
	static struct {
		char const *gnm_name;
		gpointer handler;
	} const sc_func_handlers[] = {
		{"CHISQDIST", odf_func_chisqdist_handler},
		{"CEILING", odf_func_ceiling_handler},
		{"FLOOR", odf_func_floor_handler},
		{"ADDRESS", odf_func_address_handler},
		{"PHI", odf_func_phi_handler},
		{"GAUSS", odf_func_gauss_handler},
		{NULL, NULL}
	};

	static struct {
		char const *oo_name;
		char const *gnm_name;
	} const sc_func_renames[] = {
/* The next functions are or were used by OpenOffice but are not in ODF OpenFormula draft 20090508 */

		{ "INDIRECT_XL",	"INDIRECT" },
		{ "ADDRESS_XL",		"ADDRESS" },
		{ "ERRORTYPE",		"ERROR.TYPE" },
		{ "EASTERSUNDAY",	"EASTERSUNDAY" }, /* OOo stores this without prefix! */
		{ "ORG.OPENOFFICE.EASTERSUNDAY",	"EASTERSUNDAY" },

/* The following is a list of the functions defined in ODF OpenFormula draft 20090508 */
/* where we do not have a function with the same name                                 */

		{ "AVERAGEIFS","ODF.AVERAGEIFS" },
		{ "COUNTIFS","ODF.COUNTIFS" },
		{ "DDE","ODF.DDE" },
		{ "MULTIPLE.OPERATIONS","ODF.MULTIPLE.OPERATIONS" },
		{ "SUMIFS","ODF.SUMIFS" },

/* The following is a complete list of the functions defined in ODF OpenFormula draft 20090508 */
/* We should determine whether any mapping is needed. */

		{ "B","BINOM.DIST.RANGE" },
		{ "CEILING","ODF.CEILING" },          /* see handler */
		{ "CHISQINV","R.QCHISQ" },
		{ "CHISQDIST","ODF.CHISQDIST" },      /* see handler */
		{ "FLOOR","ODF.FLOOR" },              /* see handler */
		{ "FORMULA","GET.FORMULA" },
		{ "GAUSS","ODF.GAUSS" },              /* see handler */
		{ "LEGACY.CHIDIST","CHIDIST" },
		{ "LEGACY.CHIINV","CHIINV" },
		{ "LEGACY.CHITEST","CHITEST" },
		{ "LEGACY.FDIST","FDIST" },
		{ "LEGACY.FINV","FINV" },
		{ "LEGACY.NORMSDIST","NORMSDIST" },
		{ "LEGACY.NORMSINV","NORMSINV" },
		{ "PDURATION","G_DURATION" },
		{ "PHI","NORMDIST" },              /* see handler */
		{ "USDOLLAR","DOLLAR" },

/* { "ADDRESS","ADDRESS" },       also  see handler */
/* { "ABS","ABS" }, */
/* { "ACCRINT","ACCRINT" }, */
/* { "ACCRINTM","ACCRINTM" }, */
/* { "ACOS","ACOS" }, */
/* { "ACOSH","ACOSH" }, */
/* { "ACOT","ACOT" }, */
/* { "ACOTH","ACOTH" }, */
/* { "AMORDEGRC","AMORDEGRC" }, */
/* { "AMORLINC","AMORLINC" }, */
/* { "AND","AND" }, */
/* { "ARABIC","ARABIC" }, */
/* { "AREAS","AREAS" }, */
/* { "ASC","ASC" }, */
/* { "ASIN","ASIN" }, */
/* { "ASINH","ASINH" }, */
/* { "ATAN","ATAN" }, */
/* { "ATAN2","ATAN2" }, */
/* { "ATANH","ATANH" }, */
/* { "AVEDEV","AVEDEV" }, */
/* { "AVERAGE","AVERAGE" }, */
/* { "AVERAGEA","AVERAGEA" }, */
/* { "AVERAGEIF","AVERAGEIF" }, */
/* { "AVERAGEIFS","AVERAGEIFS" }, */
/* { "BASE","BASE" }, */
/* { "BESSELI","BESSELI" }, */
/* { "BESSELJ","BESSELJ" }, */
/* { "BESSELK","BESSELK" }, */
/* { "BESSELY","BESSELY" }, */
/* { "BETADIST","BETADIST" }, */
/* { "BETAINV","BETAINV" }, */
/* { "BIN2DEC","BIN2DEC" }, */
/* { "BIN2HEX","BIN2HEX" }, */
/* { "BIN2OCT","BIN2OCT" }, */
/* { "BINOMDIST","BINOMDIST" }, */
/* { "BITAND","BITAND" }, */
/* { "BITLSHIFT","BITLSHIFT" }, */
/* { "BITOR","BITOR" }, */
/* { "BITRSHIFT","BITRSHIFT" }, */
/* { "BITXOR","BITXOR" }, */
/* { "CHAR","CHAR" }, */
/* { "CHOOSE","CHOOSE" }, */
/* { "CLEAN","CLEAN" }, */
/* { "CODE","CODE" }, */
/* { "COLUMN","COLUMN" }, */
/* { "COLUMNS","COLUMNS" }, */
/* { "COMBIN","COMBIN" }, */
/* { "COMBINA","COMBINA" }, */
/* { "COMPLEX","COMPLEX" }, */
/* { "CONCATENATE","CONCATENATE" }, */
/* { "CONFIDENCE","CONFIDENCE" }, */
/* { "CONVERT","CONVERT" }, */
/* { "CORREL","CORREL" }, */
/* { "COS","COS" }, */
/* { "COSH","COSH" }, */
/* { "COT","COT" }, */
/* { "COTH","COTH" }, */
/* { "COUNT","COUNT" }, */
/* { "COUNTA","COUNTA" }, */
/* { "COUNTBLANK","COUNTBLANK" }, */
/* { "COUNTIF","COUNTIF" }, */
/* { "COUNTIFS","COUNTIFS" }, */
/* { "COUPDAYBS","COUPDAYBS" }, */
/* { "COUPDAYS","COUPDAYS" }, */
/* { "COUPDAYSNC","COUPDAYSNC" }, */
/* { "COUPNCD","COUPNCD" }, */
/* { "COUPNUM","COUPNUM" }, */
/* { "COUPPCD","COUPPCD" }, */
/* { "COVAR","COVAR" }, */
/* { "CRITBINOM","CRITBINOM" }, */
/* { "CSC","CSC" }, */
/* { "CSCH","CSCH" }, */
/* { "CUMIPMT","CUMIPMT" }, */
/* { "CUMPRINC","CUMPRINC" }, */
/* { "DATE","DATE" }, */
/* { "DATEDIF","DATEDIF" }, */
/* { "DATEVALUE","DATEVALUE" }, */
/* { "DAVERAGE","DAVERAGE" }, */
/* { "DAY","DAY" }, */
/* { "DAYS","DAYS" }, */
/* { "DAYS360","DAYS360" }, */
/* { "DB","DB" }, */
/* { "DCOUNT","DCOUNT" }, */
/* { "DCOUNTA","DCOUNTA" }, */
/* { "DDB","DDB" }, */
/* { "DDE","DDE" }, */
/* { "DEC2BIN","DEC2BIN" }, */
/* { "DEC2HEX","DEC2HEX" }, */
/* { "DEC2OCT","DEC2OCT" }, */
/* { "DECIMAL","DECIMAL" }, */
/* { "DEGREES","DEGREES" }, */
/* { "DELTA","DELTA" }, */
/* { "DEVSQ","DEVSQ" }, */
/* { "DGET","DGET" }, */
/* { "DISC","DISC" }, */
/* { "DMAX","DMAX" }, */
/* { "DMIN","DMIN" }, */
/* { "DOLLARDE","DOLLARDE" }, */
/* { "DOLLARFR","DOLLARFR" }, */
/* { "DPRODUCT","DPRODUCT" }, */
/* { "DSTDEV","DSTDEV" }, */
/* { "DSTDEVP","DSTDEVP" }, */
/* { "DSUM","DSUM" }, */
/* { "DURATION","DURATION" }, */
/* { "DVAR","DVAR" }, */
/* { "DVARP","DVARP" }, */
/* { "EDATE","EDATE" }, */
/* { "EFFECT","EFFECT" }, */
/* { "EOMONTH","EOMONTH" }, */
/* { "ERF","ERF" }, */
/* { "ERFC","ERFC" }, */
/* { "ERROR.TYPE","ERROR.TYPE" }, */
/* { "EUROCONVERT","EUROCONVERT" }, */
/* { "EVEN","EVEN" }, */
/* { "EXACT","EXACT" }, */
/* { "EXP","EXP" }, */
/* { "EXPONDIST","EXPONDIST" }, */
/* { "FACT","FACT" }, */
/* { "FACTDOUBLE","FACTDOUBLE" }, */
/* { "FALSE","FALSE" }, */
/* { "FDIST","FDIST" }, */
/* { "FIND","FIND" }, */
/* { "FINDB","FINDB" }, */
/* { "FINV","FINV" }, */
/* { "FISHER","FISHER" }, */
/* { "FISHERINV","FISHERINV" }, */
/* { "FIXED","FIXED" }, */
/* { "FORECAST","FORECAST" }, */
/* { "FREQUENCY","FREQUENCY" }, */
/* { "FTEST","FTEST" }, */
/* { "FV","FV" }, */
/* { "FVSCHEDULE","FVSCHEDULE" }, */
/* { "GAMMA","GAMMA" }, */
/* { "GAMMADIST","GAMMADIST" }, */
/* { "GAMMAINV","GAMMAINV" }, */
/* { "GAMMALN","GAMMALN" }, */
/* { "GCD","GCD" }, */
/* { "GEOMEAN","GEOMEAN" }, */
/* { "GESTEP","GESTEP" }, */
/* { "GETPIVOTDATA","GETPIVOTDATA" }, */
/* { "GROWTH","GROWTH" }, */
/* { "HARMEAN","HARMEAN" }, */
/* { "HEX2BIN","HEX2BIN" }, */
/* { "HEX2DEC","HEX2DEC" }, */
/* { "HEX2OCT","HEX2OCT" }, */
/* { "HLOOKUP","HLOOKUP" }, */
/* { "HOUR","HOUR" }, */
/* { "HYPERLINK","HYPERLINK" }, */
/* { "HYPGEOMDIST","HYPGEOMDIST" }, */
/* { "IF","IF" }, */
/* { "IFERROR","IFERROR" }, */
/* { "IFNA","IFNA" }, */
/* { "IMABS","IMABS" }, */
/* { "IMAGINARY","IMAGINARY" }, */
/* { "IMARGUMENT","IMARGUMENT" }, */
/* { "IMCONJUGATE","IMCONJUGATE" }, */
/* { "IMCOS","IMCOS" }, */
/* { "IMCOT","IMCOT" }, */
/* { "IMCSC","IMCSC" }, */
/* { "IMCSCH","IMCSCH" }, */
/* { "IMDIV","IMDIV" }, */
/* { "IMEXP","IMEXP" }, */
/* { "IMLN","IMLN" }, */
/* { "IMLOG10","IMLOG10" }, */
/* { "IMLOG2","IMLOG2" }, */
/* { "IMPOWER","IMPOWER" }, */
/* { "IMPRODUCT","IMPRODUCT" }, */
/* { "IMREAL","IMREAL" }, */
/* { "IMSEC","IMSEC" }, */
/* { "IMSECH","IMSECH" }, */
/* { "IMSIN","IMSIN" }, */
/* { "IMSQRT","IMSQRT" }, */
/* { "IMSUB","IMSUB" }, */
/* { "IMSUM","IMSUM" }, */
/* { "IMTAN","IMTAN" }, */
/* { "INDEX","INDEX" }, */
/* { "INDIRECT","INDIRECT" }, */
/* { "INFO","INFO" }, */
/* { "INT","INT" }, */
/* { "INTERCEPT","INTERCEPT" }, */
/* { "INTRATE","INTRATE" }, */
/* { "IPMT","IPMT" }, */
/* { "IRR","IRR" }, */
/* { "ISBLANK","ISBLANK" }, */
/* { "ISERR","ISERR" }, */
/* { "ISERROR","ISERROR" }, */
/* { "ISEVEN","ISEVEN" }, */
/* { "ISFORMULA","ISFORMULA" }, */
/* { "ISLOGICAL","ISLOGICAL" }, */
/* { "ISNA","ISNA" }, */
/* { "ISNONTEXT","ISNONTEXT" }, */
/* { "ISNUMBER","ISNUMBER" }, */
/* { "ISODD","ISODD" }, */
/* { "ISOWEEKNUM","ISOWEEKNUM" }, */
/* { "ISPMT","ISPMT" }, */
/* { "ISREF","ISREF" }, */
/* { "ISTEXT","ISTEXT" }, */
/* { "JIS","JIS" }, */
/* { "KURT","KURT" }, */
/* { "LARGE","LARGE" }, */
/* { "LCM","LCM" }, */
/* { "LEFT","LEFT" }, */
/* { "LEFTB","LEFTB" }, */
/* { "LEN","LEN" }, */
/* { "LENB","LENB" }, */
/* { "LINEST","LINEST" }, */
/* { "LN","LN" }, */
/* { "LOG","LOG" }, */
/* { "LOG10","LOG10" }, */
/* { "LOGEST","LOGEST" }, */
/* { "LOGINV","LOGINV" }, */
/* { "LOGNORMDIST","LOGNORMDIST" }, */
/* { "LOOKUP","LOOKUP" }, */
/* { "LOWER","LOWER" }, */
/* { "MATCH","MATCH" }, */
/* { "MAX","MAX" }, */
/* { "MAXA","MAXA" }, */
/* { "MDETERM","MDETERM" }, */
/* { "MDURATION","MDURATION" }, */
/* { "MEDIAN","MEDIAN" }, */
/* { "MID","MID" }, */
/* { "MIDB","MIDB" }, */
/* { "MIN","MIN" }, */
/* { "MINA","MINA" }, */
/* { "MINUTE","MINUTE" }, */
/* { "MINVERSE","MINVERSE" }, */
/* { "MIRR","MIRR" }, */
/* { "MMULT","MMULT" }, */
/* { "MOD","MOD" }, */
/* { "MODE","MODE" }, */
/* { "MONTH","MONTH" }, */
/* { "MROUND","MROUND" }, */
/* { "MULTINOMIAL","MULTINOMIAL" }, */
/* { "MULTIPLE.OPERATIONS","MULTIPLE.OPERATIONS" }, */
/* { "MUNIT","MUNIT" }, */
/* { "N","N" }, */
/* { "NA","NA" }, */
/* { "NEGBINOMDIST","NEGBINOMDIST" }, */
/* { "NETWORKDAYS","NETWORKDAYS" }, */
/* { "NOMINAL","NOMINAL" }, */
/* { "NORMDIST","NORMDIST" }, */
/* { "NORMINV","NORMINV" }, */
/* { "NOT","NOT" }, */
/* { "NOW","NOW" }, */
/* { "NPER","NPER" }, */
/* { "NPV","NPV" }, */
/* { "NUMBERVALUE","NUMBERVALUE" }, */
/* { "OCT2BIN","OCT2BIN" }, */
/* { "OCT2DEC","OCT2DEC" }, */
/* { "OCT2HEX","OCT2HEX" }, */
/* { "ODD","ODD" }, */
/* { "ODDFPRICE","ODDFPRICE" }, */
/* { "ODDFYIELD","ODDFYIELD" }, */
/* { "ODDLPRICE","ODDLPRICE" }, */
/* { "ODDLYIELD","ODDLYIELD" }, */
/* { "OFFSET","OFFSET" }, */
/* { "OR","OR" }, */
/* { "PEARSON","PEARSON" }, */
/* { "PERCENTILE","PERCENTILE" }, */
/* { "PERCENTRANK","PERCENTRANK" }, */
/* { "PERMUT","PERMUT" }, */
/* { "PERMUTATIONA","PERMUTATIONA" }, */
/* { "PI","PI" }, */
/* { "PMT","PMT" }, */
/* { "POISSON","POISSON" }, */
/* { "POWER","POWER" }, */
/* { "PPMT","PPMT" }, */
/* { "PRICE","PRICE" }, */
/* { "PRICEDISC","PRICEDISC" }, */
/* { "PRICEMAT","PRICEMAT" }, */
/* { "PROB","PROB" }, */
/* { "PRODUCT","PRODUCT" }, */
/* { "PROPER","PROPER" }, */
/* { "PV","PV" }, */
/* { "QUARTILE","QUARTILE" }, */
/* { "QUOTIENT","QUOTIENT" }, */
/* { "RADIANS","RADIANS" }, */
/* { "RAND","RAND" }, */
/* { "RANDBETWEEN","RANDBETWEEN" }, */
/* { "RANK","RANK" }, */
/* { "RATE","RATE" }, */
/* { "RECEIVED","RECEIVED" }, */
/* { "REPLACE","REPLACE" }, */
/* { "REPLACEB","REPLACEB" }, */
/* { "REPT","REPT" }, */
/* { "RIGHT","RIGHT" }, */
/* { "RIGHTB","RIGHTB" }, */
/* { "ROMAN","ROMAN" }, */
/* { "ROUND","ROUND" }, */
/* { "ROUNDDOWN","ROUNDDOWN" }, */
/* { "ROUNDUP","ROUNDUP" }, */
/* { "ROW","ROW" }, */
/* { "ROWS","ROWS" }, */
/* { "RRI","RRI" }, */
/* { "RSQ","RSQ" }, */
/* { "SEARCH","SEARCH" }, */
/* { "SEARCHB","SEARCHB" }, */
/* { "SEC","SEC" }, */
/* { "SECH","SECH" }, */
/* { "SECOND","SECOND" }, */
/* { "SERIESSUM","SERIESSUM" }, */
/* { "SHEET","SHEET" }, */
/* { "SHEETS","SHEETS" }, */
/* { "SIGN","SIGN" }, */
/* { "SIN","SIN" }, */
/* { "SINH","SINH" }, */
/* { "SKEW","SKEW" }, */
/* { "SKEWP","SKEWP" }, */
/* { "SLN","SLN" }, */
/* { "SLOPE","SLOPE" }, */
/* { "SMALL","SMALL" }, */
/* { "SQRT","SQRT" }, */
/* { "SQRTPI","SQRTPI" }, */
/* { "STANDARDIZE","STANDARDIZE" }, */
/* { "STDEV","STDEV" }, */
/* { "STDEVA","STDEVA" }, */
/* { "STDEVP","STDEVP" }, */
/* { "STDEVPA","STDEVPA" }, */
/* { "STEYX","STEYX" }, */
/* { "SUBSTITUTE","SUBSTITUTE" }, */
/* { "SUBTOTAL","SUBTOTAL" }, */
/* { "SUM","SUM" }, */
/* { "SUMIF","SUMIF" }, */
/* { "SUMIFS","SUMIFS" }, */
/* { "SUMPRODUCT","SUMPRODUCT" }, */
/* { "SUMSQ","SUMSQ" }, */
/* { "SUMX2MY2","SUMX2MY2" }, */
/* { "SUMX2PY2","SUMX2PY2" }, */
/* { "SUMXMY2","SUMXMY2" }, */
/* { "SYD","SYD" }, */
/* { "T","T" }, */
/* { "TAN","TAN" }, */
/* { "TANH","TANH" }, */
/* { "TBILLEQ","TBILLEQ" }, */
/* { "TBILLPRICE","TBILLPRICE" }, */
/* { "TBILLYIELD","TBILLYIELD" }, */
/* { "TDIST","TDIST" }, */
/* { "TEXT","TEXT" }, */
/* { "TIME","TIME" }, */
/* { "TIMEVALUE","TIMEVALUE" }, */
/* { "TINV","TINV" }, */
/* { "TODAY","TODAY" }, */
/* { "TRANSPOSE","TRANSPOSE" }, */
/* { "TREND","TREND" }, */
/* { "TRIM","TRIM" }, */
/* { "TRIMMEAN","TRIMMEAN" }, */
/* { "TRUE","TRUE" }, */
/* { "TRUNC","TRUNC" }, */
/* { "TTEST","TTEST" }, */
/* { "TYPE","TYPE" }, */
/* { "UNICHAR","UNICHAR" }, */
/* { "UNICODE","UNICODE" }, */
/* { "UPPER","UPPER" }, */
/* { "VALUE","VALUE" }, */
/* { "VAR","VAR" }, */
/* { "VARA","VARA" }, */
/* { "VARP","VARP" }, */
/* { "VARPA","VARPA" }, */
/* { "VDB","VDB" }, */
/* { "VLOOKUP","VLOOKUP" }, */
/* { "WEEKDAY","WEEKDAY" }, */
/* { "WEEKNUM","WEEKNUM" }, */
/* { "WEIBULL","WEIBULL" }, */
/* { "WORKDAY","WORKDAY" }, */
/* { "XIRR","XIRR" }, */
/* { "XNPV","XNPV" }, */
/* { "XOR","XOR" }, */
/* { "YEAR","YEAR" }, */
/* { "YEARFRAC","YEARFRAC" }, */
/* { "YIELD","YIELD" }, */
/* { "YIELDDISC","YIELDDISC" }, */
/* { "YIELDMAT","YIELDMAT" }, */
/* { "ZTEST","ZTEST" }, */
		{ NULL, NULL }
	};
	static char const OOoAnalysisPrefix[] = "com.sun.star.sheet.addin.Analysis.get";
	static char const GnumericPrefix[] = "ORG.GNUMERIC.";
	static GHashTable *namemap = NULL;
	static GHashTable *handlermap = NULL;

	GnmFunc  *f;
	char const *new_name;
	int i;
	GnmExpr const * (*handler) (GnmConventions const *convs, Workbook *scope, GnmExprList *args);

	if (NULL == namemap) {
		namemap = g_hash_table_new (go_ascii_strcase_hash,
					    go_ascii_strcase_equal);
		for (i = 0; sc_func_renames[i].oo_name; i++)
			g_hash_table_insert (namemap,
				(gchar *) sc_func_renames[i].oo_name,
				(gchar *) sc_func_renames[i].gnm_name);
	}
	if (NULL == handlermap) {
		guint i;
		handlermap = g_hash_table_new (go_ascii_strcase_hash,
					       go_ascii_strcase_equal);
		for (i = 0; sc_func_handlers[i].gnm_name; i++)
			g_hash_table_insert (handlermap,
					     (gchar *) sc_func_handlers[i].gnm_name,
					     sc_func_handlers[i].handler);
	}

	handler = g_hash_table_lookup (handlermap, name);
	if (handler != NULL) {
		GnmExpr const * res = handler (convs, scope, args);
		if (res != NULL)
			return res;
	}

	if (0 == g_ascii_strncasecmp (name, GnumericPrefix, sizeof (GnumericPrefix)-1)) {
		f = gnm_func_lookup_or_add_placeholder (name+sizeof (GnumericPrefix)-1, scope, TRUE);
	} else if (0 != g_ascii_strncasecmp (name, OOoAnalysisPrefix, sizeof (OOoAnalysisPrefix)-1)) {
		if (NULL != namemap &&
		    NULL != (new_name = g_hash_table_lookup (namemap, name)))
			name = new_name;
		f = gnm_func_lookup_or_add_placeholder (name, scope, TRUE);
	} else
		f = gnm_func_lookup_or_add_placeholder (name+sizeof (OOoAnalysisPrefix)-1, scope, TRUE);

	return gnm_expr_new_funcall (f, args);
}

static OOVer
determine_oo_version (GsfInfile *zip, OOVer def)
{
	char const *header;
	size_t size;
	GsfInput *mimetype = gsf_infile_child_by_name (zip, "mimetype");
	if (!mimetype) {
		/* Really old versions had no mimetype.  Allow that, except
		   in the probe.  */
		return def;
	}

	/* pick arbitrary size limit of 2k for the mimetype to avoid
	 * potential of any funny business */
	size = MIN (gsf_input_size (mimetype), 2048);
	header = gsf_input_read (mimetype, size, NULL);

	if (header) {
		unsigned ui;

		for (ui = 0 ; ui < G_N_ELEMENTS (OOVersions) ; ui++)
			if (size == strlen (OOVersions[ui].mime_type) &&
			    !memcmp (OOVersions[ui].mime_type, header, size)) {
				g_object_unref (mimetype);
				return OOVersions[ui].version;
			}
	}

	g_object_unref (mimetype);
	return OOO_VER_UNKNOWN;
}


void
openoffice_file_open (GOFileOpener const *fo, GOIOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input);
G_MODULE_EXPORT void
openoffice_file_open (GOFileOpener const *fo, GOIOContext *io_context,
		      WorkbookView *wb_view, GsfInput *input)
{
	GsfXMLInDoc	*doc;
	GsfInput	*contents = NULL;
	GsfInput	*styles = NULL;
	GsfDocMetaData	*meta_data;
	GsfInfile	*zip;
	GnmLocale	*locale;
	OOParseState	 state;
	GError		*err = NULL;
	int i;

	zip = gsf_infile_zip_new (input, &err);
	if (zip == NULL) {
		g_return_if_fail (err != NULL);
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			err->message);
		g_error_free (err);
		return;
	}

	state.ver = determine_oo_version (zip, OOO_VER_1);
	if (state.ver == OOO_VER_UNKNOWN) {
		/* TODO : include the unknown type in the message when
		 * we move the error handling into the importer object */
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
					     _("Unknown mimetype for openoffice file."));
		g_object_unref (zip);
		return;
	}

	contents = gsf_infile_child_by_name (zip, "content.xml");
	if (contents == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			 _("No stream named content.xml found."));
		g_object_unref (zip);
		return;
	}

	styles = gsf_infile_child_by_name (zip, "styles.xml");
	if (styles == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (io_context),
			 _("No stream named styles.xml found."));
		g_object_unref (contents);
		g_object_unref (zip);
		return;
	}

	locale = gnm_push_C_locale ();

	/* init */
	state.context	= io_context;
	state.wb_view	= wb_view;
	state.pos.wb	= wb_view_get_workbook (wb_view);
	state.zip = zip;
	state.pos.sheet = NULL;
	state.pos.eval.col	= -1;
	state.pos.eval.row	= -1;
	state.cell_comment      = NULL;
	state.chart.these_plot_styles = NULL;
	state.styles.sheet = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.styles.col = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.styles.row = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
	state.styles.cell = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) gnm_style_unref);
	state.formats = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) go_format_unref);
	state.chart.graph_styles = g_hash_table_new_full (g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) oo_chart_style_free);
	state.cur_style.cells    = NULL;
	state.cur_style.col_rows = NULL;
	state.cur_style.sheets   = NULL;
	state.default_style.cells = NULL;
	state.default_style.rows = NULL;
	state.default_style.columns = NULL;
	state.cur_style_type   = OO_STYLE_UNKNOWN;
	state.sheet_order = NULL;
	for (i = 0; i<NUM_FORMULAE_SUPPORTED; i++)
		state.convs[i] = NULL;
	state.cur_format.accum = NULL;
	state.filter = NULL;
	state.page_breaks.h = state.page_breaks.v = NULL;
	state.last_progress_update = 0;

	go_io_progress_message (state.context, _("Reading file..."));
	go_io_value_progress_set (state.context, gsf_input_size (contents), 0);

	if (state.ver == OOO_VER_OPENDOC) {
		GsfInput *meta_file = gsf_infile_child_by_name (zip, "meta.xml");
		if (NULL != meta_file) {
			meta_data = gsf_doc_meta_data_new ();
			err = gsf_opendoc_metadata_read (meta_file, meta_data);
			if (NULL != err) {
				go_io_warning (io_context,
					_("Invalid metadata '%s'"), err->message);
				g_error_free (err);
			} else
				go_doc_set_meta_data (GO_DOC (state.pos.wb), meta_data);

			g_object_unref (meta_data);
		}
	}

	if (NULL != styles) {
		GsfXMLInDoc *doc = gsf_xml_in_doc_new (styles_dtd, gsf_ooo_ns);
		gsf_xml_in_doc_parse (doc, styles, &state);
		gsf_xml_in_doc_free (doc);
		g_object_unref (styles);
	}

	doc  = gsf_xml_in_doc_new (
		(state.ver == OOO_VER_1) ? ooo1_content_dtd : opendoc_content_dtd,
		gsf_ooo_ns);
	if (gsf_xml_in_doc_parse (doc, contents, &state)) {
		GsfInput *settings;

		/* get the sheet in the right order (in case something was
		 * created out of order implictly) */
		state.sheet_order = g_slist_reverse (state.sheet_order);
#if 0
		{
			GSList *l;
			g_printerr ("Order we desire:\n");
			for (l = state.sheet_order; l; l = l->next) {
				Sheet *sheet = l->data;
				g_printerr ("Sheet %s\n", sheet->name_unquoted);
			}
		}
		{
			GSList *l;
			g_printerr ("Order we have:\n");
			for (l = workbook_sheets (state.pos.wb); l; l = l->next) {
				Sheet *sheet = l->data;
				g_printerr ("Sheet %s\n", sheet->name_unquoted);
			}
		}
#endif
		workbook_sheet_reorder (state.pos.wb, state.sheet_order);
		g_slist_free (state.sheet_order);

		/* look for the view settings */
		if (state.ver == OOO_VER_1) {
			settings = gsf_infile_child_by_name (zip, "settings.xml");
			if (settings != NULL) {
				GsfXMLInDoc *sdoc = gsf_xml_in_doc_new (opencalc_settings_dtd, gsf_ooo_ns);
				gsf_xml_in_doc_parse (sdoc, settings, &state);
				gsf_xml_in_doc_free (sdoc);
				g_object_unref (settings);
			}
		}
	} else
		go_io_error_string (io_context, _("XML document not well formed!"));
	gsf_xml_in_doc_free (doc);

	go_io_progress_unset (state.context);

	if (state.default_style.cells)
		gnm_style_unref (state.default_style.cells);
	g_free (state.default_style.rows);
	g_free (state.default_style.columns);
	g_hash_table_destroy (state.styles.sheet);
	g_hash_table_destroy (state.styles.col);
	g_hash_table_destroy (state.styles.row);
	g_hash_table_destroy (state.styles.cell);
	g_hash_table_destroy (state.chart.graph_styles);
	g_hash_table_destroy (state.formats);
	g_object_unref (contents);

	g_object_unref (zip);

	i = workbook_sheet_count (state.pos.wb);
	while (i-- > 0)
		sheet_flag_recompute_spans (workbook_sheet_by_index (state.pos.wb, i));
	workbook_queue_all_recalc (state.pos.wb);

	for (i = 0; i < NUM_FORMULAE_SUPPORTED; i++)
		if (state.convs[i] != NULL)
			gnm_conventions_free (state.convs[i]);

	gnm_pop_C_locale (locale);
}

gboolean
openoffice_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl);

gboolean
openoffice_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl)
{
	GsfInfile *zip;
	OOVer ver;

	gboolean old_ext_ok = FALSE;
	char const *name = gsf_input_name (input);
	if (name) {
		name = gsf_extension_pointer (name);
		old_ext_ok = (name != NULL &&
			      (g_ascii_strcasecmp (name, "sxc") == 0 ||
			       g_ascii_strcasecmp (name, "stc") == 0));
	}

	zip = gsf_infile_zip_new (input, NULL);
	if (zip == NULL)
		return FALSE;

	ver = determine_oo_version
		(zip, old_ext_ok ? OOO_VER_1 : OOO_VER_UNKNOWN);

	g_object_unref (zip);

	return ver != OOO_VER_UNKNOWN;
}

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	magic_transparent = style_color_auto_back ();
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	style_color_unref (magic_transparent);
	magic_transparent = NULL;
}
