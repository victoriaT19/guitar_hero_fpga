#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sndfile.h>
#include "kiss_fft.h"


#define SAMPLE_RATE 44100
#define FRAME_SIZE 1024
#define THRESHOLD 0.2  

#define NUM_NOTES 24


const char* NOTES[NUM_NOTES] = {
    "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
    "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5"
};

const float FREQS[NUM_NOTES] = {
    261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00, 415.30, 440.00, 466.16, 493.88,
    523.25, 554.37, 587.33, 622.25, 659.25, 698.46, 739.99, 783.99, 830.61, 880.00, 932.33, 987.77
};
const char* freq_to_note(float freq) {
    float min_diff = 1e9;
    int note_idx = -1;

    for (int i = 0; i < NUM_NOTES; i++) {
        float diff = fabs(freq - FREQS[i]);
        if (diff < min_diff) {
            min_diff = diff;
            note_idx = i;
        }
    }

    if (note_idx != -1 && min_diff < FREQS[note_idx] * 0.03) {
            return NOTES[note_idx];
        }

    return NULL;
}

int main() {
    SF_INFO sfinfo;
    SNDFILE* audio_file = sf_open("input.wav", SFM_READ, &sfinfo);
    if (!audio_file) {
        printf("Erro ao abrir o arquivo!\n");
        return 1;
    }

    //taxa de amostragem
    const int SAMPLE_RATE = sfinfo.samplerate; 

    kiss_fft_cfg cfg = kiss_fft_alloc(FRAME_SIZE, 0, NULL, NULL);
    kiss_fft_cpx in[FRAME_SIZE], out[FRAME_SIZE];

    float buffer[FRAME_SIZE];
    FILE* output = fopen("notes.txt", "w");

    long total_frames_read = 0;
    int frames_read_this_iteration;


    while ((frames_read_this_iteration = sf_read_float(audio_file, buffer, FRAME_SIZE)) > 0) {
        for (int i = 0; i < FRAME_SIZE; i++) {
            in[i].r = buffer[i];
            in[i].i = 0;
        }

        // Zera o resto do buffer se a leitura for menor que FRAME_SIZE
        for (int i = frames_read_this_iteration; i < FRAME_SIZE; i++) {
            in[i].r = 0;
            in[i].i = 0;
        }


        kiss_fft(cfg, in, out);

        float max_mag = 0;
        int max_idx = 0;    

        for (int i = 0; i < FRAME_SIZE / 2; i++) {
            float mag = sqrt(out[i].r * out[i].r + out[i].i * out[i].i);
            if (mag > max_mag) {
                max_mag = mag;
                max_idx = i;
            }
        }

        // Converte o índice do pico da FFT para uma frequência em Hz
        float freq = (float)max_idx * SAMPLE_RATE / FRAME_SIZE;

        if (max_mag > THRESHOLD) {
            const char* note = freq_to_note(freq);
            if (note) {
                float time = ftell(output) / (float)SAMPLE_RATE;
                fprintf(output, "%.2f\t%s\n", time, note);
            }
        }

        total_frames_read += frames_read_this_iteration;

    }

    free(cfg);
    sf_close(audio_file);
    fclose(output);

    printf("Notas salvas em 'notes.txt'!\n");
    return 0;
}