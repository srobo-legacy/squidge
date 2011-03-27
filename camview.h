#ifndef __CAMVIEW_H
#define __CAMVIEW_H
#include <gtk/gtk.h>

/* The bits of the GUI that the camview stuff needs */
typedef struct {
	GtkImage *cam_img;

} camview_t;

void camview_init( camview_t *cam, GtkBuilder *builder );

#endif	/* __CAMVIEW_H */
