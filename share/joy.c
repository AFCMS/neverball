/*
 * Copyright (C) 2019-2025 Jānis Rūcis
 *
 * NEVERBALL is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_joystick.h>

#include "joy.h"
#include "state.h"
#include "common.h"
#include "log.h"

#define JOY_MAX 16

static struct
{
    SDL_Joystick *joy;
    SDL_JoystickID id;
} joysticks[JOY_MAX];

static SDL_JoystickID joy_curr = 0;

/*
 * Initialize joystick state.
 */
void joy_init(void)
{
    size_t i = 0;

    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK))
    {
        log_printf("Failure to initialize joystick (%s)\n", SDL_GetError());
        return;
    }

    for (i = 0; i < ARRAYSIZE(joysticks); ++i)
    {
        joysticks[i].joy = NULL;
        joysticks[i].id = 0;
    }

    SDL_SetJoystickEventsEnabled(true);
}

/*
 * Close all joysticks.
 */
void joy_quit(void)
{
    size_t i = 0;

    for (i = 0; i < ARRAYSIZE(joysticks); ++i)
        if (joysticks[i].joy)
            joy_remove(joysticks[i].id);

    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

/*
 * Handle joystick add event.
 */
void joy_add(SDL_JoystickID device)
{
    log_printf("Joystick added (device %u)\n", device);

    SDL_Joystick *joy = SDL_OpenJoystick(device);

    if (joy)
    {
        size_t i = 0;

        for (i = 0; i < ARRAYSIZE(joysticks); ++i)
        {
            if (joysticks[i].joy == NULL)
            {
                joysticks[i].joy = joy;
                joysticks[i].id = SDL_GetJoystickID(joy);
                log_printf("Joystick opened (instance %u)\n", joysticks[i].id);

                joy_curr = joysticks[i].id;
                log_printf("Joystick %u made current via device addition\n", joy_curr);

                break;
            }
        }

        if (i == ARRAYSIZE(joysticks))
        {
            SDL_CloseJoystick(joy);
            log_printf("Joystick %u not opened, %zu open joysticks reached\n",
                       device, ARRAYSIZE(joysticks));
        }
    }
}

/*
 * Handle joystick remove event.
 */
void joy_remove(SDL_JoystickID instance)
{
    size_t i;

    log_printf("Joystick removed (instance %u)\n", instance);

    for (i = 0; i < ARRAYSIZE(joysticks); ++i)
    {
        if (joysticks[i].id == instance)
        {
            SDL_CloseJoystick(joysticks[i].joy);
            joysticks[i].joy = NULL;
            joysticks[i].id = 0;
            log_printf("Joystick closed (instance %u)\n", instance);
        }
    }

    if (joy_curr == instance)
        joy_curr = 0;
}

/*
 * Handle joystick button event.
 */
int joy_button(SDL_JoystickID instance, int b, int d)
{
    if (joy_curr == instance)
    {
        /* Process button event normally. */
        return st_buttn(b, d);
    }
    else
    {
        /* Do not process button event, but make joystick current. */
        joy_curr = instance;
        log_printf("Joystick %u made current via button press\n", joy_curr);
        return 1;
    }
}

/*
 * Handle joystick axis event.
 */
void joy_axis(SDL_JoystickID instance, int a, float v)
{
    if (joy_curr == instance)
    {
        /* Process axis events from current joystick only. */
        st_stick(a, v);
    }
}
