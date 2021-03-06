#
# Gnumeric's Makefile.am
# Author: Miguel de Icaza (miguel@kernel.org)

SUBDIRS = tools cut-n-paste-code

lib_LTLIBRARIES = libspreadsheet.la

bin_PROGRAMS =
check_PROGRAMS =
noinst_PROGRAMS = sstest

if WITH_GTK
  SUBDIRS += pixmaps dialogs widgets
  bin_PROGRAMS += gnumeric
  check_PROGRAMS += test-pango
endif

if ENABLE_SSCONVERT
  bin_PROGRAMS += ssconvert
endif
if ENABLE_SSINDEX
  bin_PROGRAMS += ssindex
endif
if ENABLE_SSGREP
  bin_PROGRAMS += ssgrep
endif

AM_CPPFLAGS = 								\
	-I$(top_srcdir)							\
	-I$(srcdir)							\
	-I$(srcdir)/widgets						\
	-I$(srcdir)/dialogs						\
	-I$(srcdir)/tools						\
	$(GNUMERIC_CFLAGS)						\
	$(HILDON_CFLAGS)

# A hint is needed to build these headers first:
BUILT_SOURCES =					\
	gnm-marshalers.c			\
	gnm-marshalers.h			\
	gnumeric-paths.h

# Another hint, see bugs #172211 and #172212:
non-intermediate: gnm-marshalers.c

CLEANFILES = $(BUILT_SOURCES)			\
	gnumeric.exe.a

libspreadsheet_la_CPPFLAGS = $(AM_CPPFLAGS) -DGNUMERIC_INTERNAL
libspreadsheet_la_LDFLAGS = -release $(VERSION)
libspreadsheet_la_LIBADD = 			\
	$(gnumeric_app_libs)			\
	$(GNUMERIC_LIBS)			\
	$(INTLLIBS)				\
	$(HILDON_LIBS)

libspreadsheet_la_SOURCES =			\
	go-val.h			\
	go-val.c			\
	\
	goffice-data.h 			\
	go-data-cache-source.c 		\
	go-data-cache-source.h 		\
	go-data-cache-impl.h 		\
	go-data-cache.h 		\
	go-data-cache.c 		\
	go-data-cache-field-impl.h 	\
	go-data-cache-field.h 		\
	go-data-cache-field.c 		\
	go-data-slicer-impl.h 		\
	go-data-slicer.h 		\
	go-data-slicer.c 		\
	go-data-slicer-index.h      \
	go-data-slicer-index.c      \
	go-data-slicer-tuple.h      \
	go-data-slicer-tuple.c		\
	go-data-slicer-bitmap.h     \
	go-data-slicer-bitmap.c     \
	go-data-slicer-cache-overlay.h  \
	go-data-slicer-cache-overlay.c  \
	\
	gnm-data-cache-source.h 	\
	gnm-data-cache-source.c 	\
	gnm-sheet-slicer.h 		\
	gnm-sheet-slicer.c 		\
	gnm-sheet-slicer-combo.h	\
	gnm-sheet-slicer-combo.c	\
	\
	gnm-cell-combo.h		\
	gnm-cell-combo.c		\
	\
	gnm-marshalers.c			\
	application.c				\
	auto-format.c				\
	cell.c					\
	cell-draw.c				\
	cellspan.c				\
	clipboard.c				\
	cmd-edit.c				\
	collect.c				\
	colrow.c				\
	command-context.c			\
	command-context-stderr.c		\
	commands.c				\
	complete.c				\
	complete-sheet.c			\
	complex.c				\
	consolidate.c				\
	dependent.c				\
	expr.c					\
	expr-name.c				\
	file-autoft.c				\
	format-template.c			\
	func.c					\
	func-builtin.c				\
	gnm-commands-slicer.c			\
	gnm-datetime.c				\
	gnm-format.c				\
	gnm-graph-window.c			\
	gnm-pane.c				\
	gnm-pane-impl.h				\
	gnumeric-simple-canvas.c		\
	graph.c					\
	gutils.c				\
	gui-clipboard.c				\
	gui-file.c				\
	gui-util.c				\
	hlink.c					\
	history.c				\
	input-msg.c				\
	item-bar.c				\
	item-cursor.c				\
	item-edit.c				\
	item-grid.c				\
	libgnumeric.c				\
	mathfunc.c				\
	mstyle.c				\
	number-match.c				\
	parser.y				\
	parse-util.c				\
	pattern.c				\
	position.c				\
	preview-grid.c				\
	preview-grid-impl.h			\
	print-info.c				\
	rangefunc.c				\
	rangefunc-strings.c			\
	ranges.c				\
	rendered-value.c			\
	search.c				\
	selection.c				\
	session.c				\
	sheet.c					\
	sheet-view.c		  		\
	sheet-control.c		  		\
	sheet-control-gui.c			\
	sheet-merge.c				\
	sheet-autofill.c			\
	sheet-filter.c				\
	sheet-utils.c				\
	sheet-object.c				\
	sheet-object-cell-comment.c		\
	gnm-so-filled.c				\
	gnm-so-line.c				\
	gnm-so-polygon.c			\
	sheet-object-graph.c			\
	sheet-object-image.c			\
	sheet-object-widget.c			\
	sheet-style.c				\
	gnm-plugin.c				\
	sort.c					\
	stf.c					\
	stf-export.c				\
	stf-parse.c				\
	style.c					\
	style-border.c				\
	style-color.c				\
	style-conditions.c			\
	symbol.c				\
	validation.c				\
	value.c					\
	value-sheet.c				\
	workbook.c				\
	workbook-cmd-format.c			\
	workbook-view.c				\
	workbook-control.c 			\
	wbc-gtk.c				\
	wbc-gtk-actions.c			\
	wbc-gtk-edit.c				\
	xml-sax-read.c				\
	xml-sax-write.c				\
	gnumeric-gconf.c			\
	print.c					\
	print-cell.c                                 \
	go-data-slicer-field.h                                 \
	go-data-slicer-field.c

