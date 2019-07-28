// audio - v3 - simple audio playback library - public domain - by Patrick Gaskin
#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef AUDIO_NO_AUTODETECT
#ifdef TINYALSA_PCM_H
#define AUDIO_SUPPORT_ALSA
#endif

#ifdef foosimplehfoo
#define AUDIO_SUPPORT_PULSE
#endif

#ifdef STB_VORBIS_INCLUDE_STB_VORBIS_H
#define AUDIO_SUPPORT_VORBIS
#endif

#ifdef dr_flac_h
#define AUDIO_SUPPORT_FLAC
#endif

#ifdef dr_wav_h
#define AUDIO_SUPPORT_WAV
#endif

#ifdef dr_mp3_h
#define AUDIO_SUPPORT_MP3
#endif
#endif

// audio_output_t implements an output for audio_play.
typedef struct audio_output_t {
    // open opens the audio device with the provided configuration. On
    // error, NULL should be returned and errno set. An error should be returned
    // if the provided channels and rate cannot be met. The returned object
    // should be ready to use as-is.
    void* (*open)(void* cfg, int channels, int rate);
    // close closes the audio device and stops playback.
    void  (*close)(void* obj);
    // stop stops playback immediately.
    void  (*stop)(void* obj);
    // write_frames_s16le writes audio to the device. On error, it should
    // return a negative number. Any other number is considered success. buf_sz
    // is equal to frame_count*channels*sizeof(buf[0]).
    int   (*write_frames_s16le)(void* obj, int16_t *buf, size_t buf_sz, int frame_count);
} audio_output_t;

// audio_format_t implements a format for audio_play.
typedef struct audio_format_t {
    // open opens a filename and returns the number of channels, sample
    // rate, and a pointer to the object which will be passed to the other funcs.
    void* (*open)(const char* filename, int* channels_out, int* rate_out);
    // close closes the object returned by audio_open.
    void  (*close)(void* obj);
    // read_frames_s16le reads audio samples into buf of size buf_sz and
    // returns the number of frames read (samples divided by channels). Zero
    // means the stream has ended, and a negative number is an error. The
    // channel count is the same as the one returned from audio_open.
    int   (*read_frames_s16le)(void* obj, int16_t* buf, size_t buf_sz, int channels);
} audio_format_t;

// audio_format returns the audio_format_t for the provided filename if it is
// recognized.
const audio_format_t* audio_format(const char* filename);

// audio_play plays a specified audio file on a device. A nonzero number is
// returned on error. If play_until is not NULL, the audio plays until it
// (called with the argument play_until_data) returns true.
int audio_play(const audio_output_t output, void* output_cfg, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data);

#ifdef AUDIO_SUPPORT_ALSA
// audio_play_alsa plays audio on an ALSA device. If using pulseaudio, you may
// need to run your application with pasuspender.
int audio_play_alsa(int card, int device, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data);
const audio_output_t audio_output_alsa;
typedef struct audio_output_cfg_alsa_t {
    int card;
    int device;
} audio_output_cfg_alsa_t;
#endif

#ifdef AUDIO_SUPPORT_PULSE
int audio_play_pulse(const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data);
const audio_output_t audio_output_pulse;
#endif

#ifdef AUDIO_SUPPORT_VORBIS
const audio_format_t audio_format_vorbis;
#endif

#ifdef AUDIO_SUPPORT_FLAC
const audio_format_t audio_format_flac;
#endif

#ifdef AUDIO_SUPPORT_WAV
const audio_format_t audio_format_wav;
#endif

#ifdef AUDIO_SUPPORT_MP3
const audio_format_t audio_format_mp3;
#endif
#endif

#ifdef AUDIO_IMPLEMENTATION
#include <stdio.h>
#include <errno.h>
#include <string.h>

const audio_format_t* audio_format(const char* filename) {
    char* dot = strrchr(filename, '.');
    #define audio_format_x(fmt, ext) if (dot && !strcasecmp(dot, ext)) return &fmt;
    #ifdef AUDIO_SUPPORT_VORBIS
    audio_format_x(audio_format_vorbis, ".ogg");
    audio_format_x(audio_format_vorbis, ".oga");
    #endif
    #ifdef AUDIO_SUPPORT_FLAC
    audio_format_x(audio_format_flac, ".flac");
    #endif
    #ifdef AUDIO_SUPPORT_WAV
    audio_format_x(audio_format_wav, ".wav");
    audio_format_x(audio_format_wav, ".wave");
    audio_format_x(audio_format_wav, ".riff");
    #endif
    #ifdef AUDIO_SUPPORT_MP3
    audio_format_x(audio_format_mp3, ".mp3");
    #endif
    #undef audio_format_x
    return NULL;
}

int audio_play(const audio_output_t output, void* output_cfg, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data) {
    int err, channels, rate, frame_count;
    int16_t buf[4096];
    void *fmt, *out;

    if ((fmt = format.open(filename, &channels, &rate)) == NULL)
        return 1;

    if ((out = output.open(output_cfg, channels, rate)) == NULL) {
        format.close(fmt);
        return 2;
    }

    while ((frame_count = format.read_frames_s16le(fmt, buf, 4096, channels))) {
        if ((err = output.write_frames_s16le(out, buf, (size_t)(frame_count*channels)*sizeof(buf[0]), frame_count)) < 0) {
            output.close(out);
            format.close(fmt);
            return err;
        } else if (play_until && (*play_until)(play_until_data)) {
            output.stop(out);
            break;
        }
    }

    output.close(out);
    format.close(fmt);
    return 0;
}

