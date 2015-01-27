/*
 * Copyright (c) 2014, Freescale Semiconductor, Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_IMX_VIDEO_CONVERT_ALLOCATOR_H__
#define __GST_IMX_VIDEO_CONVERT_ALLOCATOR_H__

#include <gst/gst.h>
#include "gstallocatorphymem.h"

#define GST_TYPE_IMX_VIDEO_CONVERT_ALLOCATOR             \
                                  (gst_imx_video_convert_allocator_get_type())
#define GST_IMX_VIDEO_CONVERT_ALLOCATOR(obj)             \
      (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VIDEO_CONVERT_ALLOCATOR,\
          GstImxVideoConvertAllocator))

typedef struct _GstImxVideoConvertAllocator {
  GstAllocatorPhyMem parent;
  gpointer device;
} GstImxVideoConvertAllocator;

typedef struct _GstImxVideoConvertAllocatorClass {
  GstAllocatorPhyMemClass parent_class;
} GstImxVideoConvertAllocatorClass;

GType gst_imx_video_convert_allocator_get_type (void);
GstAllocator *gst_imx_video_convert_allocator_new (gpointer device);

#endif /* __GST_IMX_VIDEO_CONVERT_ALLOCATOR_H__ */
