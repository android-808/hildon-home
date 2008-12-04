/*
 * This file is part of hildon-home
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

#include <glib/gi18n.h>

#include <hildon/hildon.h>

#include <gconf/gconf-client.h>

#include <string.h>

#define _XOPEN_SOURCE 500
#include <ftw.h>

#include "hd-task-manager.h"

#define HD_TASK_MANAGER_GET_PRIVATE(object) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((object), HD_TYPE_TASK_MANAGER, HDTaskManagerPrivate))

#define TASK_SHORTCUTS_GCONF_KEY "/apps/osso/hildon-home/task-shortcuts"

/* Task .desktop file keys */
#define HD_KEY_FILE_DESKTOP_KEY_SERVICE "X-Osso-Service"
#define HD_KEY_FILE_DESKTOP_KEY_TRANSLATION_DOMAIN "X-Text-Domain"

/* Launch tasks */
#define SERVICE_NAME_LEN        255
#define PATH_NAME_LEN           255
#define INTERFACE_NAME_LEN      255
#define TMP_NAME_LEN            255

#define OSSO_BUS_ROOT          "com.nokia"
#define OSSO_BUS_ROOT_PATH     "/com/nokia"
#define OSSO_BUS_TOP           "top_application"

struct _HDTaskManagerPrivate
{
  HDPluginConfiguration *plugin_configuration;

  GtkTreeModel *model;
  GtkTreeModel *filtered_model;

  GHashTable *available_tasks;
  GHashTable *installed_shortcuts;

  GConfClient *gconf_client;
};

typedef struct
{
  gchar *label;
  gchar *icon;

  gchar *exec;
  gchar *service;
} HDTaskInfo;

enum
{
  DESKTOP_FILE_CHANGED,
  LAST_SIGNAL
};

static guint task_manager_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (HDTaskManager, hd_task_manager, G_TYPE_OBJECT);

/** hd_task_info_free:
 * @info The #HDTaskInfo to free
 *
 * Frees a #HDTaskInfo created with g_slice_new0 (HDTaskInfo).
 **/
static void
hd_task_info_free (HDTaskInfo *info)
{
  if (!info)
    return;

  g_free (info->label);
  g_free (info->icon);

  g_free (info->exec);
  g_free (info->service);

  g_slice_free (HDTaskInfo, info);
}

