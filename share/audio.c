/*
 * Copyright (C) 2003 Robert Kooima
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

#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "config.h"
#include "audio.h"
#include "common.h"
#include "fs.h"
#include "fs_ov.h"
#include "log.h"

/*---------------------------------------------------------------------------*/

#define AUDIO_RATE 44100
#define AUDIO_CHAN 2

struct voice
{
    OggVorbis_File  vf;
    float          amp;
    float         damp;
    int           chan;
    int           play;
    int           loop;
    char         *name;
    struct voice *next;
};

static int   audio_state = 0;
static float sound_vol   = 1.0f;
static float music_vol   = 1.0f;

static SDL_AudioSpec spec;
static SDL_AudioStream *audio_stream = NULL;

static struct voice *music  = NULL;
static struct voice *queue  = NULL;
static struct voice *voices = NULL;
static short        *decode_buffer = NULL;
static Uint8        *mix_buffer = NULL;
static int           mix_buffer_size = 0;

static ov_callbacks callbacks = {
    fs_ov_read, fs_ov_seek, fs_ov_close, fs_ov_tell
};

/*---------------------------------------------------------------------------*/

#define LOG_VOLUME(v) ((float) pow((double) (v), 2.0))

#define MIX(d, s) {                           \
        int T = (int) (d) + (int) (s);        \
        if      (T >  32767) (d) =  32767;    \
        else if (T < -32768) (d) = -32768;    \
        else                 (d) = (short) T; \
    }

static int voice_step(struct voice *V, float volume, Uint8 *stream, int length)
{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    int order = 1;
#else
    int order = 0;
#endif

    short *obuf = (short *) stream;
    char  *ibuf = (char  *) decode_buffer;

    int i, b = 0, n = 1, c = 0, r = 0;

    /* Compute the total request size for the current stream. */

    if (V->chan == 1) r = length / 2;
    if (V->chan == 2) r = length    ;

    /* While data is coming in and data is still needed... */

    while (n > 0 && r > 0)
    {
        /* Read audio from the stream. */

        if ((n = (int) ov_read(&V->vf, ibuf, r, order, 2, 1, &b)) > 0)
        {
            /* Mix mono audio. */

            if (V->chan == 1)
                for (i = 0; i < n / 2; i += 1)
                {
                    short M = (short) (LOG_VOLUME(V->amp) * volume * decode_buffer[i]);

                    MIX(obuf[c], M); c++;
                    MIX(obuf[c], M); c++;

                    V->amp += V->damp;

                    if (V->amp < 0.0f) V->amp = 0.0;
                    if (V->amp > 1.0f) V->amp = 1.0;
                }

            /* Mix stereo audio. */

            if (V->chan == 2)
                for (i = 0; i < n / 2; i += 2)
                {
                    short L = (short) (LOG_VOLUME(V->amp) * volume * decode_buffer[i + 0]);
                    short R = (short) (LOG_VOLUME(V->amp) * volume * decode_buffer[i + 1]);

                    MIX(obuf[c], L); c++;
                    MIX(obuf[c], R); c++;

                    V->amp += V->damp;

                    if (V->amp < 0.0f) V->amp = 0.0;
                    if (V->amp > 1.0f) V->amp = 1.0;
                }

            r -= n;
        }
        else
        {
            /* We're at EOF.  Loop or end the voice. */

            if (V->loop)
            {
                ov_raw_seek(&V->vf, 0);
                n = 1;
            }
            else return 1;
        }
    }
    return 0;
}

static struct voice *voice_init(const char *filename, float a)
{
    struct voice *V;
    fs_file      fp;

    if (!filename)
        return NULL;

    /* Allocate and initialize a new voice structure. */

    if ((V = (struct voice *) calloc(1, sizeof (struct voice))))
    {
        if (!(V->name = strdup(filename)))
        {
            free(V);
            return NULL;
        }

        /* Attempt to open the named Ogg stream. */

        if ((fp = fs_open_read(filename)))
        {
            if (ov_open_callbacks(fp, &V->vf, NULL, 0, callbacks) == 0)
            {
                vorbis_info *info = ov_info(&V->vf, -1);

                /* On success, configure the voice. */

                V->amp  = a;
                V->damp = 0;
                V->chan = info->channels;
                V->play = 1;
                V->loop = 0;

                if (V->amp > 1.0f) V->amp = 1.0;
                if (V->amp < 0.0f) V->amp = 0.0;

                /* The file will be closed when the Ogg is cleared. */
                return V;
            }
            else fs_close(fp);
        }

        free(V->name);
        free(V);
    }
    return NULL;
}

