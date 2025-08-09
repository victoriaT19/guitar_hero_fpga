//=======================================================
// Arquivo: guitar_hero.c (Refatorado)
// Descrição: Programa principal do jogo Guitar Hero
// com correções para o hardware e a estrutura.
//=======================================================

#include "guitar_hero.h"
#include "ioctl_cmds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <linux/joystick.h>

// Definir as constantes para os displays de 7 segmentos.
// Estes valores são baseados na sua função seg7_convert_digit.
#define HEX_0 0b00111111 // 0
#define HEX_1 0b00000110 // 1
#define HEX_2 0b01011011 // 2
#define HEX_3 0b01001111 // 3
#define HEX_4 0b01100110 // 4
#define HEX_5 0b01101101 // 5
#define HEX_6 0b01111101 // 6
#define HEX_7 0b00000111 // 7
#define HEX_8 0b01111111 // 8
#define HEX_9 0b01100111 // 9
#define HEX_A 0b01110111 // A
#define HEX_B 0b01111100 // B
#define HEX_C 0b00111001 // C
#define HEX_D 0b01011110 // D
#define HEX_E 0b01111001 // E
#define HEX_F 0b01110001 // F

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
    int fd_pbuttons;
    int fd_hardware; // Novo descritor para todas as operações
} GameState;

// Estrutura de dados para o processamento do MP3 (fora do escopo da refatoração)
typedef struct {
    uint8_t *data;
    size_t length;
    int sample_rate;
} AudioData;

struct termios orig_termios;

// =============================================
// FUNÇÕES DE HARDWARE (DE2i-150)
// =============================================

// Abre o dispositivo /dev/mydev apenas uma vez
int init_hardware() {
    int fd = open("/dev/mydev", O_RDWR);
    if (fd == -1) {
        perror("Falha ao abrir /dev/mydev");
        return -1;
    }
    printf("Dispositivo /dev/mydev aberto com sucesso (fd: %d).\n", fd);
    
    // Inicializa LEDs e displays para 0
    uint32_t val = 0;
    ioctl(fd, WR_GREEN_LEDS);
    write(fd, &val, sizeof(val));
    
    ioctl(fd, WR_RED_LEDS);
    write(fd, &val, sizeof(val));
    
    // Limpa os displays
    val = 0xFFFFFFFF;
    ioctl(fd, WR_L_DISPLAY);
    write(fd, &val, sizeof(val));
    
    ioctl(fd, WR_R_DISPLAY);
    write(fd, &val, sizeof(val));
    
    return fd;
}

// Fecha o dispositivo e apaga os LEDs/displays
void close_hardware(int fd) {
    if (fd != -1) {
        uint32_t val = 0;
        
        // Apaga os LEDs
        ioctl(fd, WR_GREEN_LEDS);
        write(fd, &val, sizeof(val));
        ioctl(fd, WR_RED_LEDS);
        write(fd, &val, sizeof(val));
        
        // Apaga os displays
        val = 0xFFFFFFFF;
        ioctl(fd, WR_L_DISPLAY);
        write(fd, &val, sizeof(val));
        ioctl(fd, WR_R_DISPLAY);
        write(fd, &val, sizeof(val));
        
        close(fd);
    }
}

// Atualiza os displays de 7 segmentos com o score e erros
void update_displays(int fd, int score, int errors) {
    if (fd == -1) return;

    // Display esquerdo (erros: 1 dígito)
    // O seu código original estava com a lógica de L e R trocada.
    uint32_t left_display = (~(HEX_0 + (errors % 10))) & 0x7F;
    ioctl(fd, WR_L_DISPLAY);
    write(fd, &left_display, sizeof(left_display));

    // Display direito (pontuação: 3 dígitos)
    uint32_t right_display = 0;
    // O seu código original tinha o mapeamento incorreto.
    // O correto para a placa é: HEX3 | HEX2 | HEX1 | HEX0
    // Lembre-se que os bits são 7 por dígito.
    right_display |= (~(HEX_0 + (score / 100) % 10)) & 0x7F;
    right_display |= ((~(HEX_0 + (score / 10) % 10)) & 0x7F) << 7;
    right_display |= ((~(HEX_0 + (score % 10))) & 0x7F) << 14;
    ioctl(fd, WR_R_DISPLAY);
    write(fd, &right_display, sizeof(right_display));
}

// Pisca os LEDs (verde para acerto, vermelho para erro)
void flash_led(int fd, int color) {
    if (fd == -1) return;

    uint32_t leds_on = 0x01; // Liga apenas o primeiro LED para feedback visual
    uint32_t leds_off = 0x00;

    if (color == 1) { // LED Verde (acerto)
        ioctl(fd, WR_GREEN_LEDS);
        write(fd, &leds_on, sizeof(leds_on));
        ioctl(fd, WR_RED_LEDS);
        write(fd, &leds_off, sizeof(leds_off));
    } else { // LED Vermelho (erro)
        ioctl(fd, WR_RED_LEDS);
        write(fd, &leds_on, sizeof(leds_on));
        ioctl(fd, WR_GREEN_LEDS);
        write(fd, &leds_off, sizeof(leds_off));
    }

    usleep(100000); // Mantém LEDs acesos por 100ms
    
    // Apaga os LEDs
    ioctl(fd, WR_GREEN_LEDS);
    write(fd, &leds_off, sizeof(leds_off));
    ioctl(fd, WR_RED_LEDS);
    write(fd, &leds_off, sizeof(leds_off));
}