static void
hd_task_manager_load_desktop_file (HDTaskManager *manager,
                                   const gchar   *filename)
{
  HDTaskManagerPrivate *priv = manager->priv;
  GKeyFile *desktop_file;
  GError *error = NULL;
  HDTaskInfo *info = NULL;
  gchar *desktop_id = NULL;
  gchar *type = NULL, *translation_domain = NULL, *name = NULL;
  GdkPixbuf *pixbuf = NULL;
  /* FIXME xmas workaround */
  gchar *dirname, *w50id, *w50id_dot, *w50_filename;

  g_debug ("hd_task_manager_load_desktop_file (%s)", filename);

  /* FIXME xmas workaround */
  dirname = g_path_get_dirname (filename);
  w50id = g_path_get_basename (filename);
  w50id_dot = strrchr (w50id, '.');
  if (w50id_dot)
    *w50id_dot = '\0';
  w50_filename = g_strdup_printf ("%s/%s.w50-desktop", dirname, w50id);
  desktop_id = g_strdup_printf ("%s.desktop", w50id);
  g_free (dirname);
  g_free (w50id);
  if (strcmp (filename, w50_filename) && g_file_test (w50_filename, G_FILE_TEST_EXISTS))
    {
      g_free (w50_filename);
      return;
    }
  g_free (w50_filename);

  desktop_file = g_key_file_new ();
  if (!g_key_file_load_from_file (desktop_file,
                                  filename,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      g_debug ("Could not read .desktop file `%s'. %s",
               filename,
               error->message);
      g_error_free (error);
      goto cleanup;
    }

  type = g_key_file_get_string (desktop_file,
                                G_KEY_FILE_DESKTOP_GROUP,
                                G_KEY_FILE_DESKTOP_KEY_TYPE,
                                NULL);

  /* Test if type is Application */
  if (!type || strcmp (type, G_KEY_FILE_DESKTOP_TYPE_APPLICATION))
    {
      goto cleanup;
    }

  /* Get translation domain if set, so Name can be translated */
  translation_domain = g_key_file_get_string (desktop_file,
                                              G_KEY_FILE_DESKTOP_GROUP,
                                              HD_KEY_FILE_DESKTOP_KEY_TRANSLATION_DOMAIN,
                                              NULL);

  name = g_key_file_get_string (desktop_file,
                                G_KEY_FILE_DESKTOP_GROUP,
                                G_KEY_FILE_DESKTOP_KEY_NAME,
                                &error);
  if (error)
    {
      g_debug ("Could not read Name entry in .desktop file `%s'. %s",
               filename,
               error->message);
      g_error_free (error);
      goto cleanup;
    }

  info = g_slice_new0 (HDTaskInfo);

  /* Translate name */
  if (!translation_domain)
    {
      /* Use GETTEXT_PACKAGE as default translation domain */
      info->label = g_strdup (dgettext (GETTEXT_PACKAGE, name));
    }
  else
    {
      info->label = g_strdup (dgettext (translation_domain, name));
    }

  /* Get the icon */
  info->icon = g_key_file_get_string (desktop_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_ICON,
                                      &error);
  if (!info->icon)
    {
      g_debug ("Could not read Icon entry in .desktop file `%s'. %s",
               filename,
               error->message);
      g_error_free (error);
    }

  info->exec = g_key_file_get_string (desktop_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_EXEC,
                                      NULL);
  info->service = g_key_file_get_string (desktop_file,
                                         G_KEY_FILE_DESKTOP_GROUP,
                                         HD_KEY_FILE_DESKTOP_KEY_SERVICE,
                                         NULL);

  /* Get the desktop_id */
  /* FIXME xmas workaround */
  /* desktop_id = g_path_get_basename (filename);*/

  g_hash_table_insert (priv->available_tasks,
                       desktop_id,
                       info);

  /* Load icon for list */
  if (info->icon)
    {
      GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();
      GtkIconInfo *icon_info;

      icon_info = gtk_icon_theme_lookup_icon (icon_theme,
                                              info->icon,
                                              64,
                                              GTK_ICON_LOOKUP_NO_SVG);
      if (icon_info)
        {
          pixbuf = gtk_icon_info_load_icon (icon_info, NULL);
          gtk_icon_info_free (icon_info);
        }
    }

  gtk_list_store_insert_with_values (GTK_LIST_STORE (priv->model),
                                     NULL, -1,
                                     0, info->label,
                                     1, info->icon,
                                     2, desktop_id,
                                     3, pixbuf,
                                     -1);

  g_signal_emit (manager,
                 task_manager_signals[DESKTOP_FILE_CHANGED],
                 g_quark_from_string (desktop_id));

cleanup:
  g_key_file_free (desktop_file);
  g_free (type);
  g_free (translation_domain);
  g_free (name);
  if (pixbuf)
    g_object_unref (pixbuf);
}

static int
visit_func (const char        *f_path,
            const struct stat *sb,
            int                type_flag,
            struct FTW        *ftw_buf)
{
  g_debug ("visit_func %s, %d", f_path, type_flag);

  /* Directory */
  switch (type_flag)
    {
      case FTW_D:
          {
/*            GnomeVFSMonitorHandle* handle;

            gnome_vfs_monitor_add (&handle,
                                   f_path,
                                   GNOME_VFS_MONITOR_DIRECTORY,
                                   (GnomeVFSMonitorCallback) applications_dir_changed,
                                   NULL);*/
          }
        break;
      case FTW_F:
        hd_task_manager_load_desktop_file (hd_task_manager_get (),
                                           f_path);
        break;
      default:
        g_debug ("%s, %d", f_path, type_flag);
    }

  return 0;
}

static gboolean
hd_task_manager_scan_for_desktop_files (const gchar *directory)
{
  g_debug ("hd_task_manager_scan_for_desktop_files: %s", directory);

  nftw (directory, visit_func, 20, FTW_PHYS); 

  return FALSE;
}

static void
update_installed_shortcuts (HDTaskManager *manager)
{
  HDTaskManagerPrivate *priv = manager->priv;
  GSList *list, *l;
  GError *error = NULL;

  /* Get the list of strings of task shortcuts */
  list = gconf_client_get_list (priv->gconf_client,
                                TASK_SHORTCUTS_GCONF_KEY,
                                GCONF_VALUE_STRING,
                                &error);

  /* Check if there was an error */
  if (error)
    {
      g_warning ("Could not get list of task shortcuts from GConf: %s", error->message);
      g_error_free (error);
      return;
    }

  /* Replace content of hash table with list of installed shortcuts */
  g_hash_table_remove_all (priv->installed_shortcuts);
  for (l = list; l; l = l->next)
    {
      g_hash_table_insert (priv->installed_shortcuts,
                           l->data,
                           GUINT_TO_POINTER (1));
    }

  /* Update filtered model */
  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (priv->filtered_model));

  /* Free the list, the list content is still referenced by the hash table */
  g_slist_free (list);
}

