#include "guitar_hero.h"
#include "display.h"
#include "ioctl_cmds.h"

struct termios orig_termios;

// Variáveis globais para controle de hardware
static int fd_leds = -1;
static int fd_display = -1;

// Inicializa os dispositivos da placa DE2i-150
void init_hardware() {
    fd_leds = open("/dev/mydev", O_WRONLY);
    if (fd_leds == -1) {
        perror("Falha ao abrir /dev/mydev para LEDs");
    }

    fd_display = open("/dev/mydev", O_WRONLY);
    if (fd_display == -1) {
        perror("Falha ao abrir /dev/mydev para displays");
    }

    // Inicializa LEDs e displays
    uint32_t val = 0;
    if (fd_leds != -1) {
        ioctl(fd_leds, WR_GREEN_LEDS);
        write(fd_leds, &val, sizeof(val));
        ioctl(fd_leds, WR_RED_LEDS);
        write(fd_leds, &val, sizeof(val));
    }

    if (fd_display != -1) {
        val = HEX_0;
        ioctl(fd_display, WR_L_DISPLAY);
        write(fd_display, &val, sizeof(val));
        ioctl(fd_display, WR_R_DISPLAY);
        write(fd_display, &val, sizeof(val));
    }
}

// Fecha os dispositivos da placa
void close_hardware() {
    uint32_t val = 0;
    if (fd_leds != -1) {
        ioctl(fd_leds, WR_GREEN_LEDS);
        write(fd_leds, &val, sizeof(val));
        ioctl(fd_leds, WR_RED_LEDS);
        write(fd_leds, &val, sizeof(val));
        close(fd_leds);
    }

    if (fd_display != -1) {
        val = HEX_0;
        ioctl(fd_display, WR_L_DISPLAY);
        write(fd_display, &val, sizeof(val));
        ioctl(fd_display, WR_R_DISPLAY);
        write(fd_display, &val, sizeof(val));
        close(fd_display);
    }
}

// Atualiza os displays de 7 segmentos
void update_displays(int score, int errors) {
    if (fd_display == -1) return;

    // Display direito (pontuação: 3 dígitos)
    uint32_t right_display = 0;
    right_display |= (HEX_0 + (score % 10)) << 16;      // Unidade
    right_display |= (HEX_0 + ((score / 10) % 10)) << 8; // Dezena
    right_display |= (HEX_0 + (score / 100) % 10);      // Centena

    // Display esquerdo (erros: 1 dígito)
    uint32_t left_display = HEX_0 + (errors % 10);

    ioctl(fd_display, WR_R_DISPLAY);
    write(fd_display, &right_display, sizeof(right_display));

    ioctl(fd_display, WR_L_DISPLAY);
    write(fd_display, &left_display, sizeof(left_display));
}

// Pisca os LEDs (verde = acerto, vermelho = erro)
void flash_led(int color) {
    if (fd_leds == -1) return;

    uint32_t leds_on = 0xFF;  // Todos os LEDs acesos
    uint32_t leds_off = 0x00; // Todos os LEDs apagados

    if (color == 1) { // LED Verde (acerto)
        ioctl(fd_leds, WR_GREEN_LEDS);
        write(fd_leds, &leds_on, sizeof(leds_on));
        ioctl(fd_leds, WR_RED_LEDS);
        write(fd_leds, &leds_off, sizeof(leds_off));
    } else { // LED Vermelho (erro)
        ioctl(fd_leds, WR_RED_LEDS);
        write(fd_leds, &leds_on, sizeof(leds_on));
        ioctl(fd_leds, WR_GREEN_LEDS);
        write(fd_leds, &leds_off, sizeof(leds_off));
    }

    SDL_Delay(100); // Mantém LEDs acesos por 100ms

    // Apaga os LEDs
    ioctl(fd_leds, WR_GREEN_LEDS);
    write(fd_leds, &leds_off, sizeof(leds_off));
    ioctl(fd_leds, WR_RED_LEDS);
    write(fd_leds, &leds_off, sizeof(leds_off));
}

// Lê os push-buttons da placa
int read_pbuttons(int fd) {
    if (fd == -1) return 0;

    uint32_t buttons = 0;
    ssize_t r = read(fd, &buttons, sizeof(buttons));
    if (r == sizeof(buttons)) {
        return (~buttons) & 0xF; // Inverte os bits (botões são ativos baixo)
    }
    return 0;
}

// Verifica se o jogador acertou a nota
void check_hits(GameState *state, int pista, double tempo_decorrido) {
    int hit = 0;

    for (int i = 0; i < state->note_count; i++) {
        if (state->level_notes[i].foi_processada) continue;

        float timestamp_nota = state->level_notes[i].timestamp;
        int pista_nota = state->level_notes[i].note_index;

        // Verifica se o botão foi pressionado na hora certa (±150ms)
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

            flash_led(1); // Acende LED verde
            break;
        }
    }

    if (!hit && pista != 0) {
        state->consecutive_misses++;
        state->combo = 1;
        flash_led(0); // Acende LED vermelho

        if (state->consecutive_misses >= MAX_MISSES) {
            state->game_over = 1;
            Mix_HaltMusic();
            state->musica_playing = 0;
        }
    }
}

// Processa entrada do teclado, joystick e push-buttons
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
    if (state->fd_pbuttons != -1) {
        int buttons = read_pbuttons(state->fd_pbuttons);
        for (int i = 0; i < 4; i++) {
            if (buttons & (1 << i)) {
                check_hits(state, i + 1, tempo_decorrido);
            }
        }
    }
}

// Atualiza o estado do jogo
void update_game(GameState *state, double tempo_decorrido) {
    for (int i = 0; i < state->note_count; i++) {
        if (!state->level_notes[i].foi_processada && 
            tempo_decorrido > state->level_notes[i].timestamp + 0.15) {

            state->level_notes[i].foi_processada = 1;

            if (!state->level_notes[i].foi_pressionada) {
                state->consecutive_misses++;
                state->combo = 1;
                flash_led(0); // LED vermelho (erro por não apertar a tempo)

                if (state->consecutive_misses >= MAX_MISSES) {
                    state->game_over = 1;
                    Mix_HaltMusic();
                    state->musica_playing = 0;
                }
            }
        }
    }

    // Atualiza os displays
    update_displays(state->score, state->consecutive_misses);
}

// Renderiza o jogo no terminal
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

// Função principal
int main() {
    enableRawMode();
    init_terminal();
    init_hardware();

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

    close_hardware();
    finalizar_jogo(&game_state);
    return 0;
}