# When we switch to subversion fix this to nest as
# libspreadsheet-ver/spreadsheet/header
# doing it now would require massive cvs surgery.
libspreadsheet_includedir = $(includedir)/libspreadsheet-@GNUMERIC_API_VER@/spreadsheet
libspreadsheet_include_HEADERS = 	\
	application.h				\
	auto-format.h				\
	cell.h					\
	cell-draw.h				\
	cellspan.h				\
	clipboard.h				\
	cmd-edit.h				\
	collect.h				\
	colrow.h				\
	command-context.h			\
	command-context-stderr.h		\
	commands.h				\
	complete.h				\
	complete-sheet.h			\
	complex.h				\
	consolidate.h				\
	dependent.h				\
	expr.h					\
	expr-impl.h				\
	expr-name.h				\
	file-autoft.h				\
	format-template.h			\
	func.h					\
	func-builtin.h				\
	gnm-command-impl.h			\
	gnm-commands-slicer.h			\
	gnm-data-cache-source.h			\
	gnm-datetime.h				\
	gnm-format.h				\
	gnm-graph-window.h			\
	gnm-pane.h				\
	gnm-sheet-slicer.h			\
	gnm-style-impl.h			\
	gnumeric.h				\
	gnumeric-simple-canvas.h		\
	go-data-cache-source.h 			\
	go-data-cache.h 			\
	go-data-cache-field.h 			\
	go-val.h				\
	graph.h					\
	gutils.h				\
	gui-clipboard.h				\
	gui-file.h				\
	gui-util.h				\
	gui-gnumeric.h				\
	hlink.h					\
	hlink-impl.h				\
	history.h				\
	input-msg.h				\
	item-bar.h				\
	item-cursor.h				\
	item-edit.h				\
	item-grid.h				\
	libgnumeric.h				\
	mathfunc.h				\
	mstyle.h				\
	number-match.h				\
	numbers.h				\
	parse-util.h				\
	pattern.h				\
	position.h				\
	preview-grid.h				\
	print-info.h				\
	rangefunc.h				\
	rangefunc-strings.h			\
	ranges.h				\
	regression.h				\
	rendered-value.h			\
	search.h				\
	selection.h				\
	session.h				\
	sheet.h					\
	sheet-view.h    			\
	sheet-control.h    			\
	sheet-control-priv.h 			\
	sheet-control-gui.h			\
	sheet-control-gui-priv.h		\
	sheet-merge.h				\
	sheet-private.h				\
	sheet-autofill.h			\
	sheet-filter.h				\
	sheet-utils.h				\
	sheet-filter-combo.h			\
	sheet-object.h				\
	sheet-object-impl.h			\
	sheet-object-cell-comment.h		\
	sheet-object-widget-impl.h		\
	gnm-so-filled.h				\
	gnm-so-line.h				\
	gnm-so-polygon.h			\
	sheet-object-graph.h			\
	sheet-object-image.h			\
	sheet-object-widget.h			\
	sheet-style.h				\
	gnm-plugin.h				\
	sort.h					\
	stf.h					\
	stf-export.h				\
	stf-parse.h				\
	style.h					\
	style-border.h				\
	style-color.h				\
	style-conditions.h			\
	style-font.h				\
	symbol.h				\
	validation.h				\
	validation-combo.h			\
	value.h					\
	workbook.h				\
	workbook-priv.h				\
	workbook-cmd-format.h			\
	workbook-view.h				\
	workbook-control.h 			\
	workbook-control-priv.h 		\
	wbc-gtk.h				\
	wbc-gtk-impl.h				\
	xml-io-version.h			\
	xml-sax.h				\
	gnumeric-gconf.h			\
	print.h					\
	print-cell.h

