/*
 * render.c — minimal markdown -> HTML body renderer.
 *
 * Scope (deliberately small): headings, paragraphs, code fences,
 * blockquotes, horizontal rules, single-level ul/ol lists, and inline
 * code / bold / italic / [links](url) / bare-URL autolinks.
 *
 * NOT supported (by design): tables, nested lists, reference-style
 * links, raw HTML passthrough, indented fences. Text outside these
 * constructs is preserved verbatim and HTML-escaped.
 */
#include <string.h>

#include "render.h"

static void esc(GString *o, const char *s, gsize n) {
  for (gsize i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
    case '&':
      g_string_append(o, "&amp;");
      break;
    case '<':
      g_string_append(o, "&lt;");
      break;
    case '>':
      g_string_append(o, "&gt;");
      break;
    case '"':
      g_string_append(o, "&quot;");
      break;
    default:
      g_string_append_c(o, (char)c);
    }
  }
}

static void inline_span(GString *o, const char *s, gsize len);

/* Autolink a bare URL starting at s. Returns TRUE and sets *end to the
   number of bytes consumed on success. */
static gboolean try_autolink(GString *o, const char *s, gsize len, gsize *end) {
  gsize skip = 0;
  if (len >= 7 && strncmp(s, "http://", 7) == 0)
    skip = 7;
  else if (len >= 8 && strncmp(s, "https://", 8) == 0)
    skip = 8;
  else if (len >= 4 && strncmp(s, "www.", 4) == 0)
    skip = 4;
  else
    return FALSE;

  gsize e = skip;
  while (e < len) {
    char c = s[e];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '<' ||
        c == '>' || c == '"' || c == '\'')
      break;
    e++;
  }
  /* strip trailing punctuation that is rarely part of the URL */
  while (e > skip) {
    char c = s[e - 1];
    if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' ||
        c == ')' || c == ']' || c == '}')
      e--;
    else
      break;
  }
  if (e <= skip)
    return FALSE;

  g_string_append(o, "<a href=\"");
  if (skip == 4)
    g_string_append(o, "http://");
  esc(o, s, e);
  g_string_append(o, "\">");
  esc(o, s, e);
  g_string_append(o, "</a>");
  *end = e;
  return TRUE;
}

static void inline_span(GString *o, const char *s, gsize len) {
  gsize i = 0;
  while (i < len) {
    char c = s[i];

    if (c == '`') {
      gsize j = i + 1;
      while (j < len && s[j] != '`')
        j++;
      if (j < len) {
        g_string_append(o, "<code>");
        esc(o, s + i + 1, j - i - 1);
        g_string_append(o, "</code>");
        i = j + 1;
        continue;
      }
    } else if (c == '*' && i + 1 < len && s[i + 1] == '*') {
      gsize j = i + 2;
      while (j + 1 < len && !(s[j] == '*' && s[j + 1] == '*'))
        j++;
      if (j + 1 < len) {
        g_string_append(o, "<strong>");
        inline_span(o, s + i + 2, j - i - 2);
        g_string_append(o, "</strong>");
        i = j + 2;
        continue;
      }
    } else if (c == '*') {
      gsize j = i + 1;
      while (j < len && s[j] != '*')
        j++;
      if (j < len && j > i + 1) {
        g_string_append(o, "<em>");
        inline_span(o, s + i + 1, j - i - 1);
        g_string_append(o, "</em>");
        i = j + 1;
        continue;
      }
    } else if (c == '[') {
      gsize j = i + 1;
      while (j < len && s[j] != ']')
        j++;
      if (j < len && j + 1 < len && s[j + 1] == '(') {
        gsize k = j + 2, ustart = j + 2;
        int depth = 1;
        while (k < len) {
          if (s[k] == '(')
            depth++;
          else if (s[k] == ')') {
            depth--;
            if (depth == 0)
              break;
          }
          k++;
        }
        if (k < len) {
          g_string_append(o, "<a href=\"");
          esc(o, s + ustart, k - ustart);
          g_string_append(o, "\">");
          inline_span(o, s + i + 1, j - i - 1);
          g_string_append(o, "</a>");
          i = k + 1;
          continue;
        }
      }
    } else if (c == 'h' || c == 'w') {
      gsize end = 0;
      if (try_autolink(o, s + i, len - i, &end)) {
        i += end;
        continue;
      }
    }

    char tmp[2] = {c, 0};
    esc(o, tmp, 1);
    i++;
  }
}

static gboolean is_blank(const char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\r')
    s++;
  return *s == '\0';
}

/* skip leading whitespace; write the length of the trimmed span to *len */
static const char *trim_ws(const char *s, gsize *len) {
  while (*s == ' ' || *s == '\t' || *s == '\r')
    s++;
  gsize n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
    n--;
  if (len)
    *len = n;
  return s;
}

static gboolean is_hr(const char *t, gsize n) {
  if (n < 3)
    return FALSE;
  char c = t[0];
  if (c != '-' && c != '*' && c != '_')
    return FALSE;
  for (gsize i = 0; i < n; i++)
    if (t[i] != c && t[i] != ' ' && t[i] != '\t')
      return FALSE;
  return TRUE;
}

