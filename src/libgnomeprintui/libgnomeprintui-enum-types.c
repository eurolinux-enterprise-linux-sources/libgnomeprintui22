
/* Generated data (by glib-mkenums) */

#include "libgnomeprintui-enum-types.h"
#include "gnome-print-dialog.h"
#include "gnome-print-paper-selector.h"

/* enumerations from "gnome-print-dialog.h" */
GType
gnome_print_range_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GNOME_PRINT_RANGETYPE_NONE, "GNOME_PRINT_RANGETYPE_NONE", "none" },
      { GNOME_PRINT_RANGETYPE_CUSTOM, "GNOME_PRINT_RANGETYPE_CUSTOM", "custom" },
      { GNOME_PRINT_RANGETYPE_PAGES, "GNOME_PRINT_RANGETYPE_PAGES", "pages" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GnomePrintRangeType", values);
  }
  return etype;
}
GType
gnome_print_dialog_range_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GFlagsValue values[] = {
      { GNOME_PRINT_RANGE_CURRENT, "GNOME_PRINT_RANGE_CURRENT", "current" },
      { GNOME_PRINT_RANGE_ALL, "GNOME_PRINT_RANGE_ALL", "all" },
      { GNOME_PRINT_RANGE_RANGE, "GNOME_PRINT_RANGE_RANGE", "range" },
      { GNOME_PRINT_RANGE_SELECTION, "GNOME_PRINT_RANGE_SELECTION", "selection" },
      { GNOME_PRINT_RANGE_SELECTION_UNSENSITIVE, "GNOME_PRINT_RANGE_SELECTION_UNSENSITIVE", "selection-unsensitive" },
      { 0, NULL, NULL }
    };
    etype = g_flags_register_static ("GnomePrintDialogRangeFlags", values);
  }
  return etype;
}
GType
gnome_print_dialog_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GFlagsValue values[] = {
      { GNOME_PRINT_DIALOG_RANGE, "GNOME_PRINT_DIALOG_RANGE", "range" },
      { GNOME_PRINT_DIALOG_COPIES, "GNOME_PRINT_DIALOG_COPIES", "copies" },
      { 0, NULL, NULL }
    };
    etype = g_flags_register_static ("GnomePrintDialogFlags", values);
  }
  return etype;
}
GType
gnome_print_buttons_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GNOME_PRINT_DIALOG_RESPONSE_PRINT, "GNOME_PRINT_DIALOG_RESPONSE_PRINT", "print" },
      { GNOME_PRINT_DIALOG_RESPONSE_PREVIEW, "GNOME_PRINT_DIALOG_RESPONSE_PREVIEW", "preview" },
      { GNOME_PRINT_DIALOG_RESPONSE_CANCEL, "GNOME_PRINT_DIALOG_RESPONSE_CANCEL", "cancel" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static ("GnomePrintButtons", values);
  }
  return etype;
}

/* enumerations from "gnome-print-paper-selector.h" */
GType
gnome_paper_selector_flags_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GFlagsValue values[] = {
      { GNOME_PAPER_SELECTOR_MARGINS, "GNOME_PAPER_SELECTOR_MARGINS", "margins" },
      { GNOME_PAPER_SELECTOR_FEED_ORIENTATION, "GNOME_PAPER_SELECTOR_FEED_ORIENTATION", "feed-orientation" },
      { 0, NULL, NULL }
    };
    etype = g_flags_register_static ("GnomePaperSelectorFlags", values);
  }
  return etype;
}

/* Generated data ends here */

