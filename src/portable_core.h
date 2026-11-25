#ifndef NULLNOISE_PORTABLE_CORE_H
#define NULLNOISE_PORTABLE_CORE_H

#include <stdint.h>

// Applies RNNoise noise reduction followed by peak-normalized loudness to a
// mono 48 kHz PCM WAV file.
//
// input_path  must exist and be a valid source WAV.
// output_path must not already exist; it is created and finalized on
//             success. An existing output file is never overwritten.
//
// Returns non-zero on success and 0 on any failure (file, format, model, or
// write error). Neither argument may be NULL.
int rnnoise_normalize_file(const char *input_path, const char *output_path);

#endif
