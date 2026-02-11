#include <kernel/keyboard.h>
#include <kernel/io.h>
#include <kernel/ports.h>
#include <kernel/ringbuf.h>
#include <stdint.h>

#define KB_BUFFER_SIZE 256  /* Must be power of 2 */

#define SCANCODE_EXTENDED 0xE0
#define SCANCODE_UP       0x48
#define SCANCODE_DOWN     0x50
#define SCANCODE_LEFT     0x4B
#define SCANCODE_RIGHT    0x4D

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85
#define KEY_PGUP  0x86
#define KEY_PGDN  0x87
#define KEY_INS   0x88
#define KEY_DEL   0x89

#define SCANCODE_HOME  0x47
#define SCANCODE_END   0x4F
#define SCANCODE_PGUP  0x49
#define SCANCODE_PGDN  0x51
#define SCANCODE_INS   0x52
#define SCANCODE_DEL   0x53

static uint8_t kb_buffer_data[sizeof(ringbuf_t) + KB_BUFFER_SIZE];
static ringbuf_t *kb_ring = (ringbuf_t *)kb_buffer_data;

static volatile int extended_scancode = 0;

static volatile uint8_t shift_pressed = 0;
static volatile uint8_t ctrl_pressed  = 0;
static volatile uint8_t alt_pressed   = 0;
static volatile uint8_t caps_lock     = 0;

static volatile int raw_mode = 0;

#define SCANCODE_LSHIFT   0x2A
#define SCANCODE_RSHIFT   0x36
#define SCANCODE_CTRL     0x1D
#define SCANCODE_ALT      0x38
#define SCANCODE_CAPSLOCK 0x3A

static const char scancode_to_ascii[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char scancode_to_ascii_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void kb_put_char(char c) {
    ringbuf_push(kb_ring, (uint8_t)c);
}

void keyboard_init(void) {
    ringbuf_init(kb_ring, KB_BUFFER_SIZE);
    extended_scancode = 0;
    shift_pressed     = 0;
    ctrl_pressed      = 0;
    alt_pressed       = 0;
    caps_lock         = 0;
}

void keyboard_handle_irq(void) {
    uint8_t scancode = inb(KB_DATA_PORT);
    
    if (raw_mode) {
        kb_put_char(scancode);
        return;
    }
    
    if (scancode == SCANCODE_EXTENDED) {
        extended_scancode = 1;
        return;
    }
    
    uint8_t key_released = (scancode & 0x80) != 0;
    uint8_t key_code     =  scancode & 0x7F;
    
    if (key_code == SCANCODE_LSHIFT || key_code == SCANCODE_RSHIFT) {
        shift_pressed = !key_released;
        return;
    }
    if (key_code == SCANCODE_CTRL) {
        ctrl_pressed = !key_released;
        return;
    }
    if (key_code == SCANCODE_ALT) {
        alt_pressed = !key_released;
        return;
    }
    if (key_code == SCANCODE_CAPSLOCK && !key_released) {
        caps_lock = !caps_lock;
        return;
    }
    
    if (extended_scancode) {
        extended_scancode = 0;
        if (!key_released) {  /* Key press, not release */
            switch (key_code) {
                case SCANCODE_UP:    kb_put_char(KEY_UP);    break;
                case SCANCODE_DOWN:  kb_put_char(KEY_DOWN);  break;
                case SCANCODE_LEFT:  kb_put_char(KEY_LEFT);  break;
                case SCANCODE_RIGHT: kb_put_char(KEY_RIGHT); break;
                case SCANCODE_HOME:  kb_put_char(KEY_HOME);  break;
                case SCANCODE_END:   kb_put_char(KEY_END);   break;
                case SCANCODE_PGUP:  kb_put_char(KEY_PGUP);  break;
                case SCANCODE_PGDN:  kb_put_char(KEY_PGDN);  break;
                case SCANCODE_INS:   kb_put_char(KEY_INS);   break;
                case SCANCODE_DEL:   kb_put_char(KEY_DEL);   break;
            }
        }
        return;
    }
    
    if (!key_released) {
        char c;
        
        uint8_t use_shift = shift_pressed;
        
        if (use_shift) {
            c = scancode_to_ascii_shift[key_code];
        } else {
            c = scancode_to_ascii[key_code];
        }
        
        if (caps_lock) {
            if (c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';  
            } else if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';  
            }
        }
        
        if (ctrl_pressed && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            c = (c & 0x1F);  
        }
        
        if (c != 0) {
            kb_put_char(c);
        }
    }
}

int kb_has_char(void) {
    return !ringbuf_empty(kb_ring);
}

char kb_get_char(void) {
    while (ringbuf_empty(kb_ring)) {
        __asm__ volatile("sti; hlt");
    }
    return kb_try_get_char();
}

char kb_try_get_char(void) {
    uint8_t c;
    if (ringbuf_pop(kb_ring, &c) < 0) {
        return 0;
    }
    return (char)c;
}

int keyboard_set_raw_mode(int enable) {
    raw_mode = enable ? 1 : 0;
    return 0;
}

int keyboard_get_raw_mode(void) {
    return raw_mode;
}
