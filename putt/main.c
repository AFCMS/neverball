/*
 * Copyright (C) 2003 Robert Kooima
 *
 * NEVERPUTT is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

/*---------------------------------------------------------------------------*/

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include "version.h"
#include "glext.h"
#include "audio.h"
#include "image.h"
#include "state.h"
#include "config.h"
#include "video.h"
#include "mtrl.h"
#include "course.h"
#include "hole.h"
#include "game.h"
#include "gui.h"
#include "hmd.h"
#include "fs.h"
#include "joy.h"
#include "log.h"
#include "common.h"
#include "lang.h"
#include "key.h"

#include "st_conf.h"
#include "st_all.h"

const char TITLE[] = "Neverputt";
const char ICON[] = "icon/neverputt.png";

/*---------------------------------------------------------------------------*/

static void shot(void)
{
    static char filename[MAXSTR];
    sprintf(filename, "Screenshots/screen%05d.png", config_screenshot());
    video_snap(filename);
}

/*---------------------------------------------------------------------------*/

static void toggle_wire(void)
{
#if !ENABLE_OPENGLES
    static int wire = 0;

    if (wire)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_TEXTURE_2D);
        wire = 0;
    }
    else
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDisable(GL_TEXTURE_2D);
        wire = 1;
    }
#endif
}

/*---------------------------------------------------------------------------*/

static int loop(void)
{
    SDL_Event e;
    int d = 1;
    int c;

    int ax, ay, dx, dy;

    while (d && SDL_PollEvent(&e))
    {
        if (e.type == SDL_EVENT_QUIT)
            return 0;

        switch (e.type)
        {
        case SDL_EVENT_MOUSE_MOTION :
            /* Convert to OpenGL coordinates. */

            ax = +e.motion.x;
            ay = -e.motion.y + video.window_h;
            dx = +e.motion.xrel;
            dy = -e.motion.yrel;

            /* Convert to pixels. */

            ax = ROUND(ax * video.device_scale);
            ay = ROUND(ay * video.device_scale);
            dx = ROUND(dx * video.device_scale);
            dy = ROUND(dy * video.device_scale);

            st_point(ax, ay, dx, dy);

            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN :
            d = st_click(e.button.button, 1);
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP :
            d = st_click(e.button.button, 0);
            break;

        case SDL_EVENT_KEY_DOWN :

            c = e.key.key;

#ifdef SDL_PLATFORM_APPLE
            if (c == SDLK_Q && e.key.mod & SDL_KMOD_GUI)
            {
                d = 0;
                break;
            }
#endif
#ifdef _WIN32
            if (c == SDLK_F4 && e.key.mod & SDL_KMOD_ALT)
            {
                d = 0;
                break;
            }
#endif

            switch (c)
            {
            case KEY_SCREENSHOT:
                shot();
                break;
            case KEY_FPS:
                config_tgl_d(CONFIG_FPS);
                break;
            case KEY_WIREFRAME:
                toggle_wire();
                break;
            case KEY_FULLSCREEN:
                video_fullscreen(!config_get_d(CONFIG_FULLSCREEN));
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_A), 1);
                break;
            case SDLK_ESCAPE:
                if (video_get_grab())
                    d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_START), 1);
                else
                    d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_B), 1);
                break;

            default:
                if (config_tst_d(CONFIG_KEY_FORWARD, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_Y0), -1.0f);
                else if (config_tst_d(CONFIG_KEY_BACKWARD, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_Y0), +1.0f);
                else if (config_tst_d(CONFIG_KEY_LEFT, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_X0), -1.0f);
                else if (config_tst_d(CONFIG_KEY_RIGHT, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_X0), +1.0f);
                else
                    d = st_keybd(e.key.key, 1);
            }
            break;

        case SDL_EVENT_KEY_UP :

            c = e.key.key;

            switch (c)
            {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_A), 0);
                break;
            case SDLK_ESCAPE:
                if (video_get_grab())
                    d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_START), 0);
                else
                    d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_B), 0);
                break;

            default:
                if (config_tst_d(CONFIG_KEY_FORWARD, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_Y0), 0.0f);
                else if (config_tst_d(CONFIG_KEY_BACKWARD, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_Y0), 0.0f);
                else if (config_tst_d(CONFIG_KEY_LEFT, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_X0), 0.0f);
                else if (config_tst_d(CONFIG_KEY_RIGHT, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_X0), 0.0f);
                else
                    d = st_keybd(e.key.key, 0);
            }
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST :
            if (video_get_grab())
                goto_pause(&st_over);
            break;

        case SDL_EVENT_WINDOW_MOVED :
            if (config_get_d(CONFIG_DISPLAY) != video_display())
                config_set_d(CONFIG_DISPLAY, video_display());
            break;

        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN :
            config_set_d(CONFIG_FULLSCREEN, 1);
            break;

        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN :
            config_set_d(CONFIG_FULLSCREEN, 0);
            break;

        case SDL_EVENT_WINDOW_RESIZED :
            video_resize(e.window.data1, e.window.data2);
            gui_resize();
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED :
            video_resize(video.window_w, video.window_h);
            gui_resize();
            break;

        case SDL_EVENT_JOYSTICK_AXIS_MOTION :
            joy_axis(e.jaxis.which, e.jaxis.axis, JOY_VALUE(e.jaxis.value));
            break;

        case SDL_EVENT_JOYSTICK_BUTTON_DOWN :
            d = joy_button(e.jbutton.which, e.jbutton.button, 1);
            break;

        case SDL_EVENT_JOYSTICK_BUTTON_UP :
            d = joy_button(e.jbutton.which, e.jbutton.button, 0);
            break;

        case SDL_EVENT_JOYSTICK_ADDED :
            joy_add(e.jdevice.which);
            break;

        case SDL_EVENT_JOYSTICK_REMOVED :
            joy_remove(e.jdevice.which);
            break;
        }
    }
    return d;
}

