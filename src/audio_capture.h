#ifndef NULLNOISE_AUDIO_CAPTURE_H
#define NULLNOISE_AUDIO_CAPTURE_H

#include <stdint.h>
#include <stdio.h>

int audio_capture_start(FILE *output);
int audio_capture_stop(uint64_t *sample_count, uint64_t *overrun_count);
uint64_t audio_capture_samples_written(void);

#endif
