noinst_PROGRAMS = test-page-selector test-dialog test-preview simple test-layout-selector

INCLUDES = \
	-I$(top_srcdir) \
	$(LIBGNOMEPRINTUI_CFLAGS) \
     -DWE_ARE_LIBGNOMEPRINT_INTERNALS \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-DBINDIR=\""$(bindir)"\" 	\
	-DDATADIR=\""$(datadir)"\"	\
	-DG_DISABLE_DEPRECATED		\
	-DGDK_DISABLE_DEPRECATED	\
	-DGDK_PIXBUF_DISABLE_DEPRECATED

if EXAMPLES_USE_LOCAL_LIBGNOMEPRINT
libgnomeprint_local=$(top_builddir)/../libgnomeprint/libgnomeprint/libgnomeprint-2-2.la
else
libgnomeprint_local=
endif

## including ../../libngomeprint/libgnomeprint/libgnomeprint-2.la is not very robust
## but it allows us to not have to make install in libgnomeprint to test new stuff
printui_libs = \
	$(top_builddir)/libgnomeprintui/libgnomeprintui-2-2.la \
	$(libgnomeprint_local) \
	$(LIBGNOMEPRINTUI_LIBS)

test_dialog_SOURCES = test-dialog.c test-common.c test-common.h
test_dialog_LDFLAGS =
test_dialog_DEPENDENCIES = $(DEPS)
test_dialog_LDADD = $(printui_libs)

test_preview_SOURCES = test-preview.c
test_preview_LDFLAGS =
test_preview_DEPENDENCIES = $(DEPS)
test_preview_LDADD = $(printui_libs)

test_page_selector_SOURCES = test-page-selector.c
test_page_selector_DEPENDENCIES = $(DEPS)
test_page_selector_LDADD = $(printui_libs)

test_layout_selector_SOURCES = test-layout-selector.c test-common.c test-common.h
test_layout_selector_DEPENDENCIES = $(DEPS)
test_layout_selector_LDADD = $(printui_libs)

simple_SOURCES = simple.c
simple_LDFLAGS =
simple_DEPENDENCIES = $(DEPS)
simple_LDADD = $(printui_libs);
