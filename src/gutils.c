/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * utils.c:  Various utility routines that do not depend on the GUI of Gnumeric
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jukka-Pekka Iivonen (iivonen@iki.fi)
 *    Zbigniew Chyla (cyba@gnome.pl)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "gutils.h"
#include "gnumeric-paths.h"

#include "sheet.h"
#include "ranges.h"
#include "mathfunc.h"

#include <goffice/goffice.h>

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <locale.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-doc-meta-data.h>

static char *gnumeric_lib_dir;
static char *gnumeric_data_dir;
static char *gnumeric_icon_dir;
static char *gnumeric_locale_dir;
static char *gnumeric_usr_dir;

static gboolean
running_in_tree (void)
{
	const char *argv0 = g_get_prgname ();

	if (!argv0)
		return FALSE;

	/* Sometime we see, e.g., "lt-gnumeric" as basename.  */
	{
		char *base = g_path_get_basename (argv0);
		gboolean has_lt_prefix = (strncmp (base, "lt-", 3) == 0);
		g_free (base);
		if (has_lt_prefix)
			return TRUE;
	}

	/* Look for ".libs" as final path element.  */
	{
		const char *dotlibs = strstr (argv0, ".libs/");
		if (dotlibs &&
		    (dotlibs == argv0 || G_IS_DIR_SEPARATOR (dotlibs[-1])) &&
		    strchr (dotlibs + 6, G_DIR_SEPARATOR) == NULL)
			return TRUE;
	}

	return FALSE;
}

void
gutils_init (void)
{
	char const *home_dir;
#ifdef G_OS_WIN32
	gchar *dir = g_win32_get_package_installation_directory_of_module (NULL);
	gnumeric_lib_dir = g_build_filename (dir, "lib",
					     "gnumeric", GNM_VERSION_FULL,
					     NULL);
	gnumeric_data_dir = g_build_filename (dir, "share",
					      "gnumeric", GNM_VERSION_FULL,
					      NULL);
	gnumeric_icon_dir = g_build_filename (dir, "share", "pixmaps",
					      "gnumeric", NULL);
	gnumeric_locale_dir = g_build_filename (dir, "share", "locale", NULL);
	g_free (dir);
#else
	if (running_in_tree ()) {
		const char *argv0 = g_get_prgname ();
		char *dotlibs = g_path_get_dirname (argv0);
		char *top = g_build_filename (dotlibs, "..", "../", NULL);
		char *plugins = g_build_filename (top, PLUGIN_SUBDIR, NULL);
		if (g_file_test (plugins, G_FILE_TEST_IS_DIR))
			gnumeric_lib_dir =
				go_filename_simplify (top, GO_DOTDOT_SYNTACTIC,
						      FALSE);
		g_free (top);
		g_free (plugins);
		g_free (dotlibs);
		if (0) g_printerr ("Running in-tree\n");
	}

	if (!gnumeric_lib_dir)
		gnumeric_lib_dir = g_strdup (GNUMERIC_LIBDIR);
	gnumeric_data_dir = g_strdup (GNUMERIC_DATADIR);
	gnumeric_icon_dir = g_strdup (GNUMERIC_ICONDIR);
	gnumeric_locale_dir = g_strdup (GNUMERIC_LOCALEDIR);
#endif
	home_dir = g_get_home_dir ();
	gnumeric_usr_dir = (home_dir == NULL ? NULL :
	   g_build_filename (home_dir, ".gnumeric", GNM_VERSION_FULL, NULL));
}

void
gutils_shutdown (void)
{
	g_free (gnumeric_lib_dir);
	gnumeric_lib_dir = NULL;
	g_free (gnumeric_data_dir);
	gnumeric_data_dir = NULL;
	g_free (gnumeric_icon_dir);
	gnumeric_icon_dir = NULL;
	g_free (gnumeric_locale_dir);
	gnumeric_locale_dir = NULL;
	g_free (gnumeric_usr_dir);
	gnumeric_usr_dir = NULL;
}