gnumeric_SOURCES =				\
	io-context-gtk.c			\
	io-context-gtk.h			\
	main-application.c

gnumeric_LDADD =				\
	libspreadsheet.la			\
	$(GNUMERIC_LIBS)			\
	$(INTLLIBS)				\
	$(HILDON_LIBS)
gnumeric_LDFLAGS = -export-dynamic

ssconvert_LDADD   = $(gnumeric_LDADD)
ssconvert_LDFLAGS = -export-dynamic
ssconvert_SOURCES =				\
	ssconvert.c
ssindex_LDADD   = $(gnumeric_LDADD)
ssindex_LDFLAGS = -export-dynamic
ssindex_SOURCES =				\
	ssindex.c
ssgrep_LDADD   = $(gnumeric_LDADD)
ssgrep_LDFLAGS = -export-dynamic
ssgrep_SOURCES =				\
	ssgrep.c
sstest_LDADD   = $(gnumeric_LDADD)
sstest_LDFLAGS = -export-dynamic
sstest_SOURCES =				\
	sstest.c

test_pango_LDADD   = $(gnumeric_LDADD)
test_pango_LDFLAGS = -export-dynamic
test_pango_SOURCES =				\
	test-pango.c

# A stub program for testing
#bin_PROGRAMS += harness
#harness_LDADD   = $(gnumeric_LDADD)
#harness_LDFLAGS = -export-dynamic
#harness_SOURCES = test-harness.c

if WITH_NATIVE_WIN32
    gnumeric_LDADD += gnumeric_rc.o
    gnumeric_LDFLAGS	+= -mwindows
    ssconvert_LDFLAGS	+= -mconsole
    ssindex_LDFLAGS	+= -mconsole
    ssgrep_LDFLAGS	+= -mconsole

    libspreadsheet_la_DEPENDENCIES = libspreadsheet.def
    libspreadsheet_la_LDFLAGS += -no-undefined -export-symbols libspreadsheet.def
endif

if WITH_WIN32
libspreadsheet.def: local.def 
	echo EXPORTS > $@ && \
	cat $^ | sort >> $@

local.def: stamp-local.def
	@true

stamp-local.def: $(libspreadsheet_include_HEADERS) Makefile $(top_srcdir)/tools/dumpdef.pl
	hdrs='$(libspreadsheet_include_HEADERS) dialogs/dialogs.h dialogs/tool-dialogs.h tools/goal-seek.h tools/analysis-tools.h tools/gnm-solver.h widgets/gnumeric-expr-entry.h'; \
	hdrs_list=''; \
	for hdr in $$hdrs; do \
	  if test -f $(srcdir)/$$hdr; then \
	    hdrs_list="$$hdrs_list $(srcdir)/$$hdr"; \
	  else \
	    hdrs_list="$$hdrs_list $$hdr"; \
	  fi; \
	done; \
	cat $(top_builddir)/gnumeric-config.h $$hdrs_list | \
		sed -e 's/^#[ \t]*include[ \t]\+.*$$//g' | \
		$(CPP) $(AM_CPPFLAGS) "-DGNM_VAR_DECL=__declspec(dllexport)" -P - > xgen-localdef.1 && \
	perl $(top_srcdir)/tools/dumpdef.pl \
		xgen-localdef.1 > xgen-localdef.2 \
	&& (cmp -s xgen-localdef.2 local.def || \
		cp xgen-localdef.2 local.def) \
	&& rm -f xgen-localdef.1 xgen-localdef.2 \
	&& echo timestamp > $@	