static void
shortcuts_gconf_notify (GConfClient   *client,
                        guint          cnxn_id,
                        GConfEntry    *entry,
                        HDTaskManager *manager)
{
  update_installed_shortcuts (manager);
}

static gboolean
filtered_model_visible_func (GtkTreeModel *model,
                             GtkTreeIter  *iter,
                             gpointer      data)
{
  HDTaskManagerPrivate *priv = HD_TASK_MANAGER (data)->priv;
  gchar *desktop_id;
  gpointer value;

  gtk_tree_model_get (model, iter,
                      2, &desktop_id,
                      -1);

  /* Check if a shortcut for desktop-id is already installed */
  value = g_hash_table_lookup (priv->installed_shortcuts,
                               desktop_id);

  g_free (desktop_id);

  return value == NULL;
}

static void
hd_task_manager_init (HDTaskManager *manager)
{
  HDTaskManagerPrivate *priv;

  /* Install private */
  manager->priv = HD_TASK_MANAGER_GET_PRIVATE (manager);
  priv = manager->priv;

  priv->available_tasks = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, (GDestroyNotify) hd_task_info_free);
  priv->installed_shortcuts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                     g_free, NULL);

  priv->model = GTK_TREE_MODEL (gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->model),
                                        0,
                                        GTK_SORT_ASCENDING);
  priv->filtered_model = gtk_tree_model_filter_new (priv->model,
                                                    NULL);
  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (priv->filtered_model),
                                          filtered_model_visible_func,
                                          manager,
                                          NULL);

  /* GConf */
  priv->gconf_client = gconf_client_get_default ();

  /* Add notification of shortcuts key */
  gconf_client_notify_add (priv->gconf_client,
                           TASK_SHORTCUTS_GCONF_KEY,
                           (GConfClientNotifyFunc) shortcuts_gconf_notify,
                           manager,
                           NULL, NULL);

  update_installed_shortcuts (manager);
}

static void
hd_task_manager_dispose (GObject *obj)
{
  HDTaskManagerPrivate *priv = HD_TASK_MANAGER (obj)->priv;

  if (priv->gconf_client)
    {
      g_object_unref (priv->gconf_client);
      priv->gconf_client = NULL;
    }

  if (priv->filtered_model)
    {
      g_object_unref (priv->filtered_model);
      priv->filtered_model = NULL;
    }

  if (priv->model)
    {
      g_object_unref (priv->model);
      priv->model = NULL;
    }

  G_OBJECT_CLASS (hd_task_manager_parent_class)->dispose (obj);
}

static void
hd_task_manager_finalize (GObject *obj)
{
  HDTaskManagerPrivate *priv = HD_TASK_MANAGER (obj)->priv;

  g_hash_table_destroy (priv->available_tasks);
  g_hash_table_destroy (priv->installed_shortcuts);

  G_OBJECT_CLASS (hd_task_manager_parent_class)->finalize (obj);
}

