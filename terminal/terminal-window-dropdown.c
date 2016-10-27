/*-
 * Copyright (C) 2012 Nick Schermer <nick@xfce.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <libxfce4ui/libxfce4ui.h>

#include <terminal/terminal-private.h>
#include <terminal/terminal-window.h>
#include <terminal/terminal-util.h>
#include <terminal/terminal-window-dropdown.h>

/* animation fps */
#define ANIMATION_FPS (1000 / 20)



enum
{
  PROP_0,
  PROP_DROPDOWN_WIDTH,
  PROP_DROPDOWN_HEIGHT,
  PROP_DROPDOWN_POSITION,
  PROP_DROPDOWN_OPACITY,
  PROP_DROPDOWN_STATUS_ICON,
  PROP_DROPDOWN_KEEP_ABOVE,
  PROP_DROPDOWN_ANIMATION_TIME,
  PROP_DROPDOWN_ALWAYS_SHOW_TABS,
  N_PROPERTIES
};



static void     terminal_window_dropdown_finalize                (GObject                *object);
static void     terminal_window_dropdown_set_property            (GObject                *object,
                                                                  guint                   prop_id,
                                                                  const GValue           *value,
                                                                  GParamSpec             *pspec);
static gboolean terminal_window_dropdown_focus_in_event          (GtkWidget              *widget,
                                                                  GdkEventFocus          *event);
static gboolean terminal_window_dropdown_focus_out_event         (GtkWidget              *widget,
                                                                  GdkEventFocus          *event);
static gboolean terminal_window_dropdown_status_icon_press_event (GtkStatusIcon          *status_icon,
                                                                  GdkEventButton         *event,
                                                                  TerminalWindowDropdown *dropdown);
static void     terminal_window_dropdown_status_icon_popup_menu  (GtkStatusIcon          *status_icon,
                                                                  guint                   button,
                                                                  guint32                 timestamp,
                                                                  TerminalWindowDropdown *dropdown);
static void     terminal_window_dropdown_hide                    (TerminalWindowDropdown *dropdown);
static void     terminal_window_dropdown_show                    (TerminalWindowDropdown *dropdown,
                                                                  guint32                 timestamp);
static void     terminal_window_dropdown_toggle_real             (TerminalWindowDropdown *dropdown,
                                                                  guint32                 timestamp,
                                                                  gboolean                force_show);
static void     terminal_window_dropdown_update_geometry         (TerminalWindowDropdown *dropdown);


struct _TerminalWindowDropdownClass
{
  TerminalWindowClass parent_class;
};

typedef enum
{
  ANIMATION_DIR_NONE,
  ANIMATION_DIR_UP,
  ANIMATION_DIR_DOWN,
} TerminalDirection;

struct _TerminalWindowDropdown
{
  TerminalWindow       parent_instance;

  /* timeout for animation */
  guint                animation_timeout_id;
  guint                animation_time;
  TerminalDirection    animation_dir;

  /* ui widgets */
  GtkWidget           *keep_open;

  /* idle for detecting focus out during grabs (Alt+Tab) */
  guint                grab_timeout_id;

  /* measurements */
  gdouble              rel_width;
  gdouble              rel_height;
  gdouble              rel_position;

  GtkStatusIcon       *status_icon;

  /* last screen and monitor */
  GdkScreen           *screen;
  gint                 monitor_num;

  /* server time of focus out with grab */
  gint64               focus_out_time;
};



G_DEFINE_TYPE (TerminalWindowDropdown, terminal_window_dropdown, TERMINAL_TYPE_WINDOW)



static GParamSpec *dropdown_props[N_PROPERTIES] = { NULL, };



