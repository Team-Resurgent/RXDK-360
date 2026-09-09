#ifndef RXDK360_CORPUS_RT_H
#define RXDK360_CORPUS_RT_H
/* Freestanding declarations for the corpus. The C library entry points resolve
   from whichever runtime the harness links (the translated libcMT today, the
   modern picolibc/C++ runtime later) -- so a swap that changes their behaviour
   shows up as a diff. */
#ifdef __cplusplus
extern "C" {
#endif
int  DbgPrint(const char* fmt, ...);          /* kernel output channel (ordinal 3) */
int  sprintf(char* buf, const char* fmt, ...);/* CRT formatter under test */
unsigned int strlen(const char* s);
char* strcpy(char* d, const char* s);
int   memcmp(const void* a, const void* b, unsigned int n);
void* memcpy(void* d, const void* s, unsigned int n);
void* malloc(unsigned int n);
void  free(void* p);
void  title_main(void);                        /* each corpus program implements this */
#ifdef __cplusplus
}
#endif
#endif
