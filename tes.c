//=======================================================
// Arquivo: main.c
// Descrição: Programa principal do jogo Guitar Hero
// usando as novas bibliotecas de hardware.
//=======================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <linux/joystick.h>

#include "device.h" // Inclui todas as novas funções de hardware

// Constantes do jogo
#define LEVEL_FILENAME "level_data.txt"
#define MAX_NOTES 5000
#define MAX_MISSES 3
#define FRAME_DELAY 10 // Aprox. 100 FPS
#define ALTURA_DA_PISTA 10
#define TEMPO_DE_ANTEVISAO 2.0f

// Cores para o terminal
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED "\033[0;31m"
#define COLOR_BLUE "\033[0;34m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_RESET "\033[0m"

// Estruturas do jogo
typedef struct {
    float timestamp;
    char note_name[5];
    int note_index;
    int foi_processada;
    int foi_pressionada;
} Note;

typedef struct {
    int score;
    int combo;
    int consecutive_misses;
    int game_over;
    Uint32 start_time;
    int musica_playing;
    Mix_Music *musica;
    Note level_notes[MAX_NOTES];
    int note_count;
    int joy_fd;
    // O fd_pbuttons agora é gerenciado pela biblioteca 'device.c'
} GameState;

// Estrutura de dados para o processamento do MP3 (fora do escopo da refatoração)
typedef struct {
    Uint8 *audio_buffer;
    Uint32 audio_len;
    SDL_AudioSpec spec;
} AudioData;

// Protótipos de funções (algumas foram removidas pois estão na nova biblioteca)
AudioData *load_mp3_file(const char *filename);
void analyze_audio_to_file(AudioData *audio_data, const char *filename);
void free_audio_data(AudioData *audio_data);
void enableRawMode();
void disableRawMode();
int kbhit(void);
void init_terminal();
int init_joystick(GameState *state);
void inicializar_jogo(GameState *state);
void carregar_nivel(GameState *state);
void check_hits(GameState *state, int pista, double tempo_decorrido);
void process_input(GameState *state, double tempo_decorrido);
void update_game(GameState *state, double tempo_decorrido);
void render_game(GameState *state, double tempo_decorrido);
void finalizar_jogo(GameState *state);

// =============================================
// FUNÇÕES DE JOGO REFATORADAS
// =============================================

void check_hits(GameState *state, int pista, double tempo_decorrido) {
    int hit = 0;
    for (int i = 0; i < state->note_count; i++) {
        if (state->level_notes[i].foi_processada) continue;
        float timestamp_nota = state->level_notes[i].timestamp;
        int pista_nota = state->level_notes[i].note_index;
        if ((pista == pista_nota + 1) &&
            (tempo_decorrido > timestamp_nota - 0.15 &&
             tempo_decorrido < timestamp_nota + 0.15)) {
            printf("\a");
            state->score += 10 * state->combo;
            state->combo++;
            state->consecutive_misses = 0;
            state->level_notes[i].foi_processada = 1;
            state->level_notes[i].foi_pressionada = 1;
            hit = 1;
            d_write_green_leds(1); // Usa a nova função
            usleep(100000); // 100ms
            d_write_green_leds(0);
            break;
        }
    }
    if (!hit && pista != 0) {
        state->consecutive_misses++;
        state->combo = 1;
        d_write_red_leds(1); // Usa a nova função
        usleep(100000); // 100ms
        d_write_red_leds(0);
        if (state->consecutive_misses >= MAX_MISSES) {
            state->game_over = 1;
            Mix_HaltMusic();
            state->musica_playing = 0;
        }
    }
}

void process_input(GameState *state, double tempo_decorrido) {
    if (state->game_over || !state->musica_playing) return;
    // 1) Teclado
    if (kbhit()) {
        char ch = getchar();
        if (ch == 3) { // Ctrl+C para sair
            printf("\nJogo encerrado pelo usuário.\n");
            state->game_over = 1;
            return;
        }
        if (ch >= '1' && ch <= '4') {
            check_hits(state, ch - '0', tempo_decorrido);
        }
    }
    // 2) Joystick
    if (state->joy_fd != -1) {
        struct js_event e;
        while (read(state->joy_fd, &e, sizeof(e)) > 0) {
            if (e.type == JS_EVENT_BUTTON && e.value == 1 && e.number < 4) {
                check_hits(state, e.number + 1, tempo_decorrido);
            }
        }
    }
    // 3) Push-buttons da placa (agora usando a nova função)
    int buttons = d_button_read();
    for (int i = 0; i < 4; i++) {
        if (buttons & (1 << i)) {
            check_hits(state, i + 1, tempo_decorrido);
        }
    }
}