static void
terminal_window_dropdown_class_init (TerminalWindowDropdownClass *klass)
{
  GObjectClass   *gobject_class;
  GtkWidgetClass *gtkwidget_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = terminal_window_dropdown_set_property;
  gobject_class->finalize = terminal_window_dropdown_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->focus_in_event = terminal_window_dropdown_focus_in_event;
  gtkwidget_class->focus_out_event = terminal_window_dropdown_focus_out_event;

  dropdown_props[PROP_DROPDOWN_WIDTH] =
      g_param_spec_uint ("dropdown-width",
                         NULL, NULL,
                         10, 100, 80,
                         G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  dropdown_props[PROP_DROPDOWN_HEIGHT] =
      g_param_spec_uint ("dropdown-height",
                         NULL, NULL,
                         10, 100, 50,
                         G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  dropdown_props[PROP_DROPDOWN_POSITION] =
      g_param_spec_uint ("dropdown-position",
                         NULL, NULL,
                         0, 100, 0,
                         G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  dropdown_props[PROP_DROPDOWN_OPACITY] =
      g_param_spec_uint ("dropdown-opacity",
                         NULL, NULL,
                         0, 100, 100,
                         G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  dropdown_props[PROP_DROPDOWN_STATUS_ICON] =
      g_param_spec_boolean ("dropdown-status-icon",
                            NULL, NULL,
                            TRUE,
                            G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  dropdown_props[PROP_DROPDOWN_KEEP_ABOVE] =
      g_param_spec_boolean ("dropdown-keep-above",
                            NULL, NULL,
                            TRUE,
                            G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  dropdown_props[PROP_DROPDOWN_ANIMATION_TIME] =
      g_param_spec_uint ("dropdown-animation-time",
                         NULL, NULL,
                         0, 500, 0,
                         G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  dropdown_props[PROP_DROPDOWN_ALWAYS_SHOW_TABS] =
      g_param_spec_boolean ("dropdown-always-show-tabs",
                            NULL, NULL,
                            TRUE,
                            G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);

  /* install all properties */
  g_object_class_install_properties (gobject_class, N_PROPERTIES, dropdown_props);
}



static void
terminal_window_dropdown_init (TerminalWindowDropdown *dropdown)
{
  TerminalWindow *window = TERMINAL_WINDOW (dropdown);
  GtkAction      *action;
  GtkWidget      *hbox;
  GtkWidget      *button;
  GtkWidget      *img;
  guint           n;
  const gchar    *name;
  gboolean        keep_open;
  gboolean        show_borders;

  dropdown->rel_width = 0.80;
  dropdown->rel_height = 0.50;
  dropdown->rel_position = 0.50;
  dropdown->animation_dir = ANIMATION_DIR_NONE;

  /* shared setting to disable some functionality in TerminalWindow */
  window->drop_down = TRUE;

  /* default window settings */
  gtk_window_set_decorated (GTK_WINDOW (dropdown), FALSE);
  gtk_window_set_gravity (GTK_WINDOW (dropdown), GDK_GRAVITY_STATIC);
  gtk_window_set_type_hint (GTK_WINDOW (dropdown), GDK_WINDOW_TYPE_HINT_NORMAL);
  gtk_window_stick (GTK_WINDOW (dropdown));

  /* this avoids to return focus to the window after dialog changes,
   * but we have terminal_util_activate_window() for that */
  gtk_window_set_skip_pager_hint (GTK_WINDOW (dropdown), TRUE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dropdown), TRUE);

  /* adjust notebook for drop-down usage */
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (window->notebook), GTK_POS_BOTTOM);
  g_object_get (G_OBJECT (window->preferences), "misc-borders-default", &show_borders, NULL);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (window->notebook), show_borders);
  terminal_window_notebook_show_tabs (window);

  /* actions we don't want */
  action = gtk_action_group_get_action (window->action_group, "show-borders");
  gtk_action_set_visible (action, FALSE);

  /* notebook buttons */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_notebook_set_action_widget (GTK_NOTEBOOK (window->notebook), hbox, GTK_PACK_END);

  button = dropdown->keep_open = gtk_toggle_button_new ();
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text (button, _("Keep window open when it loses focus"));
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION (3,20,0)
  gtk_widget_set_focus_on_click (button, FALSE);
#else
  gtk_button_set_focus_on_click (GTK_BUTTON (button), FALSE);
#endif

  g_object_get (G_OBJECT (window->preferences), "dropdown-keep-open-default", &keep_open, NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), keep_open);

  img = gtk_image_new_from_icon_name ("go-bottom", GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (button), img);

  action = gtk_action_group_get_action (window->action_group, "preferences");

  button = gtk_button_new ();
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text (button, gtk_action_get_tooltip (action));
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION (3,20,0)
  gtk_widget_set_focus_on_click (button, FALSE);
#else
  gtk_button_set_focus_on_click (GTK_BUTTON (button), FALSE);
#endif
  g_signal_connect_swapped (G_OBJECT (button), "clicked",
      G_CALLBACK (gtk_action_activate), action);

  img = gtk_action_create_icon (action, GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (button), img);

  /* connect bindings */
  for (n = 1; n < N_PROPERTIES; n++)
    {
      name = g_param_spec_get_name (dropdown_props[n]);
      g_object_bind_property (G_OBJECT (window->preferences), name,
                              G_OBJECT (dropdown), name,
                              G_BINDING_SYNC_CREATE);
    }

  gtk_widget_show_all (hbox);
}



static void
terminal_window_dropdown_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  TerminalWindowDropdown *dropdown = TERMINAL_WINDOW_DROPDOWN (object);
  TerminalWindow         *window = TERMINAL_WINDOW (object);
  gdouble                 opacity;
  GdkScreen              *screen;

  switch (prop_id)
    {
    case PROP_DROPDOWN_WIDTH:
      dropdown->rel_width = g_value_get_uint (value) / 100.0;
      break;

    case PROP_DROPDOWN_HEIGHT:
      dropdown->rel_height = g_value_get_uint (value) / 100.0;
      break;

    case PROP_DROPDOWN_POSITION:
      dropdown->rel_position = g_value_get_uint (value) / 100.0;
      break;

    case PROP_DROPDOWN_OPACITY:
      screen = gtk_widget_get_screen (GTK_WIDGET (dropdown));
      if (gdk_screen_is_composited (screen))
        opacity = g_value_get_uint (value) / 100.0;
      else
        opacity = 1.00;

      gtk_widget_set_opacity (GTK_WIDGET (dropdown), opacity);
      return;

    case PROP_DROPDOWN_STATUS_ICON:
      if (g_value_get_boolean (value))
        {
          if (dropdown->status_icon == NULL)
            {
              dropdown->status_icon = gtk_status_icon_new_from_icon_name ("utilities-terminal");
              gtk_status_icon_set_title (dropdown->status_icon, _("Drop-down Terminal"));
              gtk_status_icon_set_tooltip_text (dropdown->status_icon, _("Toggle Drop-down Terminal"));
              g_signal_connect (G_OBJECT (dropdown->status_icon), "button-press-event",
                  G_CALLBACK (terminal_window_dropdown_status_icon_press_event), dropdown);
              g_signal_connect (G_OBJECT (dropdown->status_icon), "popup-menu",
                  G_CALLBACK (terminal_window_dropdown_status_icon_popup_menu), dropdown);
            }
        }
      else if (dropdown->status_icon != NULL)
        {
          g_object_unref (G_OBJECT (dropdown->status_icon));
          dropdown->status_icon = NULL;
        }
      return;

    case PROP_DROPDOWN_KEEP_ABOVE:
      gtk_window_set_keep_above (GTK_WINDOW (dropdown), g_value_get_boolean (value));
      if (window->preferences_dialog != NULL)
        terminal_util_activate_window (GTK_WINDOW (window->preferences_dialog));
      return;

    case PROP_DROPDOWN_ANIMATION_TIME:
      dropdown->animation_time = g_value_get_uint (value);
      return;

    case PROP_DROPDOWN_ALWAYS_SHOW_TABS:
      terminal_window_notebook_show_tabs (TERMINAL_WINDOW (dropdown));
      return;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
    }

  if (gtk_widget_get_visible (GTK_WIDGET (dropdown)))
    terminal_window_dropdown_show (dropdown, 0);
}



static void
terminal_window_dropdown_finalize (GObject *object)
{
  TerminalWindowDropdown *dropdown = TERMINAL_WINDOW_DROPDOWN (object);

  if (dropdown->grab_timeout_id != 0)
    g_source_remove (dropdown->grab_timeout_id);

  if (dropdown->animation_timeout_id != 0)
    g_source_remove (dropdown->animation_timeout_id);

  if (dropdown->status_icon != NULL)
    g_object_unref (G_OBJECT (dropdown->status_icon));

  (*G_OBJECT_CLASS (terminal_window_dropdown_parent_class)->finalize) (object);
}



static gboolean
terminal_window_dropdown_focus_in_event (GtkWidget     *widget,
                                         GdkEventFocus *event)
{
  TerminalWindowDropdown *dropdown = TERMINAL_WINDOW_DROPDOWN (widget);

  /* unset */
  dropdown->focus_out_time = 0;

  /* stop a possible grab test */
  if (dropdown->grab_timeout_id != 0)
    g_source_remove (dropdown->grab_timeout_id);

  return (*GTK_WIDGET_CLASS (terminal_window_dropdown_parent_class)->focus_in_event) (widget, event);
}



static gboolean
terminal_window_dropdown_can_grab (gpointer data)
{
  GdkWindow    *window = gtk_widget_get_window (GTK_WIDGET (data));
  GdkGrabStatus status;

#if GTK_CHECK_VERSION (3, 20, 0)
  GdkSeat *seat = gdk_display_get_default_seat (gtk_widget_get_display (GTK_WIDGET (data)));
  if (!gdk_window_is_visible (window))
    return FALSE;
  status = gdk_seat_grab (seat, window, GDK_SEAT_CAPABILITY_KEYBOARD, FALSE, NULL, NULL, NULL, NULL);
#else
  status = gdk_keyboard_grab (window, FALSE, GDK_CURRENT_TIME);
#endif

  if (status == GDK_GRAB_SUCCESS)
    {
      /* drop the grab */
#if GTK_CHECK_VERSION (3, 20, 0)
      gdk_seat_ungrab (seat);
#else
      gdk_keyboard_ungrab (GDK_CURRENT_TIME);
#endif

      return FALSE;
    }

  return TRUE;
}



static void
terminal_window_dropdown_can_grab_destroyed (gpointer data)
{
  TERMINAL_WINDOW_DROPDOWN (data)->grab_timeout_id = 0;
}



static gboolean
terminal_window_dropdown_focus_out_event (GtkWidget     *widget,
                                          GdkEventFocus *event)
{
  TerminalWindowDropdown *dropdown = TERMINAL_WINDOW_DROPDOWN (widget);
  gboolean                retval;

  /* let Gtk do its thingy */
  retval = (*GTK_WIDGET_CLASS (terminal_window_dropdown_parent_class)->focus_out_event) (widget, event);

  /* check if keep open is not enabled */
  if (gtk_widget_get_visible (widget)
      && TERMINAL_WINDOW (dropdown)->n_child_windows == 0
      && gtk_grab_get_current () == NULL
      && dropdown->animation_dir != ANIMATION_DIR_UP) /* popup menu check */
    {
      if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dropdown->keep_open)))
        {
          /* check if the user is not pressing a key */
          if (!terminal_window_dropdown_can_grab (dropdown))
            {
              /* hide the window */
              terminal_window_dropdown_hide (dropdown);

              return retval;
            }
          else if (dropdown->grab_timeout_id == 0)
            {
              /* focus-out with keyboard grab */
              dropdown->grab_timeout_id =
                  g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, 50, terminal_window_dropdown_can_grab,
                                      dropdown, terminal_window_dropdown_can_grab_destroyed);
            }
        }

      /* focus out time */
      dropdown->focus_out_time = g_get_real_time ();

    }

  return retval;
}



