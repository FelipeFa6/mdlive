#ifndef MDLIVE_RENDER_H
#define MDLIVE_RENDER_H

#include <glib.h>

/* Render markdown text into an HTML body fragment.
   Caller owns the result (free with g_free). */
gchar *mdlive_render(const gchar *markdown);

#endif
