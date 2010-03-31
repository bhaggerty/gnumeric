/*
 * sstest.c: Test code for Gnumeric
 *
 * Copyright (C) 2009 Morten Welinder (terra@gnome.org)
 *                                Simon Guo (simongjh@gmail.com)
 *                                Raza Atif (atif56@gmail.com)
 *                                Claudio Yin (claudio.yin.utoronto.ca)
 */
 
 //---------------------------------------------------
 
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include <goffice/goffice.h>
#include "command-context-stderr.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gutils.h"
#include "gnm-plugin.h"
#include "parse-util.h"
#include "expr-name.h"
#include "search.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "func.h"
#include "parse-util.h"
#include "sheet-object-cell-comment.h"

#include "ranges.h"
#include "gnm-data-cache-source.h"
#include "go-val.h"
#include "go-data-cache-field.h"
#include "go-data-cache.h"
#include "go-data-cache-field-impl.h"

#include "go-data-cache-impl.h"
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <glib/gi18n.h>
#include <string.h>

static gboolean sstest_show_version = FALSE;
//~ static char *sstest_slicer_file = NULL;

static GOptionEntry const sstest_options [] = {
	{
		"version", 'V',
		0, G_OPTION_ARG_NONE, &sstest_show_version,
		N_("Display program version"),
		NULL
	},
	//~ {
		//~ "export-slicer-data", 0,
		//~ G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &sstest_slicer_file,
		//~ N_("The data file to export"),
		//~ NULL
	//~ },

	{ NULL }
};

static void
mark_test_start (const char *name)
{
	g_printerr ("-----------------------------------------------------------------------------\nStart: %s\n-----------------------------------------------------------------------------\n\n", name);
}

static void
mark_test_end (const char *name)
{
	g_printerr ("End: %s\n\n", name);
}

static void
cb_collect_names (const char *name, GnmNamedExpr *nexpr, GSList **names)
{
	*names = g_slist_prepend (*names, nexpr);
}

static void
dump_names (Workbook *wb)
{
	GSList *l, *names = NULL;

	workbook_foreach_name (wb, FALSE, (GHFunc)cb_collect_names, &names);
	names = g_slist_sort (names, (GCompareFunc)expr_name_cmp_by_name);

	g_printerr ("Dumping names...\n");
	for (l = names; l; l = l->next) {
		GnmNamedExpr *nexpr = l->data;
		GnmConventionsOut out;

		out.accum = g_string_new (NULL);
		out.pp = &nexpr->pos;
		out.convs = gnm_conventions_default;

		g_string_append (out.accum, "Scope=");
		if (out.pp->sheet)
			g_string_append (out.accum, out.pp->sheet->name_quoted);
		else
			g_string_append (out.accum, "Global");

		g_string_append (out.accum, " Name=");
		go_strescape (out.accum, expr_name_name (nexpr));

		g_string_append (out.accum, " Expr=");
		gnm_expr_top_as_gstring (nexpr->texpr, &out);

		g_printerr ("%s\n", out.accum->str);
		g_string_free (out.accum, TRUE);
	}
	g_printerr ("Dumping names... Done\n");

	g_slist_free (names);
}

static void
define_name (const char *name, const char *expr_txt, gpointer scope)
{
	GnmParsePos pos;
	GnmExprTop const *texpr;
	GnmNamedExpr const *nexpr;
	GnmConventions const *convs;

	if (IS_SHEET (scope)) {
		parse_pos_init_sheet (&pos, scope);
		convs = sheet_get_conventions (pos.sheet);
	} else {
		parse_pos_init (&pos, WORKBOOK (scope), NULL, 0, 0);
		convs = gnm_conventions_default;
	}

	texpr = gnm_expr_parse_str (expr_txt, &pos,
				    GNM_EXPR_PARSE_DEFAULT,
				    convs, NULL);
	if (!texpr) {
		g_printerr ("Failed to parse %s for name %s\n",
			    expr_txt, name);
		return;
	}

	nexpr = expr_name_add (&pos, name, texpr, NULL, TRUE, NULL);
	if (!nexpr)
		g_printerr ("Failed to add name %s\n", name);
}

