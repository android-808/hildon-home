/*
 * This file is part of hildon-home
 * 
 * Copyright (C) 2006, 2007, 2008 Nokia Corporation.
 *
 * Based on main.c from hildon-desktop.
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

#include <glib/gstdio.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libhildondesktop/libhildondesktop.h>
#include <hildon/hildon.h>
#include <gconf/gconf-client.h>

#include <libintl.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>

#include "hd-notification-manager.h"
#include "hd-system-notifications.h"
#include "hd-incoming-events.h"
#include "hd-bookmark-manager.h"
#include "hd-bookmark-shortcut.h"
#include "hd-task-manager.h"
#include "hd-task-shortcut.h"
#include "hd-hildon-home-dbus.h"
#include "hd-applet-manager.h"

#define HD_STAMP_DIR   "/tmp/hildon-desktop/"
#define HD_HOME_STAMP_FILE HD_STAMP_DIR "hildon-home.stamp"

#define OPERATOR_APPLET_MODULE_PATH "/usr/lib/hildon-desktop/connui-cellular-operator-home-item.so"
#define OPERATOR_APPLET_PLUGIN_ID "_HILDON_OPERATOR_APPLET"

#define HD_GCONF_DIR_HILDON_HOME "/apps/osso/hildon-home"
#define HD_GCONF_KEY_HILDON_HOME_TASK_SHORTCUTS HD_GCONF_DIR_HILDON_HOME "/task-shortcuts"
#define HD_GCONF_KEY_HILDON_HOME_BOOKMARK_SHORTCUTS HD_GCONF_DIR_HILDON_HOME "/bookmark-shortcuts"

/* signal handler, hildon-desktop sends SIGTERM to all tracked applications
 * when it receives SIGTEM itselgf */
static void
signal_handler (int signal)
{
  if (signal == SIGTERM)
  {
    hd_stamp_file_finalize (HD_HOME_STAMP_FILE);

    exit (0);
  }
}

static void
load_operator_applet (void)
{
  GTypeModule *module;
  GObject *operator_applet;

  /* Load operator applet module */
  module = (GTypeModule *) hd_plugin_module_new (OPERATOR_APPLET_MODULE_PATH);

  if (!g_type_module_use (module))
    {
      g_warning ("Could not load operator module %s.", OPERATOR_APPLET_MODULE_PATH);

      return;
    }

  /* Create operator applet */
  operator_applet = hd_plugin_module_new_object (HD_PLUGIN_MODULE (module),
                                                 OPERATOR_APPLET_PLUGIN_ID);

  /* Show operator applet */
  if (GTK_IS_WIDGET (operator_applet))
    gtk_widget_show (GTK_WIDGET (operator_applet));

  g_type_module_unuse (module);
}

int
main (int argc, char **argv)
{
  GConfClient *client;
  GError *error = NULL;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, "/usr/share/locale");
  textdomain (GETTEXT_PACKAGE);

  g_thread_init (NULL);

  /* Initialize Gtk+ */
  gtk_init (&argc, &argv);

  /* Initialize Hildon */
  hildon_init ();

  /* Initialize GnomeVFS */
  gnome_vfs_init ();

  /* Add handler for TERM signal */
  signal (SIGTERM, signal_handler);

  hd_stamp_file_init (HD_HOME_STAMP_FILE);

  /* Load operator applet */
  load_operator_applet ();

  /* Initialize applet manager */
  hd_applet_manager_get ();

  /* Intialize notifications */
  hd_notification_manager_get ();
  hd_system_notifications_get ();
  hd_incoming_events_get ();

  /* Add shortcuts gconf dirs so hildon-home gets notifications about changes */
  client = gconf_client_get_default ();
  gconf_client_add_dir (client,
                        HD_GCONF_DIR_HILDON_HOME,
                        GCONF_CLIENT_PRELOAD_ONELEVEL,
                        &error);
  if (error)
    {
      g_warning ("Could not add gconf watch for dir " HD_GCONF_DIR_HILDON_HOME ". %s", error->message);
      g_error_free (error);
    }
  g_object_unref (client);

  /* Task Shortcuts */
  hd_task_manager_get ();
  hd_shortcuts_new (HD_GCONF_KEY_HILDON_HOME_TASK_SHORTCUTS,
                    HD_TYPE_TASK_SHORTCUT);

  /* Bookmark Shortcuts */
  hd_bookmark_manager_get ();
  hd_shortcuts_new (HD_GCONF_KEY_HILDON_HOME_BOOKMARK_SHORTCUTS,
                    HD_TYPE_BOOKMARK_SHORTCUT);

  /* D-Bus */
  hd_hildon_home_dbus_get ();

  /* Start the main loop */
  gtk_main ();

  return 0;
}