static gboolean
terminal_window_dropdown_status_icon_press_event (GtkStatusIcon          *status_icon,
                                                  GdkEventButton         *event,
                                                  TerminalWindowDropdown *dropdown)
{
  /* keep this event for the menu */
  if (event->button == 3)
    return FALSE;

  if (gdk_window_is_visible (gtk_widget_get_window (GTK_WIDGET (dropdown))))
    terminal_window_dropdown_hide (dropdown);
  else
    terminal_window_dropdown_show (dropdown, event->time);

  return TRUE;
}



static void
terminal_window_dropdown_status_icon_popup_menu (GtkStatusIcon          *status_icon,
                                                 guint                   button,
                                                 guint32                 timestamp,
                                                 TerminalWindowDropdown *dropdown)
{
  GtkActionGroup *group = TERMINAL_WINDOW (dropdown)->action_group;
  GtkWidget      *menu;
  GtkAction      *action;

  menu = gtk_menu_new ();
  g_signal_connect (G_OBJECT (menu), "selection-done",
      G_CALLBACK (gtk_widget_destroy), NULL);

  action = gtk_action_group_get_action (group, "preferences");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
                         gtk_action_create_menu_item (action));

  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
                         gtk_separator_menu_item_new ());

  action = gtk_action_group_get_action (group, "close-window");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
                         gtk_action_create_menu_item (action));

  gtk_widget_show_all (menu);
