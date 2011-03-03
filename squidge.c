/*#include <fcntl.h>*/
/* including fcntl conflicts with linux/inotify. Broken headers are broken. */
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <linux/inotify.h>

int file_fd, inotify_fd;
GtkWindow *top_window;
GtkTextView *text_view;
GtkTextBuffer *text_buffer;
GIOChannel *log_io, *inotify_io;
bool log_io_active = false;

gboolean
read_log_file(GIOChannel *src, GIOCondition cond, gpointer data)
{
	GtkTextIter iter;
	char buffer[1024];
	int len;

	len = read(file_fd, buffer, 1024);
	if (len == 0) {
		log_io_active = false;
		return FALSE;
	}

	gtk_text_buffer_get_end_iter(text_buffer, &iter);
	gtk_text_buffer_insert(text_buffer, &iter, buffer, len);

	return TRUE;
}

gboolean
read_inotify_fd(GIOChannel *src, GIOCondition cond, gpointer data)
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
		g_io_add_watch(log_io, G_IO_IN, read_log_file, NULL);
		log_io_active = true;
	}

	return TRUE;
}

gboolean
key_evt_handler(GtkWidget *wind, GdkEventKey *key, gpointer unused)
{
	GtkTextIter iter;

	if (key->type == GDK_KEY_PRESS) {
		if (key->keyval == GDK_Page_Up) {
			gtk_text_buffer_get_end_iter(text_buffer, &iter);
			gtk_text_view_scroll_to_iter(text_view, &iter, 0.0,
							FALSE, 0, 0);
			return TRUE;
		}
	}

	return FALSE;
}

int
main(int argc, char **argv)
{
	GtkScrolledWindow *scroll;

	if (argc != 2) {
		fprintf(stderr, "Usage: squidge logfile\n");
		return 1;
	}

	file_fd = open(argv[1], O_RDONLY, 0);
	if (file_fd < 0) {
		perror("Couldn't open log file");
		return 1;
	}

	inotify_fd = inotify_init();
	inotify_add_watch(inotify_fd, argv[1], IN_MODIFY);

	inotify_io = g_io_channel_unix_new(inotify_fd);
	g_io_add_watch(inotify_io, G_IO_IN, read_inotify_fd, NULL);

	log_io = g_io_channel_unix_new(file_fd);
	g_io_add_watch(log_io, G_IO_IN, read_log_file, NULL);
	log_io_active = true;

	gtk_init(&argc, &argv);

	top_window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	gtk_window_set_decorated(top_window, FALSE);
	gtk_window_set_default_size(top_window, 480, 272);
	gtk_container_set_border_width(GTK_CONTAINER(top_window), 0);
	gtk_window_set_resizable(top_window, true);

	scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
	gtk_container_add(GTK_CONTAINER(top_window), GTK_WIDGET(scroll));

	text_view = GTK_TEXT_VIEW(gtk_text_view_new());
	gtk_text_view_set_editable(text_view, FALSE);
	text_buffer = gtk_text_view_get_buffer(text_view);

	gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(text_view));

	gtk_widget_show_all(GTK_WIDGET(top_window));

	GdkDisplay *display = gdk_display_get_default();
	GdkScreen *scr = gdk_screen_get_default();
	gdk_display_warp_pointer(display, scr, 1000, 1000);

	g_signal_connect_swapped(G_OBJECT(top_window), "destroy",
			G_CALLBACK(gtk_main_quit), G_OBJECT(top_window));
	g_signal_connect(G_OBJECT(top_window), "key-press-event",
			key_evt_handler, NULL);

	gtk_window_set_focus(top_window, GTK_WIDGET(text_view));

	gtk_main();

	return 0;
}
