/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include <gconf/gconf-client.h>

#include <libhildondesktop/libhildondesktop.h>

#include <string.h>

#include <osso_bookmark_parser.h>

#include "hd-bookmark-manager.h"

#define HD_BOOKMARK_MANAGER_GET_PRIVATE(object) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((object), HD_TYPE_BOOKMARK_MANAGER, HDBookmarkManagerPrivate))

#define BOOKMARK_SHORTCUTS_GCONF_KEY "/apps/osso/hildon-home/bookmark-shortcuts"

#define BOOKMARK_EXTENSION_LEN 3

/* GConf path for boomarks */
#define BOOKMARKS_GCONF_PATH      "/apps/osso/hildon-home/bookmarks"
#define BOOKMARKS_GCONF_KEY_LABEL BOOKMARKS_GCONF_PATH "/%s/label"
#define BOOKMARKS_GCONF_KEY_URL   BOOKMARKS_GCONF_PATH "/%s/url"
#define BOOKMARKS_GCONF_KEY_ICON  BOOKMARKS_GCONF_PATH "/%s/icon"

/* Definitions for the ID generation */ 
#define ID_VALID_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_+?"
#define ID_SUBSTITUTOR '_'

struct _HDBookmarkManagerPrivate
{
  HDPluginConfiguration *plugin_configuration;

  GtkTreeModel *model;

  GHashTable *available_bookmarks;
};

typedef struct
{
  gchar *name;
  gchar *icon;
  gchar *url;
} HDBookmarkInfo;

G_DEFINE_TYPE (HDBookmarkManager, hd_bookmark_manager, G_TYPE_OBJECT);

static void
hd_bookmark_manager_add_bookmark_item (HDBookmarkManager *manager,
                                       BookmarkItem      *item)
{
  HDBookmarkManagerPrivate *priv = manager->priv;
  HDBookmarkInfo *info;
  GdkPixbuf *pixbuf = NULL;
  gchar *icon_path = NULL;

  g_debug ("hd_bookmark_manager_add_bookmark_item");

  /* If it is a folder recurse over all children */
  if (item->isFolder)
    {
      GSList *c;

      for (c = item->list; c; c = c->next)
        {
          hd_bookmark_manager_add_bookmark_item (manager,
                                                 c->data);
        }

      return;
    }

  /* Else add the bookmark info to our table */
  info = g_slice_new0 (HDBookmarkInfo);

  info->name = g_strndup (item->name, strlen (item->name) - BOOKMARK_EXTENSION_LEN);
  info->icon = g_strdup (item->favicon_file);
  info->url = g_strdup (item->url);

  g_hash_table_insert (priv->available_bookmarks,
                       info->url,
                       info);

  if (item->favicon_file)
    {
      icon_path = g_build_filename (g_get_home_dir (),
                                    FAVICONS_PATH,
                                    item->favicon_file,
                                    NULL);

      pixbuf = gdk_pixbuf_new_from_file (icon_path, NULL);
    }

  gtk_list_store_insert_with_values (GTK_LIST_STORE (priv->model),
                                     NULL, -1,
                                     0, info->name,
                                     1, icon_path,
                                     2, info->url,
                                     3, pixbuf,
                                     -1);

  g_free (icon_path);
}

static gboolean
hd_bookmark_manager_parse_bookmark_files (HDBookmarkManager *manager)
{
  BookmarkItem *root = create_bookmark_new ();
  gboolean result;

  result = get_root_bookmark (&root, MYBOOKMARKS);

/*  if (!result)
    result = get_bookmark_from_backup(&root,
                                      MYBOOKMARKSFILEBACKUP);*/

  if (result)
    hd_bookmark_manager_add_bookmark_item (manager,
                                           root);
  else
    g_warning ("Could not read bookmark file");

  return FALSE;
}

static void
hd_bookmark_manager_init (HDBookmarkManager *manager)
{
  HDBookmarkManagerPrivate *priv;
  manager->priv = HD_BOOKMARK_MANAGER_GET_PRIVATE (manager);
  priv = manager->priv;

  priv->available_bookmarks = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                     g_free, NULL);

  priv->model = GTK_TREE_MODEL (gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->model),
                                        0,
                                        GTK_SORT_ASCENDING);

}

static void
hd_bookmark_manager_class_init (HDBookmarkManagerClass *klass)
{
  g_type_class_add_private (klass, sizeof (HDBookmarkManagerPrivate));
}

HDBookmarkManager *
hd_bookmark_manager_get (void)
{
  static HDBookmarkManager *manager = NULL;

  if (G_UNLIKELY (!manager))
    {
      manager = g_object_new (HD_TYPE_BOOKMARK_MANAGER, NULL);

      gdk_threads_add_idle ((GSourceFunc) hd_bookmark_manager_parse_bookmark_files,
                            manager);
    }

  return manager;
}

GtkTreeModel *
hd_bookmark_manager_get_model (HDBookmarkManager *manager)
{
  HDBookmarkManagerPrivate *priv = manager->priv;

  return g_object_ref (priv->model);
}

void
hd_bookmark_manager_install_bookmark (HDBookmarkManager *manager,
                                      GtkTreeIter     *iter)
{
  HDBookmarkManagerPrivate *priv = manager->priv;
  gchar *label, *icon, *url;

  gtk_tree_model_get (priv->model, iter,
                      0, &label,
                      1, &icon,
                      2, &url,
                      -1);

  hd_shortcuts_add_bookmark_shortcut (url,
                                      label,
                                      icon);

  /* Free memory */
  g_free (label);
  g_free (icon);
  g_free (url);
}