#if GTK_CHECK_VERSION (3, 22, 0)
  gtk_menu_popup_at_pointer (GTK_MENU (menu), NULL);
#else
  gtk_menu_popup (GTK_MENU (menu),
                  NULL, NULL,
                  NULL, NULL,
                  button, timestamp);
#endif
}



static gboolean
terminal_window_dropdown_animate_down (gpointer data)
{
  TerminalWindowDropdown *dropdown = TERMINAL_WINDOW_DROPDOWN (data);
  TerminalWindow         *window = TERMINAL_WINDOW (data);
  GtkRequisition          req1;
  GdkRectangle            rect;
  gint                    step_size, vbox_h;

  /* get window size */
#if GTK_CHECK_VERSION (3, 22, 0)
  GdkDisplay *display = gdk_screen_get_display (dropdown->screen);
  GdkMonitor *monitor =
      gdk_display_get_monitor_at_window (display, gtk_widget_get_window (GTK_WIDGET (data)));
  gdk_monitor_get_geometry (monitor, &rect);
#else
  gdk_screen_get_monitor_geometry (dropdown->screen, dropdown->monitor_num, &rect);
#endif
  if (!gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (window->action_fullscreen)))
    {
      /* calculate width/height if not fullscreen */
      rect.width *= dropdown->rel_width;
      rect.height *= dropdown->rel_height;
    }

  /* decrease each interval */
  step_size = rect.height * ANIMATION_FPS / dropdown->animation_time;
  if (step_size < 1)
    step_size = 1;

  /* new vbox size */
  gtk_widget_get_preferred_size (window->vbox, &req1, NULL);
  vbox_h = req1.height + step_size;
  if (vbox_h > rect.height)
    vbox_h = rect.height;

  /* resize */
  gtk_widget_set_size_request (window->vbox, req1.width, vbox_h);
  gtk_window_resize (GTK_WINDOW (window), req1.width, vbox_h);

  /* continue the animation */
  if (vbox_h < rect.height)
    return TRUE;

  /* restore the fullscreen state */
  if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (window->action_fullscreen)))
    gtk_window_fullscreen (GTK_WINDOW (window));

  /* animation complete */
  return FALSE;
}



