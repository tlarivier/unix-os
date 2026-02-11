int isgraph(int c) { return c > ' ' && c <= '~'; }
int iscntrl(int c) { return (c >= 0 && c < ' ') || c == 127; }
int isxdigit(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isblank(int c) { return c == ' ' || c == '\t'; }
