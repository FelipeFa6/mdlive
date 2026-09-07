/*
 * mdlive.c — a minimal X11 panel: live markdown preview in a bare window.
 *
 * Watches a markdown file, renders it to HTML with md4c-html, and shows
 * it in a WebKitGTK view. External URLs are opened with the system
 * handler (xdg-open) instead of navigating the panel.
 *
 * Build: make
 * Run:   ./mdlive path/to/file.md
 *
 * Deps: gtk+-3.0, webkit2gtk-4.1, md4c-html (see Makefile).
 */
#include <gtk/gtk.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <webkit2/webkit2.h>

#include <md4c-html.h>

typedef struct {
        char *path;
        char *base_uri;
        WebKitWebView *view;
        time_t mtime;
} App;

/* --- markdown -> HTML fragment, via md4c-html --------------------------- */

/*
 * Parser flags: MD_DIALECT_GITHUB turns on the GFM extensions, including
 * MD_FLAG_TABLES (pipe tables), MD_FLAG_STRIKETHROUGH, MD_FLAG_TASKLISTS,
 * MD_FLAG_FOOTNOTES and MD_FLAG_ADMONITIONS. Without MD_FLAG_TABLES, md4c
 * treats "| a | b |" rows as an ordinary paragraph and never emits a
 * <table> at all — that's the cause of tables not rendering. Plain
 * MD_DIALECT_COMMONMARK (or a hand-rolled flag set that forgets
 * MD_FLAG_TABLES) will silently reproduce that bug, so keep this as
 * MD_DIALECT_GITHUB (or OR in MD_FLAG_TABLES explicitly) if you ever
 * change it.
 */
#define MDLIVE_PARSER_FLAGS (MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEATXHEADERS)

#define MDLIVE_RENDERER_FLAGS (MD_HTML_FLAG_SKIP_UTF8_BOM)

static void md_append_chunk(const MD_CHAR *chunk, MD_SIZE size,
                void *userdata) {
        GString *out = userdata;
        g_string_append_len(out, chunk, size);
}

/* Renders `markdown` to an HTML fragment (no <html>/<body> wrapper).
 * Returns a newly allocated string owned by the caller (g_free()). */
static gchar *render_markdown(const char *markdown) {
        if (markdown == NULL) {
                markdown = "";
        }

        GString *out = g_string_new(NULL);

        int rc = md_html(markdown, (MD_SIZE)strlen(markdown), md_append_chunk, out,
                        MDLIVE_PARSER_FLAGS, MDLIVE_RENDERER_FLAGS);

        if (rc != 0) {
                /* md_html() only fails on internal/allocation errors — md4c has no
                 * notion of "invalid" markdown, everything parses as something. */
                g_string_free(out, TRUE);
                return g_strdup(
                                "<p><em>(markdown render failed — check mdlive's stderr)</em></p>");
        }

        return g_string_free(out, FALSE);
}

/* --- HTML page shell ------------------------------------------------------ */

