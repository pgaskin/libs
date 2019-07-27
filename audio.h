// audio - v2 - simple audio playback library - public domain - by Patrick Gaskin
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
    // audio_open opens the audio device with the provided configuration. On
    // error, NULL should be returned and errno set. An error should be returned
    // if the provided channels and rate cannot be met. The returned object
    // should be ready to use as-is.
    void* (*audio_open)(void* cfg, int channels, int rate);
    // audio_close closes the audio device and stops playback.
    void  (*audio_close)(void* obj);
    // audio_stop stops playback immediately.
    void  (*audio_stop)(void* obj);
    // audio_write_frames_s16le writes audio to the device. On error, it should
    // return a negative number. Any other number is considered success. The 
    // channels will be the same as that passed in audio_open.
    int   (*audio_write_frames_s16le)(void* obj, int16_t *buf, int frame_count, int channels);
} audio_output_t;

// audio_format_t implements a format for audio_play.
typedef struct audio_format_t {
    // audio_open opens a filename and returns the number of channels, sample
    // rate, and a pointer to the object which will be passed to the other funcs.
    void* (*audio_open)(const char* filename, int* channels_out, int* rate_out);
    // audio_close closes the object returned by audio_open.
    void  (*audio_close)(void* obj);
    // audio_read_frames_s16le reads audio samples into buf of size buf_sz and
    // returns the number of frames read (samples divided by channels). Zero
    // means the stream has ended, and a negative number is an error. The
    // channel count is the same as the one returned from audio_open.
    int   (*audio_read_frames_s16le)(void* obj, int16_t* buf, size_t buf_sz, int channels);
} audio_format_t;

// audio_format returns the audio_format_t for the provided filename if it is
// recognized.
const audio_format_t* audio_format(const char* filename);

// audio_play plays a specified audio file on a device. A nonzero number is
// returned on error. If play_until is not NULL, the audio plays until it
// (called with the argument play_until_data) returns true.
int audio_play(const audio_output_t output, void* output_cfg, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data);

#ifdef AUDIO_SUPPORT_ALSA
const audio_output_t audio_output_alsa;
typedef struct audio_output_cfg_alsa_t {
    int card;
    int device;
} audio_output_cfg_alsa_t;

// audio_play_alsa plays audio on an ALSA device. If using pulseaudio, you may
// need to run your application with pasuspender.
int audio_play_alsa(int card, int device, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data);
#endif

#ifdef AUDIO_SUPPORT_PULSE
const audio_output_t audio_output_pulse;
int audio_play_pulse(const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data);
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
    char* dot = strchr(filename, '.');
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
    void *obj, *out;

    if ((obj = format.audio_open(filename, &channels, &rate)) == NULL)
        return 1;

    if ((out = output.audio_open(output_cfg, channels, rate)) == NULL) {
        format.audio_close(obj);
        return 2;
    }

    while ((frame_count = format.audio_read_frames_s16le(obj, buf, 4096, channels))) {
        if ((err = output.audio_write_frames_s16le(out, buf, frame_count, channels)) < 0) {
            output.audio_close(out);
            format.audio_close(obj);
            return err;
        } else if (play_until && (*play_until)(play_until_data)) {
            output.audio_stop(out);
            break;
        }
    }

    output.audio_close(out);
    format.audio_close(obj);
    return 0;
}

#define out__impl__open(name)  static void* audio_open_ ## name(void* cfg, int channels, int rate)
#define out__impl__close(name) static void  audio_close_ ## name(void* obj)
#define out__impl__stop(name)  static void  audio_stop_ ## name(void* obj)
#define out__impl__write(name) static int   audio_write_frames_s16le_ ## name(void* obj, int16_t *buf, int frame_count, int channels)
#define out__impl(name)        const audio_output_t audio_output_ ## name = {\
    .audio_open               = audio_open_ ## name,\
    .audio_close              = audio_close_ ## name,\
    .audio_stop               = audio_stop_ ## name,\
    .audio_write_frames_s16le = audio_write_frames_s16le_ ## name,\
}

#define fmt__impl__open(format)  static void* audio_open_ ## format(const char* filename, int* channels_out, int* rate_out)
#define fmt__impl__close(format) static void  audio_close_ ## format(void* obj)
#define fmt__impl__read(format)  static int   audio_read_frames_s16le_ ## format(void* obj, int16_t* buf, size_t buf_sz, int channels)
#define fmt__impl(format)        const audio_format_t audio_format_ ## format = {\
    .audio_open              = audio_open_ ## format,\
    .audio_close             = audio_close_ ## format,\
    .audio_read_frames_s16le = audio_read_frames_s16le_ ## format,\
}

