/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Written by: Ondrej Holy <oholy@redhat.com>,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <glib/gi18n.h>
#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gnome-settings-daemon/gsd-enums.h>
#include <math.h>

#include "gnome-mouse-test.h"

#include <sys/types.h>
#include <sys/stat.h>

#define WID(x) (GtkWidget *) gtk_builder_get_object (d->builder, x)

/* Click test button sizes. */
#define SHADOW_SIZE (10.0 / 180 * size)
#define SHADOW_SHIFT_Y (-1.0 / 180 * size)
#define SHADOW_OPACITY (0.15 / 180 * size)
#define OUTER_CIRCLE_SIZE (22.0 / 180 * size)
#define ANNULUS_SIZE (6.0 / 180 * size)
#define INNER_CIRCLE_SIZE (52.0 / 180 * size)

#define CC_MOUSE_TEST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), CC_TYPE_MOUSE_TEST, CcMouseTestPrivate))

static void setup_information_label (CcMouseTestPrivate *d);
static void setup_scroll_image (CcMouseTestPrivate *d);

enum
{
	DOUBLE_CLICK_TEST_OFF,
	DOUBLE_CLICK_TEST_MAYBE,
	DOUBLE_CLICK_TEST_ON,
	DOUBLE_CLICK_TEST_STILL_ON,
	DOUBLE_CLICK_TEST_ALMOST_THERE,
	DOUBLE_CLICK_TEST_GEGL
};

struct _CcMouseTestPrivate
{
	GtkBuilder *builder;

	guint32 double_click_timestamp;
	gint double_click_state;
	gint button_state;

	GSettings *mouse_settings;

	gint information_label_timeout_id;
	gint button_drawing_area_timeout_id;
	gint scroll_image_timeout_id;
};

G_DEFINE_TYPE (CcMouseTest, cc_mouse_test, GTK_TYPE_ALIGNMENT);

/* Timeout for the double click test */

static gboolean
test_maybe_timeout (CcMouseTestPrivate *d)
{
	d->double_click_state = DOUBLE_CLICK_TEST_OFF;

	gtk_widget_queue_draw (WID ("button_drawing_area"));

	d->button_drawing_area_timeout_id = 0;

	return FALSE;
}

/* Timeout for the information label */

static gboolean
information_label_timeout (CcMouseTestPrivate *d)
{
	setup_information_label (d);

	d->information_label_timeout_id = 0;

	return FALSE;
}

/* Timeout for the scroll image */

static gboolean
scroll_image_timeout (CcMouseTestPrivate *d)
{
	setup_scroll_image (d);

	d->scroll_image_timeout_id = 0;

	return FALSE;
}

/* Set information label */

static void
setup_information_label (CcMouseTestPrivate *d)
{
	gchar *message = NULL;
	gchar *label_text = NULL;
	gboolean double_click;

	if (d->information_label_timeout_id != 0) {
		g_source_remove (d->information_label_timeout_id);
		d->information_label_timeout_id = 0;
	}

	if (d->double_click_state == DOUBLE_CLICK_TEST_OFF) {
		gtk_label_set_label (GTK_LABEL (WID ("information_label")), _("Try clicking, double clicking, scrolling"));
		return;
	}

	if (d->double_click_state == DOUBLE_CLICK_TEST_GEGL) {
		message = _("Five clicks, GEGL time!"), "</b>";
	} else {
		double_click = (d->double_click_state >= DOUBLE_CLICK_TEST_ON);
		switch (d->button_state) {
		case 1:
			message = (double_click) ? _("Double click, primary button") : _("Single click, primary button");
			break;
		case 2:
			message = (double_click) ? _("Double click, middle button") : _("Single click, middle button");
			break;
		case 3:
			message = (double_click) ? _("Double click, secondary button") : _("Single click, secondary button");
			break;
		}
	}

	label_text = g_strconcat ("<b>", message, "</b>", NULL);
	gtk_label_set_markup (GTK_LABEL (WID ("information_label")), label_text);
	g_free (label_text);

	d->information_label_timeout_id = g_timeout_add (2500, (GSourceFunc) information_label_timeout, d);
}

/* Update scroll image */

