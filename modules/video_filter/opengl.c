/*****************************************************************************
 * opengl.c: OpenGL filter
 *****************************************************************************
 * Copyright (C) 2020 Videolabs
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_plugin.h>
#include <vlc_modules.h>
#include <vlc_picture.h>
#include <vlc_filter.h>
#include <vlc_opengl.h>
#include <vlc_window.h>
#include <vlc_vout_display.h>
#include <vlc_atomic.h>
#include "../video_output/opengl/vout_helper.h"
#include "../video_output/opengl/filters.h"
#include "../video_output/opengl/gl_api.h"
#include "../video_output/opengl/gl_common.h"
#include "../video_output/opengl/interop.h"

#define OPENGL_CFG_PREFIX "opengl-"
static const char *const opengl_options[] = { "filter", "gl", "gles", NULL };

typedef struct
{
    vlc_gl_t *gl;
    struct vlc_gl_filters *filters;
    struct vlc_gl_interop *interop;
    struct vlc_gl_api api;
} filter_sys_t;

static picture_t *Filter(filter_t *filter, picture_t *input)
{
    filter_sys_t *sys = filter->p_sys;

    if (vlc_gl_MakeCurrent(sys->gl) != VLC_SUCCESS)
        return NULL;

    int ret = vlc_gl_filters_UpdatePicture(sys->filters, input);
    if (ret != VLC_SUCCESS)
    {
        vlc_gl_ReleaseCurrent(sys->gl);
        return NULL;
    }

    ret = vlc_gl_filters_Draw(sys->filters);
    if (ret != VLC_SUCCESS)
    {
        vlc_gl_ReleaseCurrent(sys->gl);
        return NULL;
    }

    picture_t *output = vlc_gl_SwapOffscreen(sys->gl);
    vlc_gl_ReleaseCurrent(sys->gl);

    if (output == NULL)
        goto end;

    output->date = input->date;
    output->b_force = input->b_force;
    output->b_still = input->b_still;

    output->format.i_frame_rate =
        filter->fmt_out.video.i_frame_rate;
    output->format.i_frame_rate_base =
        filter->fmt_out.video.i_frame_rate_base;

end:
    picture_Release(input);
    return output;
}

static int
AppendVFlip(struct vlc_gl_filters *filters)
{
    config_chain_t *cfg = NULL;
    char *name;
    char *leftover = config_ChainCreate(&name, &cfg, "draw{vflip}");
    free(leftover);
    free(name);

    /* The OpenGL filter will do the real job, this file is just a filter_t
     * wrapper */
    struct vlc_gl_filter *glfilter =
        vlc_gl_filters_Append(filters, "draw", cfg);
    config_ChainDestroy(cfg);
    if (!glfilter)
        return VLC_EGENERIC;

    return VLC_SUCCESS;
}

static int
LoadFilters(filter_sys_t *sys, const char *glfilters_config)
{
    struct vlc_gl_filters *filters = sys->filters;
    assert(glfilters_config);

    const char *string = glfilters_config;
    char *next_module = NULL;
    do
    {
        char *name;
        config_chain_t *config = NULL;
        char *leftover = config_ChainCreate(&name, &config, string);

        free(next_module);
        next_module = leftover;
        string = next_module; /* const view of next_module */

        if (name == NULL)
            continue;

        struct vlc_gl_filter *filter =
            vlc_gl_filters_Append(filters, name, config);
        config_ChainDestroy(config);
        if (!filter)
        {
            msg_Err(sys->gl, "Could not load GL filter: %s", name);
            free(name);
            return VLC_EGENERIC;
        }

        free(name);
    } while (string);

    return VLC_SUCCESS;
}

static void Close( filter_t *filter )
{
    filter_sys_t *sys = filter->p_sys;

    if (sys != NULL)
    {
        vlc_gl_MakeCurrent(sys->gl);
        vlc_gl_filters_Delete(sys->filters);
        vlc_gl_interop_Delete(sys->interop);
        vlc_gl_ReleaseCurrent(sys->gl);

        vlc_gl_Delete(sys->gl);
        free(sys);
    }
}

static vlc_gl_t *CreateGL(vlc_object_t *obj, struct vlc_decoder_device *device,
                          unsigned width, unsigned height)
{

#ifdef USE_OPENGL_ES2
    char *opengles_name = var_InheritString(obj, "opengl-gles");
    vlc_gl_t *gl = vlc_gl_CreateOffscreen(obj, device, width, height,
                                         VLC_OPENGL_ES2, opengles_name, NULL);
    free(opengles_name);
    return gl;
#else
    vlc_gl_t *gl = NULL;
    char *opengl_name = var_InheritString(obj, "opengl-gl");
    char *opengles_name = var_InheritString(obj, "opengl-gles");

    const char *gl_module = opengl_name;
    const char *gles_module = opengles_name;

    if (EMPTY_STR(gl_module))
    {
        if (EMPTY_STR(opengles_name) || strcmp(opengles_name, "none") == 0)
        {
            gl_module = "any";
        }
        else
        {
            gl_module = NULL;
        }
    }

    if (EMPTY_STR(gles_module))
    {
        if (EMPTY_STR(opengl_name) || strcmp(opengl_name, "none") == 0)
        {
            gles_module = "any";
        }
        else
        {
            gles_module = NULL;
        }
    }

    if (opengl_name == NULL)
        opengl_name = strdup("");

    if (gl_module != NULL)
        gl = vlc_gl_CreateOffscreen(obj, device, width, height,
                                         VLC_OPENGL, gl_module, NULL);
    if (gl != NULL)
        goto end;

    if (gles_module != NULL)
        gl = vlc_gl_CreateOffscreen(obj, device, width, height,
                                         VLC_OPENGL_ES2, opengles_name, NULL);
end:
    free(opengl_name);
    free(opengles_name);
    return gl;
#endif
}

