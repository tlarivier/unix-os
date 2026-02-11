#include <stdint.h>
#include <stddef.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/errno.h>
#include <kernel/types.h>

struct input_event {
    uint32_t tv_sec;
    uint32_t tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#define EV_SYN       0x00
#define EV_KEY       0x01
#define EV_REL       0x02
#define EV_ABS       0x03

#define KEY_ESC      1
#define KEY_1        2
#define KEY_2        3
#define KEY_3        4
#define KEY_4        5
#define KEY_5        6
#define KEY_6        7
#define KEY_7        8
#define KEY_Q        16
#define KEY_W        17
#define KEY_E        18
#define KEY_R        19
#define KEY_A        30
#define KEY_S        31
#define KEY_D        32
#define KEY_SPACE    57
#define KEY_ENTER    28
#define KEY_LSHIFT   42
#define KEY_LCTRL    29
#define KEY_UP       103
#define KEY_DOWN     108
#define KEY_LEFT     105
#define KEY_RIGHT    106
#define KEY_TAB      15

static const uint16_t scancode_to_keycode[128] = {
    [0x01] = KEY_ESC,
    [0x02] = KEY_1,
    [0x03] = KEY_2,
    [0x04] = KEY_3,
    [0x05] = KEY_4,
    [0x06] = KEY_5,
    [0x07] = KEY_6,
    [0x08] = KEY_7,
    [0x0F] = KEY_TAB,
    [0x10] = KEY_Q,
    [0x11] = KEY_W,
    [0x12] = KEY_E,
    [0x13] = KEY_R,
    [0x1C] = KEY_ENTER,
    [0x1D] = KEY_LCTRL,
    [0x1E] = KEY_A,
    [0x1F] = KEY_S,
    [0x20] = KEY_D,
    [0x2A] = KEY_LSHIFT,
    [0x39] = KEY_SPACE,
    [0x48] = KEY_UP,
    [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT,
    [0x50] = KEY_DOWN,
};

#define INPUT_BUFFER_SIZE 64
static struct input_event input_buffer[INPUT_BUFFER_SIZE];
static volatile uint32_t input_read_idx = 0;
static volatile uint32_t input_write_idx = 0;

static uint8_t key_state[128] = {0};

static void input_add_event(uint16_t type, uint16_t code, int32_t value) {
    uint32_t next = (input_write_idx + 1) % INPUT_BUFFER_SIZE;
    if (next == input_read_idx) return;  /* Buffer full */
    
    struct input_event *ev = &input_buffer[input_write_idx];
    ev->tv_sec  = get_seconds();
    ev->tv_usec = (get_timer_ticks() % 100) * 10000;
    ev->type    = type;
    ev->code    = code;
    ev->value   = value;
    
    input_write_idx = next;
}

void input_process_scancode(uint8_t scancode) {
    int release = (scancode & 0x80) != 0;
    uint8_t code = scancode & 0x7F;
    
    uint16_t keycode = scancode_to_keycode[code];
    if (keycode == 0) return;
    
    if (release) {
        if (key_state[code]) {
            key_state[code] = 0;
            input_add_event(EV_KEY, keycode, 0);  
            input_add_event(EV_SYN, 0, 0);        
        }
    } else {
        if (!key_state[code]) {
            key_state[code] = 1;
            input_add_event(EV_KEY, keycode, 1);  
            input_add_event(EV_SYN, 0, 0);        
        }
    }
}

ssize_t input_read(void *buf, size_t count) {
    if (count < sizeof(struct input_event)) return -EINVAL;
    
    if (input_read_idx == input_write_idx) {
        return 0;  
    }
    
    size_t events_read = 0;
    struct input_event *dst = (struct input_event*)buf;
    size_t max_events = count / sizeof(struct input_event);
    
    while (events_read < max_events && input_read_idx != input_write_idx) {
        dst[events_read] = input_buffer[input_read_idx];
        input_read_idx = (input_read_idx + 1) % INPUT_BUFFER_SIZE;
        events_read++;
    }
    
    return events_read * sizeof(struct input_event);
}

int input_poll(void) {
    return input_read_idx != input_write_idx;
}

void input_dev_init(void) {
    input_read_idx = 0;
    input_write_idx = 0;
    for (int i = 0; i < 128; i++) key_state[i] = 0;
}
