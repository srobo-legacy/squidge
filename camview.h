#ifndef __CAMVIEW_H
#define __CAMVIEW_H
#include <gtk/gtk.h>
#include <stdint.h>

typedef struct {
	/* The bits of the GUI that the camview stuff needs */
	GtkWindow *cam_window;
	GtkImage *cam_img;

	/* The fifo we receive blobs from */
	int fifo;
	/* And its associated GIOChannel */
	GIOChannel *fifo_gio;

	/* The fd for our shared memory data */
	int shm_fd;
	uint8_t *img_data;
} camview_t;

void camview_init( camview_t *cam, GtkBuilder *builder );

#endif	/* __CAMVIEW_H */
