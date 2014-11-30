/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2006, Thomas Leonard and others (see changelog for details).
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


/* 
 * xtypes.c - Extended filesystem attribute support for MIME types
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>

#include "global.h"
#include "type.h"
#include "xtypes.h"
#include "options.h"

#include "diritem.h"
#include "pixmaps.h"
#include "support.h"
#include "gui_support.h"

Option o_xattr_ignore;

#define RETURN_IF_IGNORED(val) if(o_xattr_ignore.int_value) return (val)

#if defined(HAVE_GETXATTR)
/* Linux implementation */

#include <dlfcn.h>

static int (*dyn_setxattr)(const char *path, const char *name,
		     const void *value, size_t size, int flags) = NULL;
static ssize_t (*dyn_getxattr)(const char *path, const char *name,
			 void *value, size_t size) = NULL;
static ssize_t (*dyn_listxattr)(const char *path, char *list,
			 size_t size) = NULL;

void xattr_init(void)
{
	void *libc;
	
	libc = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
	if (!libc)
	{
		/* Try a different name for uClib support */
		libc = dlopen("libc.so", RTLD_LAZY | RTLD_NOLOAD);
	}

	if (!libc)
		return;	/* Give up on xattr support */

	dyn_setxattr = (void *) dlsym(libc, "setxattr");
	dyn_getxattr = (void *) dlsym(libc, "getxattr");
	dyn_listxattr = (void *) dlsym(libc, "listxattr");
	
	option_add_int(&o_xattr_ignore, "xattr_ignore", FALSE);
}

int xattr_supported(const char *path)
{
	char buf[1];
	ssize_t nent;
	
	RETURN_IF_IGNORED(FALSE);
	
	if (!dyn_getxattr)
		return FALSE;

	if(path) {
		errno=0;
		nent=dyn_getxattr(path, XATTR_MIME_TYPE, buf, sizeof(buf));

		if(nent<0 && errno==ENOTSUP)
			return FALSE;
	}

	return TRUE;
}

int xattr_have(const char *path)
{
	ssize_t nent;
	
	RETURN_IF_IGNORED(FALSE);
	
	if (!dyn_listxattr)
		return FALSE;

	errno=0;
	nent=dyn_listxattr(path, NULL, 0);

	if(nent<0 && errno==ERANGE)
		return TRUE;
	
	return (nent>0);
}

gchar *xattr_get(const char *path, const char *attr, int *len)
{
	ssize_t size;
	gchar *buf;

	RETURN_IF_IGNORED(NULL);
	
	if (!dyn_getxattr)
		return NULL;

	size = dyn_getxattr(path, attr, "", 0);
	if (size > 0)
	{
		int new_size;

		buf = g_new(gchar, size + 1);
		new_size = dyn_getxattr(path, attr, buf, size);

		if(size == new_size)
		{
			buf[size] = '\0';
			
			if(len)
				*len=(int) size;

			return buf;
		}

		g_free(buf);
	}

	return NULL;

}

/* 0 on success */
int xattr_set(const char *path, const char *attr,
	      const char *value, int value_len)
{
	if(o_xattr_ignore.int_value)
	{
		errno = ENOSYS;
		return 1;
	}

	if (!dyn_setxattr)
	{
		errno = ENOSYS;
		return 1; /* Set attr failed */
	}

	if(value && value_len<0)
		value_len = strlen(value);

	return dyn_setxattr(path, attr, value, value_len, 0);
}

#elif defined(HAVE_ATTROPEN)

/* Solaris 9 implementation */

void xattr_init(void)
{	
	option_add_int(&o_xattr_ignore, "xattr_ignore", FALSE);
}

int xattr_supported(const char *path)
{
	RETURN_IF_IGNORED(FALSE);
#ifdef _PC_XATTR_ENABLED
	if(!path)
		return TRUE;
	
	return pathconf(path, _PC_XATTR_ENABLED);
#else
	return FALSE;
#endif
}

