#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h> // Adicionado para usar o clock_gettime

// Definir os comandos ioctl
#define RD_SWITCHES   _IO('a', 'a')
#define RD_PBUTTONS   _IO('a', 'b')
#define WR_L_DISPLAY  _IO('a', 'c')
#define WR_R_DISPLAY  _IO('a', 'd')
#define WR_RED_LEDS   _IO('a', 'e')
#define WR_GREEN_LEDS _IO('a', 'f')

// Definições para os displays de 7 segmentos
#define HEX_0 0xFFFFFFC0
#define HEX_1 0xFFFFFFF9
#define HEX_2 0xFFFFFFA4
#define HEX_3 0xFFFFFFB0
#define HEX_4 0xFFFFFF99
#define HEX_5 0xFFFFFF92
#define HEX_6 0xFFFFFF82
#define HEX_7 0xFFFFFFF8
#define HEX_8 0xFFFFFF80
#define HEX_9 0xFFFFFF90

// Variável para o descritor de arquivo do dispositivo
static int fd = -1;

// Mapeamento dos hexadecimais para os displays
static const uint32_t hex_digits[] = {
    HEX_0, HEX_1, HEX_2, HEX_3, HEX_4,
    HEX_5, HEX_6, HEX_7, HEX_8, HEX_9
};

// Variáveis para a lógica de temporização dos LEDs
static int led_is_on = 0;
static long long led_on_time_ms = 0;

// Obtém o tempo atual em milissegundos
long long get_current_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + (long long)ts.tv_nsec / 1000000;
}

// Configura o terminal para não bloquear a leitura de teclado
void set_terminal_raw_mode() {
    struct termios old_termios, new_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    new_termios = old_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

// Restaura as configurações originais do terminal
void restore_terminal_mode() {
    struct termios old_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    old_termios.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
}

// Checa se uma tecla foi pressionada
int kbhit() {
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

// Envia um valor para os displays de 7 segmentos
void set_display(int number) {
    if (fd == -1) return;
    if (number < 0 || number > 9) return;

    // Acender o display da esquerda com o número
    uint32_t val_left = hex_digits[number];
    ioctl(fd, WR_L_DISPLAY);
    write(fd, &val_left, sizeof(val_left));

    // Acender o display da direita com o número
    uint32_t val_right = hex_digits[number];
    ioctl(fd, WR_R_DISPLAY);
    write(fd, &val_right, sizeof(val_right));

    printf("Exibindo numero %d nos displays.\n", number);
}

// Acende os LEDs da cor especificada e marca o tempo
void set_leds(int color) {
    if (fd == -1) return;

    // Ajustamos os valores para corresponderem ao número de LEDs no hardware
    // LEDs Verdes (9) e LEDs Vermelhos (18)
    uint32_t val_on_green = 0x1FF;   // 9 bits para os LEDs verdes
    uint32_t val_on_red = 0x3FFFF;   // 18 bits para os LEDs vermelhos
    uint32_t val_off = 0x00000;

    if (color == 1) { // Verde
        ioctl(fd, WR_GREEN_LEDS);
        write(fd, &val_on_green, sizeof(val_on_green));
        ioctl(fd, WR_RED_LEDS);
        write(fd, &val_off, sizeof(val_off));
        printf("LEDs Verdes acesos.\n");
    } else { // Vermelho
        ioctl(fd, WR_RED_LEDS);
        write(fd, &val_on_red, sizeof(val_on_red));
        ioctl(fd, WR_GREEN_LEDS);
        write(fd, &val_off, sizeof(val_off));
        printf("LEDs Vermelhos acesos.\n");
    }

    // Grava o tempo em que os LEDs foram acesos
    led_on_time_ms = get_current_time_ms();
    led_is_on = 1;
}

// Apaga todos os LEDs
void clear_leds() {
    if (fd == -1) return;
    uint32_t val_off = 0x00;
    ioctl(fd, WR_GREEN_LEDS);
    write(fd, &val_off, sizeof(val_off));
    ioctl(fd, WR_RED_LEDS);
    write(fd, &val_off, sizeof(val_off));
    led_is_on = 0;
}

// Lê o estado dos push-buttons
void read_pbuttons() {
    if (fd == -1) return;

    // IMPORTANTE: Chamar ioctl antes de ler para configurar o ponteiro no driver
    ioctl(fd, RD_PBUTTONS);

    uint32_t buttons = 0;
    ssize_t r = read(fd, &buttons, sizeof(buttons));
    if (r == sizeof(buttons)) {
        buttons = (~buttons) & 0xF; // Os botões são ativos baixo
        if (buttons != 0) {
            printf("Botoes pressionados: 0x%X\n", buttons);
        }
    }
}

int main() {
    // Abre o arquivo do dispositivo
    fd = open("/dev/mydev", O_RDWR);
    if (fd == -1) {
        perror("Erro ao abrir /dev/mydev. Execute como root?");
        return -1;
    }

    printf("Pressione 'g' (Verde) ou 'r' (Vermelho) para os LEDs.\n");
    printf("Pressione '0'-'9' para os displays de 7 segmentos.\n");
    printf("Pressione os botoes na placa para ver o estado.\n");
    printf("Pressione 'q' para sair.\n");

    set_terminal_raw_mode();

    char ch;
    while (1) {
        // Verifica se é hora de apagar os LEDs
        if (led_is_on && get_current_time_ms() - led_on_time_ms >= 500) {
            clear_leds();
        }

        if (kbhit()) {
            ch = getchar();
            if (ch == 'q') {
                break;
            } else if (ch == 'g') {
                set_leds(1);
            } else if (ch == 'r') {
                set_leds(0);
            } else if (ch >= '0' && ch <= '9') {
                set_display(ch - '0');
            }
        }
        read_pbuttons();
        usleep(50000); // 50ms de espera
    }

    restore_terminal_mode();
    clear_leds();
    close(fd);
    printf("Programa de teste finalizado.\n");

    return 0;
}
