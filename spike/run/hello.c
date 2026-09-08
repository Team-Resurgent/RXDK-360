/* Minimal Xbox 360 ELF: no CRT, no imports. Spins on a counter so the
   emulator has something to execute, then returns. */
volatile int counter;
void _start(void) {
    for (int i = 0; i < 1000; ++i)
        counter += i;
}