static gboolean
terminal_window_dropdown_animate_up (gpointer data)
{
  TerminalWindowDropdown *dropdown = TERMINAL_WINDOW_DROPDOWN (data);
  TerminalWindow         *window = TERMINAL_WINDOW (data);
  GtkRequisition          req1;
  GdkRectangle            rect;
  gint                    step_size, vbox_h, min_size;

  /* get window size */
#if GTK_CHECK_VERSION (3, 22, 0)
  GdkDisplay *display = gdk_screen_get_display (dropdown->screen);
  GdkMonitor *monitor =
      gdk_display_get_monitor_at_window (display, gtk_widget_get_window (GTK_WIDGET (data)));
  gdk_monitor_get_geometry (monitor, &rect);
#else
  gdk_screen_get_monitor_geometry (dropdown->screen, dropdown->monitor_num, &rect);
#endif
  if (!gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (window->action_fullscreen)))
    {
      /* calculate width/height if not fullscreen */
      rect.width *= dropdown->rel_width;
      rect.height *= dropdown->rel_height;
    }

  /* decrease each interval */
  step_size = rect.height * ANIMATION_FPS / dropdown->animation_time;
  if (step_size < 1)
    step_size = 1;

  /* new vbox size */
  gtk_widget_get_preferred_size (window->vbox, &req1, NULL);
  vbox_h = req1.height - step_size;

  /* sizes of the widgets that cannot be shrunk */
  gtk_widget_get_preferred_size (window->notebook, &req1, NULL);
  min_size = req1.height;
  if (window->menubar != NULL
      && gtk_widget_get_visible (window->menubar))
    {
      gtk_widget_get_preferred_size (window->menubar, &req1, NULL);
      min_size += req1.height;
    }
  if (window->toolbar != NULL
      && gtk_widget_get_visible (window->toolbar))
    {
      gtk_widget_get_preferred_size (window->toolbar, &req1, NULL);
      min_size += req1.height;
    }

  if (vbox_h < min_size)
    {
      /* animation complete */
      gtk_widget_hide (GTK_WIDGET (dropdown));
      return FALSE;
    }

  /* resize window */
  gtk_widget_set_size_request (window->vbox, rect.width, vbox_h);
  gtk_window_resize (GTK_WINDOW (window), rect.width, vbox_h);

  return TRUE;
}



