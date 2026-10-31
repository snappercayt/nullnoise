#define _POSIX_C_SOURCE 200809L

#include "portable_core.h"
#include "wav.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int run_case(const char *dir)
{
    char input[512];
    char output[512];
    snprintf(input, sizeof(input), "%s/nullnoise_portable_in.wav", dir);
    snprintf(output, sizeof(output), "%s/nullnoise_portable_out.wav", dir);

    FILE *f = fopen(input, "wb");
    if (f == NULL) { perror("fopen input"); return 0; }
    if (!write_wav_header(f, 48000)) { fclose(f); return 0; }
    for (uint32_t i = 0; i < 48000; ++i) {
        int16_t s = (int16_t)(32767 * ((i * 13) % 10000) / 10000.0f);
        if (fwrite(&s, sizeof(s), 1, f) != 1) { fclose(f); return 0; }
    }
    fclose(f);

    if (!rnnoise_normalize_file(input, output)) {
        fprintf(stderr, "rnnoise_normalize_file returned failure\n");
        return 0;
    }

    FILE *out = fopen(output, "rb");
    if (out == NULL) { perror("fopen output"); return 0; }
    unsigned char hdr[44];
    if (fread(hdr, 1, sizeof(hdr), out) != sizeof(hdr)) {
        fclose(out);
        return 0;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVEfmt ", 8) != 0) {
        fclose(out);
        return 0;
    }
    uint32_t data_size = (uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8) | ((uint32_t)hdr[42] << 16) | ((uint32_t)hdr[43] << 24);
    uint16_t block_align = (uint16_t)hdr[32] | ((uint16_t)hdr[33] << 8);
    fclose(out);
    if (data_size % block_align != 0) {
        fprintf(stderr, "size/align mismatch\n");
        return 0;
    }
    uint32_t out_samples = data_size / block_align;
    if (out_samples != 48000) {
        fprintf(stderr, "sample count mismatch: got %u, expected %u\n",
                out_samples, 48000);
        return 0;
    }
    return 1;
}

int main(void)
{
    char dir[] = "/tmp/nullnoise_portable_XXXXXX";
    if (mkdtemp(dir) == NULL) { perror("mkdtemp"); return 1; }
    /* dir printed below if needed */

    int ok = run_case(dir);

    rmdir(dir);
    return ok ? 0 : 1;
}
