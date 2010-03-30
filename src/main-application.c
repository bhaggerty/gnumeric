/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * main-application.c: Main entry point for the Gnumeric application
 *
 * Author:
 *   Jon K�re Hellan <hellan@acm.org>
 *   Morten Welinder <terra@gnome.org>
 *   Jody Goldberg <jody@gnome.org>
 *
 * Copyright (C) 2002-2004, Jon K�re Hellan
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#ifdef G_OS_WIN32
#define _WIN32_WINNT 0x0501
#include <windows.h>
#endif

#include "command-context.h"
#include <goffice/goffice.h>
#include "io-context-gtk.h"
/* TODO: Get rid of this one */
#include "command-context-stderr.h"
#include "wbc-gtk-impl.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gui-file.h"
#include "gnumeric-gconf.h"
#include "gnumeric-paths.h"
#include "session.h"
#include "sheet.h"
#include "gutils.h"
#include "gnm-plugin.h"
#include "application.h"
#include "func.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <locale.h>

#ifdef GNM_WITH_GNOME
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-ui-main.h>
#include <libgnome/gnome-program.h>
#include <libgnome/gnome-init.h>
#include <libgnomeui/gnome-ui-init.h>
#endif

#ifdef GNM_USE_HILDON
#include <libosso.h>
#endif

static gboolean split_funcdocs = FALSE;
static gboolean immediate_exit_flag = FALSE;
static gboolean gnumeric_no_splash = FALSE;
static gboolean gnumeric_no_warnings = FALSE;
static gchar  *func_def_file = NULL;
static gchar  *func_state_file = NULL;
static gchar  *ext_refs_file = NULL;
static gchar  *geometry = NULL;
static gchar **startup_files;

static const GOptionEntry gnumeric_options [] = {
	/*********************************
	 * Public Variables */
	{ "geometry", 'g', 0, G_OPTION_ARG_STRING, &geometry,
		N_("Specify the size and location of the initial window"),
		N_("WIDTHxHEIGHT+XOFF+YOFF")
	},
	{ "no-splash", 0, 0, G_OPTION_ARG_NONE, &gnumeric_no_splash,
		N_("Don't show splash screen"), NULL },
	{ "no-warnings", 0, 0, G_OPTION_ARG_NONE, &gnumeric_no_warnings,
		N_("Don't display warning dialogs when importing"),
		NULL
	},

	/*********************************
	 * Hidden Actions */
	{
		"dump-func-defs", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &func_def_file,
		N_("Dumps the function definitions"),
		N_("FILE")
	},
	{
		"dump-func-state", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &func_state_file,
		N_("Dumps the function definitions"),
		N_("FILE")
	},
	{
		"ext-refs-file", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &ext_refs_file,
		N_("Dumps web page for function help"),
		N_("FILE")
	},
	{
		"split-func", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &split_funcdocs,
		N_("Generate new help and po files"),
		NULL
	},
	{
		"quit", 0,
		G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &immediate_exit_flag,
		N_("Exit immediately after loading the selected books"),
		NULL
	},
	//~ {
		//~ "quit", 0,
		//~ G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &immediate_exit_flag,
		//~ N_("Exit immediately after loading the selected books"),
		//~ NULL
	//~ },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &startup_files, NULL, NULL },
	{ NULL }
};


static void
handle_paint_events (void)
{
	/* FIXME: we need to mask input events correctly here */
	/* Show something coherent */
	while (gtk_events_pending () && !initial_workbook_open_complete)
		gtk_main_iteration_do (FALSE);
}

static GObject *program = NULL;

static void
gnumeric_arg_shutdown (void)
{
	if (program) {
		g_object_unref (program);
		program = NULL;
	}
}

/* If something links in the authentication manager, initialize it.  */
static void
call_gnome_authentication_manager_init (void)
{
	GModule *self = g_module_open (NULL, 0);
	gboolean ok;
	gpointer gami = NULL;
	void (*_gnome_authentication_manager_init) (void);

	if (!self) return;
	ok = g_module_symbol (self, "gnome_authentication_manager_init", &gami);
	g_module_close (self);
	if (!ok || gami == NULL) return;

	_gnome_authentication_manager_init = (void (*) (void))gami;
	_gnome_authentication_manager_init ();
}

