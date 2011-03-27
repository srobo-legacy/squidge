#include "camview.h"

#define obj(t, n) do {							\
		cam-> n = t(gtk_builder_get_object( builder, #n ));	\
	} while (0)

void camview_init( camview_t *cam, GtkBuilder *builder )
{
	obj( GTK_IMAGE, cam_img );
}

#undef obj
