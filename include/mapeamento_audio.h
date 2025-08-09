#ifndef MAPEAMENTO_AUDIO_H
#define MAPEAMENTO_AUDIO_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define FRAME_SIZE 4096
#define THRESHOLD 10.0
#define NUM_NOTES 48

typedef struct {
    short* pcm_buffer;
    size_t pcm_size;
    int sample_rate;
    int channels;
} AudioData;

// Function prototypes
AudioData* load_mp3_file(const char* filename);
void free_audio_data(AudioData* audio_data);
void analyze_audio_to_file(AudioData* audio_data, const char* output_filename);

#endif // AUDIO_ANALYSIS_H