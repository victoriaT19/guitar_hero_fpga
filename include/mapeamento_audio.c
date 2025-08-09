#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "kiss_fft.h"
#include "mapeamento_audio.h"
#include <string.h>

const char* NOTES[NUM_NOTES] = {
    "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2",
    "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3",
    "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
    "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5"
};

const float FREQS[NUM_NOTES] = {
    65.41, 69.30, 73.42, 77.78, 82.41, 87.31, 92.50, 98.00, 103.83, 110.00, 116.54, 123.47,
    130.81, 138.59, 146.83, 155.56, 164.81, 174.61, 185.00, 196.00, 207.65, 220.00, 233.08, 246.94,
    261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00, 415.30, 440.00, 466.16, 493.88,
    523.25, 554.37, 587.33, 622.25, 659.25, 698.46, 739.99, 783.99, 830.61, 880.00, 932.33, 987.77
};

static const char* freq_to_note(float freq) {
    if (freq < FREQS[0] * 0.97) return NULL;
    float min_diff = 1e9;
    int note_idx = -1;

    for (int i = 0; i < NUM_NOTES; i++) {
        float diff = fabs(freq - FREQS[i]);
        if (diff < min_diff) {
            min_diff = diff;
            note_idx = i;
        }
    }
    
    if (note_idx != -1 && min_diff < FREQS[note_idx] * 0.05) {
        return NOTES[note_idx];
    }
    return NULL;
}

AudioData* load_mp3_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        printf("Erro ao abrir o arquivo '%s'!\n", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char* mp3_buffer_orig = (unsigned char*)malloc(file_size);
    if (!mp3_buffer_orig) {
        printf("Erro ao alocar memoria para o buffer do MP3.\n");
        fclose(f);
        return NULL;
    }
    
    if (fread(mp3_buffer_orig, 1, file_size, f) != file_size) {
        printf("Erro ao ler o arquivo MP3 para o buffer.\n");
        free(mp3_buffer_orig);
        fclose(f);
        return NULL;
    }
    fclose(f);

    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    
    // Calculate needed size
    size_t total_pcm_samples = 0;
    unsigned char *mp3_ptr = mp3_buffer_orig;
    long file_size_remaining = file_size;
    int samples = 0;

    while ((samples = mp3dec_decode_frame(&dec, mp3_ptr, file_size_remaining, NULL, &info)) > 0) {
        total_pcm_samples += samples * info.channels;
        mp3_ptr += info.frame_bytes;
        file_size_remaining -= info.frame_bytes;
    }

    // Allocate memory and decode
    AudioData* audio_data = (AudioData*)malloc(sizeof(AudioData));
    if (!audio_data) {
        printf("Erro ao alocar memoria para AudioData.\n");
        free(mp3_buffer_orig);
        return NULL;
    }

    audio_data->pcm_buffer = (short*)malloc(total_pcm_samples * sizeof(short));
    if (!audio_data->pcm_buffer) {
        printf("Erro ao alocar memoria para o buffer PCM.\n");
        free(mp3_buffer_orig);
        free(audio_data);
        return NULL;
    }

    size_t pcm_size = 0;
    mp3dec_init(&dec);
    mp3_ptr = mp3_buffer_orig;
    file_size_remaining = file_size;
    
    while ((samples = mp3dec_decode_frame(&dec, mp3_ptr, file_size_remaining, 
                                         audio_data->pcm_buffer + pcm_size, &info)) > 0) {
        pcm_size += samples * info.channels;
        mp3_ptr += info.frame_bytes;
        file_size_remaining -= info.frame_bytes;
    }

    free(mp3_buffer_orig);
    
    audio_data->pcm_size = pcm_size;
    audio_data->sample_rate = info.hz;
    audio_data->channels = info.channels;
    
    return audio_data;
}

void free_audio_data(AudioData* audio_data) {
    if (audio_data) {
        if (audio_data->pcm_buffer) {
            free(audio_data->pcm_buffer);
        }
        free(audio_data);
    }
}

void analyze_audio_to_file(AudioData* audio_data, const char* output_filename) {
    if (!audio_data || !output_filename) return;

    kiss_fft_cfg cfg = kiss_fft_alloc(FRAME_SIZE, 0, NULL, NULL);
    kiss_fft_cpx in[FRAME_SIZE];
    kiss_fft_cpx out[FRAME_SIZE];
    FILE* output = fopen(output_filename, "w");

    if (!output) {
        printf("Erro ao abrir arquivo de saída '%s'!\n", output_filename);
        kiss_fft_free(cfg);
        return;
    }

    printf("Analisando áudio com taxa de amostragem de %d Hz...\n", audio_data->sample_rate);
    
    char ultima_nota_encontrada[5] = "";
    
    for (long pcm_offset = 0; 
         pcm_offset + (FRAME_SIZE * audio_data->channels) < audio_data->pcm_size; 
         pcm_offset += (FRAME_SIZE * audio_data->channels)) {
        
        for (int i = 0; i < FRAME_SIZE; i++) {
            if (audio_data->channels == 2) {
                in[i].r = (float)(audio_data->pcm_buffer[pcm_offset + i*2] + 
                                  audio_data->pcm_buffer[pcm_offset + i*2 + 1]) / 2.0f / 32768.0f;
            } else {
                in[i].r = (float)audio_data->pcm_buffer[pcm_offset + i] / 32768.0f;
            }
            in[i].i = 0;
        }
        kiss_fft(cfg, in, out);

        float max_mag = 0;
        int max_idx = 0;
        for (int i = 1; i < FRAME_SIZE / 2; i++) {
            float mag = sqrt(out[i].r * out[i].r + out[i].i * out[i].i);
            if (mag > max_mag) { max_mag = mag; max_idx = i; }
        }
        float freq = (float)max_idx * audio_data->sample_rate / FRAME_SIZE;
        
        if (max_mag > THRESHOLD) {
            const char* note = freq_to_note(freq);
            if (note) {
                if (strcmp(note, ultima_nota_encontrada) != 0) {
                    double time = (double)pcm_offset / (audio_data->channels * audio_data->sample_rate);
                    fprintf(output, "%.2f\t%s\n", time, note);
                    strcpy(ultima_nota_encontrada, note);
                }
            }
        }
    }

    kiss_fft_free(cfg);
    fclose(output);
    printf("Notas salvas em '%s'!\n", output_filename);
}