#include "camview.h"
#include <fcntl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define IMG_X 320
#define IMG_Y 240
#define IMG_SIZE (IMG_X * IMG_Y * 3)

#define FLAG_POS (IMG_SIZE)

/* Callback for when data is available for reading from the fifo */
static gboolean fifo_data_ready( GIOChannel *src, GIOCondition cond, gpointer _camview )
{
	camview_t *cam = _camview;
	uint8_t b;
	GdkPixbuf *pbuf;

	if( read(cam->fifo, &b, 1) != 1 ) {
		g_debug( "Horror, no bytes were available" );
		return FALSE;
	}

	/* There's a new frame available in the shared memory */
	pbuf = gdk_pixbuf_new_from_data( cam->img_data, GDK_COLORSPACE_RGB,
					 /* No alpha channel */
					 FALSE,
					 8, IMG_X, IMG_Y, IMG_X * 3,
					 NULL, NULL );
	gtk_image_set_from_pixbuf( cam->cam_img, pbuf );
	g_object_unref( pbuf );

	cam->img_data[FLAG_POS] = 0;
	return TRUE;
}

static gboolean fifo_err( GIOChannel *src, GIOCondition cond, gpointer _camview )
{
	g_debug( "ERR" );
	return FALSE;
}

/* Load the shared memory */
static void open_hueblobs( camview_t *cam )
{
	cam->shm_fd = shm_open( "/robovis_frame", O_RDWR, 0 );

	if( cam->shm_fd < 0 ) {
		/* The webcam might not be present etc. */
		cam->img_data = NULL;
		g_debug( "Robovis not found" );
		return;
	}

	/* mmap the shared memory, including the flag byte */
	cam->img_data = mmap( NULL,
			      IMG_SIZE + 1, PROT_READ | PROT_WRITE, MAP_SHARED,
			      cam->shm_fd, 0 );

	if( cam->img_data == NULL ) {
		/* mmap failed */
		g_debug( "Failed to mmap robovis's shared memory" );
		goto error0;
	}

	/* Open the fifo */
	cam->fifo = open( "/tmp/robovis_frame_fifo", O_RDONLY | O_NONBLOCK );
	if( cam->fifo == -1 ) {
		g_debug( "Opening robovis fifo failed" );
		goto error1;
	}

	/* Wait for stuff to happen on the fifo */
	cam->fifo_gio = g_io_channel_unix_new( cam->fifo );
	g_io_add_watch( cam->fifo_gio, G_IO_IN, fifo_data_ready, cam );
	g_io_add_watch( cam->fifo_gio, G_IO_ERR, fifo_err, cam );
	return;
error1:
	munmap( cam->img_data, IMG_SIZE + 1 );
error0:
	close( cam->shm_fd );
	cam->shm_fd = -1;
}

#define obj(t, n) do {							\
		cam-> n = t(gtk_builder_get_object( builder, #n ));	\
	} while (0)

void camview_init( camview_t *cam, GtkBuilder *builder )
{
	obj( GTK_IMAGE, cam_img );
	obj( GTK_WINDOW, cam_window );

	gtk_widget_show( GTK_WIDGET(cam->cam_window) );

	open_hueblobs(cam);
}

#undef obj
