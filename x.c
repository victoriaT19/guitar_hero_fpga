#ifndef LCD_H_INCLUDED
#define LCD_H_INCLUDED

#include <stdint.h>

#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYLEFT 0x02

// flags for display and cursor control
#define LCD_BLINKON 0x01
#define LCD_CURSORON 0x02
#define LCD_DISPLAYON 0x04

// flags for display and cursor shift
#define LCD_MOVERIGHT 0x04
#define LCD_DISPLAYMOVE 0x08

// flags for function set
#define LCD_5x10DOTS 0x04
#define LCD_2LINE 0x08
#define LCD_8BITMODE 0x10

#define LCD_ENABLE_BIT 0x04


// Modes for lcd_send_byte
#define LCD_CHARACTER  1
#define LCD_COMMAND    0

#define MAX_LINES      2
#define MAX_CHARS      16

#define LCD_DELAY_US 600

void lcd_init(int fd); 

void lcd_clear(void); 

// go to location on LCD
void lcd_set_cursor(int line, int position); 

void lcd_char(char val); 

void lcd_string(const char *s);

#endif

lcd.c

#include "lcd.h"
#include "ioctl_cmds.h"
#include <stdio.h>
#include <unistd.h>	/* close() read() write() */
#include <fcntl.h>	/* open() */
#include <sys/ioctl.h>	/* ioctl() */

static int file_id = 0;

/* Quick helper function for single byte transfers */
static void write_byte(uint16_t val) 
{
    val = val | 0x800;
    ioctl(file_id, WR_LCD_DISPLAY);
    write(file_id, &val, sizeof(val));
}

static void lcd_toggle_enable(uint8_t val, int mode) 
{
    // Toggle enable pin on LCD display
    // We cannot do this too quickly or things don't work
    write_byte(val | (mode << 10) | 0x100);
    usleep(LCD_DELAY_US);
    write_byte(val | (mode << 10));
    usleep(LCD_DELAY_US);
}


void lcd_init(int fd) 
{
	file_id = fd;
	lcd_toggle_enable(0x08, LCD_COMMAND);
    lcd_toggle_enable(0x0f, LCD_COMMAND);
    lcd_toggle_enable(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND);
    lcd_toggle_enable(LCD_FUNCTIONSET | 0x10 | LCD_2LINE, LCD_COMMAND);
    lcd_clear();
    lcd_set_cursor(0, 0);
}

void lcd_clear(void) 
{
    lcd_toggle_enable(LCD_CLEARDISPLAY, LCD_COMMAND);
}

// go to location on LCD
void lcd_set_cursor(int line, int position) 
{
    int val = (line == 0) ? 0x80 + position : 0xC0 + position;
    lcd_toggle_enable(val, LCD_COMMAND);
}

void lcd_char(char val) 
{
    lcd_toggle_enable(val, LCD_CHARACTER);
}

void lcd_string(const char *s) 
{
    while (*s)
        lcd_char(*s++);
}

#ifndef DEVICE_H_INCLUDED
#define DEVICE_H_INCLUDED

#include "typedefs.h"
#include "7_seg.h"
#include "lcd.h"

void d_init();

uint8_t d_button_read();

uint32_t d_switch_read();

void d_write_green_leds(int32_t i);
void d_write_red_leds(int32_t i);

void d_shutdown();
	
#endif

device.c 


#include "device.h"
#include "ioctl_cmds.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>	/* ioctl() */
static int32_t file_id = 0;

void d_init()
{
	file_id = open("/dev/mydev", O_RDWR);

	seg7_init(file_id);
	lcd_init (file_id);
	
	int i = 0;
	ioctl(file_id, WR_GREEN_LEDS);
	write(file_id, &i, sizeof(i));
	
	ioctl(file_id, WR_RED_LEDS);
	write(file_id, &i, sizeof(i));
}

uint8_t d_button_read()
{
	uint8_t r = 0, e = 0;
	ioctl(file_id, RD_PBUTTONS);
	e = read(file_id, &r, sizeof(r));
	return r;
}

uint32_t d_switch_read()
{
	uint32_t r = 0, e = 0;
	ioctl(file_id, RD_SWITCHES);
	e = read(file_id, &r, sizeof(r));
	return r;
}

void d_write_green_leds(int32_t i)
{
	int retval;
	ioctl(file_id, WR_GREEN_LEDS);
	retval = write(file_id, &i, sizeof(i));
}

void d_write_red_leds(int32_t i)
{
	int retval;
	ioctl(file_id, WR_RED_LEDS);
	retval = write(file_id, &i, sizeof(i));
}

