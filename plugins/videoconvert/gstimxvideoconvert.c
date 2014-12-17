/* GStreamer IMX video convert plugin
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/video/video.h>
#include "gstallocatorphymem.h"
#include "gstimxvideoconvert.h"

#define IMX_VCT_IN_POOL_MAX_BUFFERS   30

#define GST_IMX_VCT_PARAMS_QDATA   g_quark_from_static_string("imxvct-params")

#define GST_IMX_VIDEO_ROTATION_DEFAULT      IMX_VIDEO_ROTATION_0
#define GST_IMX_VIDEO_DEINTERLACE_DEFAULT   IMX_VIDEO_DEINTERLACE_NONE

#define GST_IMX_CONVERT_UNREF_BUFFER(buffer) {\
    if (buffer) {                             \
      GST_LOG ("unref buffer (%p)", buffer);  \
      gst_buffer_unref(buffer);               \
      buffer = NULL;                          \
    }                                         \
  }

#define GST_IMX_CONVERT_UNREF_POOL(pool)  {   \
    if (pool) {                               \
      GST_LOG ("unref pool (%p)", pool);      \
      gst_buffer_pool_set_active (pool, FALSE);\
      gst_object_unref(pool);                 \
      pool = NULL;                            \
    }                                         \
  }

/* properties utility*/
enum {
  PROP_0,
  PROP_OUTPUT_ROTATE,
  PROP_DEINTERLACE_MODE
};

static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY (imxvideoconvert_debug);
#define GST_CAT_DEFAULT imxvideoconvert_debug

GType gst_imx_video_convert_rotation_get_type(void) {
  static GType gst_imx_video_convert_rotation_type = 0;

  if (!gst_imx_video_convert_rotation_type) {
    static GEnumValue rotation_values[] = {
      {IMX_VIDEO_ROTATION_0, "No rotation", "none"},
      {IMX_VIDEO_ROTATION_90, "Rotate 90 degrees", "rotate-90"},
      {IMX_VIDEO_ROTATION_180, "Rotate 180 degrees", "rotate-180"},
      {IMX_VIDEO_ROTATION_270, "Rotate 270 degrees", "rotate-270"},
      {IMX_VIDEO_ROTATION_HFLIP, "Flip horizontally", "horizontal-flip"},
      {IMX_VIDEO_ROTATION_VFLIP, "Flip vertically", "vertical-flip"},
      {0, NULL, NULL },
    };

    gst_imx_video_convert_rotation_type =
        g_enum_register_static("ImxVideoConvertRotationMode", rotation_values);
  }

  return gst_imx_video_convert_rotation_type;
}

GType gst_imx_video_convert_deinterlace_get_type(void) {
  static GType gst_imx_video_convert_deinterlace_type = 0;

  if (!gst_imx_video_convert_deinterlace_type) {
    static GEnumValue deinterlace_values[] = {
      { IMX_VIDEO_DEINTERLACE_NONE, "No deinterlace", "none" },
      { IMX_VIDEO_DEINTERLACE_LOW_MOTION,
          "low-motion deinterlace", "low-motion" },
      { IMX_VIDEO_DEINTERLACE_MID_MOTION,
          "midium-motion deinterlace", "mid-motion" },
      { IMX_VIDEO_DEINTERLACE_HIGH_MOTION,
          "high-motion deinterlace", "high-motion" },
      { 0, NULL, NULL },
    };

    gst_imx_video_convert_deinterlace_type =
        g_enum_register_static("ImxVideoConvertDeinterlaceMode",
                                deinterlace_values);
  }

  return gst_imx_video_convert_deinterlace_type;
}

