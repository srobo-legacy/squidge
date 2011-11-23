#ifndef __CAMVIEW_H
#define __CAMVIEW_H
#include <gtk/gtk.h>
#include <stdint.h>

#define IMG_X 320
#define IMG_Y 240
#define IMG_SIZE (IMG_X * IMG_Y * 3)

typedef struct {
	/* The bits of the GUI that the camview stuff needs */
	GtkWindow *cam_window;
	GtkNotebook *cam_notebook;
	GtkImage *cam_img;
	GtkLabel *cam_label;

	/* The fifo we receive blobs from */
	int fifo;
	/* And its associated GIOChannel */
	GIOChannel *fifo_gio;
	guint fifo_sources[3];
	/* And its stream */
	FILE *fifo_stream;

	/* The fd for our shared memory data */
	int shm_fd;
	uint8_t *img_data;
	/* A buffer to hold the image with the R and B channels flipped */
	uint8_t flipped_img_data[IMG_SIZE];
} camview_t;

void camview_init( camview_t *cam, GtkBuilder *builder );

#endif	/* __CAMVIEW_H */
