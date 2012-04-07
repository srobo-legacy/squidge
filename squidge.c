#define _GNU_SOURCE
#include "camview.h"
#include <fcntl.h>
#include "squidge.h"
#include "squidge-gtkbuilder.h"
#include "squidge-splash-img.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* GDK key constants changed name in Sept 2010 */
#ifndef GDK_KEY_Right
#define GDK_KEY_Right GDK_Right
#define GDK_KEY_Left GDK_Left
#endif

static int file_fd, inotify_fd;
static GIOChannel *log_io, *inotify_io, *stdin_io;
static bool log_io_active = false;

void
text_scroll_to_bottom(squidge_t *squidge)
{
	GtkTextIter iter;

	gtk_text_buffer_get_end_iter(squidge->ui.text_buffer, &iter);
	gtk_text_view_scroll_to_iter(squidge->ui.log_textview, &iter, 0.0, FALSE, 0, 0);
}

gboolean
read_log_file(GIOChannel *src, GIOCondition cond, gpointer _squidge)
{
	squidge_t *squidge = _squidge;
	GtkTextIter iter;
	char buffer[1024];
	int len;

	len = read(file_fd, buffer, sizeof(buffer));
	if (len == 0) {
		log_io_active = false;
		return FALSE;
	}

	gtk_text_buffer_get_end_iter(squidge->ui.text_buffer, &iter);
	gtk_text_buffer_insert(squidge->ui.text_buffer, &iter, buffer, len);

	return TRUE;
}

gboolean
read_inotify_fd(GIOChannel *src, GIOCondition cond, gpointer _squidge)
{
	char buffer[1024];
	struct inotify_event evt;
	int ret;

	ret = read(inotify_fd, &evt, sizeof(evt));
	if (ret != sizeof(evt))
		/* Much weirdness */
		return FALSE;

	if (evt.len != 0)
		read(inotify_fd, buffer, evt.len);

	if (evt.mask & IN_MODIFY && !log_io_active) {
		g_io_add_watch(log_io, G_IO_IN, read_log_file, _squidge);
		log_io_active = true;
	}

	return TRUE;
}

/* Write the match information to the mode file */
static void match_info_write( squidge_t *sq )
{
	char *tmp;
	FILE *f;

	/* Write the match info to a temporary file first,
	   then move the file into place.  This avoids the client
	   reading it when it is half complete. */

	g_assert( asprintf( &tmp, "%s.tmp", sq->mode_fname ) != -1 );

	f = fopen( tmp, "w" );
	g_assert( f != NULL );

	fprintf( f, "{\n"
		 "\"mode\":" );

	if( sq->comp_mode )
		fprintf( f, "\"comp\",\n" );
	else
		fprintf( f, "\"dev\",\n" );

	fprintf( f, "\"zone\": %hhu\n", sq->zone );
	fprintf( f, "}\n" );

	fclose(f);

	g_assert( rename( tmp, sq->mode_fname ) == 0 );
	free( tmp );
}

gboolean
read_stdin(GIOChannel *src, GIOCondition cond, gpointer _squidge)
{
	squidge_t *squidge = _squidge;
	char buffer[1024];
	int ret;

	ret = read(0, &buffer[0], sizeof(buffer));
	if (ret != 0) {
		/* We've been fed some input (any input) from pyenv, indicating
		 * that the user button has been pressed. */

		/* Output the match information */
		match_info_write( squidge );

		/* Switch to the log */
		gtk_notebook_set_current_page( squidge->ui.notebook, 1 );
		gtk_window_set_focus(squidge->ui.win, GTK_WIDGET(squidge->ui.log_textview));

		if( squidge->follow_bottom )
			text_scroll_to_bottom(squidge);

		return FALSE;
	}

	return TRUE;
}

/* Synchronise the display with the contents of *sq */
static void match_settings_update( squidge_t *sq )
{
	char *s;
	const char *ms;

	g_assert( asprintf( &s,
			    "<span color=\"white\" size=\"large\" font_weight=\"bold\">Zone: %hhu</span>",
			    sq->zone ) != -1 );
	gtk_label_set_markup( sq->ui.label_zone, s );
	free(s);

	if( sq->comp_mode )
		ms = "Competition Mode";
	else
		ms = "Development Mode";

	g_assert( asprintf( &s,
			    "<span color=\"white\" size=\"large\" font_weight=\"bold\">%s</span>",
			    ms ) != -1 );
	gtk_label_set_markup( sq->ui.label_mode, s );
	free(s);
}

