#include "audio.h"

#include <nds.h>
#include <maxmod9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "minimp3.h"

/* The DS sound stream is limited to 32768 Hz. Decode at the MP3's native
 * rate, then resample to 32 kHz before handing interleaved PCM to Maxmod. */
#define OUTPUT_RATE       32768u
#define STREAM_FRAMES     4096u
#define INPUT_CAPACITY    8192u
#define REFILL_THRESHOLD  2048u
#define PCM_CAPACITY      MINIMP3_MAX_SAMPLES_PER_FRAME

typedef struct { int16_t left, right; } StereoSample;

static FILE *g_file;
static mp3dec_t g_decoder;
static mp3d_sample_t g_decode_pcm[PCM_CAPACITY];
static StereoSample g_frame[1152];
static unsigned g_frame_count;
static unsigned g_frame_rate;
static uint32_t g_phase; /* 16.16 source-frame position in current MP3 frame */
static uint8_t g_input[INPUT_CAPACITY];
static size_t g_input_len, g_input_pos;
static bool g_input_eof;
static bool g_stream_open;
static volatile bool g_playing;
static volatile bool g_paused;
static volatile bool g_finished;
static int g_volume = 100;
static mm_stream g_stream;

static void clear_input(void)
{
    g_input_len = 0;
    g_input_pos = 0;
    g_input_eof = false;
}

/* Ensure enough compressed bytes are available without discarding a partial
 * MPEG frame. The decoder can skip ID3 tags and junk before the first frame. */
static bool refill_input(void)
{
    if (!g_file || g_input_eof) return false;
    if (g_input_pos) {
        size_t remain = g_input_len - g_input_pos;
        if (remain) memmove(g_input, g_input + g_input_pos, remain);
        g_input_len = remain;
        g_input_pos = 0;
    }
    if (g_input_len >= INPUT_CAPACITY) return false;
    size_t n = fread(g_input + g_input_len, 1, INPUT_CAPACITY - g_input_len, g_file);
    g_input_len += n;
    if (n == 0) g_input_eof = true;
    return n != 0;
}

static bool decode_next_frame(void)
{
    for (;;) {
        size_t available = g_input_len - g_input_pos;
        if (available < REFILL_THRESHOLD && !g_input_eof) {
            if (!refill_input()) {
                available = g_input_len - g_input_pos;
                if (!available) return false;
            } else {
                available = g_input_len - g_input_pos;
            }
        }
        if (!available) return false;

        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));
        int samples = mp3dec_decode_frame(&g_decoder,
            g_input + g_input_pos, (int)available, g_decode_pcm, &info);

        if (info.frame_bytes <= 0) {
            if (g_input_eof) return false;
            /* Keep a small tail for a sync word split across reads. */
            if (available > 4) g_input_pos = g_input_len - 4;
            if (g_input_len - g_input_pos >= INPUT_CAPACITY - 4) return false;
            continue;
        }
        g_input_pos += (size_t)info.frame_bytes;
        if (samples <= 0) continue; /* valid header, but no decoded PCM */
        if (info.hz == 0 || (info.channels != 1 && info.channels != 2)) continue;

        if (samples > 1152) samples = 1152;
        g_frame_count = (unsigned)samples;
        g_frame_rate = (unsigned)info.hz;
        g_phase = 0;
        for (int i = 0; i < samples; ++i) {
            int16_t l, r;
            if (info.channels == 2) {
                l = (int16_t)g_decode_pcm[i * 2];
                r = (int16_t)g_decode_pcm[i * 2 + 1];
            } else {
                l = r = (int16_t)g_decode_pcm[i];
            }
            g_frame[i].left = l;
            g_frame[i].right = r;
        }
        return true;
    }
}

