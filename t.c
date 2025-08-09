#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <linux/joystick.h>
#include "guitar_hero.h"
#include "ioctl_cmds.h"  // seu header com os defines do ioctl
#include "mapeamento_audio.h" // seu header com os mapeamentos de áudio

struct termios orig_termios;

#define FRAME_DELAY 16 // ~60 FPS

void enableRawMode();
void disableRawMode();
int kbhit(void);
void init_terminal();
int init_joystick(GameState *state);
void carregar_nivel(GameState *state);
void inicializar_jogo(GameState *state);
void process_input(GameState *state, double tempo_decorrido);
void check_joystick_input(GameState *state, double tempo_decorrido);
void check_hits(GameState *state, int pista, double tempo_decorrido);
void update_game(GameState *state, double tempo_decorrido);
void render_game(GameState *state, double tempo_decorrido);
void finalizar_jogo(GameState *state);

int main() {
    enableRawMode();
    init_terminal();

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        printf("Não foi possível inicializar o SDL: %s\n", SDL_GetError());
        return -1;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, AUDIO_BUFFER_SIZE) < 0) {
        printf("Não foi possível inicializar o SDL_mixer: %s\n", Mix_GetError());
        return -1;
    }

    GameState game_state;
    memset(&game_state, 0, sizeof(GameState));

    const char *arquivo_musica = "musica_sweet.mp3";

    AudioData *audio_data = load_mp3_file(arquivo_musica);
    if (!audio_data) {
        fprintf(stderr, "Erro ao carregar áudio para análise!\n");
        Mix_CloseAudio();
        SDL_Quit();
        return -1;
    }

    analyze_audio_to_file(audio_data, LEVEL_FILENAME)

    free_audio_data(audio_data);
    
    carregar_nivel(&game_state);
    inicializar_jogo(&game_state);
    game_state.joy_fd = init_joystick(&game_state);

    // Abrir device driver para LEDs e displays
    game_state.device_fd = open("/dev/guitar_hero", O_RDWR);
    if (game_state.device_fd < 0) {
        perror("Erro ao abrir /dev/guitar_hero");
        // Pode continuar sem hardware se quiser
    }

    const char *arquivo_musica = "musica_sweet.mp3";
    game_state.musica = Mix_LoadMUS(arquivo_musica);
    if (game_state.musica == NULL) {
        printf("Não foi possível carregar a música '%s': %s\n", arquivo_musica, Mix_GetError());
        return -1;
    }

    if (system("clear") != 0) {
        fprintf(stderr, "Falha ao limpar tela\n");
    }
    printf("=== GUITAR HERO ===\n\n");
    for (int i = 3; i > 0; i--) {
        printf("Começando em: %d\n", i);
        fflush(stdout);
        SDL_Delay(1000);
    }
    printf("\nJOGUE!\n");

    Mix_PlayMusic(game_state.musica, 1);
    game_state.musica_playing = 1;
    game_state.start_time = SDL_GetTicks();

    SDL_Delay(50);

    float tempo_final_do_nivel = game_state.note_count > 0 ? 
        game_state.level_notes[game_state.note_count - 1].timestamp + 2.0f : 5.0f;

    while (!game_state.game_over && game_state.musica_playing) {
        Uint32 frame_start = SDL_GetTicks();
        double tempo_decorrido = (double)(frame_start - game_state.start_time) / 1000.0;

        if (tempo_decorrido > 0.5f) {
            process_input(&game_state, tempo_decorrido - 0.5f);
            update_game(&game_state, tempo_decorrido - 0.5f);
        }

        render_game(&game_state, tempo_decorrido);

        if (!Mix_PlayingMusic()) {
            game_state.musica_playing = 0;
        }

        Uint32 frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < FRAME_DELAY) {
            SDL_Delay(FRAME_DELAY - frame_time);
        }

        if (tempo_decorrido > tempo_final_do_nivel) {
            game_state.game_over = 1;
        }
    }

    if (game_state.game_over) {
        Mix_HaltMusic();
        game_state.musica_playing = 0;
    }

    while (!kbhit()) {
        render_game(&game_state, 0);
        SDL_Delay(100);
    }

    finalizar_jogo(&game_state);
    return 0;
}

/***********************************************
 *           IMPLEMENTAÇÕES DAS FUNÇÕES
 ***********************************************/

void init_terminal() {
    printf("\033[?25l");
    printf("\033[2J");
}

