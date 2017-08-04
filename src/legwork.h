#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct legwork_config_t {
  // The number of fibers to create for the pool.
  unsigned int fiber_count;

  // The number of worker threads to create that will execute the fibers
  unsigned int thread_count;
};

struct legwork_context_t;
struct legwork_counter_t;

typedef void (*legwork_task_func_t)(struct legwork_context_t* context, void* arg);

// TODO: do we really need to require the caller to pack these together in a struct?
struct legwork_task_t {
  legwork_task_func_t func;
  void* arg;
};

void legwork_config_init(struct legwork_config_t* config);
void legwork_init();
void legwork_shutdown();

void legwork_task_add(struct legwork_task_t* tasks, unsigned int task_count, struct legwork_counter_t* counter);

void legwork_task_wait(struct legwork_counter_t* counter, unsigned int value);

#ifdef __cplusplus
}
#endif
