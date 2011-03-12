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
GtkScrolledWindow *scroll;
GtkLabel *start_robot;
GIOChannel *log_io, *inotify_io, *stdin_io;
gdouble prevscrollbarmaxvalue = -1.0; // to ensure that it is not initially = scrollbarvalue.
bool log_io_active = false;

void
text_scroll_to_bottom(void)
{
	GtkTextIter iter;

	gtk_text_buffer_get_end_iter(text_buffer, &iter);
	gtk_text_view_scroll_to_iter(text_view, &iter, 0.0, FALSE, 0, 0);
}

gboolean
read_log_file(GIOChannel *src, GIOCondition cond, gpointer data)
{
	GtkTextIter iter;
	char buffer[1024];
	int len;

	len = read(file_fd, buffer, sizeof(buffer));
	if (len == 0) {
		log_io_active = false;
		return FALSE;
	}

	gtk_text_buffer_get_end_iter(text_buffer, &iter);
	gtk_text_buffer_insert(text_buffer, &iter, buffer, len);

	GtkAdjustment *scrollbaradjustment;
	GtkScrollbar *vertscrollbar;
	gdouble scrollbarvalue;

	vertscrollbar = GTK_SCROLLBAR(gtk_scrolled_window_get_vscrollbar(scroll));
	if (vertscrollbar == NULL)
		return FALSE; // Much weirdness.

	scrollbaradjustment = gtk_range_get_adjustment(GTK_RANGE(vertscrollbar));
	if (scrollbaradjustment == NULL)
		return FALSE; // Much weirdness II - The weirdness returns!

	scrollbarvalue = gtk_adjustment_get_value(scrollbaradjustment);

	if (prevscrollbarmaxvalue == scrollbarvalue)
		text_scroll_to_bottom();

	// This expression calculates the true max value, due to the size of the slider on the scroll bar itself:
	prevscrollbarmaxvalue = gtk_adjustment_get_upper(scrollbaradjustment); // expression split for legibility.
	prevscrollbarmaxvalue -= gtk_adjustment_get_page_size(scrollbaradjustment);

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
read_stdin(GIOChannel *src, GIOCondition cond, gpointer data)
{
	char buffer[1024];
	int ret;

	ret = read(0, &buffer[0], sizeof(buffer));
	if (ret != 0) {
		/* We've been fed some input (any input) from pyenv, indicating
		 * that the user button has been pressed. Switch view. */
		gtk_container_remove(GTK_CONTAINER(top_window), GTK_WIDGET(start_robot));
		gtk_container_add(GTK_CONTAINER(top_window), GTK_WIDGET(scroll));
		gtk_widget_grab_focus(GTK_WIDGET(text_view));
		gtk_widget_show_all(GTK_WIDGET(top_window));
		/* I assume we'll want to scroll to the bottom so we get the autoscrolling,
		 * even if text was already present - Steven */
		text_scroll_to_bottom();
		return FALSE;
	}

	return TRUE;
}

gboolean
key_evt_handler(GtkWidget *wind, GdkEventKey *key, gpointer unused)
{
	if (key->type == GDK_KEY_PRESS) {
		if (key->keyval == GDK_Page_Up) {
			text_scroll_to_bottom();
			return TRUE;
		}
	}

	return FALSE;
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

	stdin_io = g_io_channel_unix_new(0);
	g_io_add_watch(stdin_io, G_IO_IN, read_stdin, NULL);

	gtk_init(&argc, &argv);

	top_window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	gtk_window_set_decorated(top_window, FALSE);
	gtk_window_set_default_size(top_window, 480, 272);
	gtk_container_set_border_width(GTK_CONTAINER(top_window), 0);
	gtk_window_set_resizable(top_window, true);

	scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
	// Ensure we have scrollbars in both directions:
	// Sanity check scroll != NULL?
	gtk_scrolled_window_set_policy(scroll, GTK_POLICY_ALWAYS, GTK_POLICY_ALWAYS);

	text_view = GTK_TEXT_VIEW(gtk_text_view_new());
	gtk_text_view_set_editable(text_view, FALSE);
	text_buffer = gtk_text_view_get_buffer(text_view);

	gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(text_view));

	start_robot = GTK_LABEL(gtk_label_new("<--- Press button to start"));
	gtk_widget_modify_font(GTK_WIDGET(start_robot), pango_font_description_from_string("sans-serif 24"));
	gtk_container_add(GTK_CONTAINER(top_window), GTK_WIDGET(start_robot));

	gtk_widget_show_all(GTK_WIDGET(top_window));

	/* Hide the cursor by making it transparent */
	GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(top_window));
	GdkCursor *cursor;
	cursor = gdk_cursor_new(GDK_BLANK_CURSOR);
	gdk_window_set_cursor(gdk_window, cursor);

	GdkDisplay *display = gdk_display_get_default();
	GdkScreen *scr = gdk_screen_get_default();
	gdk_display_warp_pointer(display, scr, 1000, 1000);

	g_signal_connect_swapped(G_OBJECT(top_window), "destroy",
			G_CALLBACK(gtk_main_quit), G_OBJECT(top_window));
	g_signal_connect(G_OBJECT(top_window), "key-press-event",
			G_CALLBACK(key_evt_handler), NULL);

	gtk_window_set_focus(top_window, GTK_WIDGET(text_view));

	gtk_main();

	return 0;
}