// Lê o estado dos push-buttons
int read_pbuttons(int fd) {
    if (fd == -1) return 0;
    
    uint32_t buttons = 0;
    
    // Configura o ioctl para leitura dos botões
    if (ioctl(fd, RD_PBUTTONS) < 0) {
        perror("ioctl RD_PBUTTONS falhou");
        return 0;
    }
    
    ssize_t r = read(fd, &buttons, sizeof(buttons));
    if (r == sizeof(buttons)) {
        return (~buttons) & 0xF; // Retorna apenas os 4 bits menos significativos
    }
    
    return 0;
}

// =============================================
// FUNÇÕES DO JOGO
// =============================================

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

void init_terminal() {
    printf("\033[?25l"); // Esconde cursor
    printf("\033[2J");   // Limpa tela
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
    state->fd_pbuttons = -1;
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

void check_hits(GameState *state, int pista, double tempo_decorrido) {
    int hit = 0;

    for (int i = 0; i < state->note_count; i++) {
        if (state->level_notes[i].foi_processada) continue;
        float timestamp_nota = state->level_notes[i].timestamp;
        int pista_nota = state->level_notes[i].note_index;

        if ((pista == pista_nota + 1) && 
            (tempo_decorrido > timestamp_nota - 0.15 && 
             tempo_decorrido < timestamp_nota + 0.15)) {
            printf("\a"); // Beep sonoro
            state->score += 10 * state->combo;
            state->combo++;
            state->consecutive_misses = 0;
            state->level_notes[i].foi_processada = 1;
            state->level_notes[i].foi_pressionada = 1;
            hit = 1;
            flash_led(state->fd_hardware, 1); // Acende LED verde
            break;
        }
    }

    if (!hit && pista != 0) {
        state->consecutive_misses++;
        state->combo = 1;
        flash_led(state->fd_hardware, 0); // Acende LED vermelho

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
    // 3) Push-buttons da placa
    if (state->fd_hardware != -1) {
        int buttons = read_pbuttons(state->fd_hardware);
        for (int i = 0; i < 4; i++) {
            if (buttons & (1 << i)) {
                check_hits(state, i + 1, tempo_decorrido);
            }
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
                flash_led(state->fd_hardware, 0); // LED vermelho (erro por não apertar a tempo)
                if (state->consecutive_misses >= MAX_MISSES) {
                    state->game_over = 1;
                    Mix_HaltMusic();
                    state->musica_playing = 0;
                }
            }
        }
    }
    // Atualiza os displays
    update_displays(state->fd_hardware, state->score, state->consecutive_misses);
}

void render_game(GameState *state, double tempo_decorrido) {
    if (system("clear") != 0) {
        fprintf(stderr, "Falha ao limpar tela\n");
    }
    char pista_visual[ALTURA_DA_PISTA][5];
    for (int i = 0; i < ALTURA_DA_PISTA; i++) {
        sprintf(pista_visual[i], "    ");
    }
    // Mostrar notas apenas após 0.5s
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

void finalizar_jogo(GameState *state) {
    if (state->musica_playing) {
        Mix_HaltMusic();
    }
    close_hardware(state->fd_hardware);
    if (state->joy_fd != -1) close(state->joy_fd);
    if (state->musica != NULL) Mix_FreeMusic(state->musica);
    Mix_CloseAudio();
    SDL_Quit();
    disableRawMode();
}

// Funções de análise de áudio (simplificadas)
AudioData* load_mp3_file(const char *filename) {
    AudioData *audio = malloc(sizeof(AudioData));
    if (!audio) return NULL;
    audio->data = NULL;
    audio->length = 0;
    audio->sample_rate = 44100;
    return audio;
}

void free_audio_data(AudioData *audio_data) {
    if (audio_data) {
        free(audio_data->data);
        free(audio_data);
    }
}

void analyze_audio_to_file(AudioData *audio_data, const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        perror("Não foi possível criar arquivo de nível");
        return;
    }
    // Gera notas aleatórias para demonstração
    for (int i = 0; i < 100; i++) {
        float timestamp = i * 0.5f;
        const char *notes[] = {"C", "D", "E", "F", "G", "A", "B"};
        fprintf(file, "%.2f %s\n", timestamp, notes[i % 7]);
    }
    fclose(file);
}

// =============================================
// MAIN (Ponto de entrada do programa)
// =============================================
int main() {
    enableRawMode();
    init_terminal();
    
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
    
    game_state.fd_hardware = init_hardware();
    if (game_state.fd_hardware == -1) {
        fprintf(stderr, "O jogo não pode continuar sem acesso ao hardware. A comunicação com a placa pode estar com problemas.\n");
        SDL_Quit();
        return -1;
    }

    const char *arquivo_musica = "musica_sweet.mp3";
    AudioData *audio_data = load_mp3_file(arquivo_musica);
    if (!audio_data) {
        fprintf(stderr, "Erro ao carregar áudio para análise!\n");
        finalizar_jogo(&game_state);
        return -1;
    }
    analyze_audio_to_file(audio_data, LEVEL_FILENAME);
    free_audio_data(audio_data);

    carregar_nivel(&game_state);
    inicializar_jogo(&game_state);
    game_state.joy_fd = init_joystick(&game_state);
    
    game_state.musica = Mix_LoadMUS(arquivo_musica);
    if (game_state.musica == NULL) {
        fprintf(stderr, "Não foi possível carregar a música '%s': %s\n", arquivo_musica, Mix_GetError());
        finalizar_jogo(&game_state);
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
