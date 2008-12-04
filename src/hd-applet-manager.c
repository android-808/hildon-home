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
#include <glib-object.h>
#include <glib/gi18n.h>

#include <gdk/gdkx.h>

#include <X11/Xlib.h>

#include <string.h>

#include "hd-applet-manager.h"

#define HD_APPLET_MANAGER_GET_PRIVATE(object) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((object), HD_TYPE_APPLET_MANAGER, HDAppletManagerPrivate))

#define HD_PLUGIN_MANAGER_CONFIG_GROUP "X-PluginManager"

#define ITEMS_KEY_DESKTOP_FILE "X-Desktop-File"

#define DESKTOP_KEY_TEXT_DOMAIN "X-Text-Domain"
#define DESKTOP_KEY_MULTIPLE "X-Multiple"

struct _HDAppletManagerPrivate 
{
  HDPluginManager *plugin_manager;

  GtkTreeModel *model;

  GList *available_applets;

  GHashTable *displayed_applets;
  GHashTable *used_ids;

  GHashTable *installed;

  GKeyFile *applets_key_file;
};

typedef struct
{
  gchar *name;
  gboolean multiple;
} HDPluginInfo;

G_DEFINE_TYPE (HDAppletManager, hd_applet_manager, G_TYPE_OBJECT);

static void
items_configuration_loaded_cb (HDPluginConfiguration *configuration,
                               GKeyFile              *key_file,
                               HDAppletManager       *manager)
{
  HDAppletManagerPrivate *priv = manager->priv;
  gchar **groups;
  guint i;
  gchar **plugins;
  GHashTableIter iter;
  gpointer key, value;

  priv->applets_key_file = key_file;

  /* Clear displayed applets */
  g_hash_table_remove_all (priv->displayed_applets);
  g_hash_table_remove_all (priv->used_ids);

  /* Iterate over all groups and get all displayed applets */
  groups = g_key_file_get_groups (key_file, NULL);
  for (i = 0; groups && groups[i]; i++)
    {
      gchar *desktop_file;

      g_hash_table_insert (priv->used_ids,
                           g_strdup (groups[i]),
                           GUINT_TO_POINTER (1));

      desktop_file = g_key_file_get_string (key_file,
                                            groups[i],
                                            ITEMS_KEY_DESKTOP_FILE,
                                            NULL);
      g_debug ("Group: %s, desktop-id: %s", groups[i], desktop_file);

      if (desktop_file != NULL)
        g_hash_table_insert (priv->displayed_applets,
                             desktop_file,
                             GUINT_TO_POINTER (1));
    }
  g_strfreev (groups);

  /* Get all plugin paths FIXME: does not need to be done all times */
  plugins = hd_plugin_configuration_get_all_plugin_paths (HD_PLUGIN_CONFIGURATION (configuration));
  for (i = 0; plugins[i]; i++)
    {
      if (!g_hash_table_lookup (priv->installed, plugins[i]))
        {
          GKeyFile *key_file = g_key_file_new ();
          GError *error = NULL;
          HDPluginInfo *info;
          gchar *text_domain, *name;

          g_key_file_load_from_file (key_file, plugins[i],
                                     G_KEY_FILE_NONE, &error);

          if (error)
            {
              g_debug ("Could not read plugin .desktop file %s: %s", plugins[i], error->message);
              g_error_free (error);
              g_key_file_free (key_file);
              continue;
            }

          text_domain = g_key_file_get_string (key_file,
                                               G_KEY_FILE_DESKTOP_GROUP,
                                               DESKTOP_KEY_TEXT_DOMAIN,
                                               NULL);

          name = g_key_file_get_string (key_file,
                                        G_KEY_FILE_DESKTOP_GROUP,
                                        G_KEY_FILE_DESKTOP_KEY_NAME,
                                        NULL);

          if (!name)
            {
              g_warning ("Could not read plugin .desktop file %s. %s",
                         plugins[i],
                         error->message);
              g_error_free (error);
              g_key_file_free (key_file);
              g_free (text_domain);
              continue;
            }

          info = g_slice_new (HDPluginInfo);

          /* Translate name with given or default text domain */
          if (text_domain)
            info->name = dgettext (text_domain, name);
          else
            info->name = gettext (name);

          g_free (text_domain);
          if (info->name != name)
            g_free (name);

          info->multiple = g_key_file_get_boolean (key_file,
                                                   G_KEY_FILE_DESKTOP_GROUP,
                                                   DESKTOP_KEY_MULTIPLE,
                                                   NULL);

          g_hash_table_insert (priv->installed, plugins[i],
                               info);

          g_key_file_free (key_file);
        }
    }

  gtk_list_store_clear (GTK_LIST_STORE (priv->model));

  g_hash_table_iter_init (&iter, priv->installed);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_debug (".desktop: %s, %d, %x", (gchar *) key, ((HDPluginInfo *)value)->multiple, GPOINTER_TO_UINT (g_hash_table_lookup (priv->displayed_applets, key)));

      if (((HDPluginInfo *)value)->multiple ||
          g_hash_table_lookup (priv->displayed_applets, key) == NULL)
        {
          gtk_list_store_insert_with_values (GTK_LIST_STORE (priv->model),
                                             NULL,
                                             -1,
                                             0, ((HDPluginInfo *)value)->name,
                                             1, key,
                                             -1);
        }
    }
}