static void
test_insdel_rowcol_names (void)
{
	Workbook *wb;
	Sheet *sheet1,*sheet2;
	const char *test_name = "test_insdel_rowcol_names";
	GOUndo *undo;
	int i;

	mark_test_start (test_name);

	wb = workbook_new ();
	sheet1 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	sheet2 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);

	define_name ("Print_Area", "Sheet1!$A$1:$IV$65536", sheet1);
	define_name ("Print_Area", "Sheet2!$A$1:$IV$65536", sheet2);

	define_name ("NAMEGA1", "A1", wb);
	define_name ("NAMEG2", "$A$14+Sheet1!$A$14+Sheet2!$A$14", wb);

	define_name ("NAMEA1", "A1", sheet1);
	define_name ("NAMEA2", "A2", sheet1);
	define_name ("NAMEA1ABS", "$A$1", sheet1);
	define_name ("NAMEA2ABS", "$A$2", sheet1);

	dump_names (wb);

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %d on %s\n",
			    i, sheet1->name_unquoted);
		sheet_insert_cols (sheet1, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %d on %s\n",
			    i, sheet2->name_unquoted);
		sheet_insert_cols (sheet2, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to delete column %d on %s\n",
			    i, sheet1->name_unquoted);
		sheet_delete_cols (sheet1, i, 1, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	g_object_unref (wb);

	mark_test_end (test_name);
}

static void
test_func_help (void)
{
	const char *test_name = "test_insdel_rowcol_names";
	int res;

	mark_test_start (test_name);

	res = gnm_func_sanity_check ();
	g_printerr ("Result = %d\n", res);

	mark_test_end (test_name);
}

/**
 * Tests here
 */

static void
test_go_data_cache_build_cache(void)
{
	Workbook *wb;
	Sheet *sheet;
	GODataCache *cache;
	GnmRange *range;
	int row, col;
	
	const char *test_name = "test_go_data_cache_build_cache";
	int numRows = 60, numCols = 5;
	
	mark_test_start (test_name);
	
	wb = workbook_new();
	sheet = workbook_sheet_add (wb, -1, 1024, 1024);
	
	for (row = 0; row < numRows; row++) {
		for (col = 0; col < numCols; col++) {
			GnmCell * tempCell = sheet_cell_create(sheet, col, row);
			GnmValue * tempVal;
			if (col == 0) {
				if (row%4 == 0) {
					tempVal = value_new_string_nocopy((char *)"A");
				} else if (row%4 == 1) {
					tempVal = value_new_string_nocopy((char *)"B");
				} else if (row%4 == 2) {
					tempVal = value_new_string_nocopy((char *)"C");
				} else {
					tempVal = value_new_string_nocopy((char *)"D");
				}
			} else if (col == 1) {
				tempVal = value_new_int(row);
			} else if (col == 2) {
				if (row%5 == 0) {
					tempVal = value_new_float(14.4);
				} else if (row%5 == 1) {
					tempVal = value_new_float(18.8);
				} else if (row%5 == 2) {
					tempVal = value_new_float(7.6);
				} else if (row%5 == 3) {
					tempVal = value_new_float(3.3);
				} else {
					tempVal = value_new_float(11.6);
				}
			} else if (col == 3) {
				tempVal = value_new_int(row % 10);
			} else if (col == 4) {
				if (row == 0) {
					GnmEvalPos *pos = g_new(GnmEvalPos, 1);
					pos = eval_pos_init(pos, sheet, col, row);
					tempVal = value_new_error_DIV0(pos);
				} else if (row == 1) {
					GnmEvalPos *pos = g_new(GnmEvalPos, 1);
					pos = eval_pos_init(pos, sheet, col, row);
					tempVal = value_new_error_NA(pos);
				} else if (row == 2) {
					GnmEvalPos *pos = g_new(GnmEvalPos, 1);
					pos = eval_pos_init(pos, sheet, col, row);
					tempVal = value_new_error_REF(pos);
				} else if (row == 3) {
					tempVal = value_new_bool(TRUE);
				} else if (row == 4) {
					tempVal = value_new_bool(FALSE);
				} else if (row == 5) {
					tempVal = value_new_empty();
				} else {
					if (row%5 == 1) {
						tempVal = value_new_string_nocopy((char *)"a");
					} else if (row%5 == 2) {
						tempVal = value_new_string_nocopy((char *)"b");
					} else if (row%5 == 3) {
						tempVal = value_new_string_nocopy((char *)"c");
					} else if (row%5 == 4) {
						tempVal = value_new_string_nocopy((char *)"d");
					} else {
						tempVal = value_new_string_nocopy((char *)"e");
					}
				}
			}
			sheet_cell_set_value(tempCell, tempVal);
		}
	}
	
	cache = g_object_new(GO_DATA_CACHE_TYPE, NULL);
	range = g_new(GnmRange, 1);
	range = range_init(range, 0, 0, numCols - 1, numRows - 1);
	
	go_data_cache_build_cache(cache, sheet, range);
	go_data_cache_dump(cache, NULL, NULL);
	
	g_object_unref (wb);

	mark_test_end (test_name);
	
	//~ if (sstest_slicer_file) {
		//~ GOFileOpener *fo = NULL;
		//~ char *infile = sstest_slicer_file; //go_shell_arg_to_uri (sstest_slicer_file);
		//~ int res = 0, row = 0, col = 0, numRows = 10, numCols = 2;
		
		//~ GOIOContext *io_context = go_io_context_new (cc);
		//~ WorkbookView *wbv;
		
		//~ const char *test_name = "test_go_data_cache_build_cache";
	
		//~ mark_test_start (test_name);
		
		//~ wbv = wb_view_new_from_uri (infile, fo,
					    //~ io_context,
					    //~ NULL);

		//~ if (wbv == NULL || go_io_error_occurred (io_context)) {
			//~ go_io_error_display (io_context);
			//~ res = 1;
		//~ } else {
			//Workbook *wb = wb_view_get_workbook (wbv);
			//~ Sheet *sheet = wb_view_cur_sheet (wbv);
			
			//~ for (row = 0; row < numRows; row++) {
				//~ for (col = 0; col < numCols; col++) {
					//~ GnmCell * tempCell = sheet_cell_get(sheet, col, row);
					//~ GnmValue * tempVal = tempCell->value;
					//~ value_dump(tempVal);
				//~ }
			//~ }

			//~ res = handle_export_options (fs, GO_DOC (wb));
			//~ if (res) {
				//~ g_object_unref (wb);
				//~ goto out;
			//~ }
			
			//~ GSList *ptr;
			//~ GString *s;
			//~ char *tmpfile;
			//~ int idx = 0;
			//~ res = 0;
			
			//~ for (ptr = workbook_sheets(wb); ptr && !res; ptr = ptr->next, idx++) {
				//~ wb_view_sheet_focus(wbv, (Sheet *)ptr->data);
				//~ s = g_string_new (outfile);
				//~ g_string_append_printf(s, ".%d", idx);
				//~ tmpfile = g_string_free (s, FALSE);
				//~ res = !wb_view_save_as (wbv, fs, tmpfile, cc);
				//~ g_free(tmpfile);
			//~ }
			//~ g_object_unref (wb);
		//~ }
		//~ g_object_unref (io_context);
		
		//~ g_object_unref (wb);
	
		//~ mark_test_end (test_name);
	//~ }
	
}