static void voice_free(struct voice *V)
{
    if (V)
    {
        ov_clear(&V->vf);

        free(V->name);
        free(V);
    }
}

/*---------------------------------------------------------------------------*/

static void audio_step(Uint8 *stream, int length)
{
    struct voice *V = voices;
    struct voice *P = NULL;

    /* Zero the output buffer. */

    memset(stream, 0, length);

    /* Mix the background music. */

    if (music)
    {
        voice_step(music, music_vol, stream, length);

        /* If the track has faded out, move to a queued track. */

        if (music->amp <= 0.0f && music->damp < 0.0f && queue)
        {
            voice_free(music);
            music = queue;
            queue = NULL;
        }
    }

    /* Iterate over all active voices. */

    while (V)
    {
        /* Mix this voice. */

        if (V->play && voice_step(V, sound_vol, stream, length))
        {
            /* Delete a finished voice... */

            struct voice *T = V;

            if (P)
                V = P->next = V->next;
            else
                V = voices  = V->next;

            voice_free(T);
        }
        else
        {
            /* ... or continue to the next. */

            P = V;
            V = V->next;
        }
    }
}

/* Generated by Codex CLI 0.144.4 (GPT-5). */
static void audio_stream_callback(void *data, SDL_AudioStream *stream,
                                  int additional_amount, int total_amount)
{
    const int frame_size = AUDIO_CHAN * (int) sizeof (short);

    (void) data;
    (void) total_amount;

    while (additional_amount > 0)
    {
        int length = MIN(additional_amount, mix_buffer_size);

        length -= length % frame_size;
        if (length <= 0)
            break;

        audio_step(mix_buffer, length);

        if (!SDL_PutAudioStreamData(stream, mix_buffer, length))
        {
            log_printf("Failure to queue audio data (%s)\n", SDL_GetError());
            break;
        }

        additional_amount -= length;
    }
}

/*---------------------------------------------------------------------------*/

void audio_init(void)
{
    int frames = config_get_d(CONFIG_AUDIO_BUFF);
    char frame_hint[32];

    audio_state = 0;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        log_printf("Failure to initialize audio (%s)\n", SDL_GetError());
        return;
    }

    /* Configure the audio. */

    spec.format   = SDL_AUDIO_S16;
    spec.channels = AUDIO_CHAN;
    spec.freq     = AUDIO_RATE;

    mix_buffer_size = frames * AUDIO_CHAN * (int) sizeof (short);
    SDL_snprintf(frame_hint, sizeof (frame_hint), "%d", frames);
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frame_hint);

    /* Allocate decode and output buffers. */

    decode_buffer = (short *) malloc((size_t) mix_buffer_size);
    mix_buffer = (Uint8 *) malloc((size_t) mix_buffer_size);

    if (decode_buffer && mix_buffer)
    {
        audio_stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec,
            audio_stream_callback,
            NULL);

        if (audio_stream && SDL_ResumeAudioStreamDevice(audio_stream))
        {
            audio_state = 1;
        }
        else log_printf("Failure to open audio device (%s)\n", SDL_GetError());
    }
    else
        log_printf("Failure to allocate audio buffers\n");

    if (!audio_state)
    {
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;

        free(decode_buffer);
        free(mix_buffer);
        decode_buffer = NULL;
        mix_buffer = NULL;
        mix_buffer_size = 0;
    }

    /* Set the initial volumes. */

    audio_volume(config_get_d(CONFIG_SOUND_VOLUME),
                 config_get_d(CONFIG_MUSIC_VOLUME));
}

