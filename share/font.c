/*
 * Copyright (C) 2014 Neverball authors
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

#include <string.h>
#include <stdlib.h>

#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL_iostream.h>

#include "font.h"
#include "common.h"
#include "fs.h"

/*---------------------------------------------------------------------------*/

int font_load(struct font *ft, const char *path, int sizes[FONT_SIZE_MAX])
{
    if (ft && path && *path)
    {
        memset(ft, 0, sizeof (*ft));

        if ((ft->data = fs_load(path, &ft->datalen)))
        {
            int i;

            SAFECPY(ft->path, path);

            if ((ft->rwops = SDL_IOFromConstMem(ft->data, ft->datalen)))
            {
                int opened = 0;

                for (i = 0; i < ARRAYSIZE(ft->ttf); i++)
                {
                    SDL_SeekIO(ft->rwops, 0, SEEK_SET);
                    if ((ft->ttf[i] = TTF_OpenFontIO(ft->rwops, false,
                                                    (float) sizes[i])))
                        opened++;
                }

                if (opened > 0)
                    return 1;
            }

            font_free(ft);
        }
    }
    return 0;
}

void font_free(struct font *ft)
{
    if (ft)
    {
        int i;

        for (i = 0; i < ARRAYSIZE(ft->ttf); i++)
            if (ft->ttf[i])
                TTF_CloseFont(ft->ttf[i]);

        if (ft->rwops)
            SDL_CloseIO(ft->rwops);

        if (ft->data)
            free(ft->data);

        memset(ft, 0, sizeof (*ft));
    }
}

int font_init(void)
{
    return (TTF_Init());
}

void font_quit(void)
{
    TTF_Quit();
}

/*---------------------------------------------------------------------------*/