char const *
gnm_sys_lib_dir (void)
{
	return gnumeric_lib_dir;
}

char const *
gnm_sys_data_dir (void)
{
	return gnumeric_data_dir;
}

char const *
gnm_icon_dir (void)
{
	return gnumeric_icon_dir;
}

char const *
gnm_locale_dir (void)
{
	return gnumeric_locale_dir;
}

char const *
gnm_usr_dir (void)
{
	return gnumeric_usr_dir;
}

int
gnm_regcomp_XL (GORegexp *preg, char const *pattern, int cflags,
		gboolean full)
{
	GString *res = g_string_new (NULL);
	int retval;

	if (full)
		g_string_append_c (res, '^');

	while (*pattern) {
		switch (*pattern) {
		case '*':
			g_string_append (res, ".*");
			pattern++;
			break;

		case '?':
			g_string_append_c (res, '.');
			pattern++;
			break;

		case '~':
			if (pattern[1] == '*' ||
			    pattern[1] == '?' ||
			    pattern[1] == '~')
				pattern++;
			/* Fall through */
		default:
			pattern = go_regexp_quote1 (res, pattern);
		}
	}

	if (full)
		g_string_append_c (res, '$');

	retval = go_regcomp (preg, res->str, cflags);
	g_string_free (res, TRUE);
	return retval;
}

#if 0
static char const *
color_to_string (PangoColor color)
{
	static char result[100];
	sprintf (result, "%04x:%04x:%04x", color.red, color.green, color.blue);
	return result;
}

static const char *
enum_name (GType typ, int i)
{
	static char result[100];
	GEnumClass *ec = g_type_class_ref (typ);

	if (ec) {
		GEnumValue *ev = g_enum_get_value (ec, i);
		g_type_class_unref (ec);

		if (ev && ev->value_nick)
			return ev->value_nick;
		if (ev && ev->value_name)
			return ev->value_name;
	}

	sprintf (result, "%d", i);
	return result;
}

