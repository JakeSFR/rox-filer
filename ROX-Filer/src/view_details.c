/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* view_details.c - display a list of files in a TreeView */

#include "config.h"

#include <gtk/gtk.h>

#include "global.h"

#include "view_iface.h"
#include "view_details.h"

#include "filer.h"

static gpointer parent_class = NULL;

struct _ViewDetailsClass {
	GtkTreeViewClass parent;
};

typedef struct _ViewDetails ViewDetails;

struct _ViewDetails {
	GtkTreeView treeview;

	FilerWindow *filer_window;	/* Used for styles, etc */

	int	cursor_base;		/* Cursor when minibuffer opened */
};

/* Static prototypes */
static void view_details_finialize(GObject *object);
static void view_details_class_init(gpointer gclass, gpointer data);
static void view_details_init(GTypeInstance *object, gpointer gclass);

static void view_details_iface_init(gpointer giface, gpointer iface_data);

static void view_details_sort(ViewIface *view);
static void view_details_style_changed(ViewIface *view, int flags);
static gboolean view_details_autoselect(ViewIface *view, const gchar *leaf);
static void view_details_add_items(ViewIface *view, GPtrArray *items);
static void view_details_update_items(ViewIface *view, GPtrArray *items);
static void view_details_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data);
static void view_details_clear(ViewIface *view);
static void view_details_select_all(ViewIface *view);
static void view_details_clear_selection(ViewIface *view);
static int view_details_count_items(ViewIface *view);
static int view_details_count_selected(ViewIface *view);
static void view_details_show_cursor(ViewIface *view);
static void view_details_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags);
static void view_details_cursor_to_iter(ViewIface *view, ViewIter *iter);
static void view_details_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected);
static gboolean view_details_get_selected(ViewIface *view, ViewIter *iter);
static void view_details_select_only(ViewIface *view, ViewIter *iter);
static void view_details_set_frozen(ViewIface *view, gboolean frozen);
static void view_details_wink_item(ViewIface *view, ViewIter *iter);
static void view_details_autosize(ViewIface *view);
static gboolean view_details_cursor_visible(ViewIface *view);
static void view_details_set_base(ViewIface *view, ViewIter *iter);
static DirItem *iter_peek(ViewIter *iter);
static DirItem *iter_next(ViewIter *iter);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkWidget *view_details_new(FilerWindow *filer_window)
{
	ViewDetails *view_details;

	view_details = g_object_new(view_details_get_type(), NULL);
	view_details->filer_window = filer_window;

	gtk_range_set_adjustment(GTK_RANGE(filer_window->scrollbar),
		gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(view_details)));

	return GTK_WIDGET(view_details);
}

GType view_details_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (ViewDetailsClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			view_details_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(ViewDetails),
			0,			/* n_preallocs */
			view_details_init
		};
		static const GInterfaceInfo iface_info =
		{
			view_details_iface_init, NULL, NULL
		};

		type = g_type_register_static(gtk_tree_view_get_type(),
						"ViewDetails", &info, 0);
		g_type_add_interface_static(type, VIEW_TYPE_IFACE, &iface_info);
	}

	return type;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void view_details_destroy(GtkObject *view_details)
{
	VIEW_DETAILS(view_details)->filer_window = NULL;
}

static void view_details_finialize(GObject *object)
{
	/* ViewDetails *view_details = (ViewDetails *) object; */

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void view_details_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = view_details_finialize;
	GTK_OBJECT_CLASS(object)->destroy = view_details_destroy;
}

static void view_details_init(GTypeInstance *object, gpointer gclass)
{
	GtkTreeView *treeview = (GtkTreeView *) object;
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;
	GtkListStore *model;

	model = gtk_list_store_new(1, G_TYPE_STRING);

	gtk_tree_view_set_model(treeview, GTK_TREE_MODEL(model));
	column = gtk_tree_view_column_new();
	
	gtk_tree_view_append_column(treeview, column);

	cell = gtk_cell_renderer_toggle_new();
	gtk_tree_view_insert_column_with_attributes(treeview,
			0, NULL, cell);

	cell = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(treeview,
			1, _("Name"), cell, "text", 0);
}

/* Create the handers for the View interface */
static void view_details_iface_init(gpointer giface, gpointer iface_data)
{
	ViewIfaceClass *iface = giface;

	g_assert(G_TYPE_FROM_INTERFACE(iface) == VIEW_TYPE_IFACE);

	/* override stuff */
	iface->sort = view_details_sort;
	iface->style_changed = view_details_style_changed;
	iface->autoselect = view_details_autoselect;
	iface->add_items = view_details_add_items;
	iface->update_items = view_details_update_items;
	iface->delete_if = view_details_delete_if;
	iface->clear = view_details_clear;
	iface->select_all = view_details_select_all;
	iface->clear_selection = view_details_clear_selection;
	iface->count_items = view_details_count_items;
	iface->count_selected = view_details_count_selected;
	iface->show_cursor = view_details_show_cursor;
	iface->get_iter = view_details_get_iter;
	iface->cursor_to_iter = view_details_cursor_to_iter;
	iface->set_selected = view_details_set_selected;
	iface->get_selected = view_details_get_selected;
	iface->set_frozen = view_details_set_frozen;
	iface->select_only = view_details_select_only;
	iface->wink_item = view_details_wink_item;
	iface->autosize = view_details_autosize;
	iface->cursor_visible = view_details_cursor_visible;
	iface->set_base = view_details_set_base;
}

/* Implementations of the View interface. See view_iface.c for comments. */

static void view_details_style_changed(ViewIface *view, int flags)
{
}

static void view_details_sort(ViewIface *view)
{
}

static gboolean view_details_autoselect(ViewIface *view, const gchar *leaf)
{
	return FALSE;
}

static void view_details_add_items(ViewIface *view, GPtrArray *items)
{
}

static void view_details_update_items(ViewIface *view, GPtrArray *items)
{
}

static void view_details_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data)
{
}

static void view_details_clear(ViewIface *view)
{
}

static void view_details_select_all(ViewIface *view)
{
}

static void view_details_clear_selection(ViewIface *view)
{
}

static int view_details_count_items(ViewIface *view)
{
	return 0;
}

static int view_details_count_selected(ViewIface *view)
{
	return 0;
}

static void view_details_show_cursor(ViewIface *view)
{
}

static void view_details_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags)
{
	iter->next = iter_next;
	iter->peek = iter_peek;
}

static void view_details_cursor_to_iter(ViewIface *view, ViewIter *iter)
{
}

static void view_details_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected)
{
}

static gboolean view_details_get_selected(ViewIface *view, ViewIter *iter)
{
	return FALSE;
}

static void view_details_select_only(ViewIface *view, ViewIter *iter)
{
}

static void view_details_set_frozen(ViewIface *view, gboolean frozen)
{
}

static void view_details_wink_item(ViewIface *view, ViewIter *iter)
{
}

static void view_details_autosize(ViewIface *view)
{
}

static gboolean view_details_cursor_visible(ViewIface *view)
{
	return FALSE;
}

static void view_details_set_base(ViewIface *view, ViewIter *iter)
{
}

static DirItem *iter_peek(ViewIter *iter)
{
	return NULL;
}

static DirItem *iter_next(ViewIter *iter)
{
	return NULL;
}