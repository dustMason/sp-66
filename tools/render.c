/* Desktop audition and trigger trace. Links the same C engine as firmware. */
#include "sequencer.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RATE = 48000, BLOCK = 64 };

static enum Tr66PatternId pattern_id(const char *name)
{
    for (unsigned i = 0; i <= TR66_PATTERN_COUNT; ++i)
        if (strcmp(name, tr66_pattern_names[i]) == 0)
            return (enum Tr66PatternId)i;
    return (enum Tr66PatternId)(TR66_PATTERN_COUNT + 1);
}

static bool parse_number(const char *input, long min, long max, unsigned *output)
{
    char *end;
    long value = strtol(input, &end, 10);
    if (end == input || *end != '\0' || value < min || value > max)
        return false;
    *output = (unsigned)value;
    return true;
}

static bool parse_tempo(const char *input, unsigned *bpm_milli)
{
    char *end;
    const double bpm = strtod(input, &end);
    if (end == input || *end != '\0' || !isfinite(bpm) || bpm < 50.0 || bpm > 200.0)
        return false;
    *bpm_milli = (unsigned)lround(bpm * 1000.0);
    return true;
}

static void le(FILE *stream, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        fputc((unsigned char)(value >> (8 * i)), stream);
}

static void wave_header(FILE *stream, uint32_t frames)
{
    fputs("RIFF", stream);
    le(stream, 36 + 2 * frames, 4);
    fputs("WAVEfmt ", stream);
    le(stream, 16, 4);         /* PCM format chunk size */
    le(stream, 1, 2);          /* PCM */
    le(stream, 1, 2);          /* mono */
    le(stream, RATE, 4);
    le(stream, 2 * RATE, 4);
    le(stream, 2, 2);          /* sample bytes per frame */
    le(stream, 16, 2);
    fputs("data", stream);
    le(stream, 2 * frames, 4);
}

static void record_hit(void *context, uint64_t frame, uint16_t voices)
{
    fprintf(context, "%" PRIu64 ",%" PRIu16 "\n", frame, voices);
}

int main(int argc, char **argv)
{
    if (argc != 7 && argc != 8) {
        fprintf(stderr, "Usage: %s out.wav rhythm variation a|auto|b bpm bars [events.csv]\n", argv[0]);
        return 2;
    }

    Tr66Selection selection = {.rhythm = pattern_id(argv[2]), .variation = pattern_id(argv[3])};
    if (strcmp(argv[4], "a") == 0)
        selection.arrange = TR66_ARRANGE_A;
    else if (strcmp(argv[4], "auto") == 0)
        selection.arrange = TR66_ARRANGE_AUTO;
    else if (strcmp(argv[4], "b") == 0)
        selection.arrange = TR66_ARRANGE_B;
    else {
        fprintf(stderr, "Expected Arrange a, auto, or b.\n");
        return 2;
    }

    unsigned bpm_milli, bars;
    Tr66Sequencer seq;
    tr66_seq_init(&seq, RATE, UINT32_C(14293));
    const char *balance_text = getenv("TR66_RENDER_BALANCE");
    if (balance_text != NULL) {
        char *end;
        float balance = strtof(balance_text, &end);
        if (end == balance_text || *end != '\0' || !isfinite(balance) ||
            balance < -1.0f || balance > 1.0f) {
            fprintf(stderr, "TR66_RENDER_BALANCE must be a number from -1 to 1.\n");
            return 2;
        }
        tr66_balance(&seq.synth, balance);
        seq.synth.balance = seq.synth.balance_target;
    }
    if (!tr66_seq_select(&seq, selection)) {
        fprintf(stderr, "The requested rhythm/variation is unknown or still awaiting transcription.\n");
        return 2;
    }
    if (!parse_tempo(argv[5], &bpm_milli) || !parse_number(argv[6], 1, 64, &bars)) {
        fprintf(stderr, "Expected 50–200 BPM and 1–64 bars.\n");
        return 2;
    }

    FILE *wave = fopen(argv[1], "wb");
    if (wave == NULL) {
        perror(argv[1]);
        return 1;
    }
    FILE *trace = NULL;
    if (argc == 8) {
        trace = fopen(argv[7], "w");
        if (trace == NULL) {
            perror(argv[7]);
            fclose(wave);
            return 1;
        }
        fputs("frame,voices\n", trace);
        seq.listener = record_hit;
        seq.listener_context = trace;
    }
    tr66_seq_tempo(&seq, bpm_milli);
    tr66_seq_start(&seq);
    const uint64_t span = (uint64_t)bars * seq.bar_ticks * RATE * 60000;
    const uint64_t divisor = bpm_milli * TR66_PPQN;
    const uint32_t song_frames = (uint32_t)((span + divisor - 1) / divisor);
    const uint32_t total_frames = song_frames + RATE;
    wave_header(wave, total_frames);

    for (uint32_t frame = 0; frame < total_frames;) {
        const uint32_t boundary = frame < song_frames ? song_frames : total_frames;
        const uint32_t remaining = boundary - frame;
        const unsigned count = remaining < BLOCK ? remaining : BLOCK;
        float buffer[BLOCK];
        if (frame == song_frames)
            tr66_seq_stop(&seq);
        tr66_seq_render(&seq, buffer, count);
        for (unsigned i = 0; i < count; ++i) {
            const float scaled = fmaxf(-1.0f, fminf(1.0f, buffer[i] * tr66_line_gain));
            le(wave, (uint16_t)(int16_t)lrintf(32767.0f * scaled), 2);
        }
        frame += count;
    }
    int status = fclose(wave);
    if (trace != NULL && fclose(trace) != 0)
        status = -1;
    return status == 0 ? 0 : 1;
}