#define __audio_output__play0(name)           int audio_play_ ## name(const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data) { return audio_play(audio_output_ ## name, NULL, format, filename, play_until, play_until_data); };
#define __audio_output__play(name, conv, ...) int audio_play_ ## name(__VA_ARGS__, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data) { return audio_play(audio_output_ ## name, conv, format, filename, play_until, play_until_data); };
#define __audio_output__open(name)  static void* audio_open_ ## name(void* cfg, int channels, int rate)
#define __audio_output__close(name) static void  audio_close_ ## name(void* obj)
#define __audio_output__stop(name)  static void  audio_stop_ ## name(void* obj)
#define __audio_output__write(name) static int   audio_write_frames_s16le_ ## name(void* obj, int16_t *buf, size_t buf_sz, int frame_count)
#define __audio_output(name)        const audio_output_t audio_output_ ## name = {\
    .open               = audio_open_ ## name,\
    .close              = audio_close_ ## name,\
    .stop               = audio_stop_ ## name,\
    .write_frames_s16le = audio_write_frames_s16le_ ## name,\
}

#define __audio_format__open(format)  static void* audio_open_ ## format(const char* filename, int* channels_out, int* rate_out)
#define __audio_format__close(format) static void  audio_close_ ## format(void* obj)
#define __audio_format__read(format)  static int   audio_read_frames_s16le_ ## format(void* obj, int16_t* buf, size_t buf_sz, int channels)
#define __audio_format(format)        const audio_format_t audio_format_ ## format = {\
    .open              = audio_open_ ## format,\
    .close             = audio_close_ ## format,\
    .read_frames_s16le = audio_read_frames_s16le_ ## format,\
}

#ifdef AUDIO_SUPPORT_ALSA
__audio_output__play(alsa, (&(audio_output_cfg_alsa_t){ .card = card, .device = device }), int card, int device);
__audio_output__open(alsa) {
    audio_output_cfg_alsa_t *acfg = (audio_output_cfg_alsa_t*)(cfg);
    struct pcm *obj = pcm_open(acfg->card, acfg->device, PCM_OUT, &(struct pcm_config) {
        .channels = channels,
        .rate = rate,
        .format = PCM_FORMAT_S16_LE,
        .period_size = 1024, // default
        .period_count = 2 // default
    });
    return pcm_is_ready(obj) ? obj : NULL;
}
__audio_output__close(alsa) { pcm_close((struct pcm*)(obj)); }
__audio_output__stop(alsa)  { pcm_stop((struct pcm*)(obj)); }
__audio_output__write(alsa) { return pcm_writei((struct pcm*)(obj), buf, frame_count); }
__audio_output(alsa);
#endif

#ifdef AUDIO_SUPPORT_PULSE
__audio_output__play0(pulse);
__audio_output__open(pulse) {
    return pa_simple_new(NULL, "audio.h", PA_STREAM_PLAYBACK, NULL, "audio", &(pa_sample_spec) {
        .channels = channels,
        .rate = rate,
        .format = PA_SAMPLE_S16NE,
    }, NULL, NULL, NULL);
}
__audio_output__close(pulse) { pa_simple_free((pa_simple*)(obj)); }
__audio_output__stop(pulse)  { pa_simple_flush((pa_simple*)(obj), NULL); }
__audio_output__write(pulse) { return pa_simple_write((pa_simple*)(obj), buf, buf_sz, NULL); }
__audio_output(pulse);
#endif

#ifdef AUDIO_SUPPORT_VORBIS
__audio_format__open(vorbis) {
    stb_vorbis* v = stb_vorbis_open_filename(filename, NULL, NULL);
    stb_vorbis_info i = stb_vorbis_get_info(v);
    *channels_out = i.channels;
    *rate_out = i.sample_rate;
    return v;
}
__audio_format__close(vorbis) { stb_vorbis_close((stb_vorbis*)(obj)); }
__audio_format__read(vorbis)  { return stb_vorbis_get_samples_short_interleaved((stb_vorbis*)(obj), channels, buf, buf_sz/channels); }
__audio_format(vorbis);
#endif

#ifdef AUDIO_SUPPORT_FLAC
__audio_format__open(flac) {
    drflac* f = drflac_open_file(filename);
    *channels_out = f->channels;
    *rate_out = f->sampleRate;
    return f;
}
__audio_format__close(flac) { drflac_close((drflac*)(obj)); }
__audio_format__read(flac)  { return drflac_read_pcm_frames_s16((drflac*)(obj), buf_sz/channels, buf); }
__audio_format(flac);
#endif

#ifdef AUDIO_SUPPORT_WAV
__audio_format__open(wav) {
    drwav* f = drwav_open_file(filename);
    *channels_out = f->channels;
    *rate_out = f->sampleRate;
    return f;
}
__audio_format__close(wav) { drwav_close((drwav*)(obj)); }
__audio_format__read(wav)  { return drwav_read_pcm_frames_s16((drwav*)(obj), buf_sz/channels, buf); }
__audio_format(wav);
#endif

#ifdef AUDIO_SUPPORT_MP3
__audio_format__open(mp3) {
    drmp3* f = malloc(sizeof(drmp3));
    if (!drmp3_init_file(f, filename, NULL))
        return NULL;
    *channels_out = f->channels;
    *rate_out = f->sampleRate;
    return f;
}
__audio_format__close(mp3) { drmp3_uninit((drmp3*)(obj)); free(obj); }
__audio_format__read(mp3)  { return drmp3_read_pcm_frames_s16((drmp3*)(obj), buf_sz/channels, buf); }
__audio_format(mp3);
#endif
#endif