static gboolean
cb_gnm_pango_attr_dump (PangoAttribute *attr, gpointer user_data)
{
	g_print ("  start=%u; end=%u\n", attr->start_index, attr->end_index);
	switch (attr->klass->type) {
	case PANGO_ATTR_FAMILY:
		g_print ("    family=\"%s\"\n", ((PangoAttrString *)attr)->value);
		break;
	case PANGO_ATTR_LANGUAGE:
		g_print ("    language=\"%s\"\n", pango_language_to_string (((PangoAttrLanguage *)attr)->value));
		break;
	case PANGO_ATTR_STYLE:
		g_print ("    style=%s\n",
			 enum_name (PANGO_TYPE_STYLE, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_WEIGHT:
		g_print ("    weight=%s\n",
			 enum_name (PANGO_TYPE_WEIGHT, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_VARIANT:
		g_print ("    variant=%s\n",
			 enum_name (PANGO_TYPE_VARIANT, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_STRETCH:
		g_print ("    stretch=%s\n",
			 enum_name (PANGO_TYPE_STRETCH, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_UNDERLINE:
		g_print ("    underline=%s\n",
			 enum_name (PANGO_TYPE_UNDERLINE, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_STRIKETHROUGH:
		g_print ("    strikethrough=%d\n", ((PangoAttrInt *)attr)->value);
		break;
	case PANGO_ATTR_RISE:
		g_print ("    rise=%d\n", ((PangoAttrInt *)attr)->value);
		break;
	case PANGO_ATTR_FALLBACK:
		g_print ("    fallback=%d\n", ((PangoAttrInt *)attr)->value);
		break;
	case PANGO_ATTR_LETTER_SPACING:
		g_print ("    letter_spacing=%d\n", ((PangoAttrInt *)attr)->value);
		break;
	case PANGO_ATTR_SIZE:
		g_print ("    size=%d%s\n",
			 ((PangoAttrSize *)attr)->size,
			 ((PangoAttrSize *)attr)->absolute ? " abs" : "");
		break;
	case PANGO_ATTR_SCALE:
		g_print ("    scale=%g\n", ((PangoAttrFloat *)attr)->value);
		break;
	case PANGO_ATTR_FOREGROUND:
		g_print ("    foreground=%s\n", color_to_string (((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_BACKGROUND:
		g_print ("    background=%s\n", color_to_string (((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_UNDERLINE_COLOR:
		g_print ("    underline_color=%s\n", color_to_string (((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_STRIKETHROUGH_COLOR:
		g_print ("    strikethrough_color=%s\n", color_to_string (((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_FONT_DESC: {
		char *desc = pango_font_description_to_string (((PangoAttrFontDesc*)attr)->desc);
		g_print  ("    font=\"%s\"\n", desc);
		g_free (desc);
		break;
	}
	default:
		g_print ("    type=%s\n", enum_name (PANGO_TYPE_ATTR_TYPE, attr->klass->type));
	}

	return FALSE;
}

void
gnm_pango_attr_dump (PangoAttrList *list)
{
	g_print ("PangoAttrList at %p\n", list);
	pango_attr_list_filter (list, cb_gnm_pango_attr_dump, NULL);
}
#endif

/* ------------------------------------------------------------------------- */

struct _GnmLocale {
	char *num_locale;
	char *monetary_locale;
};
/**
 * gnm_push_C_locale :
 *
 * Returns the current locale, and sets the locale and the value-format
 * engine's locale to 'C'.  The caller must call gnm_pop_C_locale to free the
 * result and restore the previous locale.
 **/
GnmLocale *
gnm_push_C_locale (void)
{
	GnmLocale *old = g_new0 (GnmLocale, 1);

	old->num_locale = g_strdup (go_setlocale (LC_NUMERIC, NULL));
	go_setlocale (LC_NUMERIC, "C");
	old->monetary_locale = g_strdup (go_setlocale (LC_MONETARY, NULL));
	go_setlocale (LC_MONETARY, "C");
	go_locale_untranslated_booleans ();

	return old;
}

/**
 * gnm_pop_C_locale :
 * @locale : #GnmLocale
 *
 * Frees the result of gnm_push_C_locale and restores the original locale.
 **/
void
gnm_pop_C_locale (GnmLocale *locale)
{
	/* go_setlocale restores bools to locale translation */
	go_setlocale (LC_MONETARY, locale->monetary_locale);
	g_free (locale->monetary_locale);
	go_setlocale (LC_NUMERIC, locale->num_locale);
	g_free (locale->num_locale);
	g_free (locale);
}

/* ------------------------------------------------------------------------- */

gboolean
gnm_debug_flag (const char *flag)
{
	GDebugKey key;
	key.key = (char *)flag;
	key.value = 1;

	return g_parse_debug_string (g_getenv ("GNM_DEBUG"), &key, 1) != 0;
}

/* ------------------------------------------------------------------------- */

void
gnm_string_add_number (GString *buf, gnm_float d)
{
	size_t old_len = buf->len;
	double d2;
	static int digits;

	if (digits == 0) {
		gnm_float l10 = gnm_log10 (FLT_RADIX);
		digits = (int)gnm_ceil (GNM_MANT_DIG * l10) +
			(l10 == (int)l10 ? 0 : 1);
	}

	g_string_append_printf (buf, "%.*" GNM_FORMAT_g, digits - 1, d);
	d2 = gnm_strto (buf->str + old_len, NULL);

	if (d != d2) {
		g_string_truncate (buf, old_len);
		g_string_append_printf (buf, "%.*" GNM_FORMAT_g, digits, d);
	}
}

/* ------------------------------------------------------------------------- */

void       
gnm_insert_meta_date (GODoc *doc, char const *name)
{
	GValue *value = g_new0 (GValue, 1);
	GTimeVal time;

	g_get_current_time (&time);
	time.tv_usec = 0L;
	g_value_init (value, G_TYPE_STRING);
	g_value_take_string (value,
			     g_time_val_to_iso8601 (&time));
	gsf_doc_meta_data_insert (go_doc_get_meta_data (doc), 
				  g_strdup (name), 
				  value);
}