static int OpenOpenGL(filter_t *filter)
{
    filter_sys_t *sys
        = filter->p_sys
        = malloc(sizeof *sys);
    if (sys == NULL)
        return VLC_ENOMEM;

    config_ChainParse(filter, OPENGL_CFG_PREFIX, opengl_options, filter->p_cfg);

    unsigned width = filter->fmt_out.video.i_visible_width;
    unsigned height = filter->fmt_out.video.i_visible_height;

    struct vlc_decoder_device *device = filter_HoldDecoderDevice(filter);

    sys->gl = CreateGL(VLC_OBJECT(filter), device, width, height);

    /* The vlc_gl_t instance must have hold the device if it needs it. */
    if (device)
        vlc_decoder_device_Release(device);

    if (sys->gl == NULL)
    {
        msg_Err(filter, "Failed to create opengl context");
        goto gl_create_failure;
    }

    if (vlc_gl_MakeCurrent (sys->gl) != VLC_SUCCESS)
    {
        msg_Err(filter, "Failed to gl make current");
        assert(false);
        goto make_current_failure;
    }

    struct vlc_gl_api *api = &sys->api;
    if (vlc_gl_api_Init(api, sys->gl) != VLC_SUCCESS)
    {
        msg_Err(filter, "Failed to init vlc_gl_api");
        goto gl_api_failure;
    }

    sys->interop = vlc_gl_interop_New(sys->gl, filter->vctx_in,
                                      &filter->fmt_in.video);
    if (!sys->interop)
    {
        msg_Err(filter, "Could not create interop");
        goto gl_interop_failure;
    }

    char *glfilters_config =
        var_InheritString(filter, OPENGL_CFG_PREFIX "filter");
    if (!glfilters_config)
    {
        msg_Err(filter, "No filters requested");
        goto filter_config_failure;
    }


    sys->filters = vlc_gl_filters_New(sys->gl, api, sys->interop, ORIENT_NORMAL);
    if (!sys->filters)
    {
        msg_Err(filter, "Could not create filters");
        free(glfilters_config);
        goto filters_new_failure;
    }

    int ret;

    ret = LoadFilters(sys, glfilters_config);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "Could not load filters: %s", glfilters_config);
        free(glfilters_config);
        goto filters_load_failure;
    }
    free(glfilters_config);

    if (sys->gl->orientation == ORIENT_VFLIPPED)
    {
        /* OpenGL renders upside-down, add a filter to get the pixels in the
         * normal orientation */
        ret = AppendVFlip(sys->filters);
        if (ret != VLC_SUCCESS)
            return VLC_EGENERIC;
    }

    ret = vlc_gl_filters_InitFramebuffers(sys->filters);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "Could not init filters framebuffers");
        goto init_framebuffer_failure;
    }

    vlc_gl_filters_SetViewport(sys->filters, 0, 0, filter->fmt_out.video.i_visible_width, filter->fmt_out.video.i_visible_height);

    vlc_gl_ReleaseCurrent(sys->gl);

    static const struct vlc_filter_operations ops = {
        .filter_video = Filter,
        .close = Close,
    };
    filter->ops = &ops;
    filter->fmt_out.video.orientation = ORIENT_NORMAL;

    filter->fmt_out.video.i_chroma
        = filter->fmt_out.i_codec
        = sys->gl->offscreen_chroma_out;

    filter->vctx_out = sys->gl->offscreen_vctx_out;

    return VLC_SUCCESS;

init_framebuffer_failure:
filters_load_failure:
    vlc_gl_filters_Delete(sys->filters);

filters_new_failure:
filter_config_failure:
    vlc_gl_interop_Delete(sys->interop);

gl_interop_failure:
gl_api_failure:
    vlc_gl_ReleaseCurrent(sys->gl);

make_current_failure:
    vlc_gl_Delete(sys->gl);

gl_create_failure:
    free(sys);

    return VLC_EGENERIC;
}

#define FILTER_LIST_TEXT N_( "OpenGL filter" )
#define FILTER_LIST_LONGTEXT N_( "List of OpenGL filters to execute" )

vlc_module_begin()
    set_shortname( N_("opengl") )
    set_description( N_("Opengl filter executor") )
    set_subcategory( SUBCAT_VIDEO_VFILTER )
    add_shortcut( "opengl" )
    set_callback_video_filter( OpenOpenGL )
    add_module_list( "opengl-filter", "opengl filter", NULL,
                     FILTER_LIST_TEXT, FILTER_LIST_LONGTEXT )
    add_module( "opengl-gl", "opengl offscreen", "", "OpenGL provider",
                "OpenGL provider to execute the filters with" )
    add_module("opengl-gles", "opengl es2 offscreen", "", "OpenGL ES2 provider",
               "OpenGL ES2 provider to execute the filters with")
vlc_module_end()
