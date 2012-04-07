#ifndef __SQUIDGE_H
#define __SQUIDGE_H
#include "camview.h"
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
	/* The main window */
	GtkWindow *win;

	/* First panel of notebook contains 'press button' label,
	   second contains the log */
	GtkNotebook *notebook;

	/*** Splash related things ***/
	GdkPixbuf *splash_pxb;
	GtkLabel *label_zone;
	GtkLabel *label_mode;

	/*** Log related things ***/
	GtkTextView *log_textview;
	GtkTextBuffer *text_buffer;
	GtkScrolledWindow *log_scroll;
} squidge_ui_t;

typedef struct {
	squidge_ui_t ui;
	camview_t camview;

	/* Whether we're following the bottom of the log */
	bool follow_bottom;

	/*** Match settings ***/
	/* Zone number (0-3 inclusive) */
	uint8_t zone;

	/* Whether we're in competition mode */
	bool comp_mode;

} squidge_t;

#endif /* __SQUIDGE_H */
