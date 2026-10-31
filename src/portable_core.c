#include "portable_core.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rnnoise.h"
#include "wav.h"

#define FRAME_SAMPLES 480
#define TARGET_PEAK 0.89f
#define MAX_NORMALIZE_GAIN 4.0f

int rnnoise_normalize_file(const char *input_path, const char *output_path)
{
    struct stat output_info;
    if (stat(output_path, &output_info) == 0) {
        /* Never overwrite an existing enhanced file. */
        return 0;
    }

    FILE *input = fopen(input_path, "rb");
    if (input == NULL || fseek(input, WAV_HEADER_SIZE, SEEK_SET) != 0) {
        if (input != NULL) {
            fclose(input);
        }
        return 0;
    }

    FILE *output = fopen(output_path, "wb+");
    if (output == NULL) {
        fclose(input);
        return 0;
    }

    static const unsigned char blank_header[WAV_HEADER_SIZE] = {0};
    if (fwrite(blank_header, 1, sizeof(blank_header), output) != sizeof(blank_header)) {
        fclose(input);
        fclose(output);
        unlink(output_path);
        return 0;
    }

    DenoiseState *denoise = rnnoise_create(NULL);
    if (denoise == NULL) {
        fclose(input);
        fclose(output);
        unlink(output_path);
        return 0;
    }

    int16_t input_pcm[FRAME_SAMPLES];
    int16_t output_pcm[FRAME_SAMPLES];
    float input_frame[FRAME_SAMPLES];
    float output_frame[FRAME_SAMPLES];
    uint32_t sample_count = 0;
    float peak = 0.0f;
    int ok = 1;

    for (;;) {
        size_t count = fread(input_pcm, sizeof(input_pcm[0]), FRAME_SAMPLES, input);
        if (count == 0) {
            if (ferror(input)) {
                ok = 0;
            }
            break;
        }
        for (size_t i = 0; i < FRAME_SAMPLES; ++i) {
            input_frame[i] = (i < count) ? (float)input_pcm[i] : 0.0f;
        }
        rnnoise_process_frame(denoise, output_frame, input_frame);
        for (size_t i = 0; i < count; ++i) {
            float value = output_frame[i];
            if (value > peak) {
                peak = value;
            } else if (-value > peak) {
                peak = -value;
            }
            if (value > 32767.0f) {
                value = 32767.0f;
            } else if (value < -32768.0f) {
                value = -32768.0f;
            }
            output_pcm[i] = (int16_t)value;
        }
        if (count > (UINT32_MAX - 36) / sizeof(int16_t) - sample_count ||
            fwrite(output_pcm, sizeof(output_pcm[0]), count, output) != count) {
            ok = 0;
            break;
        }
        sample_count += (uint32_t)count;
    }

    rnnoise_destroy(denoise);

    if (ok && fflush(output) == 0 && fseek(output, WAV_HEADER_SIZE, SEEK_SET) == 0) {
        float gain = (peak > 0.0f) ? (TARGET_PEAK * 32767.0f / peak) : 1.0f;
        if (gain > MAX_NORMALIZE_GAIN) {
            gain = MAX_NORMALIZE_GAIN;
        }

        uint32_t remaining = sample_count;
        while (remaining > 0) {
            size_t count = (remaining > FRAME_SAMPLES) ? FRAME_SAMPLES : remaining;
            if (fread(output_pcm, sizeof(output_pcm[0]), count, output) != count) {
                ok = 0;
                break;
            }
            for (size_t i = 0; i < count; ++i) {
                float value = output_pcm[i] * gain;
                if (value > 32767.0f) {
                    value = 32767.0f;
                } else if (value < -32768.0f) {
                    value = -32768.0f;
                }
                output_pcm[i] = (int16_t)value;
            }
            if (fseek(output, -(long)(count * sizeof(output_pcm[0])), SEEK_CUR) != 0 ||
                fwrite(output_pcm, sizeof(output_pcm[0]), count, output) != count ||
                fseek(output, 0, SEEK_CUR) != 0) {
                ok = 0;
                break;
            }
            remaining -= (uint32_t)count;
        }
    } else {
        ok = 0;
    }

    if (ok) {
        ok = write_wav_header(output, sample_count);
    }
    if (fclose(input) != 0) {
        ok = 0;
    }
    if (fclose(output) != 0) {
        ok = 0;
    }
    if (!ok) {
        unlink(output_path);
        return 0;
    }
    return 1;
}
