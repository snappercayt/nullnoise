#ifndef NULLNOISE_WAV_H
#define NULLNOISE_WAV_H

#include <stdint.h>
#include <stdio.h>

#define WAV_HEADER_SIZE 44

int write_wav_header(FILE *file, uint32_t sample_count);

#endif
