#include "Conduit.h"
#include "Service.h"
#include "output-queue.h"

#include <stdlib.h>
#include <gio/gio.h>
#include <gio/gwin32inputstream.h>
#include <gio/gwin32outputstream.h>


#define MAX_BUFFER_SIZE 64*1024

typedef struct _DemuxData    {
    gint64  client;
    gint64  bufferSize;
    gchar   buffer[MAX_BUFFER_SIZE];
} Demux;

typedef struct _Client {
    gint64              id;
    GSocketConnection * clientConnection;
    OutputQueue       * outputQueue;
    gint64              bufferSize;
    guint8              buffer[MAX_BUFFER_SIZE];
} Client;

/* DECLARE */
static void startConduitRead (GInputStream *istream);
static void clientReadData (Client *client);

/* DEFINE */
static LPCWSTR              conduitPortName = L"\\\\.\\global\\io.bplayer.data.0";
static HANDLE               conduitHandle;
static GInputStream *       conduitInputStream;
static GOutputStream *      conduitOutputStream;
static OutputQueue *        conduitOutputQueue;
static Demux                conduitInputBuffer;
/*static GHashTable *         clients;*/
static GSocketService *     localSocketService;
static guint16              localSocketPortNumber;
static GCancellable *       cancel;
static Client*              theClient; /*singleton*/


static Client*  createClientFromConnection (GSocketConnection *clientConnection)   {
    GIOStream *iostream = G_IO_STREAM (clientConnection);
    GOutputStream *outputStream = g_io_stream_get_output_stream (iostream);
    GOutputStream *bufferedOutputStream = g_buffered_output_stream_new (outputStream);
    Client *client;
    g_buffered_output_stream_set_auto_grow (G_BUFFERED_OUTPUT_STREAM (bufferedOutputStream), TRUE);
    client = g_new0 (Client, 1);
    client->clientConnection = g_object_ref (clientConnection);
    // TODO: check if usage of this idiom is portable, or if we need to check collisions
    client->id = GPOINTER_TO_INT (clientConnection);
    client->outputQueue = output_queue_new (bufferedOutputStream, cancel);
    g_object_unref (bufferedOutputStream);
    /* g_hash_table_insert (clients, &client->id, client);
    g_warn_if_fail (g_hash_table_lookup (clients, &client->id)); */
    theClient = client; /*One Conduit <-> One Client*/
    return client;
}

static void deleteClient (Client *c) {
    g_debug ("[CLIENT] %p Free resources", c);
    g_io_stream_close (G_IO_STREAM (c->clientConnection), NULL, NULL);
    g_object_unref (c->clientConnection);
    g_object_unref (c->outputQueue);
    g_free (c);
}

static void removeClient (Client *client)  {
    g_debug ("[CLIENT] %p Removed!", client);
    /*g_hash_table_remove (clients, &client->id);*/
    theClient = 0;
    deleteClient(client); /**/
}

typedef struct ReadDataBuffer {
    void  *buffer;
    gsize  size;
} ReadDataBuffer;

static void readThread (GTask *task, gpointer sender, gpointer task_data, GCancellable *cancellable)    {
    GError *error = NULL;
    GInputStream *inputStream = G_INPUT_STREAM (sender);
    gsize bytesRead;
    ReadDataBuffer *readDataBuffer = g_task_get_task_data (task);
    g_debug ("[CONDUIT] read-thread reading... buffer size %d (blocking)", readDataBuffer->size);
     bytesRead = g_input_stream_read (inputStream, readDataBuffer->buffer, readDataBuffer->size, cancellable, &error);
    /*g_input_stream_read_all (inputStream, readDataBuffer->buffer, readDataBuffer->size, &bytesRead, cancellable, &error);*/
    g_debug ("[CONDUIT] read-thread Finished:  %d bytes read", bytesRead);
    if (error)    {
        g_debug ("error: %s", error->message);
        g_task_return_error (task, error);
    } else
        g_task_return_int (task, (int)bytesRead);
}

static void inputStreamReadThreadAsync (GInputStream *inputStream, void *buffer, gsize bufferSize, int io_priority,GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
    g_debug("[TASK] new - ReadThread");
    ReadDataBuffer *readDataBuffer = g_new (ReadDataBuffer, 1);
    readDataBuffer->buffer = buffer;
    readDataBuffer->size = bufferSize;
    GTask *task = g_task_new (inputStream, cancellable, callback, user_data);
    g_task_set_task_data (task, readDataBuffer, g_free);
    g_task_run_in_thread (task, readThread);
    g_object_unref (task);
}

static gssize inputStreamReadThreadFinish (GInputStream *inputStream, GAsyncResult *result, GError **error) {
    g_return_val_if_fail (g_task_is_valid (result, inputStream), -1);
    return g_task_propagate_int (G_TASK (result), error);
}

static void     handlePushError (OutputQueue *q, gpointer user_data, GError *error) {
    if (error) {
        g_warning ("push error: %s", error->message);
        removeClient ((Client*)user_data);
    }
}

static void     clientPushElementFinished_cb (OutputQueue *q, gpointer user_data, GError *error) {
    if (error)  handlePushError (q, user_data, error);
    else        startConduitRead (conduitInputStream);
}