static void
test_go_data_slicer_tuple_compare_to (void)
{
	
	const char *test_name = "test_go_data_slicer_tuple_compare_to";

	mark_test_start (test_name);
	
	g_printerr("pass haha\n");

	mark_test_end (test_name);
	
}

#define MAYBE_DO(name) if (strcmp (testname, "all") != 0 && strcmp (testname, (name)) != 0) { } else

int
main (int argc, char const **argv)
{
	GOErrorInfo	*plugin_errs;
	GOCmdContext	*cc;
	GOptionContext	*ocontext;
	GError		*error = NULL;
	const char *testname;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	ocontext = g_option_context_new (_("[testname]"));
	g_option_context_add_main_entries (ocontext, sstest_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (gchar ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, g_get_prgname ());
		g_error_free (error);
		return 1;
	}

	if (sstest_show_version) {
		g_printerr (_("version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			    GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	}

	gnm_init ();

	cc = cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);
	if (plugin_errs) {
		/* FIXME: What do we want to do here? */
		go_error_info_free (plugin_errs);
	}

	testname = argv[1];
	if (!testname) testname = "all";

	/* ---------------------------------------- */

	MAYBE_DO ("test_insdel_rowcol_names") test_insdel_rowcol_names ();
	MAYBE_DO ("test_func_help") test_func_help ();
	MAYBE_DO ("test_go_data_cache_build_cache") test_go_data_cache_build_cache();
	MAYBE_DO ("test_go_data_slicer_tuple_compare_to") test_go_data_slicer_tuple_compare_to ();

	/* ---------------------------------------- */

	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return 0;
}