static void
hd_task_manager_class_init (HDTaskManagerClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);

  obj_class->dispose = hd_task_manager_dispose;
  obj_class->finalize = hd_task_manager_finalize;

  task_manager_signals [DESKTOP_FILE_CHANGED] = g_signal_new ("desktop-file-changed",
                                                              G_TYPE_FROM_CLASS (klass),
                                                              G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED,
                                                              G_STRUCT_OFFSET (HDTaskManagerClass,
                                                                               desktop_file_changed),
                                                              NULL, NULL,
                                                              g_cclosure_marshal_VOID__VOID,
                                                              G_TYPE_NONE, 0);

  g_type_class_add_private (klass, sizeof (HDTaskManagerPrivate));
}

HDTaskManager *
hd_task_manager_get (void)
{
  static HDTaskManager *manager = NULL;

  if (G_UNLIKELY (!manager))
    {
      manager = g_object_new (HD_TYPE_TASK_MANAGER, NULL);

      gdk_threads_add_idle ((GSourceFunc) hd_task_manager_scan_for_desktop_files,
                            HD_APPLICATIONS_DIR);
    }

  return manager;
}

GtkTreeModel *
hd_task_manager_get_model (HDTaskManager *manager)
{
  HDTaskManagerPrivate *priv = manager->priv;

  return g_object_ref (priv->filtered_model);
}

void
hd_task_manager_install_task (HDTaskManager *manager,
                              GtkTreeIter   *tree_iter)
{
  HDTaskManagerPrivate *priv = manager->priv;
  gchar *desktop_id;
  GHashTableIter hash_iter;
  gpointer key;
  GSList *list = NULL;
  GError *error = NULL;

  gtk_tree_model_get (priv->filtered_model, tree_iter,
                      2, &desktop_id,
                      -1);

  g_hash_table_insert (priv->installed_shortcuts,
                       desktop_id,
                       GUINT_TO_POINTER (1));

  /* Iterate over all installed shortcuts and add them to the list */
  g_hash_table_iter_init (&hash_iter, priv->installed_shortcuts);
  while (g_hash_table_iter_next (&hash_iter, &key, NULL))
    list = g_slist_append (list, key);

  /* Set the new list to GConf */
  gconf_client_set_list (priv->gconf_client,
                         TASK_SHORTCUTS_GCONF_KEY,
                         GCONF_VALUE_STRING,
                         list,
                         &error);

  if (error)
    {
      g_warning ("Could not write string list to GConf (%s): %s.",
                 TASK_SHORTCUTS_GCONF_KEY,
                 error->message);
      g_error_free (error);
    }

  /* Free the list, the content is still referenced by the hash table */
  g_slist_free (list);
}

const gchar *
hd_task_manager_get_label (HDTaskManager *manager,
                           const gchar   *desktop_id)
{
  HDTaskManagerPrivate *priv = manager->priv;
  HDTaskInfo *info;

  g_return_val_if_fail (HD_IS_TASK_MANAGER (manager), NULL);
  g_return_val_if_fail (desktop_id, NULL);

  /* Lookup task */
  info = g_hash_table_lookup (priv->available_tasks,
                              desktop_id);

  /* Return NULL if task is not available */
  if (!info)
    {
      g_warning ("Could not get label for %s", desktop_id);
      return NULL;
    }

  return info->label;
}

const gchar *
hd_task_manager_get_icon (HDTaskManager *manager,
                          const gchar   *desktop_id)
{
  HDTaskManagerPrivate *priv = manager->priv;
  HDTaskInfo *info;

  g_return_val_if_fail (HD_IS_TASK_MANAGER (manager), NULL);
  g_return_val_if_fail (desktop_id, NULL);

  /* Lookup task */
  info = g_hash_table_lookup (priv->available_tasks,
                              desktop_id);

  /* Return NULL if task is not available */
  if (!info)
    {
      g_warning ("Could not get label for %s", desktop_id);
      return NULL;
    }

  return info->icon;
}

