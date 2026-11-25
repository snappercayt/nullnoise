#define MA_NO_DEVICE_IO
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

static void write_samples(ma_pcm_rb *queue, const int16_t *samples,
                          ma_uint32 frame_count)
{
    ma_uint32 offset = 0;
    while (offset < frame_count) {
        ma_uint32 count = frame_count - offset;
        void *buffer = NULL;
        assert(ma_pcm_rb_acquire_write(queue, &count, &buffer) == MA_SUCCESS);
        assert(count > 0 && buffer != NULL);
        for (ma_uint32 i = 0; i < count; ++i) {
            ((int16_t *)buffer)[i] = samples[offset + i];
        }
        assert(ma_pcm_rb_commit_write(queue, count) == MA_SUCCESS);
        offset += count;
    }
}

static void read_samples(ma_pcm_rb *queue, int16_t *samples,
                         ma_uint32 frame_count)
{
    ma_uint32 offset = 0;
    while (offset < frame_count) {
        ma_uint32 count = frame_count - offset;
        void *buffer = NULL;
        assert(ma_pcm_rb_acquire_read(queue, &count, &buffer) == MA_SUCCESS);
        assert(count > 0 && buffer != NULL);
        for (ma_uint32 i = 0; i < count; ++i) {
            samples[offset + i] = ((int16_t *)buffer)[i];
        }
        assert(ma_pcm_rb_commit_read(queue, count) == MA_SUCCESS);
        offset += count;
    }
}

typedef struct {
    ma_pcm_rb queue;
    atomic_bool failed;
} concurrent_queue;

static void *produce_samples(void *context)
{
    concurrent_queue *state = context;
    for (int32_t sample = 0; sample < 500000; ++sample) {
        ma_uint32 count = 1;
        void *buffer = NULL;
        while (!atomic_load(&state->failed)) {
            count = 1;
            buffer = NULL;
            ma_result result =
                ma_pcm_rb_acquire_write(&state->queue, &count, &buffer);
            if (result != MA_SUCCESS) {
                atomic_store(&state->failed, true);
                break;
            }
            if (count > 0) {
                break;
            }
            sched_yield();
        }
        if (atomic_load(&state->failed)) {
            return NULL;
        }
        if (count != 1 || buffer == NULL) {
            atomic_store(&state->failed, true);
            return NULL;
        }
        *(int32_t *)buffer = sample;
        if (ma_pcm_rb_commit_write(&state->queue, 1) != MA_SUCCESS) {
            atomic_store(&state->failed, true);
            return NULL;
        }
    }
    return NULL;
}

static void *consume_samples(void *context)
{
    concurrent_queue *state = context;
    for (int32_t expected = 0; expected < 500000; ++expected) {
        ma_uint32 count = 1;
        void *buffer = NULL;
        while (!atomic_load(&state->failed)) {
            count = 1;
            buffer = NULL;
            ma_result result =
                ma_pcm_rb_acquire_read(&state->queue, &count, &buffer);
            if (result != MA_SUCCESS) {
                atomic_store(&state->failed, true);
                break;
            }
            if (count > 0) {
                break;
            }
            sched_yield();
        }
        if (atomic_load(&state->failed)) {
            return NULL;
        }
        if (count != 1 || buffer == NULL ||
            *(int32_t *)buffer != expected) {
            atomic_store(&state->failed, true);
            return NULL;
        }
        if (ma_pcm_rb_commit_read(&state->queue, 1) != MA_SUCCESS) {
            atomic_store(&state->failed, true);
            return NULL;
        }
    }
    return NULL;
}

int main(void)
{
    ma_pcm_rb queue;
    assert(ma_pcm_rb_init(ma_format_s16, 1, 4, NULL, NULL, &queue) ==
           MA_SUCCESS);

    const int16_t first[] = {1, 2, 3, 4};
    write_samples(&queue, first, 4);

    ma_uint32 available = 1;
    void *buffer = NULL;
    assert(ma_pcm_rb_acquire_write(&queue, &available, &buffer) == MA_SUCCESS);
    assert(available == 0);

    int16_t partial[3];
    read_samples(&queue, partial, 3);
    assert(partial[0] == 1 && partial[1] == 2 && partial[2] == 3);

    const int16_t wrapped[] = {5, 6, 7};
    write_samples(&queue, wrapped, 3);

    int16_t complete[4];
    read_samples(&queue, complete, 4);
    assert(complete[0] == 4 && complete[1] == 5 &&
           complete[2] == 6 && complete[3] == 7);

    available = 1;
    buffer = NULL;
    assert(ma_pcm_rb_acquire_read(&queue, &available, &buffer) == MA_SUCCESS);
    assert(available == 0);

    ma_pcm_rb_uninit(&queue);

    concurrent_queue concurrent;
    atomic_init(&concurrent.failed, false);
    assert(ma_pcm_rb_init(ma_format_s32, 1, 128, NULL, NULL,
                          &concurrent.queue) == MA_SUCCESS);
    pthread_t producer;
    pthread_t consumer;
    assert(pthread_create(&producer, NULL, produce_samples, &concurrent) == 0);
    assert(pthread_create(&consumer, NULL, consume_samples, &concurrent) == 0);
    assert(pthread_join(producer, NULL) == 0);
    assert(pthread_join(consumer, NULL) == 0);
    assert(!atomic_load(&concurrent.failed));
    ma_pcm_rb_uninit(&concurrent.queue);

    puts("Miniaudio capture queue tests passed.");
    return 0;
}
