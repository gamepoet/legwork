#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* fcontext_t;

fcontext_t make_fcontext(void* sp, size_t size, void (*func)(void*));
void jump_fcontext(fcontext_t* from, fcontext_t to, void* arg);

#ifdef __cplusplus
}
#endif