static void
hd_task_manager_activate_service (const gchar *app)
{
  gchar service[SERVICE_NAME_LEN], path[PATH_NAME_LEN],
        interface[INTERFACE_NAME_LEN], tmp[TMP_NAME_LEN];
  DBusMessage *msg = NULL;
  DBusError error;
  DBusConnection *conn;

  g_debug ("%s: app=%s\n", __FUNCTION__, app);

  /* If we have full service name we will use it */
  if (g_strrstr(app, "."))
  {
    g_snprintf(service, SERVICE_NAME_LEN, "%s", app);
    g_snprintf(interface, INTERFACE_NAME_LEN, "%s", service);
    g_snprintf(tmp, TMP_NAME_LEN, "%s", app);
    g_snprintf(path, PATH_NAME_LEN, "/%s", g_strdelimit(tmp, ".", '/'));
  }
  else /* use com.nokia prefix */
  {
    g_snprintf(service, SERVICE_NAME_LEN, "%s.%s", OSSO_BUS_ROOT, app);
    g_snprintf(path, PATH_NAME_LEN, "%s/%s", OSSO_BUS_ROOT_PATH, app);
    g_snprintf(interface, INTERFACE_NAME_LEN, "%s", service);
  }

  dbus_error_init (&error);
  conn = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (dbus_error_is_set (&error))
  {
    g_warning ("could not start: %s: %s", service, error.message);
    dbus_error_free (&error);
    return;
  }

  msg = dbus_message_new_method_call (service, path, interface, OSSO_BUS_TOP);
  if (msg == NULL)
  {
    g_warning ("failed to create message");
    return;
  }

  if (!dbus_connection_send (conn, msg, NULL))
    g_warning ("dbus_connection_send failed");

  dbus_message_unref (msg);
}

void
hd_task_manager_launch_task (HDTaskManager *manager,
                             const gchar   *desktop_id)
{
  HDTaskManagerPrivate *priv = manager->priv;
  HDTaskInfo *info;
  gboolean res = FALSE;

  g_return_if_fail (HD_IS_TASK_MANAGER (manager));
  g_return_if_fail (desktop_id);

  /* Lookup task */
  info = g_hash_table_lookup (priv->available_tasks,
                              desktop_id);

  /* Return false if task is not available */
  if (!info)
    {
      g_warning ("Could not launch %s", desktop_id);
      return;
    }

  if (info->service)
    {
      g_debug ("Activating %s: `%s'", info->label, info->service);

      /* launch the application, or if it's already running
       * move it to the top
       */
      hd_task_manager_activate_service (info->service);
      return;
    }

#if 0
  if (hd_wm_is_lowmem_situation ())
    {
      if (!tn_close_application_dialog (CAD_ACTION_OPENING))
        {
          g_set_error (...);
          return FALSE;
        }
    }
#endif

  if (info->exec)
    {
      gchar *space = strchr (info->exec, ' ');
      gchar *exec;
      gint argc;
      gchar **argv = NULL;
      GPid child_pid;
      GError *error = NULL;

      g_debug ("Executing %s: `%s'", info->label, info->exec);

      if (space)
        {
          gchar *cmd = g_strndup (info->exec, space - info->exec);
          gchar *exc = g_find_program_in_path (cmd);

          exec = g_strconcat (exc, space, NULL);

          g_free (cmd);
          g_free (exc);
        }
      else
        exec = g_find_program_in_path (info->exec);

      if (!g_shell_parse_argv (exec, &argc, &argv, &error))
        {
          g_warning ("Could not parse argv. %s", error->message);
          g_error_free (error);
          
          g_free (exec);
          if (argv)
            g_strfreev (argv);

          return;
        }

      res = g_spawn_async (NULL,
                           argv, NULL,
                           0,
                           NULL, NULL,
                           &child_pid,
                           &error);
      if (error)
        {
          g_warning ("Could not spawn. %s", error->message);
          g_error_free (error);
        }

      g_free (exec);

      if (argv)
        g_strfreev (argv);

      return;
    }
  else
    {
#if 0
      g_set_error (...);
#endif
      return;
    }

  g_assert_not_reached ();

  return;
}