int init_joystick(GameState *state) {
    int joy_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
    if (joy_fd == -1) {
        printf("Joystick não detectado. Usando apenas teclado.\n");
    }
    return joy_fd;
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\033[?25h");
}

int kbhit(void) {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

void inicializar_jogo(GameState *state) {
    state->score = 0;
    state->combo = 1;
    state->consecutive_misses = 0;
    state->game_over = 0;
    state->musica_playing = 0;
    
    for (int i = 0; i < state->note_count; i++) {
        state->level_notes[i].foi_processada = 0;
        state->level_notes[i].foi_pressionada = 0;
    }
}

void carregar_nivel(GameState *state) {
    FILE *file = fopen(LEVEL_FILENAME, "r");
    if (!file) {
        perror("Não foi possível abrir o arquivo de nível");
        exit(1);
    }

    state->note_count = 0;
    float timestamp;
    char note_name[5];
    
    while (fscanf(file, "%f %s", &timestamp, note_name) == 2) {
        if (state->note_count < MAX_NOTES) {
            int pista_mapeada = -1;

            switch (note_name[0]) {
                case 'C': case 'D': pista_mapeada = 0; break;
                case 'E': case 'F': pista_mapeada = 1; break;
                case 'G': case 'A': pista_mapeada = 2; break;
                case 'B': pista_mapeada = 3; break;
            }

            if (pista_mapeada != -1) {
                state->level_notes[state->note_count].timestamp = timestamp;
                strcpy(state->level_notes[state->note_count].note_name, note_name);
                state->level_notes[state->note_count].note_index = pista_mapeada;
                state->level_notes[state->note_count].foi_processada = 0;
                state->level_notes[state->note_count].foi_pressionada = 0;
                state->note_count++;
            }
        }
    }
    fclose(file);
}

void process_input(GameState *state, double tempo_decorrido) {
    if (state->game_over || !state->musica_playing) return;

    if (kbhit()) { 
        char ch = getchar();
        if (ch == 3) {
            printf("\nJogo encerrado pelo usuário.\n");
            state->game_over = 1;
            return;
        }
        if (ch >= '1' && ch <= '4') {
            check_hits(state, ch - '0', tempo_decorrido);
        }
    }
    
    if (state->joy_fd != -1) {
        check_joystick_input(state, tempo_decorrido);
    }
}

void check_joystick_input(GameState *state, double tempo_decorrido) {
    struct js_event e;
    while (read(state->joy_fd, &e, sizeof(e)) > 0) {
        if (e.type == JS_EVENT_BUTTON && e.value == 1 && e.number < 4) {
            check_hits(state, e.number + 1, tempo_decorrido);
        }
    }
}

void check_hits(GameState *state, int pista, double tempo_decorrido) {
    int hit = 0;
    
    for (int i = 0; i < state->note_count; i++) {
        if (state->level_notes[i].foi_processada) continue;
        
        float timestamp_nota = state->level_notes[i].timestamp;
        int pista_nota = state->level_notes[i].note_index;
        
        if ((pista == pista_nota) && 
            (tempo_decorrido > timestamp_nota - 0.2 && tempo_decorrido < timestamp_nota + 0.2)) {
            
            // Acertou nota
            printf("\a");
            state->score += 10 * state->combo;
            state->combo++;
            state->consecutive_misses = 0;
            state->level_notes[i].foi_processada = 1;
            state->level_notes[i].foi_pressionada = 1;
            hit = 1;

            // Acende LEDs verdes e apaga vermelhos
            if (state->device_fd >= 0) {
                unsigned char leds_verde = 0xFF;
                unsigned char leds_vermelho = 0x00;
                ioctl(state->device_fd, WR_GREEN_LEDS, &leds_verde);
                ioctl(state->device_fd, WR_RED_LEDS, &leds_vermelho);

                // Atualiza displays
                ioctl(state->device_fd, WR_L_DISPLAY, &state->score);
                ioctl(state->device_fd, WR_R_DISPLAY, &state->combo);
            }

            break;
        }
    }
    
    if (!hit && pista != 0) {
        state->consecutive_misses++;
        state->combo = 1;

        // Acende LEDs vermelhos e apaga verdes
        if (state->device_fd >= 0) {
            unsigned char leds_verde = 0x00;
            unsigned char leds_vermelho = 0xFF;
            ioctl(state->device_fd, WR_GREEN_LEDS, &leds_verde);
            ioctl(state->device_fd, WR_RED_LEDS, &leds_vermelho);

            // Atualiza displays
            ioctl(state->device_fd, WR_L_DISPLAY, &state->score);
            ioctl(state->device_fd, WR_R_DISPLAY, &state->combo);
        }

        if (state->consecutive_misses >= MAX_MISSES) {
            state->game_over = 1;
            Mix_HaltMusic();
            state->musica_playing = 0;
        }
    }
}

void update_game(GameState *state, double tempo_decorrido) {
    for (int i = 0; i < state->note_count; i++) {
        if (!state->level_notes[i].foi_processada && 
            tempo_decorrido > state->level_notes[i].timestamp + 0.2) {
            
            state->level_notes[i].foi_processada = 1;
            
            if (!state->level_notes[i].foi_pressionada) {
                state->consecutive_misses++;
                state->combo = 1;

                // Acende LEDs vermelhos e apaga verdes
                if (state->device_fd >= 0) {
                    unsigned char leds_verde = 0x00;
                    unsigned char leds_vermelho = 0xFF;
                    ioctl(state->device_fd, WR_GREEN_LEDS, &leds_verde);
                    ioctl(state->device_fd, WR_RED_LEDS, &leds_vermelho);

                    // Atualiza displays
                    ioctl(state->device_fd, WR_L_DISPLAY, &state->score);
                    ioctl(state->device_fd, WR_R_DISPLAY, &state->combo);
                }

                if (state->consecutive_misses >= MAX_MISSES) {
                    state->game_over = 1;
                    Mix_HaltMusic();
                    state->musica_playing = 0;
                }
            }
        }
    }
}

void render_game(GameState *state, double tempo_decorrido) {
    if (system("clear") != 0) {
        fprintf(stderr, "Falha ao limpar tela\n");
    }

    char pista_visual[ALTURA_DA_PISTA][5];
    for (int i = 0; i < ALTURA_DA_PISTA; i++) {
        sprintf(pista_visual[i], "    ");
    }

    double tempo_ajustado = tempo_decorrido > 0.5f ? tempo_decorrido - 0.5f : 0;

    for (int i = 0; i < state->note_count; i++) {
        if (!state->level_notes[i].foi_processada) {
            float tempo_da_nota = state->level_notes[i].timestamp;
            float dist_temporal = tempo_da_nota - tempo_ajustado;

            if (dist_temporal >= 0 && dist_temporal < TEMPO_DE_ANTEVISAO) {
                int linha = ALTURA_DA_PISTA - 1 - (int)((dist_temporal / TEMPO_DE_ANTEVISAO) * ALTURA_DA_PISTA);
                if (linha >= 0 && linha < ALTURA_DA_PISTA) {
                    int pista_da_nota = state->level_notes[i].note_index;
                    pista_visual[linha][pista_da_nota] = (pista_da_nota + 1) + '0';
                }
            }
        }
    }

    printf(COLOR_GREEN "PISTA 1 " COLOR_RESET "| " 
           COLOR_RED "2 " COLOR_RESET "| " 
           COLOR_YELLOW "3 " COLOR_RESET "| " 
           COLOR_BLUE "4\n" COLOR_RESET);
    
    printf("+--------+\n");
    for (int i = 0; i < ALTURA_DA_PISTA; i++) {
        printf("|");
        for (int j = 0; j < 4; j++) {
            char note = pista_visual[i][j];
            if (note != ' ') {
                switch (j) {
                    case 0: printf(COLOR_GREEN "%c" COLOR_RESET, note); break;
                    case 1: printf(COLOR_RED "%c" COLOR_RESET, note); break;
                    case 2: printf(COLOR_YELLOW "%c" COLOR_RESET, note); break;
                    case 3: printf(COLOR_BLUE "%c" COLOR_RESET, note); break;
                }
            } else {
                printf(" ");
            }
            printf("|");
        }
        printf("\n");
    }
    printf("+--------+  <-- ZONA DE ACERTO\n");

    printf("Tempo: %.2f s | Pontos: %d | Combo: x%d | Erros: %d/%d\n", 
           tempo_ajustado, state->score, state->combo, 
           state->consecutive_misses, MAX_MISSES);

    if (state->game_over) {
        printf("\n\033[31mGAME OVER! Você errou %d vezes consecutivas.\033[0m\n", MAX_MISSES);
        printf("\033[33mPontuação final: %d\033[0m\n", state->score);
        printf("Pressione qualquer tecla para sair...\n");
    }
}

void finalizar_jogo(GameState *state) {
    if (state->musica_playing) {
        Mix_HaltMusic();
    }

    // Apaga LEDs ao finalizar
    if (state->device_fd >= 0) {
        unsigned char leds_off = 0x00;
        ioctl(state->device_fd, WR_GREEN_LEDS, &leds_off);
        ioctl(state->device_fd, WR_RED_LEDS, &leds_off);
        close(state->device_fd);
    }

    printf("\nFim de jogo! Pontuação Final: %d\n", state->score);
    if (state->joy_fd != -1) close(state->joy_fd);
    if (state->musica != NULL) Mix_FreeMusic(state->musica);
    Mix_Quit();
    SDL_Quit();
    disableRawMode();
}
