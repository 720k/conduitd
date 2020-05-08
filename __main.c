#include "Service.h"
#include "Conduit.h"

#include <stdlib.h>
#include <Windows.h>

static guint16      localServerPortArg;
static gboolean     noServiceArg;

static GOptionEntry commandLineArguments[] = {
    { "port",       'p', 0,     G_OPTION_ARG_INT, &localServerPortArg,        "Port to listen on", NULL },
    { "no-service", 0,  0,      G_OPTION_ARG_NONE, &noServiceArg, "Don't start as a service", NULL },
    { NULL }
};

int main (int argc, char *argv[])   {
    g_print("Conduit service starting...\n");
    GOptionContext *opts;
    GError *error = NULL;
    opts = g_option_context_new (NULL);
    g_option_context_add_main_entries (opts, commandLineArguments, NULL);
    if (!g_option_context_parse (opts, &argc, &argv, &error))     {
        g_printerr ("Could not parse arguments: %s\n", error->message);
        g_printerr ("%s", g_option_context_get_help (opts, TRUE, NULL));
        exit (1);
    }
    if (localServerPortArg == 0)    {
        g_printerr ("please specify a valid port\n");
        exit (1);
    }
    g_option_context_free (opts);

    signal (SIGINT, quitSignaled);
    /* run socket service once at beginning, there seems to be a bug on
     windows, and it can't accept new connections if cleanup and
     restart a new service */
    if (!localSocketServiceInit(localServerPortArg)) {
        g_printerr("LocalSocketService init failed!");
        exit(10);
    }

    if (!noServiceArg && !getenv("DEBUG"))  {
        if (!serviceInit())     {
            g_printerr("ServiceInit failed!");
            exit(20);
        }
    } else
        serviceMainLoopExec();
    localSocketServiceClose();
    return 0;
}