static void gst_imx_video_convert_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstImxVideoConvert *imxvct = (GstImxVideoConvert *) (object);
  ImxVideoProcessDevice *device = imxvct->device;

  GST_DEBUG_OBJECT (imxvct, "set_property (%d).", prop_id);

  if (!device)
    return;

  switch (prop_id) {
    case PROP_OUTPUT_ROTATE:
      device->set_rotate(device, g_value_get_enum (value));
      break;
    case PROP_DEINTERLACE_MODE:
      device->set_deinterlace(device, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  //TODO if property changed, it may affect the passthrough, so we need
  // reconfig the pipeline, send a reconfig event for caps re-negotiation.
}

static void gst_imx_video_convert_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstImxVideoConvert *imxvct = (GstImxVideoConvert *) (object);
  ImxVideoProcessDevice *device = imxvct->device;

  if (!device)
    return;

  switch (prop_id) {
    case PROP_OUTPUT_ROTATE:
      g_value_set_enum (value, device->get_rotate(device));
      break;
    case PROP_DEINTERLACE_MODE:
      g_value_set_enum (value, device->get_deinterlace(device));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void gst_imx_video_convert_finalize (GObject * object)
{
  GstImxVideoConvert *imxvct = (GstImxVideoConvert *) (object);
  GstStructure *config;
  GstImxVideoConvertClass *klass =
        (GstImxVideoConvertClass *) G_OBJECT_GET_CLASS (imxvct);

  GST_IMX_CONVERT_UNREF_BUFFER (imxvct->in_buf);
  GST_IMX_CONVERT_UNREF_POOL (imxvct->in_pool);
  GST_IMX_CONVERT_UNREF_POOL (imxvct->out_pool);
  if (imxvct->allocator) {
    gst_object_unref (imxvct->allocator);
    imxvct->allocator = NULL;
  }

  if (imxvct->device) {
    imxvct->device->close(imxvct->device);
    if (klass->in_plugin)
      klass->in_plugin->destroy(imxvct->device);
    imxvct->device = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (imxvct));
}

static gboolean
imx_video_convert_src_event(GstBaseTransform *transform, GstEvent *event)
{
  gdouble a;
  GstStructure *structure;
  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST(transform);

  GST_TRACE("%s event", GST_EVENT_TYPE_NAME(event));

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_NAVIGATION:
      if ((filter->in_info.width != filter->out_info.width) ||
          (filter->in_info.height != filter->out_info.height)) {
        event =
            GST_EVENT(gst_mini_object_make_writable(GST_MINI_OBJECT(event)));

        structure = (GstStructure *)gst_event_get_structure(event);
        if (gst_structure_get_double(structure, "pointer_x", &a)) {
          gst_structure_set(
            structure, "pointer_x", G_TYPE_DOUBLE,
            a * filter->in_info.width / filter->out_info.width,
            NULL
          );
        }

        if (gst_structure_get_double(structure, "pointer_y", &a)) {
          gst_structure_set(
            structure, "pointer_y", G_TYPE_DOUBLE,
            a * filter->in_info.height / filter->out_info.height,
            NULL
          );
        }
      }
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS(parent_class)->src_event(transform, event);
}

static GstCaps* imx_video_convert_transform_caps(GstBaseTransform *transform,
                     GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
  GstCaps *tmp, *tmp2, *result;
  GstStructure *st;
  gint i, n;

  GST_DEBUG("transform caps: %" GST_PTR_FORMAT, caps);
  GST_DEBUG("filter: %" GST_PTR_FORMAT, filter);
  GST_DEBUG("direction: %d", direction);

  /* Get all possible caps that we can transform to */
  /* copies the given caps */
  tmp = gst_caps_new_empty();
  n = gst_caps_get_size(caps);

  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure(caps, i);

    if ((i > 0) && gst_caps_is_subset_structure(tmp, st))
      continue;

    st = gst_structure_copy(st);

    gst_structure_set(st, "width", GST_TYPE_INT_RANGE, 64, G_MAXINT,
                          "height", GST_TYPE_INT_RANGE, 64, G_MAXINT, NULL);

    gst_structure_remove_fields(st, "format", NULL);

    /* if pixel aspect ratio, make a range of it*/
    if (gst_structure_has_field(st, "pixel-aspect-ratio")) {
      gst_structure_set(st, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    gst_caps_append_structure(tmp, st);
  }

  GST_DEBUG("transformed: %" GST_PTR_FORMAT, tmp);

  if (filter) {
    tmp2 = gst_caps_intersect_full(filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG("return caps: %" GST_PTR_FORMAT, result);

  return result;
}

#ifdef COMPARE_CONVERT_LOSS
/* calculate how much loss a conversion would be */
/* This loss calculation comes from gstvideoconvert.c of base plugins */
static gint get_format_conversion_loss(GstBaseTransform * base,
                                       GstVideoFormat in_name,
                                       GstVideoFormat out_name)
{
#define SCORE_FORMAT_CHANGE       1
#define SCORE_DEPTH_CHANGE        1
#define SCORE_ALPHA_CHANGE        1
#define SCORE_CHROMA_W_CHANGE     1
#define SCORE_CHROMA_H_CHANGE     1
#define SCORE_PALETTE_CHANGE      1

#define SCORE_COLORSPACE_LOSS     2     /* RGB <-> YUV */
#define SCORE_DEPTH_LOSS          4     /* change bit depth */
#define SCORE_ALPHA_LOSS          8     /* lose the alpha channel */
#define SCORE_CHROMA_W_LOSS      16     /* vertical sub-sample */
#define SCORE_CHROMA_H_LOSS      32     /* horizontal sub-sample */
#define SCORE_PALETTE_LOSS       64     /* convert to palette format */
#define SCORE_COLOR_LOSS        128     /* convert to GRAY */

#define COLORSPACE_MASK (GST_VIDEO_FORMAT_FLAG_YUV | \
                         GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_GRAY)
#define ALPHA_MASK      (GST_VIDEO_FORMAT_FLAG_ALPHA)
#define PALETTE_MASK    (GST_VIDEO_FORMAT_FLAG_PALETTE)

  gint loss = G_MAXINT;
  GstVideoFormatFlags in_flags, out_flags;
  const GstVideoFormatInfo *in_info = gst_video_format_get_info(in_name);
  const GstVideoFormatInfo *out_info = gst_video_format_get_info(out_name);

  if (!in_info || !out_info)
    return G_MAXINT;

  /* accept input format immediately without loss */
  if (in_info == out_info) {
    GST_LOG("same format %s", GST_VIDEO_FORMAT_INFO_NAME(in_info));
    return 0;
  }

  loss = SCORE_FORMAT_CHANGE;

  in_flags = GST_VIDEO_FORMAT_INFO_FLAGS (in_info);
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  out_flags = GST_VIDEO_FORMAT_INFO_FLAGS (out_info);
  out_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  out_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  out_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  if ((out_flags & PALETTE_MASK) != (in_flags & PALETTE_MASK)) {
    loss += SCORE_PALETTE_CHANGE;
    if (out_flags & PALETTE_MASK)
      loss += SCORE_PALETTE_LOSS;
  }

  if ((out_flags & COLORSPACE_MASK) != (in_flags & COLORSPACE_MASK)) {
    loss += SCORE_COLORSPACE_LOSS;
    if (out_flags & GST_VIDEO_FORMAT_FLAG_GRAY)
      loss += SCORE_COLOR_LOSS;
  }

  if ((out_flags & ALPHA_MASK) != (in_flags & ALPHA_MASK)) {
    loss += SCORE_ALPHA_CHANGE;
    if (in_flags & ALPHA_MASK)
      loss += SCORE_ALPHA_LOSS;
  }

  if ((in_info->h_sub[1]) != (out_info->h_sub[1])) {
    loss += SCORE_CHROMA_H_CHANGE;
    if ((in_info->h_sub[1]) < (out_info->h_sub[1]))
      loss += SCORE_CHROMA_H_LOSS;
  }
  if ((in_info->w_sub[1]) != (out_info->w_sub[1])) {
    loss += SCORE_CHROMA_W_CHANGE;
    if ((in_info->w_sub[1]) < (out_info->w_sub[1]))
      loss += SCORE_CHROMA_W_LOSS;
  }

  if ((in_info->bits) != (out_info->bits)) {
    loss += SCORE_DEPTH_CHANGE;
    if ((in_info->bits) > (out_info->bits))
      loss += SCORE_DEPTH_LOSS;
  }

  GST_LOG("%s -> %s, loss = %d", GST_VIDEO_FORMAT_INFO_NAME(in_info),
                  GST_VIDEO_FORMAT_INFO_NAME(out_info), loss);
  return loss;
}
#endif

static GstCaps* imx_video_convert_caps_from_fmt_list(GList* list)
{
  gint i;
  GstCaps *caps = NULL;

  for (i=0; i<g_list_length (list); i++) {
    GstVideoFormat fmt = (GstVideoFormat)g_list_nth_data(list, i);
    if (caps) {
      GstCaps *newcaps = gst_caps_new_simple("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string(fmt), NULL);
      gst_caps_append (caps, newcaps);
    } else {
      caps = gst_caps_new_simple("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string(fmt), NULL);
    }
  }
  return caps;
}

static guint imx_video_convert_fixate_format_caps(GstBaseTransform *transform,
                                            GstCaps *caps, GstCaps *othercaps)
{
  GstStructure *outs;
  GstStructure *tests;
  const GValue *format;
  GstVideoFormat out_fmt = GST_VIDEO_FORMAT_UNKNOWN;
  const GstVideoFormatInfo *out_info = NULL;
  const gchar *fmt_name;
  GstStructure *ins;
  const gchar *in_interlace;
  gboolean interlace = FALSE;
  GstCaps *new_caps;

  GstImxVideoConvert *imxvct = (GstImxVideoConvert *)(transform);
  ImxVideoProcessDevice *device = imxvct->device;

  //the input caps should fixed alreay, and only have caps0
  ins = gst_caps_get_structure(caps, 0);
  outs = gst_caps_get_structure(othercaps, 0);

  in_interlace = gst_structure_get_string(ins, "interlace-mode");
  if (in_interlace && (g_strcmp0(in_interlace, "interleaved") == 0
                       || g_strcmp0(in_interlace, "mixed") == 0)) {
    interlace = TRUE;
  }

  /* if rotate or deinterlace enabled & interleaved input,
   * then passthrough is not possible, we need limit the othercaps
   * with device conversion limitation
   */
  if (device->get_rotate(device) != IMX_VIDEO_ROTATION_0 ||
      (device->get_deinterlace(device) != IMX_VIDEO_DEINTERLACE_NONE &&
          interlace)) {
    GList* list = device->get_supported_out_fmts();
    GstCaps *out_caps = imx_video_convert_caps_from_fmt_list(list);
    g_list_free(list);

    new_caps = gst_caps_intersect_full(othercaps, out_caps,
                                       GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(out_caps);
  } else {
    new_caps = gst_caps_copy(othercaps);
  }

#ifdef COMPARE_CONVERT_LOSS
  GstVideoFormat in_fmt;
  gint min_loss = G_MAXINT;
  gint loss;
  guint i, j;

  fmt_name = gst_structure_get_string(ins, "format");
  if (!fmt_name) {
    gst_caps_unref(new_caps);
    return -1;
  }

  GST_LOG("source format : %s", fmt_name);

  in_fmt = gst_video_format_from_string(fmt_name);

  for (i = 0; i < gst_caps_get_size(new_caps); i++) {
    tests = gst_caps_get_structure(new_caps, i);
    format = gst_structure_get_value(tests, "format");
    if (!format) {
      gst_caps_unref(new_caps);
      return -1;
    }

    if (GST_VALUE_HOLDS_LIST(format)) {
      for (j = 0; j < gst_value_list_get_size(format); j++) {
        const GValue *val = gst_value_list_get_value(format, j);
        if (G_VALUE_HOLDS_STRING(val)) {
          out_fmt = gst_video_format_from_string(g_value_get_string(val));
          loss = get_format_conversion_loss(transform, in_fmt, out_fmt);
          if (loss < min_loss) {
            out_info = gst_video_format_get_info(out_fmt);
            min_loss = loss;
          }

          if (min_loss == 0)
            break;
        }
      }
    } else if (G_VALUE_HOLDS_STRING(format)) {
      out_fmt = gst_video_format_from_string(g_value_get_string(format));
      loss = get_format_conversion_loss(transform, in_fmt, out_fmt);
      if (loss < min_loss) {
        out_info = gst_video_format_get_info(out_fmt);
        min_loss = loss;
      }
    }

    if (min_loss == 0)
      break;
  }
#else
  format =
      gst_structure_get_value(gst_caps_get_structure(new_caps, 0), "format");
  if (format) {
    if (GST_VALUE_HOLDS_LIST(format)) {
      format = gst_value_list_get_value(format, 0);
    }
    out_fmt = gst_video_format_from_string(g_value_get_string(format));
    out_info = gst_video_format_get_info(out_fmt);
  }
#endif

  gst_caps_unref(new_caps);

  if (out_info) {
    fmt_name = GST_VIDEO_FORMAT_INFO_NAME(out_info);
    gst_structure_set(outs, "format", G_TYPE_STRING, fmt_name, NULL);
    GST_LOG("out format %s", fmt_name);
    return 0;
  } else {
    gst_structure_set(outs, "format", G_TYPE_STRING, "UNKNOWN", NULL);
    GST_LOG("out format not match");
    return -1;
  }
}

static GstCaps* imx_video_convert_fixate_caps(GstBaseTransform *transform,
    GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
  GstStructure *ins, *outs;
  GValue const *from_par, *to_par;
  GValue fpar = { 0, }, tpar = { 0, };
  const gchar *in_format;
  const GstVideoFormatInfo *in_info, *out_info = NULL;
  gint min_loss = G_MAXINT;
  guint i, capslen;

  g_return_val_if_fail(gst_caps_is_fixed (caps), othercaps);

  othercaps = gst_caps_make_writable(othercaps);

  GST_DEBUG("fixate othercaps: %" GST_PTR_FORMAT, othercaps);
  GST_DEBUG("based on caps: %" GST_PTR_FORMAT, caps);
  GST_DEBUG("direction: %d", direction);

  ins = gst_caps_get_structure(caps, 0);
  outs = gst_caps_get_structure(othercaps, 0);

  from_par = gst_structure_get_value(ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value(outs, "pixel-aspect-ratio");

  /* If no par info, then set some assuming value  */
  if (!from_par || !to_par) {
    if (direction == GST_PAD_SINK) {
      if (!from_par) {
        g_value_init(&fpar, GST_TYPE_FRACTION);
        gst_value_set_fraction(&fpar, 1, 1);
        from_par = &fpar;
      }
      if (!to_par) {
        g_value_init(&tpar, GST_TYPE_FRACTION_RANGE);
        gst_value_set_fraction_range_full(&tpar, 1, G_MAXINT, G_MAXINT, 1);
        to_par = &tpar;
      }
    } else {
      if (!to_par) {
        g_value_init(&tpar, GST_TYPE_FRACTION);
        gst_value_set_fraction(&tpar, 1, 1);
        to_par = &tpar;
        gst_structure_set(outs, "pixel-aspect-ratio",
                          GST_TYPE_FRACTION, 1, 1, NULL);
      }
      if (!from_par) {
        g_value_init(&fpar, GST_TYPE_FRACTION);
        gst_value_set_fraction (&fpar, 1, 1);
        from_par = &fpar;
      }
    }
  }

  /* from_par should be fixed now */
  gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
  gint w = 0, h = 0;
  gint from_dar_n, from_dar_d;
  gint num, den;
  GstStructure *tmp;
  gint set_w, set_h, set_par_n, set_par_d;

  from_par_n = gst_value_get_fraction_numerator(from_par);
  from_par_d = gst_value_get_fraction_denominator(from_par);

  gst_structure_get_int(ins, "width", &from_w);
  gst_structure_get_int(ins, "height", &from_h);

  gst_structure_get_int(outs, "width", &w);
  gst_structure_get_int(outs, "height", &h);

  /* if both width and height are already fixed, we can do nothing */
  if (w && h) {
    guint dar_n, dar_d;
    GST_DEBUG("dimensions already set to %dx%d", w, h);

    if (!gst_value_is_fixed(to_par)) {
      if (gst_video_calculate_display_ratio(&dar_n, &dar_d,
          from_w, from_h, from_par_n, from_par_d, w, h)) {
        GST_DEBUG("fixating to_par to %d/%d", dar_n, dar_d);

        if (gst_structure_has_field(outs, "pixel-aspect-ratio")) {
          gst_structure_fixate_field_nearest_fraction(outs,
                                        "pixel-aspect-ratio", dar_n, dar_d);
        } else if (dar_n != dar_d) {
          gst_structure_set(outs, "pixel-aspect-ratio",
                            GST_TYPE_FRACTION, dar_n, dar_d, NULL);
        }
      }
    }

    goto done;
  }

  /* Calculate input DAR */
  gst_util_fraction_multiply(from_w, from_h, from_par_n, from_par_d,
                              &from_dar_n, &from_dar_d);
  GST_LOG("Input DAR is %d/%d", from_dar_n, from_dar_d);

  /* If either width or height are fixed, choose a height or width and PAR */
  if (h) {
    GST_DEBUG("height is fixed (%d)", h);

    /* If the PAR is fixed, choose the width that match DAR */
    if (gst_value_is_fixed(to_par)) {
      to_par_n = gst_value_get_fraction_numerator(to_par);
      to_par_d = gst_value_get_fraction_denominator(to_par);
      GST_DEBUG("PAR is fixed %d/%d", to_par_n, to_par_d);

      gst_util_fraction_multiply(from_dar_n, from_dar_d,
                                 to_par_d, to_par_n, &num, &den);
      w = (guint) gst_util_uint64_scale_int(h, num, den);
      gst_structure_fixate_field_nearest_int(outs, "width", w);
    } else {
      /* The PAR is not fixed, Check if we can keep the input width */
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "width", from_w);
      gst_structure_get_int(tmp, "width", &set_w);
      gst_util_fraction_multiply(from_dar_n, from_dar_d, h, set_w,
                                 &to_par_n, &to_par_d);

      if (!gst_structure_has_field(tmp, "pixel-aspect-ratio"))
        gst_structure_set_value(tmp, "pixel-aspect-ratio", to_par);

      gst_structure_fixate_field_nearest_fraction(tmp, "pixel-aspect-ratio",
                                                  to_par_n, to_par_d);
      gst_structure_get_fraction(tmp, "pixel-aspect-ratio",
                                  &set_par_n, &set_par_d);
      gst_structure_free(tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field(outs, "pixel-aspect-ratio")
            || set_par_n != set_par_d) {
          gst_structure_set(outs, "width", G_TYPE_INT, set_w,
           "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);
        }
      } else {
        /* scale the width to the new PAR and check if the adjusted width is
         * accepted. If all that fails we can't keep the DAR */
        gst_util_fraction_multiply(from_dar_n, from_dar_d, set_par_d, set_par_n,
                                  &num, &den);

        w = (guint) gst_util_uint64_scale_int(h, num, den);
        gst_structure_fixate_field_nearest_int(outs, "width", w);
        if (gst_structure_has_field(outs, "pixel-aspect-ratio")
            || set_par_n != set_par_d) {
          gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                            set_par_n, set_par_d, NULL);
        }
      }
    }
  } else if (w) {
    GST_DEBUG("width is fixed (%d)", w);

    /* If the PAR is fixed, choose the height that match the DAR */
    if (gst_value_is_fixed(to_par)) {
      to_par_n = gst_value_get_fraction_numerator(to_par);
      to_par_d = gst_value_get_fraction_denominator(to_par);
      GST_DEBUG("PAR is fixed %d/%d", to_par_n, to_par_d);

      gst_util_fraction_multiply(from_dar_n, from_dar_d, to_par_d, to_par_n,
                                 &num, &den);
      h = (guint) gst_util_uint64_scale_int(w, den, num);
      gst_structure_fixate_field_nearest_int(outs, "height", h);
    } else {
      /* Check if we can keep the input height */
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "height", from_h);
      gst_structure_get_int(tmp, "height", &set_h);
      gst_util_fraction_multiply(from_dar_n, from_dar_d, set_h, w,
                                 &to_par_n, &to_par_d);

      if (!gst_structure_has_field(tmp, "pixel-aspect-ratio"))
        gst_structure_set_value(tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction(tmp, "pixel-aspect-ratio",
                                                  to_par_n, to_par_d);
      gst_structure_get_fraction(tmp, "pixel-aspect-ratio",
                                 &set_par_n, &set_par_d);
      gst_structure_free(tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field(outs, "pixel-aspect-ratio")
            || set_par_n != set_par_d) {
          gst_structure_set(outs, "height", G_TYPE_INT, set_h,
           "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);
        }
      } else {
        /* scale the height to the new PAR and check if the adjusted width
         * is accepted. If all that fails we can't keep the DAR */
        gst_util_fraction_multiply(from_dar_n, from_dar_d, set_par_d, set_par_n,
                                    &num, &den);

        h = (guint) gst_util_uint64_scale_int(w, den, num);
        gst_structure_fixate_field_nearest_int(outs, "height", h);
        if (gst_structure_has_field(outs, "pixel-aspect-ratio")
            || set_par_n != set_par_d) {
          gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        }
      }
    }
  } else {
    /* both h and w not fixed */
    if (gst_value_is_fixed(to_par)) {
      gint f_h, f_w;
      to_par_n = gst_value_get_fraction_numerator(to_par);
      to_par_d = gst_value_get_fraction_denominator(to_par);

      /* Calculate scale factor for the PAR change */
      gst_util_fraction_multiply(from_dar_n, from_dar_d, to_par_n, to_par_d,
                                 &num, &den);

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "height", from_h);
      gst_structure_get_int(tmp, "height", &set_h);
      w = (guint) gst_util_uint64_scale_int(set_h, num, den);
      gst_structure_fixate_field_nearest_int(tmp, "width", w);
      gst_structure_get_int(tmp, "width", &set_w);
      gst_structure_free(tmp);

      if (set_w == w) {
        gst_structure_set(outs, "width", G_TYPE_INT, set_w,
                          "height", G_TYPE_INT, set_h, NULL);
      } else {
        f_h = set_h;
        f_w = set_w;

        /* If the former failed, try to keep the input width at least */
        tmp = gst_structure_copy(outs);
        gst_structure_fixate_field_nearest_int(tmp, "width", from_w);
        gst_structure_get_int(tmp, "width", &set_w);
        h = (guint) gst_util_uint64_scale_int(set_w, den, num);
        gst_structure_fixate_field_nearest_int(tmp, "height", h);
        gst_structure_get_int(tmp, "height", &set_h);
        gst_structure_free(tmp);

        if (set_h == h)
          gst_structure_set(outs, "width", G_TYPE_INT, set_w,
                            "height", G_TYPE_INT, set_h, NULL);
        else
          gst_structure_set(outs, "width", G_TYPE_INT, f_w,
                            "height", G_TYPE_INT, f_h, NULL);
      }
    } else {
      gint tmp2;
      /* width, height and PAR are not fixed but passthrough is not possible */
      /* try to keep the height and width as good as possible and scale PAR */
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "height", from_h);
      gst_structure_get_int(tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int(tmp, "width", from_w);
      gst_structure_get_int(tmp, "width", &set_w);

      gst_util_fraction_multiply(from_dar_n, from_dar_d, set_h, set_w,
                                 &to_par_n, &to_par_d);

      if (!gst_structure_has_field(tmp, "pixel-aspect-ratio"))
        gst_structure_set_value(tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction(tmp, "pixel-aspect-ratio",
                                                  to_par_n, to_par_d);
      gst_structure_get_fraction(tmp, "pixel-aspect-ratio",
                                 &set_par_n, &set_par_d);
      gst_structure_free(tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set(outs, "width", G_TYPE_INT, set_w,
                                "height", G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field(outs, "pixel-aspect-ratio")
            || set_par_n != set_par_d) {
          gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                            set_par_n, set_par_d, NULL);
        }
      } else {
        /* Otherwise try to scale width to keep the DAR with the set
         * PAR and height */
        gst_util_fraction_multiply(from_dar_n, from_dar_d, set_par_d, set_par_n,
                                   &num, &den);

        w = (guint) gst_util_uint64_scale_int(set_h, num, den);
        tmp = gst_structure_copy(outs);
        gst_structure_fixate_field_nearest_int(tmp, "width", w);
        gst_structure_get_int(tmp, "width", &tmp2);
        gst_structure_free(tmp);

        if (tmp2 == w) {
          gst_structure_set(outs, "width", G_TYPE_INT, tmp2,
                                  "height", G_TYPE_INT, set_h, NULL);
          if (gst_structure_has_field(outs, "pixel-aspect-ratio")
              || set_par_n != set_par_d) {
            gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                              set_par_n, set_par_d, NULL);
          }
        } else {
          /* then try the same with the height */
          h = (guint) gst_util_uint64_scale_int(set_w, den, num);
          tmp = gst_structure_copy(outs);
          gst_structure_fixate_field_nearest_int(tmp, "height", h);
          gst_structure_get_int(tmp, "height", &tmp2);
          gst_structure_free(tmp);

          if (tmp2 == h) {
            gst_structure_set(outs, "width", G_TYPE_INT, set_w,
                                    "height", G_TYPE_INT, tmp2, NULL);
            if (gst_structure_has_field(outs, "pixel-aspect-ratio")
                || set_par_n != set_par_d) {
              gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                                set_par_n, set_par_d, NULL);
            }
          } else {
            /* Don't keep the DAR, take the nearest values from the first try */
            gst_structure_set(outs, "width", G_TYPE_INT, set_w,
                                    "height", G_TYPE_INT, set_h, NULL);
            if (gst_structure_has_field(outs, "pixel-aspect-ratio")
                || set_par_n != set_par_d) {
              gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                                set_par_n, set_par_d, NULL);
            }
          }
        }
      }
    }
  }

done:
  if (from_par == &fpar)
    g_value_unset(&fpar);
  if (to_par == &tpar)
    g_value_unset(&tpar);

  imx_video_convert_fixate_format_caps(transform, caps, othercaps);
  othercaps = gst_caps_fixate (othercaps);

  GST_DEBUG("fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static gboolean
gst_imx_video_convert_filter_meta (GstBaseTransform * trans, GstQuery * query,
    GType api, const GstStructure * params)
{
  /* propose all metadata upstream */
  return TRUE;
}

static GstBufferPool*
gst_imx_video_convert_create_bufferpool(GstImxVideoConvert *imxvct,
                    GstCaps *caps, guint size, guint min, guint max)
{
  GstBufferPool *pool;
  GstStructure *config;
  pool = gst_video_buffer_pool_new ();
  if (pool) {
    if (!imxvct->allocator)
      imxvct->allocator =
          gst_imx_video_convert_allocator_new((gpointer)(imxvct->device));

    if (!imxvct->allocator) {
      GST_ERROR ("new imx video convert allocator failed.");
      gst_buffer_pool_set_active (pool, FALSE);
      gst_object_unref (pool);
      return NULL;
    }

    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, caps, size, min, max);
    gst_buffer_pool_config_set_allocator(config, imxvct->allocator, NULL);
    gst_buffer_pool_config_add_option(config,
                                      GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_add_option (config,
                                      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

    GstVideoInfo info;
    GstVideoAlignment alignment;
    memset (&alignment, 0, sizeof (GstVideoAlignment));
    gst_video_info_from_caps (&info, caps);
    gint w = GST_VIDEO_INFO_WIDTH (&info);
    gint h = GST_VIDEO_INFO_HEIGHT (&info);
    if (!ISALIGNED (w, ALIGNMENT) || !ISALIGNED (h, ALIGNMENT)) {
      alignment.padding_right = ALIGNTO (w, ALIGNMENT) - w;
      alignment.padding_bottom = ALIGNTO (h, ALIGNMENT) - h;
    }

    GST_DEBUG ("[%d, %d]:padding_right (%d), padding_bottom (%d)", w, h,
        alignment.padding_right, alignment.padding_bottom);
    gst_buffer_pool_config_set_video_alignment (config, &alignment);

    if (!gst_buffer_pool_set_config(pool, config)) {
      GST_ERROR ("set buffer pool config failed.");
      gst_buffer_pool_set_active (pool, FALSE);
      gst_object_unref (pool);
      return NULL;
    }
  }

  GST_LOG ("created a buffer pool (%p).", pool);
  return pool;
}

static gboolean
imx_video_convert_propose_allocation(GstBaseTransform *transform,
                                      GstQuery *decide_query, GstQuery *query)
{
  GstImxVideoConvert *imxvct = (GstImxVideoConvert *)(transform);
  GstBufferPool *pool;
  GstVideoInfo info;
  guint size = 0;
  GstCaps *caps;
  gboolean need_pool;

  /* passthrough, we're done */
  if (decide_query == NULL) {
    GST_DEBUG ("doing passthrough query");
    return gst_pad_peer_query (transform->srcpad, query);
  } else {
    guint i, n_metas;
    /* non-passthrough, copy all metadata, decide_query does not contain the
     * metadata anymore that depends on the buffer memory */
    n_metas = gst_query_get_n_allocation_metas (decide_query);
    for (i = 0; i < n_metas; i++) {
      GType api;
      const GstStructure *params;
      api = gst_query_parse_nth_allocation_meta (decide_query, i, &params);
      gst_query_add_allocation_meta (query, api, params);
    }
  }

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL) {
    GST_ERROR_OBJECT (imxvct, "no caps specified.");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);
  size = PAGE_ALIGN(size);

  GST_IMX_CONVERT_UNREF_BUFFER (imxvct->in_buf);
  GST_IMX_CONVERT_UNREF_POOL(imxvct->in_pool);
  GST_DEBUG_OBJECT(imxvct, "creating new input pool");
  pool = gst_imx_video_convert_create_bufferpool(imxvct, caps, size, 1,
                                                 IMX_VCT_IN_POOL_MAX_BUFFERS);
  imxvct->in_pool = pool;
  imxvct->old_config = FALSE;

  if (pool) {
    GST_DEBUG_OBJECT (imxvct, "propose_allocation, pool(%p).", pool);
    gst_query_add_allocation_pool (query, pool, size, 1,
                                   IMX_VCT_IN_POOL_MAX_BUFFERS);
    gst_query_add_allocation_param (query, imxvct->allocator, NULL);
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
    gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
  } else {
    return FALSE;
  }
  return TRUE;
}

static gboolean imx_video_convert_decide_allocation(GstBaseTransform *transform,
                                                     GstQuery *query)
{
  GstImxVideoConvert *imxvct = (GstImxVideoConvert *)(transform);
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint size, num, min = 0, max = 0;
  GstStructure *config = NULL;
  GstVideoInfo vinfo;
  gboolean new_pool = TRUE;
  GstAllocator *allocator = NULL;

  gst_query_parse_allocation(query, &outcaps, NULL);
  gst_video_info_init(&vinfo);
  gst_video_info_from_caps(&vinfo, outcaps);
  num = gst_query_get_n_allocation_pools(query);
  size = vinfo.size;

  GST_DEBUG_OBJECT(imxvct, "number of allocation pools: %d", num);

  /* if downstream element provided buffer pool with phy buffers */
  if (num > 0) {
    guint i = 0;
    for (; i < num; ++i) {
      gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min, &max);
      if (pool) {
        config = gst_buffer_pool_get_config(pool);
        gst_buffer_pool_config_get_allocator(config, &allocator, NULL);
        if (allocator && GST_IS_ALLOCATOR_PHYMEM(allocator)) {
          size = MAX(size, vinfo.size);
          new_pool = FALSE;
          break;
        } else {
          GST_LOG_OBJECT (imxvct, "no phy allocator in output pool (%p)", pool);
        }

        if (config) {
          gst_structure_free (config);
          config = NULL;
        }

        if (allocator) {
          gst_object_unref (allocator);
          allocator = NULL;
        }
        gst_object_unref (pool);
      }
    }
  }

  size = MAX(size, vinfo.size);
  size = PAGE_ALIGN(size);

  /* downstream doesn't provide a pool or the pool has no ability to allocate
   * physical memory buffers, we need create new pool */
  if (new_pool) {
    GST_IMX_CONVERT_UNREF_POOL(imxvct->out_pool);
    GST_DEBUG_OBJECT(imxvct, "creating new output pool");
    pool = gst_imx_video_convert_create_bufferpool(imxvct, outcaps, size,
                                                   min, max);
    imxvct->out_pool = pool;
    gst_buffer_pool_set_active(pool, TRUE);
  }

  GST_DEBUG_OBJECT(imxvct, "pool config:  outcaps: %" GST_PTR_FORMAT "  "
      "size: %u  min buffers: %u  max buffers: %u", outcaps, size, min, max);

  if (pool) {
    if (num > 0)
      gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
    else
      gst_query_add_allocation_pool(query, pool, size, min, max);

    if (!new_pool)
      gst_object_unref (pool);
  }

  return TRUE;
}

static gboolean imx_video_convert_set_info(GstVideoFilter *filter,
                                    GstCaps *in, GstVideoInfo *in_info,
                                    GstCaps *out, GstVideoInfo *out_info)
{
  GstImxVideoConvert *imxvct = (GstImxVideoConvert *)(filter);
  ImxVideoProcessDevice *device = imxvct->device;
  GstStructure *ins, *outs;
  const gchar *from_interlace;

  if (!device)
    return FALSE;

  ins = gst_caps_get_structure(in, 0);
  outs = gst_caps_get_structure(out, 0);

  /* if interlaced and we enabled deinterlacing, make it progressive */
  from_interlace = gst_structure_get_string(ins, "interlace-mode");
  if (from_interlace &&
      (g_strcmp0(from_interlace, "interleaved") == 0
          || g_strcmp0(from_interlace, "mixed") == 0)) {
    if (IMX_VIDEO_DEINTERLACE_NONE != device->get_deinterlace(device)) {
      gst_structure_set(outs,
          "interlace-mode", G_TYPE_STRING, "progressive", NULL);
      gst_base_transform_set_passthrough((GstBaseTransform*)filter, FALSE);
    }
  }

  if (IMX_VIDEO_ROTATION_0 != device->get_rotate(device))
    gst_base_transform_set_passthrough((GstBaseTransform*)filter, FALSE);

/* can't remove since caps fixed
  if (gst_structure_get_string(outs, "colorimetry")) {
    GST_DEBUG("try to remove colorimetry");
    gst_structure_remove_fields(outs,"colorimetry", NULL);
  }

  if (gst_structure_get_string(outs, "chroma-site")) {
    GST_DEBUG("try to remove chroma-site");
    gst_structure_remove_fields(outs,"chroma-site", NULL);
  }
*/

  GST_DEBUG ("set info from %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, in, out);

  return TRUE;
}

static GstFlowReturn imx_video_convert_transform_frame(GstVideoFilter *filter,
    GstVideoFrame *in, GstVideoFrame *out)
{
  GstImxVideoConvert *imxvct = (GstImxVideoConvert *)(filter);
  ImxVideoProcessDevice *device = imxvct->device;
  GstVideoFrame *input_frame = in;
  GstCaps *caps;
  GstVideoFrame temp_in_frame;

  if (!device)
    return GST_FLOW_ERROR;

  if (!gst_buffer_is_phymem(out->buffer)) {
    GST_ERROR("out buffer is not phy memory");
    return GST_FLOW_ERROR;
  }

  /* Check if need copy input frame */
  if (!gst_buffer_is_phymem(in->buffer)) {
    GST_DEBUG ("copy input frame to phy memory");
    caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM_SINK_PAD(imxvct));

    if (!imxvct->in_pool) {
      GST_DEBUG_OBJECT(imxvct, "creating new input pool");
      imxvct->in_pool = gst_imx_video_convert_create_bufferpool(imxvct, caps,
          PAGE_ALIGN(in->info.size), 1, IMX_VCT_IN_POOL_MAX_BUFFERS);
    }

    gst_caps_unref (caps);

    if (imxvct->in_pool && !imxvct->in_buf) {
      gst_buffer_pool_set_active(imxvct->in_pool, TRUE);
      GstFlowReturn ret = gst_buffer_pool_acquire_buffer(imxvct->in_pool,
                                                  &(imxvct->in_buf), NULL);
      if (ret != GST_FLOW_OK)
        GST_ERROR("error acquiring input buffer: %s", gst_flow_get_name(ret));
      else
        GST_LOG ("created input buffer (%p)", imxvct->in_buf);
    }

    if (imxvct->in_buf) {
      gst_video_frame_map(&temp_in_frame, &(in->info),
                          imxvct->in_buf, GST_MAP_WRITE);
      gst_video_frame_copy(&temp_in_frame, in);
      input_frame = &temp_in_frame;
      gst_video_frame_unmap(&temp_in_frame);
    } else {
      GST_ERROR ("Can't get input buffer");
      return GST_FLOW_ERROR;
    }
  }

  //alignment check
  if (!imxvct->old_config) {
    if (imxvct->in_pool) {
      GstStructure *config;
      config = gst_buffer_pool_get_config (imxvct->in_pool);
      memset (&imxvct->video_align, 0, sizeof(GstVideoAlignment));

      // check if has alignment option setted.
      if (gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
        gst_buffer_pool_config_get_video_alignment (config, &imxvct->video_align);
        GST_DEBUG ("pool has alignment (%d, %d) , (%d, %d)",
          imxvct->video_align.padding_left, imxvct->video_align.padding_top,
          imxvct->video_align.padding_right, imxvct->video_align.padding_bottom);
      }

      gst_structure_free (config);
    }

    // config input
    gint ret = device->config_input(device, &(in->info), &imxvct->video_align);

    GST_LOG ("Input: %s, %dx%d", GST_VIDEO_FORMAT_INFO_NAME(in->info.finfo),
        in->info.width, in->info.height);

    // config output
    ret |= device->config_output(device, &(out->info));

    GST_LOG ("Output: %s, %dx%d", GST_VIDEO_FORMAT_INFO_NAME(out->info.finfo),
        out->info.width, out->info.height);

    if (ret != 0)
      return GST_FLOW_ERROR;

    imxvct->old_config = TRUE;
  }

  //convert
  if (device->do_convert(device, input_frame->buffer, out->buffer) == 0) {
    GST_TRACE ("frame conversion done");
    return GST_FLOW_OK;
  }

  return GST_FLOW_ERROR;
}

static void
gst_imx_video_convert_class_init (GstImxVideoConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS(klass);
  GstCaps *caps;

  ImxVideoProcessDeviceInfo *in_plugin = (ImxVideoProcessDeviceInfo *)
      g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass), GST_IMX_VCT_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  ImxVideoProcessDevice* dev = in_plugin->create();
  if (!dev)
    return;

  gst_element_class_set_static_metadata (element_class,
        in_plugin->description, "Filter/Converter/Video",
        in_plugin->detail, IMX_GST_PLUGIN_AUTHOR);

  GList *list = dev->get_supported_in_fmts();
  caps = imx_video_convert_caps_from_fmt_list(list);
  g_list_free(list);

  if (!caps) {
    GST_ERROR ("Couldn't create caps for device '%s'", in_plugin->name);
    caps = gst_caps_new_empty_simple ("video/x-raw");
  }
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));

#ifdef PASSTHOUGH_FOR_UNSUPPORTED_OUTPUT_FORMAT
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            gst_caps_copy(caps)));
#else
  list = dev->get_supported_out_fmts();
  caps = imx_video_convert_caps_from_fmt_list(list);
  g_list_free(list);

  if (!caps) {
    GST_ERROR ("Couldn't create caps for device '%s'", in_plugin->name);
    caps = gst_caps_new_empty_simple ("video/x-raw");
  }
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
#endif
  klass->in_plugin = in_plugin;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_imx_video_convert_finalize;
  gobject_class->set_property = gst_imx_video_convert_set_property;
  gobject_class->get_property = gst_imx_video_convert_get_property;

  gint capabilities = dev->get_capabilities();
  if (capabilities & IMX_VP_DEVICE_CAP_ROTATE) {
    g_object_class_install_property (gobject_class, PROP_OUTPUT_ROTATE,
        g_param_spec_enum("rotation", "Output rotation",
          "Rotation that shall be applied to output frames",
          gst_imx_video_convert_rotation_get_type(),
          GST_IMX_VIDEO_ROTATION_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }

  if (capabilities & IMX_VP_DEVICE_CAP_DEINTERLACE) {
    g_object_class_install_property (gobject_class, PROP_DEINTERLACE_MODE,
        g_param_spec_enum("deinterlace", "Deinterlace mode",
          "Deinterlacing mode to be used for incoming frames "
          "(ignored if frames are not interlaced)",
          gst_imx_video_convert_deinterlace_get_type(),
          GST_IMX_VIDEO_DEINTERLACE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }

  in_plugin->destroy(dev);

  base_transform_class->src_event =
      GST_DEBUG_FUNCPTR(imx_video_convert_src_event);
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR(imx_video_convert_transform_caps);
  base_transform_class->fixate_caps =
      GST_DEBUG_FUNCPTR(imx_video_convert_fixate_caps);
  base_transform_class->filter_meta =
      GST_DEBUG_FUNCPTR (gst_imx_video_convert_filter_meta);
  base_transform_class->propose_allocation =
      GST_DEBUG_FUNCPTR(imx_video_convert_propose_allocation);
  base_transform_class->decide_allocation =
      GST_DEBUG_FUNCPTR(imx_video_convert_decide_allocation);
  video_filter_class->set_info =
      GST_DEBUG_FUNCPTR(imx_video_convert_set_info);
  video_filter_class->transform_frame =
      GST_DEBUG_FUNCPTR(imx_video_convert_transform_frame);

  base_transform_class->passthrough_on_same_caps = TRUE;
}

static void
gst_imx_video_convert_init (GstImxVideoConvert * imxvct)
{
  GstImxVideoConvertClass *klass =
      (GstImxVideoConvertClass *) G_OBJECT_GET_CLASS (imxvct);

  if (klass->in_plugin)
    imxvct->device = klass->in_plugin->create();

  if (imxvct->device) {
    if (imxvct->device->open(imxvct->device) < 0) {
      GST_ERROR ("Open video process device failed.");
    } else {
      imxvct->in_buf = NULL;
      imxvct->in_pool = NULL;
      imxvct->out_pool = NULL;
    }
  } else {
    GST_ERROR ("Create video process device failed.");
  }
}

static gboolean gst_imx_video_convert_register (GstPlugin * plugin)
{
  GTypeInfo tinfo = {
    sizeof (GstImxVideoConvertClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_imx_video_convert_class_init,
    NULL,
    NULL,
    sizeof (GstImxVideoConvert),
    0,
    (GInstanceInitFunc) gst_imx_video_convert_init,
  };

  GType type;
  gchar *t_name;

  const ImxVideoProcessDeviceInfo *in_plugin = imx_get_video_process_devices();

  while (in_plugin->name) {
    GST_LOG ("Registering %s [%s]", in_plugin->name, in_plugin->description);

    t_name = g_strdup_printf ("imxvideoconvert_%s", in_plugin->name);
    type = g_type_from_name (t_name);

    if (!type) {
      type = g_type_register_static (GST_TYPE_VIDEO_FILTER, t_name, &tinfo, 0);
      g_type_set_qdata (type, GST_IMX_VCT_PARAMS_QDATA, (gpointer) in_plugin);
    }

    if (!gst_element_register (plugin, t_name, IMX_GST_PLUGIN_RANK, type)) {
      GST_ERROR ("Failed to register %s", t_name);
      g_free (t_name);
      return FALSE;
    }
    g_free (t_name);

    in_plugin++;
  }

  return TRUE;
}

static gboolean plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (imxvideoconvert_debug, "imxvideoconvert", 0,
      "Freescale IMX Video Convert element");

  return gst_imx_video_convert_register (plugin);
}

IMX_GST_PLUGIN_DEFINE(imxvideoconvert, "IMX Video Convert Plugins",plugin_init);
