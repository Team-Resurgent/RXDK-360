#include "rt.h"
static int g_counter = 41;
static int g_arr[4];
void title_main(void) {
    g_counter += 1;
    g_arr[0] = g_counter * 100;
    DbgPrint("counter=%d arr0=%d\n", g_counter, g_arr[0]);
}
