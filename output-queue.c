/*
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
/*#include <config.h>*/

#include "output-queue.h"

static gboolean output_queue_idle (gpointer user_data);

typedef struct _OutputQueueElem {
  OutputQueue  *containerQueue;
  const guint8 *buffer;
  gsize         bufferSize;
  PushedCb      pushFinished_cb;
  gpointer      user_data;
} OutputQueueElem;

struct _OutputQueue {
  GObject        parent_instance;
  GOutputStream *outputStream;
  guint          idle_id;
  GQueue        *queue;
  GCancellable  *cancel;
  gboolean       isFlushing;
};

G_DEFINE_TYPE (OutputQueue, output_queue, G_TYPE_OBJECT);

static void output_queue_init (OutputQueue *self)   {
  self->queue = g_queue_new ();
}

static void output_queue_finalize (GObject *obj)    {
  OutputQueue *self = OUTPUT_QUEUE (obj);
  g_warn_if_fail (g_queue_get_length (self->queue) == 0);
  g_warn_if_fail (!self->isFlushing);
  g_warn_if_fail (!self->idle_id);
  g_queue_free_full (self->queue, g_free);
  g_object_unref (self->outputStream);
  g_object_unref (self->cancel);
  G_OBJECT_CLASS (output_queue_parent_class)->finalize (obj);
}

static void output_queue_class_init (OutputQueueClass *klass)   {
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = output_queue_finalize;
}

OutputQueue* output_queue_new (GOutputStream *output, GCancellable *cancel) {
  OutputQueue *self = g_object_new (OUTPUT_TYPE_QUEUE, NULL);
  self->outputStream = g_object_ref (output);
  self->cancel = g_object_ref (cancel);
  return self;
}


static void output_queue_flush_cb (GObject *source_object, GAsyncResult *res, gpointer user_data) {
  GError *error = NULL;
  OutputQueueElem *element = user_data;
  OutputQueue *outQueue = element->containerQueue;
  g_debug ("flushed");
  outQueue->isFlushing = FALSE;
  g_output_stream_flush_finish (G_OUTPUT_STREAM (source_object), res, &error);
  if (error)    g_warning ("error: %s", error->message);
  g_clear_error (&error);
  if (!outQueue->idle_id)  outQueue->idle_id = g_idle_add (output_queue_idle, g_object_ref (outQueue));
  g_free (element);
  g_object_unref (outQueue);
}

static gboolean output_queue_idle (gpointer user_data)  {
  OutputQueue *outputQueue = user_data;
  OutputQueueElem *element = NULL;
  GError *error = NULL;
  if (outputQueue->isFlushing)    {
      g_debug ("already flushing");
      goto end;
  }
  element = g_queue_pop_head (outputQueue->queue);
  if (!element)    {
      g_debug ("No more data to flush");
      goto end;
  }
  g_debug ("flushing %" G_GSIZE_FORMAT, element->bufferSize);
  g_output_stream_write_all (outputQueue->outputStream, element->buffer, element->bufferSize, NULL, outputQueue->cancel, &error);
  if (element->pushFinished_cb)    element->pushFinished_cb (outputQueue, element->user_data, error);
  if (error)    goto end;

  outputQueue->isFlushing = TRUE;
  g_output_stream_flush_async (outputQueue->outputStream, G_PRIORITY_DEFAULT, outputQueue->cancel, output_queue_flush_cb, element);

  outputQueue->idle_id = 0;
  return FALSE;

end:
  g_clear_error (&error);
  outputQueue->idle_id = 0;
  g_free (element);
  g_object_unref (outputQueue);

  return FALSE;
}

void    output_queue_push (OutputQueue *outputQueue, const guint8 *buf, gsize size,   PushedCb pushed_cb, gpointer user_data) {
  OutputQueueElem *element;
  g_return_if_fail (outputQueue != NULL);
  element = g_new (OutputQueueElem, 1);
  element->buffer = buf;
  element->bufferSize = size;
  element->pushFinished_cb = pushed_cb;
  element->user_data = user_data;
  element->containerQueue = outputQueue;
  g_queue_push_tail (outputQueue->queue, element);
  if (!outputQueue->idle_id && !outputQueue->isFlushing)     outputQueue->idle_id = g_idle_add (output_queue_idle, g_object_ref (outputQueue));
}