void d_shutdown()
{
	close(file_id);
}


#ifndef SEVEN_SEG_H_INCLUDED
#define SEVEN_SEG_H_INCLUDED


void seg7_init(int fd);
void seg7_reset(int idx);
void seg7_write_single(int seg, int number, int _reset);
int seg7_convert_digit(int n);

void seg7_write_str(char *number);
void seg7_write(int number);

void seg7_switch_base(int b);

#endif

seg.c

#include "7_seg.h"
#include "ioctl_cmds.h"
#include <stdint.h>
#include <fcntl.h> /* open() */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h> /* ioctl() */
#include <sys/types.h>
#include <unistd.h> /* close() read() write() */

static int file_d = 0;
int32_t current_R = 0;
int32_t current_L = 0;

void seg7_init(int fd) {
  file_d = fd;
  seg7_reset(0);
  seg7_reset(1);
}

void seg7_reset(int idx) {
  int reset = 0xfffffff;
  // 0 vai ser o da direita da frente da placa e 1 o da esquerda
  if (idx) {
    ioctl(file_d, WR_R_DISPLAY);
  } else {
    ioctl(file_d, WR_L_DISPLAY);
  }
  write(file_d, &reset, sizeof(reset));
}
void seg7_write_single(int seg, int number, int _reset) {
  // o seg e o index do seg comecando da direita de frente para a placa
  // em 0-7, o reset ele reseta os outros ou nao
  // R e L ta ao contrario, de costas para a placa, frente para as saidas de
  // cabos se reset 1 ele limpa se nao ele mantem os outros
  if (_reset) {
    current_L = 0;
    current_R = 0;
    seg7_reset(1);
    seg7_reset(0);
  }
  if (seg > 7) {
    return;
  }

  if (seg > 3) {
  } else {
  }

  int d = seg7_convert_digit(number) << (7 * (seg % 4));
  int mask =
      ~(0x7f << (7 * (seg % 4))); // para manter ou nao apenas o digto escolhido

  // TODO: Testar funcao de buffer_write, para ele escrever os current_*
  if (seg > 3) {
    d = (current_L & mask) | d;
    ioctl(file_d, WR_R_DISPLAY);
    current_L = d;

  } else {
    d = (current_R & mask) | d;
    ioctl(file_d, WR_L_DISPLAY);
    current_R = d;
  }

  d = ~d;
  write(file_d, &d, sizeof(d));
}

int seg7_convert_digit(int n) {
  int ret = 0;
  switch (n) {  // segmentos estao na ordem 0gfedcba, sendo o 0 o ponto nao mapeado
  case 1:
    ret = 0b00000110;
    break;
  case 2:
    ret = 0b01011011;
    break;
  case 3:
    ret = 0b01001111;
    break;
  case 4:
    ret = 0b01100110;
    break;
  case 5:
    ret = 0b01101101;
    break;
  case 6:
    ret = 0b01111101;
    break;
  case 7:
    ret = 0b00000111;
    break;
  case 8:
    ret = 0b01111111;
    break;
  case 9:
    ret = 0b01100111;
    break;
  case 10: // A
    ret = 0b01110111;
    break;
  case 11: // B
    ret = 0b01111100;
    break;
  case 12: // C
    ret = 0b00111001;
    break;
  case 13: // D
    ret = 0b01011110;
    break;
  case 14: // E
    ret = 0b01111001;
    break;
  case 15: // F
    ret = 0b01110001;
    break;
  default:
    ret = 0b00111111;
    break;
  }
  return ret;
}

void seg7_write_str(char *number) {
  // identifica a base certa
  int n = strtol(number, NULL, 0);
  seg7_write(n);
}

int BASE_S = 10;

void seg7_write(int number) {
  int digi_qtd = 0;
  u_int64_t tmp = 0;
  while (digi_qtd <= 7 && number != 0) {
    tmp |= (u_int64_t)seg7_convert_digit(number % BASE_S) << (7 * (digi_qtd++));
    number = number / BASE_S;
  }
  // usar mascara para pegar os 28 a direita e 28 a esquerda
  current_R = (int32_t)(tmp & 0xFFFFFFF);
  current_L = (int32_t)((tmp >> 28) & 0xFFFFFFF);
  
  int32_t b = ~current_R;
  ioctl(file_d, WR_L_DISPLAY);
  write(file_d, &b, sizeof(b));
  b = ~current_L;
  ioctl(file_d, WR_R_DISPLAY);
  write(file_d, &b, sizeof(b));
}

void seg7_switch_base(int b) {
  if (b != 0) {
    BASE_S = b;
  }
}
