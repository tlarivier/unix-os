#ifndef UAPI_TERMIOS_H
#define UAPI_TERMIOS_H

#include <stdint.h>

#define IGNBRK  0x00001  
#define BRKINT  0x00002  
#define IGNPAR  0x00004  
#define PARMRK  0x00008  
#define INPCK   0x00010  
#define ISTRIP  0x00020  
#define INLCR   0x00040  
#define IGNCR   0x00080  
#define ICRNL   0x00100  
#define IUCLC   0x00200  
#define IXON    0x00400  
#define IXANY   0x00800  
#define IXOFF   0x01000  
#define IMAXBEL 0x02000  

#define OPOST   0x00001  
#define OLCUC   0x00002  
#define ONLCR   0x00004  
#define OCRNL   0x00008  
#define ONOCR   0x00010  
#define ONLRET  0x00020  
#define OFILL   0x00040  
#define OFDEL   0x00080  

#define CSIZE   0x00030  
#define CS5     0x00000  
#define CS6     0x00010  
#define CS7     0x00020  
#define CS8     0x00030  
#define CSTOPB  0x00040  
#define CREAD   0x00080  
#define PARENB  0x00100  
#define PARODD  0x00200  
#define HUPCL   0x00400  
#define CLOCAL  0x00800  

#define ISIG    0x00001  
#define ICANON  0x00002  
#define XCASE   0x00004  
#define ECHO    0x00008  
#define ECHOE   0x00010  
#define ECHOK   0x00020  
#define ECHONL  0x00040  
#define NOFLSH  0x00080  
#define TOSTOP  0x00100  
#define ECHOCTL 0x00200  
#define ECHOPRT 0x00400  
#define ECHOKE  0x00800  
#define IEXTEN  0x08000  

#define VINTR    0   
#define VQUIT    1   
#define VERASE   2   
#define VKILL    3   
#define VEOF     4   
#define VTIME    5   
#define VMIN     6   
#define VSWTC    7   
#define VSTART   8   
#define VSTOP    9   
#define VSUSP    10   
#define VEOL     11   
#define VREPRINT 12  
#define VDISCARD 13  
#define VWERASE  14   
#define VLNEXT   15   
#define VEOL2    16   
#define NCCS     20

#define TCSANOW   0  
#define TCSADRAIN 1  
#define TCSAFLUSH 2  

#define TCIFLUSH  0  
#define TCOFLUSH  1  
#define TCIOFLUSH 2  

#define TCOOFF 0  /* Suspend output */
#define TCOON  1  /* Resume output */
#define TCIOFF 2  /* Suspend input */
#define TCION  3  /* Resume input */

typedef uint32_t speed_t;
typedef uint32_t tcflag_t;
typedef uint8_t  cc_t;

#define B0      0
#define B50     50
#define B75     75
#define B110    110
#define B134    134
#define B150    150
#define B200    200
#define B300    300
#define B600    600
#define B1200   1200
#define B1800   1800
#define B2400   2400
#define B4800   4800
#define B9600   9600
#define B19200  19200
#define B38400  38400
#define B57600  57600
#define B115200 115200

struct termios {
    tcflag_t c_iflag;      
    tcflag_t c_oflag;      
    tcflag_t c_cflag;      
    tcflag_t c_lflag;      
    cc_t     c_line;       
    cc_t     c_cc[NCCS];   
    speed_t  c_ispeed;     
    speed_t  c_ospeed;     
};

struct winsize {
    uint16_t ws_row;    
    uint16_t ws_col;    
    uint16_t ws_xpixel; 
    uint16_t ws_ypixel; 
};

#define TCGETS      0x5401  
#define TCSETS      0x5402  
#define TCSETSW     0x5403  
#define TCSETSF     0x5404  
#define TIOCGWINSZ  0x5413  
#define TIOCSWINSZ  0x5414  
#define TIOCGPGRP   0x540F  
#define TIOCSPGRP   0x5410  
#define TIOCSCTTY   0x540E  

#endif 
