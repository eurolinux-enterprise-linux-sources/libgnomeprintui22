SUBDIRS = libgnomeprintui po doc

DIST_SUBDIRS = $(SUBDIRS) examples tests

EXTRA_DIST = \
	MAINTAINERS			\
	ChangeLog			\
	OChangeLog.gpaui.2002-12-17	\
	gtk-doc.make			\
	libgnomeprintui-zip.in		\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in

CLEANFILES=intltool-extract intltool-merge intltool-update

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc
