/* This entire file is licensed under GNU General Public License v3.0
 *
 * Copyright 2020-2022 sfwbar maintainers
 */

#include <gtk/gtk.h>
#include <glib-unix.h>
#include "sfwbar.h"
#include "wayland.h"
#include "bar.h"
#include "tray.h"
#include "taskbar.h"
#include "pager.h"
#include "switcher.h"
#include "config.h"
#include "sway_ipc.h"

gchar *confname;
gchar *sockname;
static gchar *cssname;
static gchar *monitor;
static gchar *dfilter;
static GRegex *rfilter;
static gboolean debug = FALSE;

static GOptionEntry entries[] = {
  {"config",'f',0,G_OPTION_ARG_FILENAME,&confname,"Specify config file"},
  {"css",'c',0,G_OPTION_ARG_FILENAME,&cssname,"Specify css file"},
  {"socket",'s',0,G_OPTION_ARG_FILENAME,&sockname,"Specify sway socket file"},
  {"debug",'d',0,G_OPTION_ARG_NONE,&debug,"Display debug info"},
  {"debug-filter",'g',0,G_OPTION_ARG_STRING,&dfilter,"Filter debug output for a pattern"},
  {"monitor",'m',0,G_OPTION_ARG_STRING,&monitor,
    "Monitor to display the panel on (use \"-m list\" to list monitors`"},
  {NULL}};

void parse_command_line ( gint argc, gchar **argv)
{
  GOptionContext *optc;
  optc = g_option_context_new(" - Sway Floating Window Bar");
  g_option_context_add_main_entries(optc,entries,NULL);
  g_option_context_add_group (optc, gtk_get_option_group (TRUE));
  g_option_context_parse(optc,&argc,&argv,NULL);
  g_option_context_free(optc);
}

void list_monitors ( void )
{
  GdkDisplay *gdisp;
  GdkMonitor *gmon;
  gint nmon,i;
  gchar *name;

  gdisp = gdk_display_get_default();
  nmon = gdk_display_get_n_monitors(gdisp);
  for(i=0;i<nmon;i++)
  {
    gmon = gdk_display_get_monitor(gdisp,i);
    name = g_object_get_data(G_OBJECT(gmon),"xdg_name");
    printf("%s: %s %s\n",name,gdk_monitor_get_manufacturer(gmon),
        gdk_monitor_get_model(gmon));
  }
  exit(0);
}

void log_print ( const gchar *log_domain, GLogLevelFlags log_level, 
    const gchar *message, gpointer data )
{
  GDateTime *now;

  if(!debug && (log_level==G_LOG_LEVEL_DEBUG || log_level==G_LOG_LEVEL_INFO) )
    return;

  if(rfilter)
    if(!g_regex_match(rfilter,message,0,NULL))
      return;

  now = g_date_time_new_now_local();

  fprintf(stderr,"%02d:%02d:%02.2f %s\n",
      g_date_time_get_hour(now),
      g_date_time_get_minute(now),
      g_date_time_get_seconds(now),
      message);

  g_date_time_unref(now);
}

gboolean shell_timer ( gpointer data )
{
  taskbar_update_all();
  switcher_update();
  tray_update();

  return TRUE;
}

static void activate (GtkApplication* app, gpointer data )
{
  GdkDisplay *gdisp;
  GList *clist, *iter;

  css_init(cssname);
  sway_ipc_init();
  wayland_init();

  if( monitor && !g_ascii_strcasecmp(monitor,"list") )
    list_monitors();

  config_parse(confname?confname:"sfwbar.config",TRUE);
  taskbar_populate();
  pager_populate();
  switcher_populate();

  clist = gtk_window_list_toplevels();
  for(iter = clist; iter; iter = g_list_next(iter) )
    if(GTK_IS_BOX(gtk_bin_get_child(GTK_BIN(iter->data))))
    {
      gtk_application_add_window(app,GTK_WINDOW(iter->data));
      css_widget_cascade(GTK_WIDGET(iter->data),NULL);
      base_widget_autoexec(iter->data,NULL);
    }
  g_list_free(clist);

  if(monitor)
    bar_set_monitor(monitor,"sfwbar");

  gdisp = gdk_display_get_default();
  g_signal_connect(gdisp, "monitor-added",
      G_CALLBACK(bar_monitor_added_cb),NULL);
  g_signal_connect(gdisp, "monitor-removed",
      G_CALLBACK(bar_monitor_removed_cb),NULL);

  g_thread_unref(g_thread_new("scanner",
        (GThreadFunc)base_widget_scanner_thread,
        g_main_context_get_thread_default()));

  action_function_exec("SfwBarInit",NULL,NULL,NULL,NULL);

  g_timeout_add (100,(GSourceFunc )shell_timer,NULL);
  g_unix_signal_add(10,(GSourceFunc)switcher_event,NULL);
  g_unix_signal_add(12,(GSourceFunc)bar_hide_event,"toggle");
}

int main (int argc, gchar **argv)
{
  GtkApplication *app;
  gint status;

  g_log_set_handler(NULL,G_LOG_LEVEL_MASK,log_print,NULL);

  parse_command_line(argc,argv);

  if(dfilter)
    rfilter = g_regex_new(dfilter,0,0,NULL);

  app = gtk_application_new ("org.gtk.sfwbar", G_APPLICATION_NON_UNIQUE);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
