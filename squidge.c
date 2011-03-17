#include <fcntl.h>
#include "squidge-gtkbuilder.h"
#include "squidge-splash-img.h"
#include <stdlib.h>
#include <stdbool.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct {
	/* The main window */
	GtkWindow *win;
	/* First panel of notebook contains 'press button' label,
	   second contains the log */
	GtkNotebook *notebook;
	GtkImage *splash;

	GtkTextView *log_textview;
	GtkTextBuffer *text_buffer;
	GtkScrolledWindow *scroll;
} squidge_ui_t;

typedef struct {
	squidge_ui_t ui;
} squidge_t;

int file_fd, inotify_fd;
GIOChannel *log_io, *inotify_io, *stdin_io;
gdouble prevscrollbarmaxvalue = -1.0; // to ensure that it is not initially = scrollbarvalue.
bool log_io_active = false;

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
	/* The code below this point has bugs, which will be fixed in another commit! */

	GtkAdjustment *scrollbaradjustment;
	GtkScrollbar *vertscrollbar;
	gdouble scrollbarvalue;

	vertscrollbar = GTK_SCROLLBAR(gtk_scrolled_window_get_vscrollbar(squidge->ui.scroll));
	g_assert( vertscrollbar != NULL );

	scrollbaradjustment = gtk_range_get_adjustment(GTK_RANGE(vertscrollbar));
	g_assert( scrollbaradjustment != NULL );

	scrollbarvalue = gtk_adjustment_get_value(scrollbaradjustment);

	if (prevscrollbarmaxvalue == scrollbarvalue)
		text_scroll_to_bottom(squidge);

	// This expression calculates the true max value, due to the size of the slider on the scroll bar itself:
	prevscrollbarmaxvalue = gtk_adjustment_get_upper(scrollbaradjustment); // expression split for legibility.
	prevscrollbarmaxvalue -= gtk_adjustment_get_page_size(scrollbaradjustment);

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

#define obj(t, n) do {							\
		sq->ui. n = t(gtk_builder_get_object( builder, #n ));	\
	} while (0)

static void init_ui( squidge_t *sq )
{
	GtkBuilder *builder;

	builder = gtk_builder_new();
	g_assert( gtk_builder_add_from_string( builder, squidge_gtkbuilder, -1, NULL ) );

	obj( GTK_WINDOW, win );
	obj( GTK_NOTEBOOK, notebook );
	obj( GTK_IMAGE, splash );
	obj( GTK_TEXT_VIEW, log_textview );

	sq->ui.text_buffer = gtk_text_view_get_buffer(sq->ui.log_textview);
	load_splash(sq);

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