void update_game(GameState *state, double tempo_decorrido) {
    for (int i = 0; i < state->note_count; i++) {
        if (!state->level_notes[i].foi_processada &&
            tempo_decorrido > state->level_notes[i].timestamp + 0.15) {
            state->level_notes[i].foi_processada = 1;
            if (!state->level_notes[i].foi_pressionada) {
                state->consecutive_misses++;
                state->combo = 1;
                d_write_red_leds(1); // Usa a nova função
                usleep(100000); // 100ms
                d_write_red_leds(0);
                if (state->consecutive_misses >= MAX_MISSES) {
                    state->game_over = 1;
                    Mix_HaltMusic();
                    state->musica_playing = 0;
                }
            }
        }
    }
    // Atualiza os displays usando a nova função
    seg7_write(state->score);
    // Nota: O seg.c não tem uma função para o display da esquerda,
    // então a lógica para "erros" precisa ser implementada.
    // Para simplificar, vou apenas exibir o score no momento.
}

void finalizar_jogo(GameState *state) {
    if (state->musica_playing) {
        Mix_HaltMusic();
    }
    d_shutdown(); // Usa a nova função de desligar
    if (state->joy_fd != -1) close(state->joy_fd);
    if (state->musica != NULL) Mix_FreeMusic(state->musica);
    Mix_CloseAudio();
    SDL_Quit();
    disableRawMode();
}

int main() {
    enableRawMode();
    init_terminal();
    d_init(); // Usa a nova função de inicialização

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "Não foi possível inicializar o SDL: %s\n", SDL_GetError());
        return -1;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        fprintf(stderr, "Não foi possível inicializar o SDL_mixer: %s\n", Mix_GetError());
        SDL_Quit();
        return -1;
    }
    Mix_AllocateChannels(16);

    GameState game_state;
    memset(&game_state, 0, sizeof(GameState));
    const char *arquivo_musica = "musica_sweet.mp3";

    // O restante do main() permanece inalterado, pois a lógica não foi alterada.
    AudioData *audio_data = load_mp3_file(arquivo_musica);
    if (!audio_data) {
        fprintf(stderr, "Erro ao carregar áudio para análise!\n");
        Mix_CloseAudio();
        SDL_Quit();
        return -1;
    }
    analyze_audio_to_file(audio_data, LEVEL_FILENAME);
    free_audio_data(audio_data);

    carregar_nivel(&game_state);
    inicializar_jogo(&game_state);
    game_state.joy_fd = init_joystick(&game_state);
    
    // A inicialização dos botões agora é feita em d_init(), então removemos o init_pbuttons
    
    game_state.musica = Mix_LoadMUS(arquivo_musica);
    if (game_state.musica == NULL) {
        fprintf(stderr, "Não foi possível carregar a música '%s': %s\n", arquivo_musica, Mix_GetError());
        if (game_state.joy_fd != -1) close(game_state.joy_fd);
        Mix_CloseAudio();
        SDL_Quit();
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

    if (Mix_PlayMusic(game_state.musica, 1) == -1) {
        fprintf(stderr, "Erro ao tocar musica: %s\n", Mix_GetError());
    } else {
        game_state.musica_playing = 1;
    }
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

// Funções que não foram alteradas
void enableRawMode() {
    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode() {
    struct termios orig_termios;
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
    state->joy_fd = -1;
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
           COLOR_BLUE "3 " COLOR_RESET "| "
           COLOR_YELLOW "4\n" COLOR_RESET);
    printf("+--------+\n");
    for (int i = 0; i < ALTURA_DA_PISTA; i++) {
        printf("|");
        for (int j = 0; j < 4; j++) {
            char note = pista_visual[i][j];
            if (note != ' ') {
                switch (j) {
                    case 0: printf(COLOR_GREEN "%c" COLOR_RESET, note); break;
                    case 1: printf(COLOR_RED "%c" COLOR_RESET, note); break;
                    case 2: printf(COLOR_BLUE "%c" COLOR_RESET, note); break;
                    case 3: printf(COLOR_YELLOW "%c" COLOR_RESET, note); break;
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
        printf("\n\033[31mGAME OVER! Você errou 3 vezes consecutivas.\033[0m\n");
        printf("\033[33mPontuação final: %d\033[0m\n", state->score);
        printf("Pressione qualquer tecla para sair...\n");
    }
}

// As funções load_mp3_file, analyze_audio_to_file e free_audio_data não são relevantes
// para a depuração do hardware e não foram incluídas neste código.
