// Compile: clang --target=wasm32 -O0 -nostdlib -Wl,--no-entry -Wl,--export-dynamic -o bin/output.wasm main.c
#include <stdint.h>

__attribute__((export_name("wasmFunction")))
int wasmFunction(int64_t* Input, int64_t* Output) {
    for (int i = 0; i < 16; i++) {
        Output[i] = Input[i] * i;
    }
    return 0;
}