// audio - v1 - simple audio playback library - public domain - by Patrick Gaskin
#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef AUDIO_NO_AUTODETECT
#ifdef TINYALSA_PCM_H
#define AUDIO_SUPPORT_ALSA
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

// audio_format_t implements a format for audio_play.
typedef struct audio_format_t {
    // audio_open opens a filename and returns the number of channels, sample
    // rate, and a pointer to the object which will be passed to the other funcs.
    void* (*audio_open)(const char* filename, int* channels_out, int* rate_out);
    // audio_close closes the object returned by audio_open.
    void  (*audio_close)(void* obj);
    // audio_read_frames_s16le reads audio samples into buf of size buf_sz and
    // returns the number of frames read (samples divided by channels). Zero
    // means the stream has ended, and a negative number is an error.
    int   (*audio_read_frames_s16le)(void* obj, int16_t* buf, size_t buf_sz);
} audio_format_t;

// audio_format returns the audio_format_t for the provided filename if it is
// recognized.
const audio_format_t* audio_format(const char* filename);

#ifdef AUDIO_SUPPORT_ALSA
// audio_play_alsa plays signed 16-bit little-endian audio from a file with the
// provided format on an ALSA device. If using pulseaudio, you may need to run
// your application with pasuspender. A nonzero return is an error. If
// play_until is not NULL, the audio plays until it (called with the argument
// play_until_data) returns true.
int audio_play_alsa(int card, int device, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data);
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

#ifdef AUDIO_SUPPORT_ALSA
int audio_play_alsa(int card, int device, const audio_format_t format, const char* filename, bool (*play_until)(void*), void* play_until_data) {
    int err, channels, rate;

    void *obj;
    if ((obj = format.audio_open(filename, &channels, &rate)) == NULL)
        return 1;

    struct pcm *pcm = pcm_open(card, device, PCM_OUT, &(struct pcm_config) {
        .channels = channels,
        .rate = rate,
        .format = PCM_FORMAT_S16_LE,
        .period_size = 1024, // default
        .period_count = 2 // default
    });

    if (pcm == NULL) {
        format.audio_close(obj);
        return 1;
    }

    if (!pcm_is_ready(pcm)) {
        printf("pcm error: %s", pcm_get_error(pcm));
        pcm_close(pcm);
        format.audio_close(obj);
        return errno;
    }

    int frame_count;
    int16_t buf[4096];
    while ((frame_count = format.audio_read_frames_s16le(obj, buf, 4096))) {
        err = pcm_writei(pcm, buf, frame_count);
        if (err < 0) {
            pcm_close(pcm);
            format.audio_close(obj);
            return err;
        }
        if (play_until && (*play_until)(play_until_data)) {
            pcm_stop(pcm);
            break;
        }
    }

    pcm_close(pcm);
    format.audio_close(obj);

    return 0;
}
#endif

#define fmt__impl__open(format)  static void* audio_open_ ## format(const char* filename, int* channels_out, int* rate_out)
#define fmt__impl__close(format) static void  audio_close_ ## format(void* obj)
#define fmt__impl__read(format)  static int   audio_read_frames_s16le_ ## format(void* obj, int16_t* buf, size_t buf_sz)
#define fmt__impl(format)        const audio_format_t audio_format_ ## format = {\
    .audio_open              = audio_open_ ## format,\
    .audio_close             = audio_close_ ## format,\
    .audio_read_frames_s16le = audio_read_frames_s16le_ ## format,\
}

#ifdef AUDIO_SUPPORT_VORBIS
fmt__impl__open(vorbis) {
    stb_vorbis* v = stb_vorbis_open_filename(filename, NULL, NULL);
    stb_vorbis_info i = stb_vorbis_get_info(v);
    *channels_out = i.channels;
    *rate_out = i.sample_rate;
    return (void*)(v);
}
fmt__impl__close(vorbis) { stb_vorbis_close((stb_vorbis*)(obj)); }
fmt__impl__read(vorbis)  { return stb_vorbis_get_samples_short_interleaved((stb_vorbis*)(obj), stb_vorbis_get_info((stb_vorbis*)(obj)).channels, buf, buf_sz/stb_vorbis_get_info((stb_vorbis*)(obj)).channels); }
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
fmt__impl__read(flac)  { return drflac_read_pcm_frames_s16((drflac*)(obj), buf_sz/((drflac*)(obj))->channels, buf); }
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
fmt__impl__read(wav)  { return drwav_read_pcm_frames_s16((drwav*)(obj), buf_sz/((drwav*)(obj))->channels, buf); }
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
fmt__impl__read(mp3)  { return drmp3_read_pcm_frames_s16((drmp3*)(obj), buf_sz/((drmp3*)(obj))->channels, buf); }
fmt__impl(mp3);
#endif
#endif