static void
gnumeric_arg_parse (int argc, char **argv)
{
	GOptionContext *ocontext;
	int i;
	gboolean funcdump = FALSE;
	GError *error = NULL;

	/* no need to init gtk when dumping function info */
	for (i = 0 ; argv[i] ; i++)
		if (0 == strncmp ("--dump-func", argv[i], 11)) {
			funcdump = TRUE;
			break;
		}

	ocontext = g_option_context_new ("[FILE ...]");
	g_option_context_add_main_entries (ocontext, gnumeric_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());

#if defined(G_OS_WIN32) && defined(HAVE_G_OPTION_CONTEXT_SET_DELOCALIZE)
	/* we have already translated to utf8, do not do it again.
	 * http://bugzilla.gnome.org/show_bug.cgi?id=361321 */
	g_option_context_set_delocalize   (ocontext, FALSE);
#endif

#ifdef GNM_WITH_GNOME
#ifndef GNOME_PARAM_GOPTION_CONTEXT
	/*
	 * Bummer.  We cannot make gnome_program_init handle our args so
	 * we do it ourselves.  That, in turn, means we don't handle
	 * libgnome[ui]'s args.
	 *
	 * Upgrade to libgnome 2.13 or better to solve this.
	 */
	if (!funcdump)
		g_option_context_add_group (ocontext, gtk_get_option_group (TRUE));
	g_option_context_parse (ocontext, &argc, &argv, &error);
#endif

	if (!error) {
		program = (GObject *)
			gnome_program_init (PACKAGE, VERSION,
					    funcdump ? LIBGNOME_MODULE : LIBGNOMEUI_MODULE,
					    argc, argv,
					    GNOME_PARAM_APP_PREFIX,		GNUMERIC_PREFIX,
					    GNOME_PARAM_APP_SYSCONFDIR,		GNUMERIC_SYSCONFDIR,
					    GNOME_PARAM_APP_DATADIR,		gnm_sys_data_dir (),
					    GNOME_PARAM_APP_LIBDIR,		gnm_sys_lib_dir (),
#ifdef GNOME_PARAM_GOPTION_CONTEXT
					    GNOME_PARAM_GOPTION_CONTEXT,	ocontext,
#endif
					    NULL);
#ifdef GNOME_PARAM_GOPTION_CONTEXT
		ocontext = NULL;
#endif
	}

#else /* therefore not gnome */
	if (!funcdump)
		g_option_context_add_group (ocontext, gtk_get_option_group (TRUE));
	g_option_context_parse (ocontext, &argc, &argv, &error);
#endif

	if (ocontext)
		g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, argv[0]);
		g_error_free (error);
		exit (1);
	}

	if (!funcdump) {
		gtk_init (&argc, &argv);
		call_gnome_authentication_manager_init ();
	}
}

/*
 * WARNING WARNING WARNING
 * This does not belong here
 * but it is expedient for now to get things to compile
 */
#warning "REMOVE REMOVE REMOVE"
static void
store_plugin_state (void)
{
	GSList *active_plugins = go_plugins_get_active_plugins ();
	gnm_conf_set_plugins_active (active_plugins);
	g_slist_free (active_plugins);
}

static gboolean
cb_kill_wbcg (WBCGtk *wbcg)
{
	gboolean still_open = wbc_gtk_close (wbcg);
	g_assert (!still_open);
	return FALSE;
}

static gboolean
pathetic_qt_workaround (void)
{
	/*
	 * When using with the Qt theme, the qt library will be initialized
	 * somewhere around the time the first widget is created or maybe
	 * realized.  That code literally does
	 *
	 *        setlocale( LC_NUMERIC, "C" );	// make sprintf()/scanf() work
	 *
	 * I am not kidding.  It seems like we can fix this by re-setting the
	 * proper locale when the gui comes up.
	 *
	 * See bug 512752, for example.
	 */
	setlocale (LC_ALL, "");
	return FALSE;
}


static void
cb_workbook_removed (void)
{
	if (gnm_app_workbook_list () == NULL) {
#ifdef GNM_WITH_GNOME
		bonobo_main_quit ();
#else
		gtk_main_quit ();
#endif
	}
}

