/*
 * This file defines the "OmxBufferTransport" buffer object, which is used
 * to encapsulate an existing OMX buffer object inside of a GStreamer
 * buffer so it can be passed along the GStreamer pipeline.
 * 
 * Specifically, this object provides a finalize function that will
 * call a FTB when gst_buffer_unref() is called.  If the specified
 *
 * Downstream elements may use the GST_IS_OMXBUFFERTRANSPORT() macro to
 * check to see if a GStreamer buffer encapsulates a OMX buffer.  When passed
 * an element of this type, elements can take advantage of the fact that the
 * buffer is contiguously allocated in memory.  Also, if the element is using
 * OMX it can access the OMX buffer directly via the GST_GET_OMXBUFFER() and similar 
 * access the upstream OMX port via the GST_GET_OMXPORT.
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Author: Brijesh Singh <bksingh@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation version 2.1 of the License.
 *
 * This program is distributed #as is# WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 */

#include <stdlib.h>
#include <pthread.h>

#include "gstomx_buffertransport.h"
#include "gstomx.h"
#include "gstomx_port.h"

GST_DEBUG_CATEGORY_STATIC (gst_omxbuffertransport_debug);

static GstBufferClass *parent_class;

static void gst_omxbuffertransport_init(GstOmxBufferTransport *self);
static void gst_omxbuffertransport_log_init(void);
static void gst_omxbuffertransport_class_init(GstOmxBufferTransportClass *klass);
static void gst_omxbuffertransport_finalize(GstBuffer *gstbuffer);

G_DEFINE_TYPE_WITH_CODE (GstOmxBufferTransport, gst_omxbuffertransport, \
    GST_TYPE_BUFFER, gst_omxbuffertransport_log_init());

static void gst_omxbuffertransport_log_init(void)
{
    GST_DEBUG_CATEGORY_INIT(gst_omxbuffertransport_debug,
        "OmxBufferTransport", 0, "OMX Buffer Transport");
}

static void gst_omxbuffertransport_init(GstOmxBufferTransport *self)
{
    GST_LOG("begin\n");

    self->omxbuffer = NULL;
    self->port = NULL;

    GST_LOG("end\n");
}

static void gst_omxbuffertransport_class_init(
                GstOmxBufferTransportClass *klass)
{
    GST_LOG("begin\n");

    parent_class = g_type_class_peek_parent(klass);

    /* Override the mini-object's finalize routine so we can do cleanup when
     * a GstOmxBufferTransport is unref'd.
     */
    klass->derived_methods.mini_object_class.finalize =
        (GstMiniObjectFinalizeFunction) gst_omxbuffertransport_finalize;

    GST_LOG("end class_init\n");
}

static void
release_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer)
{
    switch (port->type)
    {
        case GOMX_PORT_INPUT:
            GST_LOG ("ETB: omx_buffer=%p, pAppPrivate=%p, pBuffer=%p",
                    omx_buffer, omx_buffer ? omx_buffer->pAppPrivate : 0, omx_buffer ? omx_buffer->pBuffer : 0);
            OMX_EmptyThisBuffer (port->core->omx_handle, omx_buffer);
            break;
        case GOMX_PORT_OUTPUT:
            GST_LOG ("FTB: omx_buffer=%p, pAppPrivate=%p, pBuffer=%p",
                    omx_buffer, omx_buffer ? omx_buffer->pAppPrivate : 0, omx_buffer ? omx_buffer->pBuffer : 0);
            OMX_FillThisBuffer (port->core->omx_handle, omx_buffer);
            break;
        default:
            break;
    }
}

static void gst_omxbuffertransport_finalize(GstBuffer *gstbuffer)
{
    GstOmxBufferTransport *self = GST_OMXBUFFERTRANSPORT(gstbuffer);
    int ii;
    GST_LOG("begin\n");

    release_buffer (self->port, self->omxbuffer);

	for(ii = 0; ii < self->numAdditionalHeaders; ii++) {
		//printf("finalize buffer:%p\n",self->addHeader[ii]);
		release_buffer(self->port,self->addHeader[ii]);
	}

    self->omxbuffer = NULL;
    self->port = NULL;

    /* Call GstBuffer's finalize routine, so our base class can do it's cleanup
     * as well.  If we don't do this, we'll have a memory leak that is very
     * difficult to track down.
     */
    GST_BUFFER_CLASS(parent_class)->
        mini_object_class.finalize(GST_MINI_OBJECT(gstbuffer));

    GST_LOG("end finalize\n");
}

GstBuffer* gst_omxbuffertransport_new (GOmxPort *port, OMX_BUFFERHEADERTYPE *buffer)
{
    GstOmxBufferTransport *tdt_buf;

    tdt_buf = (GstOmxBufferTransport*)
              gst_mini_object_new(GST_TYPE_OMXBUFFERTRANSPORT);

    g_return_val_if_fail(tdt_buf != NULL, NULL);

    GST_BUFFER_SIZE(tdt_buf) = buffer->nFilledLen;
    GST_BUFFER_DATA(tdt_buf) = buffer->pBuffer;
    gst_buffer_set_caps(GST_BUFFER (tdt_buf), port->caps);

    if (GST_BUFFER_DATA(tdt_buf) == NULL) {
        gst_mini_object_unref(GST_MINI_OBJECT(tdt_buf));
        return NULL;
    }

    tdt_buf->omxbuffer  = buffer;
    tdt_buf->port       = port;

	tdt_buf->numAdditionalHeaders = 0;
	tdt_buf->addHeader = NULL;

    GST_LOG("end new\n");

    return GST_BUFFER(tdt_buf);
}

void gst_omxbuffertransport_set_additional_headers (GstOmxBufferTransport *self ,guint numHeaders,OMX_BUFFERHEADERTYPE **buffer)
{
    int ii;

	if(numHeaders == 0)
		return;
	
    self->addHeader = malloc(numHeaders*sizeof(OMX_BUFFERHEADERTYPE *));

	for(ii = 0; ii < numHeaders; ii++) {
		//printf("additional header:%p\n", buffer[ii]);
		self->addHeader[ii] = buffer[ii];
	}
	self->numAdditionalHeaders = numHeaders;

    return ;
}


