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

  // The maximum number of times a worker thread should busy wait spin when there is no work before going to sleep.
  unsigned int worker_thread_spin_count_before_wait;

  // The function used to allocate memory. The default implementation is malloc().
  void* (*alloc)(unsigned int size, void* user_data, const char* file, int line, const char* func);

  // The function used to free memory. The default implementation is free().
  void (*free)(void* ptr, void* user_data, const char* file, int line, const char* func);

  // Opaque user data passed to alloc() and free().
  void* alloc_user_data;

  // The function to use when an assertion fails. The default implementation prints to stderr and terminates.
  void (*assert_func)(const char* file, int line, const char* func, const char* expression, const char* message);
} legwork_config_t;

typedef struct legwork_counter_t legwork_counter_t;

typedef void (*legwork_task_func_t)(void* arg);

// TODO: do we really need to require the caller to pack these together in a struct?
typedef struct legwork_task_desc_t {
  legwork_task_func_t func;
  void* task;
} legwork_task_desc_t;

void legwork_config_init(legwork_config_t* config);
void legwork_lib_init(const legwork_config_t* config);
void legwork_lib_shutdown();

void legwork_task_add(const legwork_task_desc_t* task_descs, unsigned int task_desc_count, legwork_counter_t** counter);

void legwork_wait(legwork_counter_t* counter);
void legwork_wait_value(legwork_counter_t* counter, unsigned int value);

// Tests to see if all the tasks tracked by the counter have completed.
bool legwork_is_complete(legwork_counter_t* counter);

#ifdef __cplusplus
}
#endif
