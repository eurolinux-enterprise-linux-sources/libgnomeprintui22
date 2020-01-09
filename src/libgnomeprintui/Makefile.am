SUBDIRS = gpaui

GNOME_PRINT_JOB_PREVIEW_DATADIR = $(datadir)/libgnomeprintui/$(VERSION)

libgnomeprintui_2_2_uidir = $(GNOME_PRINT_JOB_PREVIEW_DATADIR)
libgnomeprintui_2_2_ui_DATA = gnome-print-job-preview.xml

lib_LTLIBRARIES = libgnomeprintui-2-2.la

INCLUDES = \
	-I$(top_srcdir)			\
	$(LIBGNOMEPRINTUI_CFLAGS)	\
	-DG_LOG_DOMAIN=\"libgnomeprintui\"	\
	-DVERSION=\"$(VERSION)\"	\
	-DWE_ARE_LIBGNOMEPRINT_INTERNALS	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DGNOME_PRINT_LIBDIR=\"$(libdir)/gnome-print\"	\
	-DBINDIR=\""$(bindir)"\"	\
	-DDATADIR=\""$(datadir)"\"	\
	-DGNOME_PRINT_JOB_PREVIEW_DATADIR=\"$(GNOME_PRINT_JOB_PREVIEW_DATADIR)\"	\
	\
	-DG_DISABLE_DEPRECATED		\
	-DGDK_DISABLE_DEPRECATED	\
	-DGDK_PIXBUF_DISABLE_DEPRECATED	

headers = \
	gnome-print-preview.h \
	gnome-font-dialog.h \
	gnome-print-dialog.h \
	gnome-print-unit-selector.h \
	gnome-print-paper-selector.h \
	gnome-print-content-selector.h \
	gnome-print-config-dialog.h \
	gnome-print-job-preview.h

libgnomeprintui_2_2_la_LIBADD = \
	$(LIBGNOMEPRINTUI_LIBS) \
	gpaui/libgpaui.la 

libgnomeprintui_2_2_la_LDFLAGS = \
	-no-undefined \
	-version-info $(GNOMEPRINT_CURRENT):$(GNOMEPRINT_REVISION):$(GNOMEPRINT_AGE)

BUILT_SOURCES = gnome-printui-marshal.c  gnome-printui-marshal.h

libgnomeprintui_2_2_la_SOURCES = \
	gnome-print-i18n.c           gnome-print-i18n.h			\
	gnome-print-preview.c	\
	gnome-canvas-hacktext.c      gnome-canvas-hacktext.h		\
	gnome-print-job-preview.c	\
	gnome-printer-selector.c     gnome-printer-selector.h		\
	gnome-print-copies.c         gnome-print-copies.h		\
	gnome-print-dialog.c \
	gnome-print-config-dialog.c  gnome-print-config-dialog-private.h \
        gnome-print-widget.c         gnome-print-widget.h           \
	gnome-font-dialog.c		\
	gnome-print-content-selector.c \
	gnome-print-layout-selector.c gnome-print-layout-selector.h \
	gnome-print-page-selector.c   gnome-print-page-selector.h \
	gnome-print-paper-preview.c   gnome-print-paper-preview.h \
	gnome-print-unit-selector.c	\
	gnome-print-paper-selector.c	\
	gnome-print-ui-private.c     gnome-print-ui-private.h		\
	$(BUILT_SOURCES) \
	libgnomeprintui-enum-types.c

libgnomeprintuiincdir = $(includedir)/libgnomeprintui-2.2/libgnomeprintui

libgnomeprintuiinc_HEADERS = \
	$(headers) \
	libgnomeprintui-enum-types.h

EXTRA_DIST = libgnomeprintui-2.2.pc.in \
	libgnomeprintui-uninstalled-2.2.pc.in \
	gnome-printui-marshal.list	\
	gnome-print-job-preview.xml

CLEANFILES = $(BUILT_SOURCES)		\
	s-enum-types-h			\
	s-enum-types-c			\
	libgnomeprintui-enum-types.h	\
	libgnomeprintui-enum-types.c

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgnomeprintui-2.2.pc

gnome-printui-marshal.h: gnome-printui-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=libgnomeprintui_marshal > $@

gnome-printui-marshal.c: gnome-printui-marshal.list $(GLIB_GENMARSHAL)
	echo "#include \"gnome-printui-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --body --prefix=libgnomeprintui_marshal >> $@


libgnomeprintui-enum-types.h: s-enum-types-h
	@true

s-enum-types-h: $(headers) Makefile
	( cd $(srcdir) && glib-mkenums \
			--fhead "#ifndef __LIBGNOMEPRINTUI_ENUM_TYPES_H__\n#define __LIBGNOMEPRINTUI_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
			--fprod "/* enumerations from \"@filename@\" */\n" \
			--vhead "GType @enum_name@_get_type (void);\n#define GNOME_TYPE_PRINT_@ENUMSHORT@ (@enum_name@_get_type())\n" 	\
			--ftail "G_END_DECLS\n\n#endif /* __LIBGNOMEPRINTUI_ENUM_TYPES_H__ */" \
		$(headers) ) > tmp-libgnomeprintui-enum-types.h \
	&& (cmp -s tmp-libgnomeprintui-enum-types.h libgnomeprintui-enum-types.h || cp tmp-libgnomeprintui-enum-types.h libgnomeprintui-enum-types.h ) \
	&& rm -f tmp-libgnomeprintui-enum-types.h	\
	&& echo timestamp > $(@F)

libgnomeprintui-enum-types.c: s-enum-types-c
	@true

s-enum-types-c: $(headers) libgnomeprintui-enum-types.h Makefile
	( cd $(srcdir) && glib-mkenums \
			--fhead "#include \"libgnomeprintui-enum-types.h\"\n#include \"gnome-print-dialog.h\"\n#include \"gnome-print-paper-selector.h\"" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" 	\
			--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
		$(headers) ) > tmp-libgnomeprintui-enum-types.c \
	&& (cmp -s tmp-libgnomeprintui-enum-types.c libgnomeprintui-enum-types.c || cp tmp-libgnomeprintui-enum-types.c libgnomeprintui-enum-types.c ) \
	&& rm -f tmp-libgnomeprintui-enum-types.c \
	&& echo timestamp > $(@F)
