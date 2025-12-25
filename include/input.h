#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t scancode;
    uint8_t pressed;
} key_event_t;

typedef struct {
    int8_t dx;
    int8_t dy;
    uint8_t buttons;
} mouse_event_t;

void input_init(void);
void input_handle_irq1(void);
void input_handle_irq12(void);

int input_read_key(key_event_t* out);
int input_read_mouse(mouse_event_t* out);
