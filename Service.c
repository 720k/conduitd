#include "Service.h"
#include "Conduit.h"

DWORD WINAPI    service_ctrl_handler(DWORD ctrl, DWORD type, LPVOID data, LPVOID ctx);
VOID WINAPI     service_main(DWORD argc, TCHAR *argv[]);

typedef struct _ServiceData {
    gchar drive_letter;
    GMutex mutex;
} ServiceData;

static ServiceData          serviceData;
static GMainLoop *          mainLoop;
static volatile gboolean    serviceHasToQuit;
static SERVICE_TABLE_ENTRY  serviceTable[] =  {
    { SERVICE_NAME, service_main },
    { NULL, NULL }
};
static SERVICE_STATUS       serviceStatus;
static SERVICE_STATUS_HANDLE serviceStatusHandle;

/* returns FALSE if the service should quit */
static gboolean serviceMainLoop (ServiceData *service_data) {
    g_debug ("Service main loop");
    if (serviceHasToQuit)     return FALSE;
    conduitStartListen();
    mainLoop = g_main_loop_new (NULL, TRUE);
    conduitOpenRemotePort();
    g_main_loop_run (mainLoop);
    g_clear_pointer (&mainLoop, g_main_loop_unref);
    conduitCleanUp();
    return !serviceHasToQuit;
}

DWORD WINAPI    service_ctrl_handler (DWORD ctrl, DWORD type, LPVOID data, LPVOID ctx)  {
    DWORD ret = NO_ERROR;
    ServiceData *service_data = ctx;
    switch (ctrl)    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        quitSignaled (SIGTERM);
        serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus (serviceStatusHandle, &serviceStatus);
        break;
    default:
        ret = ERROR_CALL_NOT_IMPLEMENTED;
    }
    return ret;
}

VOID WINAPI     service_main (DWORD argc, TCHAR *argv[])    {
    serviceStatusHandle = RegisterServiceCtrlHandlerEx (SERVICE_NAME, service_ctrl_handler, &serviceData);
    g_return_if_fail (serviceStatusHandle != 0);
    serviceStatus.dwServiceType = SERVICE_WIN32;
    serviceStatus.dwCurrentState = SERVICE_RUNNING;
    serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    serviceStatus.dwWin32ExitCode = NO_ERROR;
    serviceStatus.dwServiceSpecificExitCode = NO_ERROR;
    serviceStatus.dwCheckPoint = 0;
    serviceStatus.dwWaitHint = 0;
    SetServiceStatus (serviceStatusHandle, &serviceStatus);
    serviceMainLoopExec();
    serviceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus (serviceStatusHandle, &serviceStatus);
}

void quitSignaled (int sig)  {
    g_debug ("quit %d", sig);
    if (sig == SIGINT || sig == SIGTERM)  serviceHasToQuit = TRUE;
    if (mainLoop) g_main_loop_quit (mainLoop);
}


BOOL serviceInit(void) {
    if (!StartServiceCtrlDispatcher (serviceTable)) {
        g_error ("%s", g_win32_error_message (GetLastError ()));
        return FALSE;
    }
    return TRUE;
}
void serviceMainLoopExec(void) {
     while (serviceMainLoop(&serviceData))         g_usleep(G_USEC_PER_SEC);
}
