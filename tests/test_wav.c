#include "wav.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t read_u32(const unsigned char *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static uint16_t read_u16(const unsigned char *data)
{
    return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static void test_writes_mono_48khz_pcm_header(void)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(write_wav_header(file, 3));

    unsigned char header[WAV_HEADER_SIZE];
    assert(fseek(file, 0, SEEK_SET) == 0);
    assert(fread(header, 1, sizeof(header), file) == sizeof(header));
    assert(memcmp(header, "RIFF", 4) == 0);
    assert(read_u32(header + 4) == 42);
    assert(memcmp(header + 8, "WAVEfmt ", 8) == 0);
    assert(read_u32(header + 16) == 16);
    assert(read_u16(header + 20) == 1);
    assert(read_u16(header + 22) == 1);
    assert(read_u32(header + 24) == 48000);
    assert(read_u32(header + 28) == 96000);
    assert(read_u16(header + 32) == 2);
    assert(read_u16(header + 34) == 16);
    assert(memcmp(header + 36, "data", 4) == 0);
    assert(read_u32(header + 40) == 6);
    fclose(file);
}

static void test_rejects_sample_count_that_overflows_riff_size(void)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(!write_wav_header(file, UINT32_MAX));
    fclose(file);
}

static void test_accepts_largest_sample_count_that_fits_riff_size(void)
{
    const uint32_t max_sample_count = (UINT32_MAX - 36) / 2;
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(write_wav_header(file, max_sample_count));

    unsigned char header[WAV_HEADER_SIZE];
    assert(fseek(file, 0, SEEK_SET) == 0);
    assert(fread(header, 1, sizeof(header), file) == sizeof(header));
    assert(read_u32(header + 4) == UINT32_MAX - 1);
    assert(read_u32(header + 40) == UINT32_MAX - 37);
    fclose(file);
}

int main(void)
{
    test_writes_mono_48khz_pcm_header();
    test_rejects_sample_count_that_overflows_riff_size();
    test_accepts_largest_sample_count_that_fits_riff_size();
    puts("WAV header tests passed.");
    return 0;
}