/* Maxmod's callback length is a count of stereo frames, not scalar samples. */
static mm_word mp3_stream_callback(mm_word length, mm_addr destination,
                                   mm_stream_formats format)
{
    int16_t *out = (int16_t *)destination;
    mm_word written = 0;
    if (format != MM_STREAM_16BIT_STEREO) {
        memset(destination, 0, (size_t)length * 4u);
        return length;
    }

    while (written < length) {
        if (g_paused || !g_playing) {
            out[written * 2] = 0;
            out[written * 2 + 1] = 0;
            ++written;
            continue;
        }
        if (!g_frame_count || (g_phase >> 16) >= g_frame_count) {
            if (g_frame_count) g_phase -= g_frame_count << 16;
            if (!decode_next_frame()) {
                g_finished = true;
                g_playing = false;
                while (written < length) {
                    out[written * 2] = 0;
                    out[written * 2 + 1] = 0;
                    ++written;
                }
                break;
            }
        }

        unsigned index = g_phase >> 16;
        /* Fixed-point linear resampling: low-cost on ARM9 and smoother than
         * nearest-neighbour when converting common 44.1/48 kHz MP3s to 32 kHz. */
        if (index >= g_frame_count) index = g_frame_count - 1;
        unsigned next = (index + 1 < g_frame_count) ? index + 1 : index;
        uint32_t fraction = g_phase & 0xFFFFu;
        int32_t left = (int32_t)g_frame[index].left * (int32_t)(65536u - fraction) +
                       (int32_t)g_frame[next].left * (int32_t)fraction;
        int32_t right = (int32_t)g_frame[index].right * (int32_t)(65536u - fraction) +
                        (int32_t)g_frame[next].right * (int32_t)fraction;
        out[written * 2] = (int16_t)(left >> 16);
        out[written * 2 + 1] = (int16_t)(right >> 16);
        ++written;
        g_phase += (uint32_t)(((uint64_t)g_frame_rate << 16) / OUTPUT_RATE);
    }
    return length;
}

bool audio_init(void)
{
    /* devkitPro Maxmod's ARM9 API initializes from a small DS system struct.
     * No soundbank is needed for PCM streaming. FIFO 7 is Maxmod's usual IPC channel. */
    mm_ds_system system;
    memset(&system, 0, sizeof(system));
    system.mod_count = 0;
    system.samp_count = 0;
    system.mem_bank = NULL;
    system.fifo_channel = 7;
    mmInit(&system);

    g_stream.sampling_rate = OUTPUT_RATE;
    g_stream.buffer_length = STREAM_FRAMES;
    g_stream.callback = mp3_stream_callback;
    g_stream.format = MM_STREAM_16BIT_STEREO;
    g_stream.timer = MM_TIMER0;
    g_stream.manual = true;
    g_stream_open = false;
    g_playing = false;
    g_paused = false;
    g_finished = false;
    return true;
}

void audio_stop(void)
{
    if (g_stream_open) {
        mmStreamClose();
        g_stream_open = false;
    }
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    g_playing = false;
    g_paused = false;
    g_finished = false;
    g_frame_count = 0;
    clear_input();
}

bool audio_play(const char *path)
{
    if (!path || !path[0]) return false;
    audio_stop();
    g_file = fopen(path, "rb");
    if (!g_file) {
        return false;
    }
    mp3dec_init(&g_decoder);
    clear_input();
    g_frame_count = 0;
    g_phase = 0;
    g_finished = false;
    g_paused = false;
    g_playing = true;
    mmStreamOpen(&g_stream);
    g_stream_open = true;
    mmStreamVolume((mm_byte)((g_volume * 127) / 100));
    /* Prime both halves immediately so playback starts without an initial gap. */
    mmStreamUpdate();
    return true;
}

void audio_toggle_pause(void)
{
    if (g_playing || g_paused) g_paused = !g_paused;
}

void audio_update(void)
{
    /* Keep the manual stream serviced even after EOF; the callback emits
     * silence once finished, avoiding a stalled hardware stream. */
    if (g_stream_open) mmStreamUpdate();
}

void audio_set_volume(int volume_0_127)
{
    if (volume_0_127 < 0) volume_0_127 = 0;
    if (volume_0_127 > 127) volume_0_127 = 127;
    g_volume = (volume_0_127 * 100) / 127;
    if (g_stream_open) mmStreamVolume((mm_byte)volume_0_127);
}

bool audio_is_playing(void) { return g_playing; }
bool audio_is_paused(void) { return g_paused; }
bool audio_finished(void) { return g_finished; }

void audio_exit(void)
{
    audio_stop();
}
