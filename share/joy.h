#ifndef JOY_H
#define JOY_H

#include <SDL3/SDL_joystick.h>

void joy_init(void);
void joy_quit(void);

void joy_add(SDL_JoystickID device);
void joy_remove(SDL_JoystickID instance);
int  joy_button(SDL_JoystickID instance, int b, int d);
void joy_axis(SDL_JoystickID instance, int a, float v);

#endif
