#ifndef __SQUIDGE_H
#define __SQUIDGE_H
#include "camview.h"
#include <gtk/gtk.h>
#include <stdbool.h>

typedef struct {
	/* The main window */
	GtkWindow *win;
	GdkPixbuf *splash_pxb;

	/* First panel of notebook contains 'press button' label,
	   second contains the log */
	GtkNotebook *notebook;

	GtkTextView *log_textview;
	GtkTextBuffer *text_buffer;
	GtkScrolledWindow *log_scroll;
} squidge_ui_t;

typedef struct {
	squidge_ui_t ui;
	camview_t camview;

	/* Whether we're following the bottom of the log */
	bool follow_bottom;
} squidge_t;

#endif /* __SQUIDGE_H */