static gboolean log_key_evt_handler(GtkWidget *wind, GdkEventKey *key, gpointer _squidge)
{
	squidge_t *squidge = _squidge;

	if (key->type == GDK_KEY_PRESS) {
		if (key->keyval == GDK_Page_Up) {
			squidge->follow_bottom = true;
			text_scroll_to_bottom(squidge);
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean splash_key_evt_handler(GtkWidget *wind, GdkEventKey *key, gpointer _squidge)
{
	squidge_t *sq = _squidge;

	if (key->type == GDK_KEY_PRESS) {
		bool update = false;

		switch( key->keyval ) {

		case GDK_KEY_Right:
			/* Zone Up: Left rotary CW */
			if( sq->zone < 3 )
				sq->zone++;
			update = true;
			break;

		case GDK_KEY_Left:
			/* Zone Down: Left rotary CCW */
			if( sq->zone > 0 )
				sq->zone--;
			update = true;
			break;

		case GDK_Page_Down:
		case GDK_Page_Up:
			/* Both these buttons toggle comp/dev mode */
			sq->comp_mode ^= 1;
			update = true;
			break;
		}

		if( update ) {
			/* Propagate the changes through to the interface */
			match_settings_update( sq );
			return TRUE;
		}
	}

	return FALSE;
}

G_MODULE_EXPORT gboolean key_evt_handler(GtkWidget *wind, GdkEventKey *key, gpointer _squidge)
{
	squidge_t *sq = _squidge;
	gint page;

	page = gtk_notebook_get_current_page( sq->ui.notebook );

	/* Different handler for each page */
	switch( page ) {
	case 0:
		return splash_key_evt_handler( wind, key, _squidge );
	case 1:
		return log_key_evt_handler( wind, key, _squidge );
	}
	return FALSE;
}

static void load_splash( squidge_t *sq )
{
	GdkPixbufLoader *ldr;

	ldr = gdk_pixbuf_loader_new();
	/* Load the splash image from the baked-in splash image buffer */
	g_assert( gdk_pixbuf_loader_write( ldr, splash_img, SPLASH_IMG_LEN, NULL ) );

	sq->ui.splash_pxb = gdk_pixbuf_loader_get_pixbuf( ldr );
	g_assert( sq->ui.splash_pxb != NULL );

	/* We want to keep this pixbuf around! */
	g_object_ref( sq->ui.splash_pxb );

	gdk_pixbuf_loader_close(ldr, NULL);
	g_object_unref(ldr);
}

/* Draw the splash screen on the back of the window */
static gboolean win_expose( GtkWidget *widget,
			    GdkEvent *ev,
			    gpointer _sq )
{
	squidge_t *sq = _sq;
	GdkWindow *gw;
	GtkWidget *page;

	page = gtk_notebook_get_nth_page( sq->ui.notebook, 0 );
	gw = gtk_widget_get_window( page );

	gdk_draw_pixbuf( GDK_DRAWABLE( gw ),
			 NULL,
			 sq->ui.splash_pxb,
			 0, 0, 0, 0,
			 -1, -1,
			 GDK_RGB_DITHER_NORMAL,
			 0, 0 );

	return FALSE;
}

/* Returns the upper scroll value
 * The adjustments for scrollbars in Gtk range from
 * the adjustment's "lower" value to its "upper - page_size"
 * value.  Ths function returns "upper - page_size" for the
 * given adjustment. */
static double adjustment_get_end_value( GtkAdjustment *adj )
{
	const gdouble upper = gtk_adjustment_get_upper(adj);
	const gdouble page_size = gtk_adjustment_get_page_size(adj);

	return upper - page_size;
}

/* Scrolled window adjustment "value-changed" signal handler
 * Called when the adjustment's "value" field has changed.
 * (i.e. someone's _changed_ the scroll position) */
static void vert_value_changed( GtkAdjustment *adj,
				gpointer _squidge )
{
	squidge_t *sq = _squidge;
	const gdouble pos = gtk_adjustment_get_value(adj);
	const gdouble end = adjustment_get_end_value(adj);

	/* If we've scrolled to the end, we're following the end */
	sq->follow_bottom = (pos == end);
}

/* Scrolled window adjustment "changed" signal handler
 * Called when any property _other_ than the "value" field has changed.
 * The only time this happens in this app is when additional lines
 * are appended to the end of the log text. */
static void vert_changed( GtkAdjustment *adj,
			  gpointer _squidge )
{
	squidge_t *sq = _squidge;
	const gdouble pos = gtk_adjustment_get_value(adj);
	const gdouble end = adjustment_get_end_value(adj);

	if( sq->follow_bottom && pos != end )
		/* Scroll to the bottom again */
		gtk_adjustment_set_value( adj, end );
}

#define obj(t, n) do {							\
		sq->ui. n = t(gtk_builder_get_object( builder, #n ));	\
	} while (0)

static void init_ui( squidge_t *sq )
{
	GtkBuilder *builder;
	PangoFontDescription *pf;
	GtkAdjustment *v;
	GtkWidget *page;

	builder = gtk_builder_new();
	g_assert( gtk_builder_add_from_string( builder, squidge_gtkbuilder, -1, NULL ) );

	obj( GTK_WINDOW, win );
	obj( GTK_NOTEBOOK, notebook );
	obj( GTK_LABEL, label_zone );
	obj( GTK_LABEL, label_mode );
	obj( GTK_TEXT_VIEW, log_textview );
	obj( GTK_SCROLLED_WINDOW, log_scroll );

	sq->ui.text_buffer = gtk_text_view_get_buffer(sq->ui.log_textview);

	load_splash(sq);

	/* Draw the splash on the first page of the notebook */
	page = gtk_notebook_get_nth_page( sq->ui.notebook, 0 );
	g_signal_connect( page, "expose-event",
			  G_CALLBACK( win_expose ), sq );

	match_settings_update( sq );

	camview_init( &sq->camview, builder );

	/* We want a monospace font */
	pf = pango_font_description_from_string("Monospace 10");
	g_assert( pf != NULL );
	gtk_widget_modify_font( GTK_WIDGET(sq->ui.log_textview), pf );
	pango_font_description_free( pf );

	/* We want the value-changed signal so we can stop following the bottom */
	v = gtk_scrolled_window_get_vadjustment( sq->ui.log_scroll );
	g_signal_connect( v, "value-changed",
			  G_CALLBACK(vert_value_changed), sq );
	g_signal_connect( v, "changed",
			  G_CALLBACK(vert_changed), sq );

	gtk_builder_connect_signals( builder, sq );
	g_object_unref( G_OBJECT(builder) );
	gtk_widget_show( GTK_WIDGET(sq->ui.win) );
}
#undef obj

int
main(int argc, char **argv)
{
	squidge_t squidge;

	if (argc != 3) {
		fprintf(stderr, "Usage: squidge LOGFILE MODEFILE\n"
			" - LOGFILE: The logfile to display.\n"
			" - MODEFILE: The file to output JSON-formatted match mode data to.\n" );
		return 1;
	}

	file_fd = open(argv[1], O_RDONLY, 0);
	if (file_fd < 0) {
		perror("Couldn't open log file");
		return 1;
	}
	squidge.mode_fname = argv[2];

	gtk_init(&argc, &argv);

	/* Default to zone 0 */
	squidge.zone = 0;
	/* and development mode */
	squidge.comp_mode = false;

	init_ui( &squidge );
	squidge.follow_bottom = true;

	inotify_fd = inotify_init();
	inotify_add_watch(inotify_fd, argv[1], IN_MODIFY);

	inotify_io = g_io_channel_unix_new(inotify_fd);
	g_io_add_watch(inotify_io, G_IO_IN, read_inotify_fd, &squidge);

	log_io = g_io_channel_unix_new(file_fd);
	g_io_add_watch(log_io, G_IO_IN, read_log_file, &squidge);
	log_io_active = true;

	stdin_io = g_io_channel_unix_new(0);
	g_io_add_watch(stdin_io, G_IO_IN, read_stdin, &squidge);

	/* Hide the cursor by making it transparent */
	GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(squidge.ui.win));
	GdkCursor *cursor;
	cursor = gdk_cursor_new(GDK_BLANK_CURSOR);
	gdk_window_set_cursor(gdk_window, cursor);

	GdkDisplay *display = gdk_display_get_default();
	GdkScreen *scr = gdk_screen_get_default();
	gdk_display_warp_pointer(display, scr, 1000, 1000);

	gtk_main();

	return 0;
}
