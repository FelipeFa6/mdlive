/* test_render.c — render a file to HTML body on stdout, for checking
   the renderer without a display. */
#include <stdio.h>

#include <glib.h>

#include "render.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s FILE.md\n", argv[0]);
    return 2;
  }
  gchar *content = NULL;
  gsize len = 0;
  GError *err = NULL;
  if (!g_file_get_contents(argv[1], &content, &len, &err)) {
    fprintf(stderr, "error: %s\n", err->message);
    return 1;
  }
  gchar *html = mdlive_render(content);
  fputs(html, stdout);
  g_free(html);
  g_free(content);
  return 0;
}