static void     conduitReadFinished_cb (GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    gssize size = inputStreamReadThreadFinish (G_INPUT_STREAM (source_object), res, &error);
    if (error)    {
        g_warning ("[CONDUIT] Read Finished error: %s", error->message);
        g_clear_error (&error);
    }
    /*Client *client = g_hash_table_lookup (clients, &demux.client);*/
    Client *client = theClient;
    g_debug ("[CLIENT] %p found, push data %d", client, size);
    /*g_warn_if_fail(client != NULL);*/
    if (client)     output_queue_push (client->outputQueue, (guint8 *) conduitInputBuffer.buffer, size, clientPushElementFinished_cb, client);
    else            startConduitRead (conduitInputStream);
}


static void startConduitRead (GInputStream *inputStream)  {
    inputStreamReadThreadAsync (inputStream, &conduitInputBuffer.buffer, MAX_BUFFER_SIZE, G_PRIORITY_DEFAULT, cancel, conduitReadFinished_cb, NULL);
}

/*CLIENT SIDE*/

static void conduitPushElementFinished_cb (OutputQueue *q, gpointer user_data, GError *error)   {
    Client *client = user_data;
    if (error)  handlePushError (q, client, error);
    else if (client->bufferSize == 0)   removeClient (client);
         else                           clientReadData (client);
}

static void clientReadDataFinished_cb (GObject *sender, GAsyncResult *res, gpointer user_data)    {
    Client *client = user_data;
    GError *error = NULL;
    gssize bytesRead = g_input_stream_read_finish (G_INPUT_STREAM (sender), res, &error);
    g_debug ("[CLIENT] %p read finished, %d bytes",client, bytesRead);
    if (error)    {
        g_warning ("[CLIENT] %p error: %s", client, error->message);
        g_clear_error (&error);
        removeClient (client);
        return;
    }
    g_return_if_fail (bytesRead <= MAX_BUFFER_SIZE);
    g_return_if_fail (bytesRead >= 0);
    client->bufferSize = bytesRead;
    output_queue_push (conduitOutputQueue, (guint8 *) client->buffer, bytesRead, conduitPushElementFinished_cb, client); /*JUST PUSH BUFFER*/
    return;
}

static void clientReadData (Client *client)  {
    g_debug ("[CLIENT] %p Start reading...", client);
    GIOStream *iostream = G_IO_STREAM (client->clientConnection);
    GInputStream *inputStream = g_io_stream_get_input_stream (iostream);
    g_input_stream_read_async (inputStream, client->buffer, MAX_BUFFER_SIZE, G_PRIORITY_DEFAULT, NULL, clientReadDataFinished_cb, client);
}

static gboolean newClientConnection_cb (GSocketService *service, GSocketConnection *client_connection, GObject *source_object, gpointer user_data) {
    g_debug ("[CONNECTION] new");
    if (theClient) return TRUE; /*DROP CONNECTION, JUST ONE CLIENT */
    Client *client  = createClientFromConnection (client_connection);
    g_debug ("[CLIENT] %p NEW", client);
    clientReadData (client);
    return FALSE;
}

static void openConduit (LPCWSTR path) {
    g_return_if_fail (path);
    g_return_if_fail (!conduitInputStream);
    g_return_if_fail (!conduitOutputStream);
    g_return_if_fail (!conduitOutputQueue);
    g_debug ("[CONDUIT] opening serial port %ls", path);
    conduitHandle = CreateFile (path, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (conduitHandle == INVALID_HANDLE_VALUE)  g_error ("%s", g_win32_error_message (GetLastError ()));
    conduitOutputStream = G_OUTPUT_STREAM (g_win32_output_stream_new (conduitHandle, TRUE));
    conduitInputStream = G_INPUT_STREAM (g_win32_input_stream_new (conduitHandle, TRUE));
    conduitOutputQueue = output_queue_new (G_OUTPUT_STREAM (conduitOutputStream), cancel);
}

BOOL localSocketServiceInit(guint16 port)  {
    GError *error = NULL;
    localSocketPortNumber = port;
    localSocketService = g_socket_service_new ();
    GInetAddress *iaddr = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
    GSocketAddress *saddr = g_inet_socket_address_new (iaddr, localSocketPortNumber);
    g_object_unref (iaddr);
    g_socket_listener_add_address (G_SOCKET_LISTENER (localSocketService), saddr, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL, NULL, &error);
    g_object_unref (saddr);
    if (error) {
        g_printerr ("[LOCAL SERVICE] error: %s\n", error->message);
        return FALSE;
    }
    g_signal_connect (localSocketService, "incoming", G_CALLBACK (newClientConnection_cb), NULL);
    return TRUE;
}

void localSocketServiceClose(void) {
    g_clear_object (&localSocketService);
}

void conduitStartListen(void) {
    g_socket_service_start (localSocketService);
    cancel = g_cancellable_new ();
    /*clients = g_hash_table_new_full (g_int64_hash, g_int64_equal, NULL, (GDestroyNotify) deleteClient);*/
    theClient=0;
}

void conduitOpenRemotePort(void) {
    /* listen on port for incoming clients, multiplex there input into
     virtio path, demultiplex input from there to the respective
     clients */
    openConduit (conduitPortName);
    startConduitRead (conduitInputStream);
}

void conduitCleanUp(void) {
    g_cancellable_cancel (cancel);
    g_clear_object (&conduitInputStream);
    g_clear_object (&conduitOutputStream);
    g_clear_object (&conduitOutputQueue);
    /*g_hash_table_unref (clients);*/
    g_socket_service_stop (localSocketService);
    g_clear_object (&cancel);
    CloseHandle (conduitHandle);
}