static void
terminal_window_dropdown_animate_destroyed (gpointer data)
{
  TERMINAL_WINDOW_DROPDOWN (data)->animation_timeout_id = 0;
  TERMINAL_WINDOW_DROPDOWN (data)->animation_dir = ANIMATION_DIR_NONE;
}



static void
terminal_window_dropdown_hide (TerminalWindowDropdown *dropdown)
{
  if (dropdown->animation_timeout_id != 0)
    g_source_remove (dropdown->animation_timeout_id);

  if (dropdown->animation_time > 0)
    {
      dropdown->animation_dir = ANIMATION_DIR_UP;
      dropdown->animation_timeout_id =
          g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, ANIMATION_FPS,
                              terminal_window_dropdown_animate_up, dropdown,
                              terminal_window_dropdown_animate_destroyed);
    }
  else
    {
      gtk_widget_hide (GTK_WIDGET (dropdown));
    }
}



static void
terminal_window_dropdown_show (TerminalWindowDropdown *dropdown,
                               guint32                 timestamp)
{
  TerminalWindow    *window = TERMINAL_WINDOW (dropdown);
  gint               w, h;
  GdkRectangle       monitor_geo;
  gint               x_dest, y_dest;
  GtkRequisition     req1;
  gboolean           move_to_active;
  gboolean           keep_above;
  gboolean           visible;
  gint               vbox_h;
  TerminalDirection  old_animation_dir = ANIMATION_DIR_NONE;
#if GTK_CHECK_VERSION (3, 22, 0)
  GdkDisplay        *display;
  GdkMonitor        *monitor;
#endif

  visible = gdk_window_is_visible (gtk_widget_get_window (GTK_WIDGET (dropdown)));

  if (dropdown->animation_timeout_id != 0)
    {
      old_animation_dir = dropdown->animation_dir;
      g_source_remove (dropdown->animation_timeout_id);
    }

  g_object_get (G_OBJECT (window->preferences),
                "dropdown-move-to-active", &move_to_active,
                NULL);

  /* target screen, either the last one or the active screen */
  if (move_to_active
      || dropdown->screen == NULL
      || dropdown->monitor_num == -1)
    dropdown->screen = xfce_gdk_screen_get_active (&dropdown->monitor_num);

  /* get the active monitor size */
#if GTK_CHECK_VERSION (3, 22, 0)
  display = gdk_screen_get_display (dropdown->screen);
  monitor = gdk_display_get_monitor_at_window (display,
                                               gtk_widget_get_window (GTK_WIDGET (dropdown)));
  gdk_monitor_get_geometry (monitor, &monitor_geo);
#else
  gdk_screen_get_monitor_geometry (dropdown->screen, dropdown->monitor_num, &monitor_geo);
#endif

  /* move window to correct screen */
  gtk_window_set_screen (GTK_WINDOW (dropdown), dropdown->screen);

  /* correct padding with notebook size */
  if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (window->action_fullscreen)))
    {
      /* don't fullscreen during animation*/
      if (dropdown->animation_time > 0)
        gtk_window_unfullscreen (GTK_WINDOW (window));

      /* use monitor geometry */
      w = monitor_geo.width;
      h = monitor_geo.height;
    }
  else
    {
      /* calculate size */
      w = monitor_geo.width * dropdown->rel_width;
      h = monitor_geo.height * dropdown->rel_height;
    }

  /* vbox size if not animated */
  vbox_h = h;

  /* vbox start height for animation */
  if (dropdown->animation_time > 0)
    {
      if (!visible)
        {
          /* start animation collapsed */
          vbox_h = 0;
        }
      else if (old_animation_dir == ANIMATION_DIR_UP)
        {
          /* pick up where we aborted */
          gtk_widget_get_preferred_size (window->vbox, &req1, NULL);
          vbox_h = req1.height;
        }
    }

  /* resize */
  gtk_widget_set_size_request (window->vbox, w, vbox_h);

  /* calc position */
  x_dest = monitor_geo.x + (monitor_geo.width - w) * dropdown->rel_position;
  y_dest = monitor_geo.y;

  /* show window */
  if (!visible)
    gtk_window_present_with_time (GTK_WINDOW (dropdown), timestamp);

  /* move */
  gtk_window_move (GTK_WINDOW (dropdown), x_dest, y_dest);

  /* force focus to the window */
  terminal_util_activate_window (GTK_WINDOW (dropdown));

  if (dropdown->animation_time > 0
      && vbox_h < h)
    {
      dropdown->animation_dir = ANIMATION_DIR_DOWN;
      dropdown->animation_timeout_id =
          g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, ANIMATION_FPS,
                              terminal_window_dropdown_animate_down, dropdown,
                              terminal_window_dropdown_animate_destroyed);
    }
  else
    {
      g_object_get (G_OBJECT (window->preferences),
                    "dropdown-keep-above", &keep_above,
                    NULL);
      gtk_window_set_keep_above (GTK_WINDOW (dropdown), keep_above);

      /* make sure all the content fits */
      gtk_window_resize (GTK_WINDOW (dropdown), w, h);
    }
}