static void
setup_scroll_image (CcMouseTestPrivate *d)
{
	const char *resource;

	if (d->scroll_image_timeout_id != 0) {
		g_source_remove (d->scroll_image_timeout_id);
		d->scroll_image_timeout_id = 0;
	}

	if (d->double_click_state == DOUBLE_CLICK_TEST_GEGL)
		resource = "/org/gnome/control-center/mouse/scroll-test-gegl.svg";
	else
		resource = "/org/gnome/control-center/mouse/scroll-test.svg";
	gtk_image_set_from_resource (GTK_IMAGE (WID ("image")), resource);

	if (d->double_click_state != DOUBLE_CLICK_TEST_GEGL)
		return;

	d->scroll_image_timeout_id = g_timeout_add (5000, (GSourceFunc) scroll_image_timeout, d);
}

/* Callback issued when the user clicks the double click testing area. */

static gboolean
button_drawing_area_button_press_event (GtkWidget *widget,
					GdkEventButton *event,
					CcMouseTestPrivate *d)
{
	gint double_click_time;

	if (event->type != GDK_BUTTON_PRESS || event->button > 3)
		return FALSE;

	double_click_time = g_settings_get_int (d->mouse_settings, "double-click");

	if (d->button_drawing_area_timeout_id != 0) {
		g_source_remove (d->button_drawing_area_timeout_id);
		d->button_drawing_area_timeout_id = 0;
	}

	/* Ignore fake double click using different buttons. */
	if (d->double_click_state != DOUBLE_CLICK_TEST_OFF && d->button_state != event->button)
		d->double_click_state = DOUBLE_CLICK_TEST_OFF;

	switch (d->double_click_state) {
	case DOUBLE_CLICK_TEST_OFF:
		d->double_click_state = DOUBLE_CLICK_TEST_MAYBE;
		d->button_drawing_area_timeout_id = g_timeout_add (double_click_time, (GSourceFunc) test_maybe_timeout, d);
		break;
	case DOUBLE_CLICK_TEST_MAYBE:
	case DOUBLE_CLICK_TEST_ON:
	case DOUBLE_CLICK_TEST_STILL_ON:
	case DOUBLE_CLICK_TEST_ALMOST_THERE:
		if (event->time - d->double_click_timestamp < double_click_time) {
			d->double_click_state++;
			d->button_drawing_area_timeout_id = g_timeout_add (2500, (GSourceFunc) test_maybe_timeout, d);
		} else {
			test_maybe_timeout (d);
		}
		break;
	case DOUBLE_CLICK_TEST_GEGL:
		d->double_click_state = DOUBLE_CLICK_TEST_OFF;
		break;
	}

	d->double_click_timestamp = event->time;

	gtk_widget_queue_draw (WID ("button_drawing_area"));

	d->button_state = event->button;
	setup_information_label (d);
	setup_scroll_image (d);

	return TRUE;
}

static gboolean
button_drawing_area_draw_event (GtkWidget *widget,
				cairo_t *cr,
				CcMouseTestPrivate *d)
{
	gdouble center_x, center_y, size;
	GdkRGBA inner_color, outer_color;
	cairo_pattern_t *pattern;

	size = MAX (MIN (gtk_widget_get_allocated_width (widget), gtk_widget_get_allocated_height (widget)), 1);
	center_x = gtk_widget_get_allocated_width (widget) / 2.0;
	center_y = gtk_widget_get_allocated_height (widget) / 2.0;

	switch (d->double_click_state) {
	case DOUBLE_CLICK_TEST_ON:
	case DOUBLE_CLICK_TEST_STILL_ON:
	case DOUBLE_CLICK_TEST_ALMOST_THERE:
	case DOUBLE_CLICK_TEST_GEGL:
		gdk_rgba_parse (&outer_color, "#729fcf");
		gdk_rgba_parse (&inner_color, "#729fcf");
		break;
	case DOUBLE_CLICK_TEST_MAYBE:
		gdk_rgba_parse (&outer_color, "#729fcf");
		gdk_rgba_parse (&inner_color, "#ffffff");
		break;
	case DOUBLE_CLICK_TEST_OFF:
		gdk_rgba_parse (&outer_color, "#ffffff");
		gdk_rgba_parse (&inner_color, "#ffffff");
		break;
	}

	/* Draw shadow. */
	cairo_rectangle (cr, center_x - size / 2,  center_y - size / 2, size, size);
	pattern = cairo_pattern_create_radial (center_x, center_y, 0, center_x, center_y, size);
	cairo_pattern_add_color_stop_rgba (pattern, 0.5 - SHADOW_SIZE / size, 0, 0, 0, SHADOW_OPACITY);
	cairo_pattern_add_color_stop_rgba (pattern, 0.5, 0, 0, 0, 0);
	cairo_set_source (cr, pattern);
	cairo_fill (cr);

	/* Draw outer circle. */
	cairo_set_line_width (cr, OUTER_CIRCLE_SIZE);
	cairo_arc (cr, center_x, center_y + SHADOW_SHIFT_Y,
		   INNER_CIRCLE_SIZE + ANNULUS_SIZE + OUTER_CIRCLE_SIZE / 2,
		   0, 2 * G_PI);
	gdk_cairo_set_source_rgba (cr, &outer_color);
	cairo_stroke (cr);

	/* Draw inner circle. */
	cairo_set_line_width (cr, 0);
	cairo_arc (cr, center_x, center_y + SHADOW_SHIFT_Y,
		   INNER_CIRCLE_SIZE,
		   0, 2 * G_PI);
	gdk_cairo_set_source_rgba (cr, &inner_color);
	cairo_fill (cr);

	return FALSE;
}

