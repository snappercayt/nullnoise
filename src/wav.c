#include "wav.h"

#include <string.h>

#define WAV_SAMPLE_RATE 48000

static void put_u16(unsigned char *dst, uint16_t value)
{
    dst[0] = (unsigned char)(value & 0xff);
    dst[1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *dst, uint32_t value)
{
    dst[0] = (unsigned char)(value & 0xff);
    dst[1] = (unsigned char)((value >> 8) & 0xff);
    dst[2] = (unsigned char)((value >> 16) & 0xff);
    dst[3] = (unsigned char)(value >> 24);
}

int write_wav_header(FILE *file, uint32_t sample_count)
{
    if (sample_count > (UINT32_MAX - 36) / sizeof(int16_t)) {
        return 0;
    }
    unsigned char header[WAV_HEADER_SIZE] = {0};
    uint32_t data_size = sample_count * sizeof(int16_t);

    memcpy(header, "RIFF", 4);
    put_u32(header + 4, 36 + data_size);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_u32(header + 16, 16);
    put_u16(header + 20, 1);
    put_u16(header + 22, 1);
    put_u32(header + 24, WAV_SAMPLE_RATE);
    put_u32(header + 28, WAV_SAMPLE_RATE * sizeof(int16_t));
    put_u16(header + 32, sizeof(int16_t));
    put_u16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    put_u32(header + 40, data_size);

    return fseek(file, 0, SEEK_SET) == 0 &&
           fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
           fflush(file) == 0;
}