gchar *mdlive_render(const gchar *markdown) {
  GString *body = g_string_new(NULL);
  GString *codebuf = g_string_new(NULL);
  gchar **lines = g_strsplit(markdown, "\n", -1);

  int in_code = 0;
  int para = 0; /* paragraph currently open */
  int list = 0; /* 1 = <ul>, 2 = <ol> */

  for (int li = 0; lines[li] != NULL; li++) {
    char *raw = lines[li];
    size_t rl = strlen(raw);
    if (rl > 0 && raw[rl - 1] == '\r')
      raw[rl - 1] = '\0';

    if (in_code) {
      if (strncmp(raw, "```", 3) == 0 || strncmp(raw, "~~~", 3) == 0) {
        g_string_append(body, "<pre><code>");
        esc(body, codebuf->str, codebuf->len);
        g_string_append(body, "</code></pre>\n");
        g_string_truncate(codebuf, 0);
        in_code = 0;
      } else {
        g_string_append(codebuf, raw);
        g_string_append_c(codebuf, '\n');
      }
      continue;
    }

    if (strncmp(raw, "```", 3) == 0 || strncmp(raw, "~~~", 3) == 0) {
      in_code = 1;
      continue;
    }

    if (is_blank(raw)) {
      if (para) {
        g_string_append(body, "</p>\n");
        para = 0;
      }
      if (list) {
        g_string_append(body, list == 1 ? "</ul>\n" : "</ol>\n");
        list = 0;
      }
      continue;
    }

    /* ATX heading */
    if (raw[0] == '#') {
      int lvl = 0;
      while (lvl < 6 && raw[lvl] == '#')
        lvl++;
      if (raw[lvl] == ' ' || raw[lvl] == '\0') {
        if (para) {
          g_string_append(body, "</p>\n");
          para = 0;
        }
        if (list) {
          g_string_append(body, list == 1 ? "</ul>\n" : "</ol>\n");
          list = 0;
        }
        gsize tn = 0;
        const char *t = trim_ws(raw + lvl, &tn);
        g_string_append_printf(body, "<h%d>", lvl);
        inline_span(body, t, tn);
        g_string_append_printf(body, "</h%d>\n", lvl);
        continue;
      }
    }

    /* horizontal rule */
    {
      gsize n = 0;
      const char *t = trim_ws(raw, &n);
      if (is_hr(t, n)) {
        if (para) {
          g_string_append(body, "</p>\n");
          para = 0;
        }
        if (list) {
          g_string_append(body, list == 1 ? "</ul>\n" : "</ol>\n");
          list = 0;
        }
        g_string_append(body, "<hr>\n");
        continue;
      }
    }

    /* blockquote */
    if (raw[0] == '>') {
      if (para) {
        g_string_append(body, "</p>\n");
        para = 0;
      }
      if (list) {
        g_string_append(body, list == 1 ? "</ul>\n" : "</ol>\n");
        list = 0;
      }
      gsize tn = 0;
      const char *t = trim_ws(raw + 1, &tn);
      g_string_append(body, "<blockquote>");
      inline_span(body, t, tn);
      g_string_append(body, "</blockquote>\n");
      continue;
    }

    /* lists */
    {
      gsize tn = 0;
      const char *t = trim_ws(raw, &tn);
      int is_ul = 0, is_ol = 0;
      if (tn >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') &&
          (t[1] == ' ' || t[1] == '\t'))
        is_ul = 1;
      else {
        gsize i = 0;
        while (i < tn && t[i] >= '0' && t[i] <= '9')
          i++;
        if (i > 0 && i < tn && t[i] == '.' &&
            (i + 1 >= tn || t[i + 1] == ' ' || t[i + 1] == '\t'))
          is_ol = 1;
      }

      if (is_ul || is_ol) {
        int type = is_ol ? 2 : 1;
        if (list != type) {
          if (list)
            g_string_append(body, list == 1 ? "</ul>\n" : "</ol>\n");
          g_string_append(body, type == 1 ? "<ul>\n" : "<ol>\n");
          list = type;
        }
        if (para) {
          g_string_append(body, "</p>\n");
          para = 0;
        }

        gsize off = is_ul ? 2 : (gsize)(strchr(t, '.') - t) + 1;
        gsize cn = 0;
        const char *c = trim_ws(t + off, &cn);
        g_string_append(body, "<li>");
        inline_span(body, c, cn);
        g_string_append(body, "</li>\n");
        continue;
      }
    }

    /* paragraph continuation */
    if (list) {
      g_string_append(body, list == 1 ? "</ul>\n" : "</ol>\n");
      list = 0;
    }
    if (!para) {
      g_string_append(body, "<p>");
      para = 1;
    } else {
      g_string_append_c(body, ' ');
    }
    {
      gsize n = 0;
      const char *t = trim_ws(raw, &n);
      inline_span(body, t, n);
    }
  }

  if (in_code) {
    g_string_append(body, "<pre><code>");
    esc(body, codebuf->str, codebuf->len);
    g_string_append(body, "</code></pre>\n");
  }
  if (para)
    g_string_append(body, "</p>\n");
  if (list)
    g_string_append(body, list == 1 ? "</ul>\n" : "</ol>\n");

  g_strfreev(lines);
  g_string_free(codebuf, TRUE);
  return g_string_free(body, FALSE);
}
