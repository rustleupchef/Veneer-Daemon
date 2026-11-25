#include <stdint.h>

__attribute__((export_name("wasmFunction")))
int wasmFunction(int64_t* Input, int64_t* Output) {
    for (int i = 0; i < 256; i++) {
        Output[i] = Input[i] * i;
    }
    return 0;
}