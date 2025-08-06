#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>    // Para controlar o tempo
#include <unistd.h>  // Para a função sleep/usleep
#include <sys/time.h>
#include <termios.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#define MAX_NOTES 2000
#define LEVEL_FILENAME "notes.txt"

// Estrutura para guardar uma única nota do mapa
typedef struct {
    float timestamp; // Em que segundo a nota aparece
    char note_name[5];
    int note_index;  // A qual "pista" a nota pertence (0 a 4, por exemplo)
    int foi_processada; // Flag para saber se já acertamos ou erramos essa nota
} GameNote;

// Estrutura para guardar todo o estado do jogo
typedef struct {
    GameNote level_notes[MAX_NOTES];
    int note_count;

    int score;
    int combo;
    
    struct timeval start_time; // Momento em que o jogo começou
} GameState;
struct termios orig_termios;

void carregar_nivel(GameState *state);
void inicializar_jogo(GameState *state);
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}
void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    // Registra a função de desabilitar para ser chamada quando o programa sair
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG );

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
//tecla pressionada
int kbhit(void) {
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

int main() {
    enableRawMode();

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("Nao foi possivel inicializar o SDL: %s\n", SDL_GetError());
        return -1;
    }

    // Formato: Frequência 44100Hz, formato padrão, 2 canais (estéreo), buffer de 2048
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        printf("Nao foi possivel inicializar o SDL_mixer: %s\n", Mix_GetError());
        return -1;
    }

    const char *arquivo_musica = "musica_sweet.mp3";

    Mix_Music *musica_de_fundo = Mix_LoadMUS(arquivo_musica);
    if (musica_de_fundo == NULL) {
        printf("Nao foi possivel carregar a musica '%s': %s\n", arquivo_musica, Mix_GetError());
        return -1;
    }

    GameState game_state;
    carregar_nivel(&game_state);
    inicializar_jogo(&game_state);

    printf("Comecando a musica!\n");
    Mix_PlayMusic(musica_de_fundo, 1);

    float tempo_final_do_nivel = 0;
    if (game_state.note_count > 0) {
        tempo_final_do_nivel = game_state.level_notes[game_state.note_count - 1].timestamp + 2.0f;
    } else {
        tempo_final_do_nivel = 5.0f;
    }

    sleep(3);
    
    int jogo_esta_rodando = 1;
    double tempo_decorrido = 0;

    while (jogo_esta_rodando) {

        struct timeval tempo_atual;
        gettimeofday(&tempo_atual, NULL);
        tempo_decorrido = (double)(tempo_atual.tv_sec - game_state.start_time.tv_sec) +
                        (double)(tempo_atual.tv_usec - game_state.start_time.tv_usec) / 1000000.0;

        int jogador_apertou_pista = -1; 
        
        if (kbhit()) { 
            char ch = getchar();

            if (ch == 3) {
                jogo_esta_rodando = 0;
                printf("\nCtrl+C pressionado. Encerrando o jogo...\n");
                continue;
            }
            if (ch >= '0' && ch <= '7') {
                jogador_apertou_pista = ch - '0';
                printf(">>> JOGADOR APERTOU A PISTA: %d\n", jogador_apertou_pista);
            }
        }

        for (int i = 0; i < game_state.note_count; i++) {
            if (game_state.level_notes[i].foi_processada) {
                continue;
            }
            float timestamp_nota = game_state.level_notes[i].timestamp;
            int pista_nota = game_state.level_notes[i].note_index;
            
            //se o jogador apertou a teclha no tempo certo
            if ((jogador_apertou_pista == pista_nota) && (tempo_decorrido > timestamp_nota - 0.2 && tempo_decorrido < timestamp_nota + 0.2)) {
                printf(">>> ACERTOU! Nota %s <<<\n", game_state.level_notes[i].note_name);
                game_state.score += 10 * game_state.combo;
                game_state.combo++;
                game_state.level_notes[i].foi_processada = 1;
                break;
            }
            if (tempo_decorrido > timestamp_nota + 0.2) {
                if (!game_state.level_notes[i].foi_processada) {
                    printf("ERROU! Nota %s\n", game_state.level_notes[i].note_name);
                    game_state.combo = 1;
                    game_state.level_notes[i].foi_processada = 1;
                }
            }
            if (tempo_decorrido > tempo_final_do_nivel) {
                jogo_esta_rodando = 0; // Desliga a flag quando o tempo acaba
            }
        
        }
        
        printf("=======================================\n");
        printf("Tempo: %.2f s\n", tempo_decorrido);
        printf("Pontos: %d\n", game_state.score);
        printf("Combo: x%d\n", game_state.combo);

        usleep(16000);
    }

    printf("\nFim do Jogo! Pontuacao Final: %d\n", game_state.score);
    
    // Libera a memória da música
    Mix_FreeMusic(musica_de_fundo);
    musica_de_fundo = NULL;

    // Fecha os sistemas de áudio
    Mix_Quit();
    SDL_Quit();

    return 0;
}

void inicializar_jogo(GameState *state) {
    state->score = 0;
    state->combo = 1;
    gettimeofday(&state->start_time, NULL);
    for (int i = 0; i < state->note_count; i++) {
        state->level_notes[i].foi_processada = 0;
    }
    printf("Jogo iniciado! Pressione Ctrl+C para sair.\n");
}

void carregar_nivel(GameState *state) {
    FILE *file = fopen(LEVEL_FILENAME, "r");
    if (!file) {
        perror("Nao foi possivel abrir o arquivo de nivel");
        exit(1);
    }

    state->note_count = 0;
    float timestamp;
    char note_name[5];
    
    while (fscanf(file, "%f %s", &timestamp, note_name) == 2) {
        if (state->note_count < MAX_NOTES) {
            state->level_notes[state->note_count].timestamp = timestamp;
            strcpy(state->level_notes[state->note_count].note_name, note_name);
            
            if (strcmp(note_name, "C4") == 0) state->level_notes[state->note_count].note_index = 0;
            else if (strcmp(note_name, "D4") == 0) state->level_notes[state->note_count].note_index = 1;
            else if (strcmp(note_name, "E4") == 0) state->level_notes[state->note_count].note_index = 2;
            else if (strcmp(note_name, "F4") == 0) state->level_notes[state->note_count].note_index = 3;
            else if (strcmp(note_name, "G4") == 0) state->level_notes[state->note_count].note_index = 4;
            else if (strcmp(note_name, "A4") == 0) state->level_notes[state->note_count].note_index = 5;
            else if (strcmp(note_name, "B4") == 0) state->level_notes[state->note_count].note_index = 6;

            else{
                continue;
            }

            state->level_notes[state->note_count].foi_processada = 0;
            state->note_count++;
        }
    }
    fclose(file);
    printf("%d notas carregadas do nivel.\n", state->note_count);
}
