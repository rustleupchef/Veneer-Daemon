#include <stdint.h>

__attribute__((export_name("wasmFunction")))
int wasmFunction(uint64_t* Input, uint64_t* Output) {
    for (int i = 0; i < 256; i++) {
        Output[i] = Input[i] * i;
    }
    return 0;
}