static void
setup_dialog (CcMouseTestPrivate *d)
{
	GtkAdjustment *adjustment;
	GdkRGBA color;

	g_signal_connect (WID ("button_drawing_area"), "button_press_event",
			  G_CALLBACK (button_drawing_area_button_press_event),
			  d);
	g_signal_connect (WID ("button_drawing_area"), "draw",
			  G_CALLBACK (button_drawing_area_draw_event),
			  d);

	adjustment = GTK_ADJUSTMENT (WID ("scrolled_window_adjustment"));
	gtk_adjustment_set_value (adjustment,
				  gtk_adjustment_get_upper (adjustment));

	gdk_rgba_parse (&color, "#565854");
	gtk_widget_override_background_color (WID ("viewport"), GTK_STATE_FLAG_NORMAL, &color);
	gtk_widget_override_background_color (WID ("button_drawing_area"), GTK_STATE_FLAG_NORMAL, &color);
}

static void
cc_mouse_test_finalize (GObject *object)
{
	CcMouseTestPrivate *d = CC_MOUSE_TEST (object)->priv;

	g_clear_object (&d->mouse_settings);
	g_clear_object (&d->builder);

	if (d->information_label_timeout_id != 0) {
		g_source_remove (d->information_label_timeout_id);
		d->information_label_timeout_id = 0;
	}

	if (d->scroll_image_timeout_id != 0) {
		g_source_remove (d->scroll_image_timeout_id);
		d->scroll_image_timeout_id = 0;
	}

	if (d->button_drawing_area_timeout_id != 0) {
		g_source_remove (d->button_drawing_area_timeout_id);
		d->button_drawing_area_timeout_id = 0;
	}

	G_OBJECT_CLASS (cc_mouse_test_parent_class)->finalize (object);
}

static void
cc_mouse_test_class_init (CcMouseTestClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = cc_mouse_test_finalize;

	g_type_class_add_private (class, sizeof (CcMouseTestPrivate));
}

static void
cc_mouse_test_init (CcMouseTest *object)
{
	CcMouseTestPrivate *d = object->priv = CC_MOUSE_TEST_GET_PRIVATE (object);
	GError *error = NULL;

	d->builder = gtk_builder_new ();
	gtk_builder_add_from_resource (d->builder,
				       "/org/gnome/control-center/mouse/gnome-mouse-test.ui",
				       &error);

	d->double_click_timestamp = 0;
	d->double_click_state = DOUBLE_CLICK_TEST_OFF;
	d->button_state = 0;

	d->mouse_settings = g_settings_new ("org.gnome.settings-daemon.peripherals.mouse");

	d->information_label_timeout_id = 0;
	d->button_drawing_area_timeout_id = 0;
	d->scroll_image_timeout_id = 0;

	gtk_container_add (GTK_CONTAINER (object), WID ("test_widget"));

	setup_dialog (d);
}

GtkWidget *
cc_mouse_test_new (void)
{
	return (GtkWidget *) g_object_new (CC_TYPE_MOUSE_TEST, NULL);
}