int xattr_have(const char *path)
{
	RETURN_IF_IGNORED(FALSE);
#ifdef _PC_XATTR_EXISTS
	return pathconf(path, _PC_XATTR_EXISTS)>0;
#else
	return FALSE;
#endif
}

#define MAX_ATTR_SIZE BUFSIZ
gchar *xattr_get(const char *path, const char *attr, int *len)
{
	int fd;
	char *buf=NULL;
	int nb;

	RETURN_IF_IGNORED(NULL);

#ifdef _PC_XATTR_EXISTS
	if(!pathconf(path, _PC_XATTR_EXISTS))
		return NULL;
#endif

	fd=attropen(path, attr, O_RDONLY);
  
	if(fd>=0) {
		buf = g_new(gchar, MAX_ATTR_SIZE);
		nb=read(fd, buf, MAX_ATTR_SIZE);
		if(nb>0) {
			buf[nb]=0;
		}
		close(fd);

		if(len)
			*len=nb;
	}

	return buf;
}

int xattr_set(const char *path, const char *attr,
	      const char *value, int value_len)
{
	int fd;
	int nb;

	if(o_xattr_ignore.int_value)
	{
		errno = ENOSYS;
		return 1;
	}

	if(value && value_len<0)
		value_len = strlen(value);

	fd=attropen(path, attr, O_WRONLY|O_CREAT, 0644);
	if(fd>0) {
		
		nb=write(fd, value, value_len);
		if(nb==value_len)
			ftruncate(fd, (off_t) nb);

		close(fd);

		if(nb>0)
			return 0;
	}
  
	return 1; /* Set type failed */
}

#else
/* No extended attributes available */

void xattr_init(void)
{
}

int xattr_supported(const char *path)
{
	return FALSE;
}

int xattr_have(const char *path)
{
	return FALSE;
}

gchar *xattr_get(const char *path, const char *attr, int *len)
{
	/* Fall back to non-extended */
	return NULL;
}

int xattr_set(const char *path, const char *attr,
	      const char *value, int value_len)
{
	errno = ENOSYS;
	return 1; /* Set type failed */
}

#endif

MIME_type *xtype_get(const char *path)
{
	MIME_type *type = NULL;
	gchar *buf;
	char *nl;

	buf = xattr_get(path, XATTR_MIME_TYPE, NULL);

	if(buf)
	{
		nl = strchr(buf, '\n');
		if(nl)
			*nl = 0;
		type = mime_type_lookup(buf);
		g_free(buf);
	}
	return type;
}

int xtype_set(const char *path, const MIME_type *type)
{
	int res;
	gchar *ttext;

	if(o_xattr_ignore.int_value)
	{
		errno = ENOSYS;
		return 1;
	}

	ttext = g_strdup_printf("%s/%s", type->media_type, type->subtype);
	res = xattr_set(path, XATTR_MIME_TYPE, ttext, -1);
	g_free(ttext);

	return res;
}

/* Extended attributes browser */
#if defined(HAVE_GETXATTR) /* Linux-only for now */

enum
{
	COLUMN_NAME,
	COLUMN_VALUE
};

typedef struct
{
	gchar	*name;
	gchar	*value;
}
XAttr;

GArray* xattr_list(const char *path)
{
	ssize_t len;
	gchar 	*list;
	gchar 	*l;
	GArray 	*xarr;
	GRegex	*re;
	XAttr	at;

	xarr = g_array_sized_new(FALSE,FALSE,sizeof(XAttr),0);
	re = g_regex_new("^user\\.",0,0,NULL);

	len = dyn_listxattr(path, NULL, 0);
	if(len <= 0)
		return xarr;

	list = g_new(gchar, len);
	len = dyn_listxattr(path, list, len);
	if(len < 0)
		return xarr;

	for(l=list;l != list + len;l = strchr(l,'\0')+1) {
		if(*l == '\0')
			continue;

		/* show only user attributes? */
		/*if(!g_regex_match(re,l,0,NULL))*/
			/*continue;*/

		at.name = g_strdup_printf("%s",l);
		at.value = xattr_get(path, at.name, NULL);

		g_array_append_vals(xarr, &at, 1);
	}

	g_free(list);
	g_regex_unref(re);

	return xarr;
}

