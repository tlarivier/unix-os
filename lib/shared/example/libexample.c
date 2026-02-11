/* Export functions */
int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int subtract(int a, int b) {
    return a - b;
}

/* Library version */
const char* lib_version(void) {
    return "libexample 1.0.0";
}

/* Init function - called when library is loaded */
void _init(void) {
    /* Initialization code here */
}

/* Fini function - called when library is unloaded */
void _fini(void) {
    /* Cleanup code here */
}