static void
terminal_window_dropdown_toggle_real (TerminalWindowDropdown *dropdown,
                                      guint32                 timestamp,
                                      gboolean                force_show)
{
  TerminalWindow *window = TERMINAL_WINDOW (dropdown);
  gboolean        toggle_focus;

  if (!force_show
      && gdk_window_is_visible (gtk_widget_get_window (GTK_WIDGET (dropdown)))
      && dropdown->animation_dir != ANIMATION_DIR_UP)
    {
      g_object_get (G_OBJECT (window->preferences), "dropdown-toggle-focus", &toggle_focus, NULL);

      /* if the focus was lost for 0.1 second and toggle-focus is used, we had
       * focus until the shortcut was pressed, and then we hide the window */
      if (!toggle_focus
          || dropdown->focus_out_time == 0
          || (g_get_real_time () - dropdown->focus_out_time) < G_USEC_PER_SEC / 10)
        {
          /* hide */
          terminal_window_dropdown_hide (dropdown);
        }
      else
        {
          terminal_window_dropdown_show (dropdown, timestamp);
        }
    }
  else
    {
      /* popup */
      terminal_window_dropdown_show (dropdown, timestamp);
    }
}



static void
terminal_window_dropdown_update_geometry (TerminalWindowDropdown *dropdown)
{
  terminal_return_if_fail (TERMINAL_IS_WINDOW_DROPDOWN (dropdown));

  /* update geometry if toolbar or menu is shown */
  if (gtk_widget_get_visible (GTK_WIDGET (dropdown))
      && dropdown->animation_dir == ANIMATION_DIR_NONE)
    terminal_window_dropdown_show (dropdown, 0);
}



static guint32
terminal_window_dropdown_get_timestamp (GtkWidget   *widget,
                                        const gchar *startup_id)
{
  const gchar *timestr;
  guint32      timestamp;
  gchar       *end;

  /* extract the time from the id */
  if (startup_id != NULL)
    {
      timestr = g_strrstr (startup_id, "_TIME");
      if (G_LIKELY (timestr != NULL))
        {
          timestr += 5;
          errno = 0;

          /* translate into integer */
          timestamp = strtoul (timestr, &end, 0);
          if (end != timestr && errno == 0)
            return timestamp;
        }
    }

  return GDK_CURRENT_TIME;
}