static void dialog_response(GtkWidget *dialog, gint response, gpointer data)
{
	switch(response) {
		case GTK_RESPONSE_CLOSE: {
			g_array_free(((gpointer *)data)[1],TRUE);
			g_free(data);
			gtk_widget_destroy(dialog);
		}
		break;

		case GTK_RESPONSE_APPLY: {
			GArray *arr = (GArray *)((gpointer *)data)[1];
			gint i;
			for(i=0;i<arr->len;i++)
				g_print("%s: %s\n", g_array_index(arr, XAttr, i).name, g_array_index(arr, XAttr, i).value);  
		}
		break;
	}
}

static void add_item(GtkWidget *button, gpointer data)
{
	XAttr item;
	GtkTreeIter iter;
	GtkTreeModel *model = (GtkTreeModel *)((gpointer *)data)[0];
	GArray *arr = (GArray *)((gpointer *)data)[1];

	item.name = g_strdup("");
	item.value = g_strdup("");
	g_array_append_vals(arr,&item,1);

	gtk_list_store_append(GTK_LIST_STORE(model),&iter);
	gtk_list_store_set(GTK_LIST_STORE(model),&iter,COLUMN_NAME,item.name,COLUMN_VALUE,item.value,-1);
}

static void remove_item(GtkWidget *button, gpointer data)
{
	GtkTreeIter iter;
	GtkTreeView *treeview = (GtkTreeView *)((gpointer *)data)[2];
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	GArray *arr = (GArray *)((gpointer *)data)[1];

	if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gint i;
		GtkTreePath *path;

		path = gtk_tree_model_get_path (model, &iter);
		i = gtk_tree_path_get_indices(path)[0];
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

		g_array_remove_index(arr,i);

		gtk_tree_path_free (path);
	}
}

static void cell_edited(GtkCellRendererText *cell, const gchar *path_string, const gchar *new_text, gpointer data)
{
	GArray *arr = (GArray *)((gpointer *)data)[1];
	GtkTreeModel *model = (GtkTreeModel *)((gpointer *)data)[0];
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter iter;

	gint column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cell), "column"));

	gtk_tree_model_get_iter (model, &iter, path);

	switch (column) {
		case COLUMN_NAME:
		{
			gint i;
			gchar *old_text;

			gtk_tree_model_get (model, &iter, column, &old_text, -1);
			g_free (old_text);

			i = gtk_tree_path_get_indices (path)[0];
			g_free (g_array_index (arr, XAttr, i).name);
			g_array_index (arr, XAttr, i).name = g_strdup (new_text);

			gtk_list_store_set (GTK_LIST_STORE (model), &iter, column,
								g_array_index (arr, XAttr, i).name, -1);
		
		}
		break;

		case COLUMN_VALUE:
		{
			gint i;
			gchar *old_text;

			gtk_tree_model_get (model, &iter, column, &old_text, -1);
			g_free (old_text);

			i = gtk_tree_path_get_indices (path)[0];
			g_free (g_array_index (arr, XAttr, i).value);
			g_array_index (arr, XAttr, i).value = g_strdup (new_text);

			gtk_list_store_set (GTK_LIST_STORE (model), &iter, column,
								g_array_index (arr, XAttr, i).value, -1);
		}
		break;
	}

	gtk_tree_path_free (path);
}

static GtkTreeModel* create_model(GArray *arr) {
	int i;
	GtkListStore *model;
	GtkTreeIter iter;
	gchar *name, *value;
	gchar *u8nam, *u8val;

	model = gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING);
	for(i=0;i<arr->len;i++) {
		name = g_array_index(arr,XAttr,i).name;
		value = g_array_index(arr,XAttr,i).value;
		u8nam = to_utf8(name);
		u8val = to_utf8(value);

		gtk_list_store_append(model,&iter);
		gtk_list_store_set(model,&iter,COLUMN_NAME,u8nam,COLUMN_VALUE,u8val,-1);
		g_free(u8nam);
		g_free(u8val);
	}
	return GTK_TREE_MODEL(model);
}

