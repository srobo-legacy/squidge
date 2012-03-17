#include "camview.h"
#include <fcntl.h>
#include "squidge.h"
#include "squidge-gtkbuilder.h"
#include "squidge-splash-img.h"
#include <stdlib.h>
#include <stdbool.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

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

gboolean
read_stdin(GIOChannel *src, GIOCondition cond, gpointer _squidge)
{
	squidge_t *squidge = _squidge;
	char buffer[1024];
	int ret;

	ret = read(0, &buffer[0], sizeof(buffer));
	if (ret != 0) {
		/* We've been fed some input (any input) from pyenv, indicating
		 * that the user button has been pressed. Switch to the log */
		gtk_notebook_set_current_page( squidge->ui.notebook, 1 );
		gtk_window_set_focus(squidge->ui.win, GTK_WIDGET(squidge->ui.log_textview));

		if( squidge->follow_bottom )
			text_scroll_to_bottom(squidge);

		return FALSE;
	}

	return TRUE;
}

G_MODULE_EXPORT gboolean
key_evt_handler(GtkWidget *wind, GdkEventKey *key, gpointer _squidge)
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

static void load_splash( squidge_t *sq )
{
	GdkPixbufLoader *ldr;
	GdkPixbuf *pxb;

	ldr = gdk_pixbuf_loader_new();
	/* Load the splash image from the baked-in splash image buffer */
	g_assert( gdk_pixbuf_loader_write( ldr, splash_img, SPLASH_IMG_LEN, NULL ) );
	pxb = gdk_pixbuf_loader_get_pixbuf( ldr );

	gtk_image_set_from_pixbuf( sq->ui.splash, pxb );

	gdk_pixbuf_loader_close(ldr, NULL);
	g_object_unref(ldr);
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

	builder = gtk_builder_new();
	g_assert( gtk_builder_add_from_string( builder, squidge_gtkbuilder, -1, NULL ) );

	obj( GTK_WINDOW, win );
	obj( GTK_NOTEBOOK, notebook );
	obj( GTK_IMAGE, splash );
	obj( GTK_TEXT_VIEW, log_textview );
	obj( GTK_SCROLLED_WINDOW, log_scroll );

	sq->ui.text_buffer = gtk_text_view_get_buffer(sq->ui.log_textview);
	load_splash(sq);
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

	if (argc != 2) {
		fprintf(stderr, "Usage: squidge logfile\n");
		return 1;
	}

	file_fd = open(argv[1], O_RDONLY, 0);
	if (file_fd < 0) {
		perror("Couldn't open log file");
		return 1;
	}

	gtk_init(&argc, &argv);
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
