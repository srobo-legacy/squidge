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
	

int
main(int argc, char **argv)
{

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

	text_view = GTK_TEXT_VIEW(gtk_text_view_new());
	gtk_text_view_set_editable(text_view, FALSE);
	text_buffer = gtk_text_view_get_buffer(text_view);

	gtk_container_add(GTK_CONTAINER(top_window), GTK_WIDGET(text_view));
	gtk_widget_show_all(GTK_WIDGET(top_window));

	gtk_main();

	return 0;
}
