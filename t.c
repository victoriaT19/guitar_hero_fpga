// main.c (integração completa com p-buttons e análise áudio)
#include "guitar_hero.h"
#include "mapeamento_audio.h"   // <<< INCLUSÃO DA BIBLIOTECA DE MAPEAMENTO DE ÁUDIO

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <linux/joystick.h>

#include "../../include/ioctl_cmds.h" // ajuste o caminho se necessário

struct termios orig_termios;

/* protótipos extras */
int init_pbuttons(GameState *state);
void process_input(GameState *state, double tempo_decorrido);
void check_joystick_input(GameState *state, double tempo_decorrido);
void check_hits(GameState *state, int pista, double tempo_decorrido);
int init_joystick(GameState *state);

/* ----------------- main adaptado para chamar análise do áudio ----------------- */
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

    // *** INÍCIO DA INTEGRAÇÃO ***
    const char *arquivo_musica = "musica_sweet.mp3";

    AudioData *audio_data = load_mp3_file(arquivo_musica);
    if (!audio_data) {
        fprintf(stderr, "Erro ao carregar áudio para análise!\n");
        Mix_CloseAudio();
        SDL_Quit();
        return -1;
    }

    if (analyze_audio_to_file(audio_data, LEVEL_FILENAME) != 0) {
        fprintf(stderr, "Erro ao gerar arquivo de notas '%s'\n", LEVEL_FILENAME);
        free_audio_data(audio_data);
        Mix_CloseAudio();
        SDL_Quit();
        return -1;
    }

    free_audio_data(audio_data);
    // *** FIM DA INTEGRAÇÃO ***

    carregar_nivel(&game_state);
    inicializar_jogo(&game_state);
    game_state.joy_fd = init_joystick(&game_state);
    game_state.fd_pbuttons = init_pbuttons(&game_state);

    game_state.musica = Mix_LoadMUS(arquivo_musica);
    if (game_state.musica == NULL) {
        fprintf(stderr, "Não foi possível carregar a música '%s': %s\n", arquivo_musica, Mix_GetError());
        if (game_state.fd_pbuttons != -1) close(game_state.fd_pbuttons);
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

/***********************************************
 *           IMPLEMENTAÇÕES DAS FUNÇÕES
 ***********************************************/

/* -- (Aqui mantêm-se todas as suas implementações originais para funções como init_terminal, init_joystick, init_pbuttons, enableRawMode, etc.) -- */

/* Resto do código permanece o mesmo, sem alterações */


/***********************************************
 *           IMPLEMENTAÇÕES DAS FUNÇÕES
 ***********************************************/

/* (mantenha suas implementações originais; abaixo apenas acrescentei verificações
   e a integração de fd_pbuttons quando necessário) */

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

/* Inicializa leitura do char device que o seu driver cria (/dev/mydev)
   Faz o ioctl RD_PBUTTONS para que o driver passe a ler os p-buttons */
int init_pbuttons(GameState *state) {
    int fd = open("/dev/mydev", O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("Falha ao abrir /dev/mydev para pbuttons");
        return -1;
    }
    if (ioctl(fd, RD_PBUTTONS) < 0) {
        perror("ioctl RD_PBUTTONS falhou");
        close(fd);
        return -1;
    }
    return fd;
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

    /* inicializa descritores como -1 por padrão */
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

void process_input(GameState *state, double tempo_decorrido) {
    if (state->game_over || !state->musica_playing) return;

    // 1) teclado
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
    
    // 2) joystick
    if (state->joy_fd != -1) {
        check_joystick_input(state, tempo_decorrido);
    }

    // 3) push-buttons da placa (lê um uint32_t do device)
    if (state->fd_pbuttons != -1) {
        uint32_t buttons = 0;
        ssize_t r = read(state->fd_pbuttons, &buttons, sizeof(buttons));
        if (r == sizeof(buttons)) {
            // supondo que bit0 -> botão 1, bit1 -> botão 2, etc.
            for (int i = 0; i < 4; i++) {
                if (buttons & (1u << i)) {
                    check_hits(state, i + 1, tempo_decorrido);
                }
            }
        } else if (r == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // erro real de leitura
            perror("Erro lendo /dev/mydev");
        }
        // se EAGAIN/EWOULDBLOCK, não havia dados — ok (non-blocking)
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
            printf("\a");
            state->score += 10 * state->combo;
            state->combo++;
            state->consecutive_misses = 0;
            state->level_notes[i].foi_processada = 1;
            state->level_notes[i].foi_pressionada = 1;
            hit = 1;
            break;
        }
    }
    
    if (!hit && pista != 0) {
        state->consecutive_misses++;
        state->combo = 1;
        
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

    // Ajuste para mostrar notas apenas após 0.5s
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
        printf("\n\033[31mGAME OVER! Você errou 3 vezes consecutivas.\033[0m\n");
        printf("\033[33mPontuação final: %d\033[0m\n", state->score);
        printf("Pressione qualquer tecla para sair...\n");
    }
}

void finalizar_jogo(GameState *state) {
    if (state->musica_playing) {
        Mix_HaltMusic();
    }
    printf("\nFim de jogo! Pontuação Final: %d\n", state->score);
    if (state->joy_fd != -1) close(state->joy_fd);
    if (state->fd_pbuttons != -1) close(state->fd_pbuttons);
    if (state->musica != NULL) Mix_FreeMusic(state->musica);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    disableRawMode();
}