int
main (int argc, char const **argv)
{
	gboolean opened_workbook = FALSE;
	gboolean with_gui;
	GOIOContext *ioc;
	WorkbookView *wbv;
	GSList *wbcgs_to_kill = NULL;

#ifdef G_OS_WIN32
	gboolean has_console;
#endif

#ifdef GNM_USE_HILDON
	osso_context_t * osso_context;
#endif

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

#ifdef G_OS_WIN32
	has_console = FALSE;
	{
		typedef BOOL (CALLBACK* LPFNATTACHCONSOLE)(DWORD);
		LPFNATTACHCONSOLE MyAttachConsole;
		HMODULE hmod;

		if ((hmod = GetModuleHandle("kernel32.dll"))) {
			MyAttachConsole = (LPFNATTACHCONSOLE) GetProcAddress(hmod, "AttachConsole");
			if (MyAttachConsole && MyAttachConsole(ATTACH_PARENT_PROCESS)) {
				freopen("CONOUT$", "w", stdout);
				freopen("CONOUT$", "w", stderr);
				dup2(fileno(stdout), 1);
				dup2(fileno(stderr), 2);
				has_console = TRUE;
			}
		}
	}
#endif

#ifdef GNM_USE_HILDON
	osso_context = osso_initialize ("gnumeric", GNM_VERSION_FULL, TRUE, NULL);
#endif

	gnumeric_arg_parse (argc, (char **)argv);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	bind_textdomain_codeset (GETTEXT_PACKAGE "-functions", "UTF-8");

	with_gui = !func_def_file && !func_state_file && !split_funcdocs;

	if (with_gui) {
		gnm_session_init (argv[0]);
	}

	gnm_init ();

	if (with_gui) {
		ioc = GO_IO_CONTEXT
			(g_object_new (GO_TYPE_IO_CONTEXT_GTK,
				       "show-splash", !gnumeric_no_splash,
				       "show-warnings", !gnumeric_no_warnings,
				       NULL));
		handle_paint_events ();
		pathetic_qt_workaround ();
	} else {
		/* TODO: Make this inconsistency go away */
		GOCmdContext *cc = cmd_context_stderr_new ();
		ioc = go_io_context_new (cc);
		g_object_unref (cc);
	}

	if (func_state_file)
		return gnm_dump_func_defs (func_state_file, 0);
	if (func_def_file)
		return gnm_dump_func_defs (func_def_file, 1);
	if (split_funcdocs)
		return gnm_dump_func_defs (NULL, 2);
	if (ext_refs_file)
		return gnm_dump_func_defs (ext_refs_file, 4);

	/* Keep in sync with .desktop file */
	g_set_application_name (_("Gnumeric Spreadsheet"));
	gnm_plugins_init (GO_CMD_CONTEXT (ioc));

#ifdef GNM_WITH_GNOME
	bonobo_activate ();
#endif
	if (startup_files) {
		int i;

		for (i = 0; startup_files [i]; i++)
			;

		go_io_context_set_num_files (ioc, i);
		for (i = 0;
		     startup_files [i] && !initial_workbook_open_complete;
		     i++) {
			char *uri = go_shell_arg_to_uri (startup_files[i]);

			if (uri == NULL) {
				g_warning ("Ignoring invalid URI.");
				continue;
			}

			go_io_context_processing_file (ioc, uri);
			wbv = wb_view_new_from_uri (uri, NULL, ioc, NULL);
			g_free (uri);

			if (go_io_error_occurred (ioc) ||
			    go_io_warning_occurred (ioc)) {
				go_io_error_display (ioc);
				go_io_error_clear (ioc);
			}
			if (wbv != NULL) {
				WBCGtk *wbcg;

				workbook_update_history (wb_view_get_workbook (wbv));

				wbcg = wbc_gtk_new (wbv, NULL, NULL, geometry);
				geometry = NULL;
				sheet_update (wb_view_cur_sheet	(wbv));
				opened_workbook = TRUE;
				icg_set_transient_for (IO_CONTEXT_GTK (ioc),
						       wbcg_toplevel (wbcg));
				if (immediate_exit_flag)
					wbcgs_to_kill = g_slist_prepend (wbcgs_to_kill,
									 wbcg);
			}
			/* cheesy attempt to keep the ui from freezing during
			   load */
			handle_paint_events ();
			if (icg_get_interrupted (IO_CONTEXT_GTK (ioc)))
				break; /* Don't load any more workbooks */
		}
	}
	/* FIXME: Maybe we should quit here if we were asked to open
	   files and failed to do so. */

	/* If we were intentionally short circuited exit now */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		if (!opened_workbook) {
			gint n_of_sheets = gnm_conf_get_core_workbook_n_sheet ();
			wbc_gtk_new (NULL,
				workbook_new_with_sheets (n_of_sheets),
				NULL, geometry);
		}

		if (immediate_exit_flag) {
			GSList *l;
			for (l = wbcgs_to_kill; l; l = l->next)
				g_idle_add ((GSourceFunc)cb_kill_wbcg, l->data);
		}
		g_object_unref (ioc);

		g_signal_connect (gnm_app_get_app (),
				  "workbook_removed",
				  G_CALLBACK (cb_workbook_removed),
				  NULL);

		g_idle_add ((GSourceFunc)pathetic_qt_workaround, NULL);
#ifdef GNM_WITH_GNOME
		bonobo_main ();
#else
		gtk_main ();
#endif
	} else {
		g_object_unref (ioc);
		g_slist_foreach (wbcgs_to_kill, (GFunc)cb_kill_wbcg, NULL);
	}

#ifdef GNM_USE_HILDON
	osso_deinitialize (osso_context);
#endif

	g_slist_free (wbcgs_to_kill);
	gnumeric_arg_shutdown ();
	store_plugin_state ();
	gnm_shutdown ();

#ifdef GNM_WITH_GNOME
	bonobo_ui_debug_shutdown ();
#elif defined(G_OS_WIN32)
	if (has_console) {
		close(1);
		close(2);
		FreeConsole();
	}
#endif

	gnm_pre_parse_shutdown ();

	/*
	 * This helps finding leaks.  We might want it in developent
	 * only.
	 */
	if (with_gui && gnm_debug_flag ("close-displays")) {
		GSList *displays;

		gdk_flush();
		while (g_main_context_iteration (NULL, FALSE))
			;/* nothing */

		displays = gdk_display_manager_list_displays
			(gdk_display_manager_get ());
		g_slist_foreach (displays, (GFunc)gdk_display_close, NULL);
		g_slist_free (displays);
	}

	return 0;
}