endif

gnumeric_app_libs =				\
	dialogs/libdialogs.la			\
	tools/libtools.la			\
	widgets/libwidgets.la

# Rules for the marshalers code:
GENMARSHAL_COMMAND = $(GLIB_GENMARSHAL) --prefix=gnm_

.list.h:
	$(GENMARSHAL_COMMAND) --header $< >$@

.list.c:
	(echo '/* This file has been automatically generated.  Do not edit. */' && \
	echo '#include "$*.h"' && \
	$(GENMARSHAL_COMMAND) --body $< ) >$@

# Depends on this Makefile, because it uses make variables.
gnumeric-paths.h: Makefile
	@echo 'creating $@'
	@( \
	echo '/* This file has been automatically generated.  Do not edit. */'; \
	echo ''; \
	echo '#ifndef GNUMERIC_PATHS_H'; \
	echo '#define GNUMERIC_PATHS_H'; \
	echo ''; \
	echo '#define GNUMERIC_PREFIX		"$(prefix)"'; \
	echo '#define GNUMERIC_SYSCONFDIR	"$(sysconfdir)"'; \
	echo '#define GNUMERIC_DATADIR		"$(gnumeric_datadir)"'; \
	echo '#define GNUMERIC_LIBDIR		"$(gnumeric_libdir)"'; \
	echo '#define GNUMERIC_ICONDIR		"$(gnumeric_icondir)"'; \
	echo '#define GNUMERIC_LOCALEDIR	"$(gnumeric_localedir)"'; \
	echo ''; \
	echo '#endif /* GNUMERIC_PATHS_H */'; \
	) >$@

if WITH_NATIVE_WIN32
gnumeric_rc.o: gnumeric.rc
	$(WINDRES) -DVERSION=$(VERSION) -DVERSION_NUMBER=`echo $(VERSION).0.0.0 | sed 's/\([0-9]*\)[^.]*\.*\([0-9]*\)[^.]*\.*\([0-9]*\)[^.]*\.*\([0-9]*\).*/\1,\2,\3,\4/'` --include-dir $(srcdir) -i $< -o $@
install-exec-hook:
else
install-exec-hook:
	cd $(DESTDIR)$(bindir) && \
	mv -f gnumeric$(EXEEXT) gnumeric-$(VERSION)$(EXEEXT) && \
	$(LN_S) gnumeric-$(VERSION)$(EXEEXT) gnumeric$(EXEEXT)
endif

uninstall-hook:
	-cd $(DESTDIR)$(bindir) && \
	rm -f gnumeric-$(VERSION)$(EXEEXT)

# include conditional files to ensure everything gets distributed no matter
# what the build
EXTRA_DIST = \
	gnm-marshalers.list			\
	README.gnm_float			\
	gnumeric.rc gnm-i18n.h			\
	GNOME_Gnumeric-gtk.xml.in		\
	HILDON_Gnumeric-gtk.xml.in

gtkactiondir=$(gnumeric_datadir)

if USE_HILDON
gtkaction_DATA = HILDON_Gnumeric-gtk.xml
HILDON_Gnumeric-gtk.xml: HILDON_Gnumeric-gtk.xml.in
	sed -e 's/ _name *=/ name=/' <$<  >$@
DISTCLEANFILES = HILDON_Gnumeric-gtk.xml
else
gtkaction_DATA = GNOME_Gnumeric-gtk.xml
GNOME_Gnumeric-gtk.xml: GNOME_Gnumeric-gtk.xml.in
	sed -e 's/ _name *=/ name=/' <$<  >$@
DISTCLEANFILES = GNOME_Gnumeric-gtk.xml
endif

##check_PROGRAMS += number-match
##number_match_SOURCES = number-match.c  formats.c dates.c number-match-test.c
##number_match_LDADD = 			\
##	$(GNOME_LIBDIR) 		\
##	$(GNOMEUI_LIBS)			\
##	$(GNOME_XML_LIB) 		\
##	$(INTLLIBS)
