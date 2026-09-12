/* XDG FileChooser adapter. No game, renderer, or ROM-format logic lives here. */
#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static const char *portal_name = "org.freedesktop.portal.Desktop";
static const char *request_interface = "org.freedesktop.portal.Request";

typedef struct {
    GMainLoop *loop;
    GDBusConnection *bus;
    gchar *request;
    gchar *uri;
    int exit_code;
    gboolean done;
} Selection;

static void receive_response(GDBusConnection *bus, const gchar *sender,
                             const gchar *path, const gchar *interface,
                             const gchar *signal, GVariant *parameters, gpointer user_data) {
    (void)bus; (void)sender; (void)interface; (void)signal;
    Selection *selection = user_data;
    if (g_strcmp0(path, selection->request) != 0 || selection->done)
        return;
    selection->done = TRUE;
    if (g_variant_is_of_type(parameters, G_VARIANT_TYPE("(ua{sv})"))) {
        guint32 response;
        GVariant *results;
        g_variant_get(parameters, "(u@a{sv})", &response, &results);
        if (response == 1) {
            selection->exit_code = 2;
        } else if (response == 0) {
            GVariant *uris = g_variant_lookup_value(results, "uris", G_VARIANT_TYPE("as"));
            if (uris && g_variant_n_children(uris) == 1) {
                g_variant_get_child(uris, 0, "s", &selection->uri);
                selection->exit_code = 0;
            }
            if (uris) g_variant_unref(uris);
        }
        g_variant_unref(results);
    }
    g_main_loop_quit(selection->loop);
}

static gboolean cancel_request(gpointer user_data) {
    Selection *selection = user_data;
    selection->done = TRUE;
    selection->exit_code = 3;
    GVariant *reply = g_dbus_connection_call_sync(selection->bus, portal_name,
        selection->request, request_interface, "Close", NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
    if (reply) g_variant_unref(reply);
    g_main_loop_quit(selection->loop);
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    gboolean directory = FALSE;
    gchar *title = NULL;
    gchar *parent = NULL;
    gint timeout_seconds = 300;
    GOptionEntry entries[] = {
        { "directory", 0, 0, G_OPTION_ARG_NONE, &directory, "Select a directory", NULL },
        { "title", 0, 0, G_OPTION_ARG_STRING, &title, "Dialog title", "TEXT" },
        { "parent", 0, 0, G_OPTION_ARG_STRING, &parent, "Portal window identifier", "ID" },
        { "timeout-seconds", 0, 0, G_OPTION_ARG_INT, &timeout_seconds, "Request timeout", "SECONDS" },
        { NULL, 0, 0, 0, NULL, NULL, NULL }
    };
    GError *error = NULL;
    GOptionContext *options = g_option_context_new("- open the desktop file portal");
    g_option_context_add_main_entries(options, entries, NULL);
    gboolean parsed = g_option_context_parse(options, &argc, &argv, &error);
    g_option_context_free(options);
    if (!parsed || argc != 1 || timeout_seconds < 1 || timeout_seconds > 3600) {
        g_printerr("Invalid file chooser arguments: %s\n", error ? error->message : "unexpected arguments");
        g_clear_error(&error);
        g_free(title); g_free(parent);
        return 1;
    }
    Selection selection = { .exit_code = 1 };
    selection.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!selection.bus) {
        g_printerr("Cannot connect to the desktop portal: %s\n", error->message);
        g_clear_error(&error);
        g_free(title); g_free(parent);
        return 1;
    }
    selection.loop = g_main_loop_new(NULL, FALSE);
    gchar *uuid = g_uuid_string_random();
    g_strdelimit(uuid, "-", '_');
    gchar *token = g_strconcat("triaevum_", uuid, NULL);
    gchar *sender = g_strdup(g_dbus_connection_get_unique_name(selection.bus) + 1);
    g_strdelimit(sender, ".", '_');
    selection.request = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
    g_free(uuid); g_free(sender);

    /* Subscribe before OpenFile: fast cancellation/selection can precede its reply. */
    guint subscription = g_dbus_connection_signal_subscribe(selection.bus, portal_name,
        request_interface, "Response", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        receive_response, &selection, NULL);
    GVariantBuilder settings;
    g_variant_builder_init(&settings, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&settings, "{sv}", "handle_token", g_variant_new_string(token));
    g_variant_builder_add(&settings, "{sv}", "multiple", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&settings, "{sv}", "directory", g_variant_new_boolean(directory));
    if (!directory) {
        GVariantBuilder patterns, filters;
        g_variant_builder_init(&patterns, G_VARIANT_TYPE("a(us)"));
        g_variant_builder_add(&patterns, "(us)", 0u, "*.[3][dD][sS]");
        g_variant_builder_add(&patterns, "(us)", 0u, "*.[cC][cC][iI]");
        g_variant_builder_init(&filters, G_VARIANT_TYPE("a(sa(us))"));
        g_variant_builder_add(&filters, "(s@a(us))", "Decrypted Nintendo 3DS ROM",
                              g_variant_builder_end(&patterns));
        g_variant_builder_add(&settings, "{sv}", "filters", g_variant_builder_end(&filters));
    }
    GVariant *reply = g_dbus_connection_call_sync(selection.bus, portal_name,
        "/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser", "OpenFile",
        g_variant_new("(ss@a{sv})", parent ? parent : "", title ? title : "Select a decrypted ROM",
                      g_variant_builder_end(&settings)),
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &error);
    if (reply) {
        g_free(selection.request);
        g_variant_get(reply, "(o)", &selection.request);
        g_variant_unref(reply);
        guint timeout = g_timeout_add_seconds(timeout_seconds, cancel_request, &selection);
        guint terminate = g_unix_signal_add(SIGTERM, cancel_request, &selection);
        if (!selection.done) g_main_loop_run(selection.loop);
        g_source_remove(timeout);
        g_source_remove(terminate);
    } else {
        g_printerr("File portal request failed: %s\n", error->message);
        g_clear_error(&error);
    }
    if (selection.exit_code == 0) {
        gchar *hostname = NULL;
        gchar *filename = g_filename_from_uri(selection.uri, &hostname, &error);
        if (!filename || !g_path_is_absolute(filename) || (hostname && *hostname &&
            g_strcmp0(hostname, "localhost") != 0) || strpbrk(selection.uri, "\r\n")) {
            g_printerr("File portal returned an invalid local file URI\n");
            selection.exit_code = 1;
        } else {
            g_print("%s\n", selection.uri);
        }
        g_clear_error(&error);
        g_free(hostname); g_free(filename);
    }
    g_dbus_connection_signal_unsubscribe(selection.bus, subscription);
    g_main_loop_unref(selection.loop);
    g_object_unref(selection.bus);
    g_free(token); g_free(selection.request); g_free(selection.uri);
    g_free(title); g_free(parent);
    return selection.exit_code;
}