GtkWidget *
terminal_window_dropdown_new (const gchar        *role,
                              gboolean            fullscreen,
                              TerminalVisibility  menubar,
                              TerminalVisibility  toolbar)
{
  TerminalWindow *window;
  gboolean        show_menubar;
  gboolean        show_toolbar;
  GtkAction      *action;

  if (G_LIKELY (role == NULL))
    role = PACKAGE_NAME "-dropdown";

  window = g_object_new (TERMINAL_TYPE_WINDOW_DROPDOWN, "role", role, NULL);

  /* read default preferences */
  g_object_get (G_OBJECT (window->preferences),
                "misc-menubar-default", &show_menubar,
                "misc-toolbar-default", &show_toolbar,
                NULL);

  /* setup full screen */
  if (fullscreen && gtk_action_is_sensitive (window->action_fullscreen))
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (window->action_fullscreen), TRUE);

  /* setup menubar visibility */
  if (G_LIKELY (menubar != TERMINAL_VISIBILITY_DEFAULT))
    show_menubar = (menubar == TERMINAL_VISIBILITY_SHOW);
  action = gtk_action_group_get_action (window->action_group, "show-menubar");
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), show_menubar);
  g_signal_connect_swapped (action, "activate",
      G_CALLBACK (terminal_window_dropdown_update_geometry), window);

  /* setup toolbar visibility */
  if (G_LIKELY (toolbar != TERMINAL_VISIBILITY_DEFAULT))
    show_toolbar = (toolbar == TERMINAL_VISIBILITY_SHOW);
  action = gtk_action_group_get_action (window->action_group, "show-toolbar");
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), show_toolbar);
  g_signal_connect_swapped (action, "activate",
      G_CALLBACK (terminal_window_dropdown_update_geometry), window);

  return GTK_WIDGET (window);
}



void
terminal_window_dropdown_toggle (TerminalWindowDropdown *dropdown,
                                 const gchar            *startup_id,
                                 gboolean                force_show)
{
  guint32 timestamp;

  /* toggle window */
  timestamp = terminal_window_dropdown_get_timestamp (GTK_WIDGET (dropdown), startup_id);
  terminal_window_dropdown_toggle_real (dropdown, timestamp, force_show);

  /* window is focussed or hidden */
  if (startup_id != NULL)
    gdk_notify_startup_complete_with_id (startup_id);
}



void
terminal_window_dropdown_get_size (TerminalWindowDropdown *dropdown,
                                   TerminalScreen         *screen,
                                   glong                  *grid_width,
                                   glong                  *grid_height)
{
  GdkScreen      *gdkscreen;
  gint            monitor_num;
  GdkRectangle    monitor_geo;
  gint            xpad, ypad;
  glong           char_width, char_height;
  GtkRequisition  req;
#if GTK_CHECK_VERSION (3, 22, 0)
  GdkDisplay     *display;
  GdkMonitor     *monitor;
#endif

  /* get the active monitor size */
  gdkscreen = xfce_gdk_screen_get_active (&monitor_num);
#if GTK_CHECK_VERSION (3, 22, 0)
  display = gdk_screen_get_display (gdkscreen);
  monitor = gdk_display_get_monitor_at_window (display,
                                               gtk_widget_get_window (GTK_WIDGET (dropdown)));
  gdk_monitor_get_geometry (monitor, &monitor_geo);
#else
  gdk_screen_get_monitor_geometry (gdkscreen, monitor_num, &monitor_geo);
#endif

  /* get terminal size */
  terminal_screen_get_geometry (screen, &char_width, &char_height, &xpad, &ypad);

  /* correct padding with visible widgets */
  gtk_widget_get_preferred_size (TERMINAL_WINDOW (dropdown)->vbox, &req, NULL);
  xpad += 2;
  ypad += req.height;

  /* return grid size */
  if (G_LIKELY (grid_width != NULL))
    *grid_width = ((monitor_geo.width * dropdown->rel_width) - xpad) / char_width;
  if (G_LIKELY (grid_height != NULL))
    *grid_height = ((monitor_geo.height * dropdown->rel_height) - ypad) / char_height;
}