static char *html_page(const char *body) {
        static const char *css =
                "body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;"
                "max-width:46em;margin:2em auto;padding:0 1.5em;line-height:1.6;"
                "color:#1a1a1a;}"
                "pre{background:#f5f5f5;padding:1em;overflow-x:auto;border-radius:6px;}"
                "code{background:#f0f0f0;padding:0.15em 0.4em;border-radius:4px;"
                "font-family:'SF Mono',Menlo,Consolas,monospace;font-size:0.9em;}"
                "pre code{background:none;padding:0;}"
                "a{color:#0969da;text-decoration:none;}a:hover{text-decoration:underline;"
                "}"
                "h1,h2,h3{border-bottom:1px solid #eee;padding-bottom:0.3em;}"
                "blockquote{border-left:4px solid #ddd;margin:0;padding:0 "
                "1em;color:#555;}"
                "li{margin:0.2em 0;}"
                /* Tables (GFM pipe tables come out as plain <table><thead><tbody>
                 * with no classes or inline styles, so they need their own rules
                 * or they render as an unstyled, borderless block). */
                "table{border-collapse:collapse;width:100%;margin:1em 0;}"
                "th,td{border:1px solid #ddd;padding:0.4em 0.8em;text-align:left;}"
                "thead th{background:#f5f5f5;}"
                "tr:nth-child(even){background:#fafafa;}";

        GString *o = g_string_new(
                        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>");
        g_string_append(o, css);
        g_string_append(o, "</style></head><body>");
        g_string_append(o, body);
        g_string_append(o, "</body></html>");
        return g_string_free(o, FALSE);
}

static void load_file(App *a) {
        gchar *content = NULL;
        gsize len = 0;
        GError *err = NULL;
        if (g_file_get_contents(a->path, &content, &len, &err)) {
                gchar *body = render_markdown(content);
                gchar *html = html_page(body);
                webkit_web_view_load_html(a->view, html, a->base_uri);
                g_free(html);
                g_free(body);
                g_free(content);
        } else {
                g_clear_error(&err);
                webkit_web_view_load_html(
                                a->view, "<p><em>(file not found — waiting for it to appear)</em></p>",
                                a->base_uri);
        }
}

static gboolean poll_cb(gpointer ud) {
        App *a = ud;
        struct stat st;
        if (stat(a->path, &st) == 0) {
                if (st.st_mtime != a->mtime) {
                        a->mtime = st.st_mtime;
                        load_file(a);
                }
        } else if (a->mtime != 0) {
                a->mtime = 0;
                load_file(a);
        }
        return G_SOURCE_CONTINUE;
}

static gboolean on_decide_policy(WebKitWebView *view,
                WebKitPolicyDecision *decision,
                WebKitPolicyDecisionType type, gpointer ud) {
        (void)view;
        (void)ud;
        if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
                WebKitNavigationPolicyDecision *nd =
                        WEBKIT_NAVIGATION_POLICY_DECISION(decision);
                WebKitNavigationAction *na =
                        webkit_navigation_policy_decision_get_navigation_action(nd);
                const char *uri =
                        webkit_uri_request_get_uri(webkit_navigation_action_get_request(na));

                if (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://")) {
                        GError *err = NULL;
                        if (!g_app_info_launch_default_for_uri(uri, NULL, &err)) {
                                g_printerr("open failed: %s\n", err ? err->message : "?");
                                g_clear_error(&err);
                        }
                        webkit_policy_decision_ignore(decision);
                        return TRUE;
                }
                /* let everything else (initial load, file://, relative links) through */
                return FALSE;
        }
        if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
                webkit_policy_decision_ignore(decision);
                return TRUE;
        }
        return FALSE;
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer ud) {
        (void)w;
        (void)ud;
        if (e->keyval == GDK_KEY_Escape || e->keyval == GDK_KEY_q) {
                gtk_main_quit();
                return TRUE;
        }
        return FALSE;
}

int main(int argc, char **argv) {
        if (argc < 2) {
                g_printerr("usage: %s FILE.md\n", argv[0]);
                return 2;
        }

        gtk_init(&argc, &argv);

        char *path = g_canonicalize_filename(argv[1], NULL);
        char *dir = g_path_get_dirname(path);

        GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(win), g_path_get_basename(path));
        gtk_window_set_default_size(GTK_WINDOW(win), 900, 700);
        g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
        g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

        WebKitWebView *view = WEBKIT_WEB_VIEW(webkit_web_view_new());
        g_signal_connect(view, "decide-policy", G_CALLBACK(on_decide_policy), NULL);
        gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(view));

        App app = {
                .path = path,
                .base_uri = g_strdup_printf("file://%s/", dir),
                .view = view,
                .mtime = 0,
        };

        gtk_widget_show_all(win);
        g_timeout_add(400, poll_cb, &app);
        gtk_main();

        g_free(app.base_uri);
        g_free(dir);
        g_free(path);
        return 0;
}