#ifdef AUDIO_SUPPORT_ALSA
out__impl__open(alsa) {
    audio_output_cfg_alsa_t *acfg = (audio_output_cfg_alsa_t*)(cfg);
    struct pcm *obj = pcm_open(acfg->card, acfg->device, PCM_OUT, &(struct pcm_config) {
        .channels = channels,
        .rate = rate,
        .format = PCM_FORMAT_S16_LE,
        .period_size = 1024, // default
        .period_count = 2 // default
    });
    if (obj == NULL)
        return NULL;
    if (!pcm_is_ready(obj)) {
        pcm_close(obj);
        return NULL;
    }
    return (void*)(obj);
}
out__impl__close(alsa)  { pcm_close((struct pcm*)(obj)); }
out__impl__stop(alsa)  { pcm_stop((struct pcm*)(obj)); }
out__impl__write(alsa) { return pcm_writei((struct pcm*)(obj), buf, frame_count); }
out__impl(alsa);

int audio_play_alsa(int card, int device, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data) {
    return audio_play(audio_output_alsa, &(audio_output_cfg_alsa_t){ .card = card, .device = device, }, format, filename, play_until, play_until_data);
}
#endif

#ifdef AUDIO_SUPPORT_PULSE
out__impl__open(pulse) {
    pa_simple *obj = pa_simple_new(NULL, "audio.h", PA_STREAM_PLAYBACK, NULL, "audio", &(pa_sample_spec) {
        .channels = channels,
        .rate = rate,
        .format = PA_SAMPLE_S16NE,
    }, NULL, NULL, NULL);
    if (obj == NULL)
        return NULL;
    return (void*)(obj);
}
out__impl__close(pulse)  { pa_simple_free((pa_simple*)(obj)); }
out__impl__stop(pulse)  { pa_simple_flush((pa_simple*)(obj), NULL); }
out__impl__write(pulse) { return pa_simple_write((pa_simple*)(obj), buf, (size_t)(frame_count*channels*2), NULL); }
out__impl(pulse);

int audio_play_pulse(const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data) {
    return audio_play(audio_output_pulse, NULL, format, filename, play_until, play_until_data);
}
#endif

#ifdef AUDIO_SUPPORT_VORBIS
fmt__impl__open(vorbis) {
    stb_vorbis* v = stb_vorbis_open_filename(filename, NULL, NULL);
    stb_vorbis_info i = stb_vorbis_get_info(v);
    *channels_out = i.channels;
    *rate_out = i.sample_rate;
    return (void*)(v);
}
fmt__impl__close(vorbis) { stb_vorbis_close((stb_vorbis*)(obj)); }
fmt__impl__read(vorbis)  { return stb_vorbis_get_samples_short_interleaved((stb_vorbis*)(obj), channels, buf, buf_sz/channels); }
fmt__impl(vorbis);
#endif

#ifdef AUDIO_SUPPORT_FLAC
fmt__impl__open(flac) {
    drflac* f = drflac_open_file(filename);
    *channels_out = f->channels;
    *rate_out = f->sampleRate;
    return (void*)(f);
}
fmt__impl__close(flac) { drflac_close((drflac*)(obj)); }
fmt__impl__read(flac)  { return drflac_read_pcm_frames_s16((drflac*)(obj), buf_sz/channels, buf); }
fmt__impl(flac);
#endif

#ifdef AUDIO_SUPPORT_WAV
fmt__impl__open(wav) {
    drwav* f = drwav_open_file(filename);
    *channels_out = f->channels;
    *rate_out = f->sampleRate;
    return (void*)(f);
}
fmt__impl__close(wav) { drwav_close((drwav*)(obj)); }
fmt__impl__read(wav)  { return drwav_read_pcm_frames_s16((drwav*)(obj), buf_sz/channels, buf); }
fmt__impl(wav);
#endif

#ifdef AUDIO_SUPPORT_MP3
fmt__impl__open(mp3) {
    drmp3* f = malloc(sizeof(drmp3));
    if (!drmp3_init_file(f, filename, NULL))
        return NULL;
    *channels_out = f->channels;
    *rate_out = f->sampleRate;
    return (void*)(f);
}
fmt__impl__close(mp3) { drmp3_uninit((drmp3*)(obj)); free(obj); }
fmt__impl__read(mp3)  { return drmp3_read_pcm_frames_s16((drmp3*)(obj), buf_sz/channels, buf); }
fmt__impl(mp3);
#endif
#endif