void xattrs_browser(DirItem *item, const guchar *path)
{
	GtkDialog	*dialog;
	GtkWidget	*content, *hbox, *name;
	GtkWidget	*sw;
	GtkWidget	*tree;
	GtkWidget	*but;
	GtkTreeModel *mod;
	GArray		*arr;
	GtkCellRenderer	*ren;
	gpointer 		*data;

	g_return_if_fail(item != NULL && path != NULL);

	data = g_new(gpointer, 3);
	arr = xattr_list(path);

	dialog = GTK_DIALOG(gtk_dialog_new());
	gtk_window_set_title(GTK_WINDOW(dialog), _("Extended attributes"));
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);
	gtk_window_set_default_size(GTK_WINDOW(dialog),300,150);

	content = gtk_dialog_get_content_area(dialog);

	hbox = gtk_hbox_new(FALSE,4);
	gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox),
			   gtk_image_new_from_pixbuf(di_image(item)->pixbuf),
			   FALSE, FALSE, 4);

	if (g_utf8_validate(item->leafname, -1, NULL))
		name = gtk_label_new(item->leafname);
	else
	{
		guchar *u8;

		u8 = to_utf8(item->leafname);
		name = gtk_label_new(u8);
		g_free(u8);
	}
	gtk_label_set_line_wrap(GTK_LABEL(name), TRUE);
	gtk_label_set_line_wrap_mode(GTK_LABEL(name), PANGO_WRAP_WORD_CHAR);
	gtk_box_pack_start(GTK_BOX(hbox), name, FALSE, TRUE, 4);
	
	make_heading(name, PANGO_SCALE_X_LARGE);

	sw = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
			GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
			GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(content),sw,TRUE,TRUE,0);

	mod = create_model(arr);
	tree = gtk_tree_view_new_with_model(mod);
	/* wrapper for variable passing */
	data[0] = mod; data[1] = arr; data[2] = tree;
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tree),TRUE);
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),
			GTK_SELECTION_SINGLE);
	/* add columns */
	ren = gtk_cell_renderer_text_new();
	g_object_set(ren,"editable",TRUE,NULL);
	g_signal_connect(ren, "edited", G_CALLBACK(cell_edited), data);
	g_object_set_data(G_OBJECT(ren),"column",GINT_TO_POINTER(0));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,
			"Name",ren,"text",0,NULL);
	ren = gtk_cell_renderer_text_new();
	g_object_set(ren,"editable",TRUE,NULL);
	g_signal_connect(ren, "edited", G_CALLBACK(cell_edited), data);
	g_object_set_data(G_OBJECT(ren),"column",GINT_TO_POINTER(1));
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,
			"Value",ren,"text",1,NULL);
	g_object_unref(mod);
	gtk_container_add(GTK_CONTAINER(sw),tree);

	hbox = gtk_hbox_new(FALSE,4);
	but = gtk_button_new_from_stock(GTK_STOCK_ADD);
	g_signal_connect(but, "clicked", G_CALLBACK(add_item), data);
	gtk_box_pack_start(GTK_BOX(hbox),but,FALSE,FALSE,0);
	but = gtk_button_new_from_stock(GTK_STOCK_REMOVE);
	g_signal_connect(but, "clicked", G_CALLBACK(remove_item), data);
	gtk_box_pack_start(GTK_BOX(hbox),but,FALSE,FALSE,0);
	gtk_box_pack_start(GTK_BOX(content),hbox,FALSE,FALSE,0);

	gtk_dialog_add_button(dialog,GTK_STOCK_APPLY,GTK_RESPONSE_APPLY);
	gtk_dialog_add_button(dialog,GTK_STOCK_CLOSE,GTK_RESPONSE_CLOSE);
	g_signal_connect(dialog, "response", G_CALLBACK(dialog_response), data);
	gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);
	gtk_widget_show_all(GTK_WIDGET(dialog));
}
#endif
