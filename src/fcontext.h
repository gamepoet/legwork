#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* fcontext_t;

fcontext_t make_fcontext(void* sp, size_t size, void (*func)(void*));
intptr_t jump_fcontext(fcontext_t* from, fcontext_t to, void* arg, int preserve_fpu);

#ifdef __cplusplus
}
#endif
