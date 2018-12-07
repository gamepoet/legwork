#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct legwork_config_t {
  // The number of fibers to create for the pool.
  unsigned int fiber_count;

  // The size in bytes allocated for each fiber's stack.
  unsigned int fiber_stack_size_bytes;

  // The number of worker threads to create that will execute the fibers. Defaults to the number of hardware threads
  // supported by the current hardware.
  unsigned int worker_thread_count;
} legwork_config_t;

typedef struct legwork_counter_t legwork_counter_t;

typedef void (*legwork_task_func_t)(void* arg);

// TODO: do we really need to require the caller to pack these together in a struct?
typedef struct legwork_task_t {
  legwork_task_func_t func;
  void* arg;
} legwork_task_t;

void legwork_config_init(legwork_config_t* config);
void legwork_lib_init(const legwork_config_t* config);
void legwork_lib_shutdown();

void legwork_counter_init(legwork_counter_t* counter);

void legwork_task_add(const legwork_task_t* tasks, unsigned int task_count, legwork_counter_t** counter);

void legwork_wait(legwork_counter_t* counter, unsigned int value);

#ifdef __cplusplus
}
#endif
