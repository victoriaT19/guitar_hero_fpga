#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "kiss_fft.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define FRAME_SIZE 4096
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
    if (freq < FREQS[0] * 0.97) return NULL; // Otimização: ignora frequências muito baixas
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

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Uso: %s <arquivo.mp3>\n", argv[0]);
        return 1;
    }

    const char* input_filename = argv[1];
    
    FILE* f = fopen(input_filename, "rb");
    if (!f) {
        printf("Erro ao abrir o arquivo '%s'!\n", input_filename);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* mp3_buffer = (unsigned char*)malloc(file_size);
    fread(mp3_buffer, 1, file_size, f);
    fclose(f);

    static mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    
    // Buffer para o áudio decodificado (PCM)
    // Usamos um buffer grande para guardar o áudio todo.
    // Para arquivos muito grandes, uma abordagem de streaming seria melhor.
    short* pcm_buffer = (short*)malloc(1024 * 1024 * 4); // Buffer de 8MB para o PCM
    size_t pcm_size = 0;
    int samples;
    
    // Decodifica o MP3 inteiro para a memória
    while ((samples = mp3dec_decode_frame(&dec, mp3_buffer, file_size, pcm_buffer + pcm_size, &info)) > 0) {
        pcm_size += samples * info.channels;
        file_size -= info.frame_bytes;
        mp3_buffer += info.frame_bytes;
    }
    free(mp3_buffer - (ftell(f) - file_size)); // Libera o buffer original do mp3
    
    const int SAMPLE_RATE = info.hz;

    kiss_fft_cfg cfg = kiss_fft_alloc(FRAME_SIZE, 0, NULL, NULL);
    kiss_fft_cpx in[FRAME_SIZE];
    kiss_fft_cpx out[FRAME_SIZE];
    FILE* output = fopen("notes.txt", "w");

    printf("Analisando áudio com taxa de amostragem de %d Hz...\n", SAMPLE_RATE);

    for (long pcm_offset = 0; pcm_offset + FRAME_SIZE < pcm_size; pcm_offset += FRAME_SIZE) {
        for (int i = 0; i < FRAME_SIZE; i++) {
            // Se for estéreo, pegamos a média dos canais. Se for mono, usamos direto.
            if (info.channels == 2) {
                in[i].r = (float)(pcm_buffer[pcm_offset + i*2] + pcm_buffer[pcm_offset + i*2 + 1]) / 2.0f / 32768.0f;
            } else {
                in[i].r = (float)pcm_buffer[pcm_offset + i] / 32768.0f;
            }
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
        
        float freq = (float)max_idx * SAMPLE_RATE / FRAME_SIZE;

        if (max_mag > THRESHOLD) {
            const char* note = freq_to_note(freq);
            if (note) {
                float time = (float)pcm_offset / (info.channels * SAMPLE_RATE);
                fprintf(output, "%.2f\t%s\n", time, note);
            }
        }
    }

    free(cfg);
    free(pcm_buffer);
    fclose(output);

    printf("Notas salvas em 'notes.txt'!\n");
    return 0;
}