/*---------------------------------------------------------------------------*/

static char *opt_data;
static char *opt_hole;

static void opt_parse(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0)
        {
            if (++i < argc)
                opt_data = argv[i];
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--hole") == 0)
        {
            if (++i < argc)
                opt_hole = argv[i];
        }
    }

    if (argc == 2)
    {
        size_t len = strlen(argv[1]);

        if (len > 4)
        {
            char *ext = argv[1] + len - 4;

            if (strcmp(ext, ".map") == 0)
                strcpy(ext, ".sol");

            if (strcmp(ext, ".sol") == 0)
                opt_hole = argv[1];
        }
    }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    int camera = 0;

    if (!fs_init(argc > 0 ? argv[0] : NULL))
    {
        fprintf(stderr, "Failure to initialize virtual file system (%s)\n",
                fs_error());
        return 1;
    }

    srand((int) time(NULL));

    opt_parse(argc, argv);

    config_paths(opt_data);
    log_init("Neverputt" VERSION, "neverputt.log");
    fs_mkdir("Screenshots");

    if (SDL_Init(SDL_INIT_VIDEO))
    {
        joy_init();

        config_init();
        config_load();

        /* Initialize localization. */

        lang_init();

        /* Cache Neverball's camera setting. */

        camera = config_get_d(CONFIG_CAMERA);

        /* Initialize the audio. */

        audio_init();

        /* Initialize the video. */

        if (video_init())
        {
            Uint64 t1, t0 = SDL_GetTicks();

            /* Material system. */

            mtrl_init();

            /* Run the main game loop. */

            init_state(&st_null);

            if (opt_hole)
            {
                const char *path = fs_resolve(opt_hole);
                int loaded = 0;

                if (path)
                {
                    hole_init(NULL);

                    if (hole_load(0, path) &&
                        hole_load(1, path) &&
                        hole_goto(1, 1))
                    {
                        goto_state(&st_next);
                        loaded = 1;
                    }
                }

                if (!loaded)
                    goto_state(&st_title);
            }
            else
                goto_state(&st_title);

            while (loop())
                if ((t1 = SDL_GetTicks()) > t0)
                {
                    st_timer((float) (t1 - t0) / 1000.f);
                    hmd_step();
                    st_paint(0.001f * (float) t1);
                    video_swap();

                    t0 = t1;

                    if (config_get_d(CONFIG_NICE))
                        SDL_Delay(1);
                }

            mtrl_quit();
        }

        /* Restore Neverball's camera setting. */

        config_set_d(CONFIG_CAMERA, camera);
        config_save();

        joy_quit();

        SDL_Quit();
    }
    else log_printf("Failure to initialize SDL (%s)\n", SDL_GetError());

    return 0;
}

/*---------------------------------------------------------------------------*/