void audio_free(void)
{
    struct voice *V;

    audio_state = 0;

    /* Halt the audio stream before releasing shared mixer state. */

    SDL_DestroyAudioStream(audio_stream);
    audio_stream = NULL;

    /* Release the mixer buffers. */

    free(decode_buffer);
    free(mix_buffer);
    decode_buffer = NULL;
    mix_buffer = NULL;
    mix_buffer_size = 0;

    /* Free the voices. */

    voice_free(music);
    voice_free(queue);

    V = voices;

    while (V)
    {
        struct voice *N = V->next;
        voice_free(V);
        V = N;
    }

    voices = NULL;
    music = NULL;
    queue = NULL;

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void audio_play(const char *filename, float a)
{
    if (audio_state)
    {
        struct voice *V;

        /* If we're already playing this sound, preempt the running copy. */

        SDL_LockAudioStream(audio_stream);
        {
            for (V = voices; V; V = V->next)
                if (V->name && strcmp(V->name, filename) == 0)
                {
                    ov_raw_seek(&V->vf, 0);

                    V->amp = a;

                    if (V->amp > 1.0f) V->amp = 1.0;
                    if (V->amp < 0.0f) V->amp = 0.0;

                    SDL_UnlockAudioStream(audio_stream);
                    return;
                }
        }
        SDL_UnlockAudioStream(audio_stream);

        /* Create a new voice structure. */

        if ((V = voice_init(filename, a)))
        {
            /* Add it to the list of sounding voices. */

            SDL_LockAudioStream(audio_stream);
            {
                V->next = voices;
                voices  = V;
            }
            SDL_UnlockAudioStream(audio_stream);
        }
    }
}

/*---------------------------------------------------------------------------*/

/* Generated by Codex CLI 0.144.4 (GPT-5). */
static void audio_music_play_locked(const char *filename)
{
    if ((music = voice_init(filename, 0.0f)))
    {
        music->loop = 1;
    }
}

/* Generated by Codex CLI 0.144.4 (GPT-5). */
static void audio_music_queue_locked(const char *filename, float t)
{
    if ((queue = voice_init(filename, 0.0f)))
    {
        queue->loop = 1;

        if (t > 0.0f)
            queue->damp = +1.0f / (AUDIO_RATE * t);
    }
}

void audio_music_stop(void)
{
    if (audio_state)
    {
        SDL_LockAudioStream(audio_stream);
        {
            if (music)
            {
                voice_free(music);
            }
            music = NULL;
        }
        SDL_UnlockAudioStream(audio_stream);
    }
}

/*---------------------------------------------------------------------------*/

void audio_music_fade_out(float t)
{
    if (!audio_state || t <= 0.0f)
        return;

    SDL_LockAudioStream(audio_stream);
    {
        if (music) music->damp = -1.0f / (AUDIO_RATE * t);
    }
    SDL_UnlockAudioStream(audio_stream);
}

void audio_music_fade_in(float t)
{
    if (!audio_state || t <= 0.0f)
        return;

    SDL_LockAudioStream(audio_stream);
    {
        if (music) music->damp = +1.0f / (AUDIO_RATE * t);
    }
    SDL_UnlockAudioStream(audio_stream);
}

void audio_music_fade_to(float t, const char *filename)
{
    if (!audio_state || !filename || !*filename)
        return;

    SDL_LockAudioStream(audio_stream);

    if (music)
    {
        if (!music->name || strcmp(filename, music->name) != 0)
        {
            if (t > 0.0f)
            {
                music->damp = -1.0f / (AUDIO_RATE * t);

                voice_free(queue);
                queue = NULL;
                audio_music_queue_locked(filename, t);
            }
            else
            {
                voice_free(music);
                voice_free(queue);
                music = NULL;
                queue = NULL;
                audio_music_play_locked(filename);

                if (music)
                    music->amp = 1.0f;
            }
        }
        else
        {
            /*
             * We're fading to the current track.  Chances are,
             * whatever track is still in the queue, we don't want to
             * hear it anymore.
             */

            if (queue)
            {
                voice_free(queue);
                queue = NULL;
            }

            if (t > 0.0f)
                music->damp = +1.0f / (AUDIO_RATE * t);
            else
                music->amp = 1.0f;
        }
    }
    else
    {
        audio_music_play_locked(filename);

        if (music && t > 0.0f)
            music->damp = +1.0f / (AUDIO_RATE * t);
        else if (music)
            music->amp = 1.0f;
    }

    SDL_UnlockAudioStream(audio_stream);
}

/*
 * Logarithmic volume control.
 */
void audio_volume(int s, int m)
{
    float sl = (float) s / 10.0f;
    float ml = (float) m / 10.0f;

    if (audio_state)
        SDL_LockAudioStream(audio_stream);

    sound_vol = LOG_VOLUME(sl);
    music_vol = LOG_VOLUME(ml);

    if (audio_state)
        SDL_UnlockAudioStream(audio_stream);
}

/*---------------------------------------------------------------------------*/