static gboolean
delete_event_cb (GtkWidget       *widget,
                 GdkEvent        *event,
                 HDAppletManager *manager)
{
  gchar *plugin_id;

  plugin_id = hd_plugin_item_get_plugin_id (HD_PLUGIN_ITEM (widget));
  hd_applet_manager_remove_applet (manager, plugin_id);
  g_free (plugin_id);

  gtk_widget_hide (widget);

  return TRUE;
}

static void
plugin_added_cb (HDPluginManager *pm,
                 GObject         *plugin,
                 HDAppletManager *manager)
{
  if (HD_IS_HOME_PLUGIN_ITEM (plugin))
    {
      Display *display;
      Window root;

      g_signal_connect (plugin, "delete-event",
                        G_CALLBACK (delete_event_cb), manager);

      /* Set menu transient for root window */
      gtk_widget_realize (GTK_WIDGET (plugin));
      display = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (plugin)));
      root = RootWindow (display, GDK_SCREEN_XNUMBER (gtk_widget_get_screen (GTK_WIDGET (plugin))));
      XSetTransientForHint (display, GDK_WINDOW_XID (GTK_WIDGET (plugin)->window), root);

      gtk_widget_show (GTK_WIDGET (plugin));
    }
}

static void
plugin_removed_cb (HDPluginManager *pm,
                   GObject         *plugin,
                   gpointer         data)
{
  if (HD_IS_HOME_PLUGIN_ITEM (plugin))
    gtk_widget_destroy (GTK_WIDGET (plugin));
}

static gboolean
run_idle (gpointer data)
{
  /* Load the configuration of the plugin manager and load plugins */
  hd_plugin_manager_run (HD_PLUGIN_MANAGER (data));

  return FALSE;
}

static void
hd_applet_manager_init (HDAppletManager *manager)
{
  HDAppletManagerPrivate *priv;
  manager->priv = HD_APPLET_MANAGER_GET_PRIVATE (manager);
  priv = manager->priv;

  priv->plugin_manager = hd_plugin_manager_new (hd_config_file_new_with_defaults ("home.conf"));

  priv->displayed_applets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  priv->used_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  priv->installed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  priv->model = GTK_TREE_MODEL (gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->model),
                                        0,
                                        GTK_SORT_ASCENDING);

  g_signal_connect (priv->plugin_manager, "items-configuration-loaded",
                    G_CALLBACK (items_configuration_loaded_cb), manager);
  g_signal_connect (priv->plugin_manager, "plugin-added",
                    G_CALLBACK (plugin_added_cb), manager);
  g_signal_connect (priv->plugin_manager, "plugin-removed",
                    G_CALLBACK (plugin_removed_cb), manager);

  gdk_threads_add_idle (run_idle, priv->plugin_manager);
}

static void
hd_applet_manager_class_init (HDAppletManagerClass *klass)
{
  g_type_class_add_private (klass, sizeof (HDAppletManagerPrivate));
}

HDAppletManager *
hd_applet_manager_get (void)
{
  static HDAppletManager *manager = NULL;

  if (G_UNLIKELY (!manager))
    manager = g_object_new (HD_TYPE_APPLET_MANAGER, NULL);

  return manager;
}

GtkTreeModel *
hd_applet_manager_get_model (HDAppletManager *manager)
{
  HDAppletManagerPrivate *priv = manager->priv;

  return g_object_ref (priv->model);
}

void
hd_applet_manager_install_applet (HDAppletManager *manager,
                                  GtkTreeIter     *iter)
{
  HDAppletManagerPrivate *priv = manager->priv;
  gchar *desktop_file;
  gchar *basename, *id;
  guint i = 0;

  gtk_tree_model_get (priv->model, iter,
                      1, &desktop_file,
                      -1);

  basename = g_path_get_basename (desktop_file);

  /* Find unique id */
  do
    id = g_strdup_printf ("%s-%u", basename, i++);
  while (g_hash_table_lookup (priv->used_ids, id));

  g_key_file_set_string (priv->applets_key_file,
                         id,
                         "X-Desktop-File",
                         desktop_file);

  g_free (desktop_file);
  g_free (basename);
  g_free (id);

  /* Store file atomically */
  hd_plugin_configuration_store_items_key_file (HD_PLUGIN_CONFIGURATION (priv->plugin_manager));
}

void
hd_applet_manager_remove_applet (HDAppletManager *manager,
                                 const gchar     *plugin_id)
{
  HDAppletManagerPrivate *priv = manager->priv;

  if (g_key_file_remove_group (priv->applets_key_file,
                               plugin_id,
                               NULL))
    {
      /* Store file atomically */
      hd_plugin_configuration_store_items_key_file (HD_PLUGIN_CONFIGURATION (priv->plugin_manager));
    }
}
