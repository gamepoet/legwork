#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <stack>
#include <stdlib.h>
#include <thread>
#include <vector>
#include "legwork.h"
#include "fcontext.h"

#define DEFAULT_FIBER_COUNT 256
#define DEFAULT_FIBER_STACK_SIZE_BYTES (64 * 1024)
#define DEFAULT_WORKER_THREAD_SPIN_COUNT_BEFORE_WAIT (1024)

#define INVALID_FIBER_ID 0

#define legwork_assert(expr, message) ((expr) ? true : (s_config.assert_func(__FILE__, __LINE__, __func__, #expr, message), false))
#define legwork_alloc(size) s_config.alloc(size, s_config.alloc_user_data, __FILE__, __LINE__, __func__)
#define legwork_free(ptr) s_config.free(ptr, s_config.alloc_user_data, __FILE__, __LINE__, __func__)

struct sleeping_fiber_t {
  int fiber_id;
  uint32_t wait_value;
  legwork_counter_t* counter;
};

struct legwork_counter_t {
  std::atomic<uint32_t> value;
};

struct task_queue_entry_t {
  legwork_task_desc_t task_desc;
  legwork_counter_t* counter;
};

struct fiber_t {
  int id;
  fcontext_t fcontext;
  legwork_task_desc_t task_desc;
  legwork_counter_t* counter;
};

struct worker_tls_t {
  int worker_id;
  int active_fiber_id;

  int worker_fiber_id; // the fiber that decides what fibers to run
  int sleep_fiber_id;  // put this fiber to sleep
  legwork_counter_t* sleep_fiber_counter;
  uint32_t sleep_fiber_wait_value;
  int free_fiber_id; // free this fiber
};

static legwork_config_t s_config;

static std::thread* s_threads;       // the worker threads
static std::atomic<bool> s_shutdown; // global shutdown request flag

static legwork_counter_t* s_counters;
static std::stack<int> s_counter_free_pool;
static std::mutex s_counter_free_pool_mutex;

static char* s_fiber_stack_memory;             // static memory for all the fibers
static fiber_t* s_fibers;                      // each fiber's instance data
thread_local static worker_tls_t s_tls_worker; // TLS data for the current worker thread

static std::stack<int> s_fiber_free_pool;  // pool of free fibers
static std::mutex s_fiber_free_pool_mutex; // lock for the fiber free pool

static std::vector<sleeping_fiber_t> s_sleeping_fibers;
static std::mutex s_sleeping_fibers_mutex;

static std::atomic<uint32_t> s_waiting_worker_count;
static std::mutex s_waiting_worker_mutex;
static std::condition_variable s_waiting_worker_cond_var;

// TODO: replace with lock-free queue
static std::mutex s_task_queue_mutex;
static std::queue<task_queue_entry_t> s_task_queue;

static void default_assert(const char* file, int line, const char* func, const char* expression, const char* message) {
  fprintf(stderr, "ASSERT FAILURE: %s\n%s\nfile: %s\nline: %d\nfunc: %s\n", message, expression, file, line, func);
  exit(EXIT_FAILURE);
}

static void* default_alloc(unsigned int size, void* user_data, const char* file, int line, const char* func) {
  return malloc(size);
}

static void default_free(void* ptr, void* user_data, const char* file, int line, const char* func) {
  free(ptr);
}

static legwork_counter_t* counter_alloc() {
  std::lock_guard<std::mutex> lock(s_counter_free_pool_mutex);
  if (!s_counter_free_pool.empty()) {
    int index = s_counter_free_pool.top();
    s_counter_free_pool.pop();
    return s_counters + index;
  }
  return nullptr;
}

static void counter_free(legwork_counter_t* counter) {
  int index = (int)(counter - s_counters);
  std::lock_guard<std::mutex> lock(s_counter_free_pool_mutex);
  s_counter_free_pool.push(index);
}

// puts the current thread into a wait state until it is woken up by a worker_thread_notify().
static void worker_thread_wait() {
  // increment the waiting worker count
  s_waiting_worker_count.fetch_add(1);

  // go to sleep
  {
    std::unique_lock<std::mutex> lock(s_waiting_worker_mutex);
    // pessimistically double-check that a shutdown hasn't been requested
    if (!s_shutdown.load(std::memory_order_seq_cst)) {
      s_waiting_worker_cond_var.wait(lock);
    }
  }

  // decrement the waiting worker count
  s_waiting_worker_count.fetch_sub(1);
}

// wakes up one worker thread if any are waiting
static void worker_thread_maybe_notify_one() {
  const uint32_t wait_count = s_waiting_worker_count.load(std::memory_order_seq_cst);
  if (wait_count > 0) {
    s_waiting_worker_cond_var.notify_one();
  }
}

// wakes up all the worker threads
static void worker_thread_notify_all() {
  // take the lock here so that we can be sure no threads will wait AFTER the notify
  std::lock_guard<std::mutex> lock(s_waiting_worker_mutex);
  s_waiting_worker_cond_var.notify_all();
}

static void task_queue_push(const legwork_task_desc_t* task_desc, legwork_counter_t* counter) {
  task_queue_entry_t entry;
  entry.task_desc = *task_desc;
  entry.counter = counter;

  {
    std::lock_guard<std::mutex> lock(s_task_queue_mutex);
    s_task_queue.emplace(entry);
  }

  worker_thread_maybe_notify_one();
}

static bool task_queue_pop(task_queue_entry_t* entry) {
  std::lock_guard<std::mutex> lock(s_task_queue_mutex);
  if (!s_task_queue.empty()) {
    *entry = s_task_queue.front();
    s_task_queue.pop();
    return true;
  }
  return false;
}

static void sleeping_fibers_push(int fiber_id, legwork_counter_t* counter, unsigned int wait_value) {
  sleeping_fiber_t entry;
  entry.fiber_id = fiber_id;
  entry.wait_value = wait_value;
  entry.counter = counter;

  {
    std::lock_guard<std::mutex> lock(s_sleeping_fibers_mutex);
    s_sleeping_fibers.emplace_back(entry);
  }

  worker_thread_maybe_notify_one();
}

static int sleeping_fibers_pop_first_ready() {
  std::lock_guard<std::mutex> lock(s_sleeping_fibers_mutex);
  for (size_t index = 0, count = s_sleeping_fibers.size(); index < count; ++index) {
    sleeping_fiber_t* sleeping_fiber = &s_sleeping_fibers[index];
    const unsigned int value = sleeping_fiber->counter->value.load(std::memory_order_relaxed);
    if (value <= sleeping_fiber->wait_value) {
      // found a ready fiber, remove it from the list
      int fiber_id = sleeping_fiber->fiber_id;
      s_sleeping_fibers.erase(s_sleeping_fibers.begin() + index);
      return fiber_id;
    }
  }
  return INVALID_FIBER_ID;
}

static void fiber_switch_to(int target_fiber_id) {
  // update the TLS for the active fiber id
  const int active_fiber_id = s_tls_worker.active_fiber_id;
  s_tls_worker.active_fiber_id = target_fiber_id;

  fiber_t* fiber_from = s_fibers + active_fiber_id;
  fiber_t* fiber_to = s_fibers + target_fiber_id;
  jump_fcontext(&fiber_from->fcontext, fiber_to->fcontext, fiber_to->task_desc.task, 1);
}

static void fiber_proc(void* task) {
  // run the fiber func
  const int self_fiber_id = s_tls_worker.active_fiber_id;
  const fiber_t* fiber = s_fibers + self_fiber_id;
  fiber->task_desc.func(task);

  // run the on_complete func
  if (fiber->task_desc.on_complete != nullptr) {
    fiber->task_desc.on_complete(task);
  }

  // denote the fiber as completed
  legwork_counter_t* counter = fiber->counter;
  if (counter != nullptr) {
    counter->value.fetch_sub(1, std::memory_order_seq_cst);
  }

  // ask to have this fiber be freed
  s_tls_worker.free_fiber_id = self_fiber_id;

  // switch back to the worker fiber
  fiber_switch_to(s_tls_worker.worker_fiber_id);
}

static int fiber_alloc(legwork_task_desc_t* task_desc, legwork_counter_t* counter) {
  fiber_t* fiber = nullptr;
  {
    // grab a fiber off the free pool
    std::lock_guard<std::mutex> lock(s_fiber_free_pool_mutex);
    if (!s_fiber_free_pool.empty()) {
      int fiber_id = s_fiber_free_pool.top();
      s_fiber_free_pool.pop();
      fiber = s_fibers + fiber_id;
    }
  }

  // bail on allocation failure
  if (fiber == nullptr) {
    return INVALID_FIBER_ID;
  }

  // initialize the fiber
  const int fiber_id = fiber->id;
  void* stack_ptr = s_fiber_stack_memory + (fiber_id * s_config.fiber_stack_size_bytes);
  fiber->fcontext = make_fcontext(stack_ptr, s_config.fiber_stack_size_bytes, &fiber_proc);
  fiber->task_desc = *task_desc;
  fiber->counter = counter;

  return fiber_id;
}

static void fiber_free(int fiber_id) {
  fiber_t* fiber = s_fibers + fiber_id;
  fiber->fcontext = nullptr;
  fiber->task_desc.func = nullptr;
  fiber->task_desc.task = nullptr;
  fiber->counter = nullptr;
  {
    std::lock_guard<std::mutex> lock(s_fiber_free_pool_mutex);
    s_fiber_free_pool.push(fiber_id);
  }
}

static int get_next_fiber() {
  int fiber_id = INVALID_FIBER_ID;

  // try grabbing a fiber that was waiting but is now ready
  fiber_id = sleeping_fibers_pop_first_ready();
  if (fiber_id != INVALID_FIBER_ID) {
    return fiber_id;
  }

  // grab a job from the queue
  task_queue_entry_t task_queue_entry;
  const bool task_is_valid = task_queue_pop(&task_queue_entry);
  if (task_is_valid) {
    fiber_id = fiber_alloc(&task_queue_entry.task_desc, task_queue_entry.counter);
  }

  return fiber_id;
}

static void worker_fiber_proc(void* task) {
  printf("shouldn't be here\n");
}

static void worker_thread_proc(int worker_id) {
  // printf("worker %d!\n", worker_id);
  s_tls_worker.worker_id = worker_id;
  s_tls_worker.active_fiber_id = INVALID_FIBER_ID;
  s_tls_worker.worker_fiber_id = INVALID_FIBER_ID;
  s_tls_worker.sleep_fiber_id = INVALID_FIBER_ID;
  s_tls_worker.sleep_fiber_counter = nullptr;
  s_tls_worker.sleep_fiber_wait_value = 0;
  s_tls_worker.free_fiber_id = INVALID_FIBER_ID;

  // alloc a fiber for this worker even though it won't execute. the fiber is used to record the current stack so that
  // completing fibers can switch back to this worker
  legwork_task_desc_t worker_task_desc = {};
  worker_task_desc.func = &worker_fiber_proc;
  s_tls_worker.worker_fiber_id = fiber_alloc(&worker_task_desc, nullptr);
  s_tls_worker.active_fiber_id = s_tls_worker.worker_fiber_id;

  unsigned int spin_wait_count = 0;
  while (!s_shutdown.load(std::memory_order_relaxed)) {
    int fiber_id = get_next_fiber();
    if (fiber_id == INVALID_FIBER_ID) {
      // no work to do. spin
      ++spin_wait_count;
      if (spin_wait_count > s_config.worker_thread_spin_count_before_wait) {
        // this thread spun too many times, so go into a wait state so CPU cycles are waited
        worker_thread_wait();
      }
      continue;
    }

    // we have work! reset the spin wait count
    spin_wait_count = 0;

    // run the fiber
    fiber_switch_to(fiber_id);

    // if the previous fiber asked to be marked as asleep, do so now
    if (s_tls_worker.sleep_fiber_id != INVALID_FIBER_ID) {
      sleeping_fibers_push(s_tls_worker.sleep_fiber_id, s_tls_worker.sleep_fiber_counter, s_tls_worker.sleep_fiber_wait_value);
      s_tls_worker.sleep_fiber_id = INVALID_FIBER_ID;
      s_tls_worker.sleep_fiber_counter = nullptr;
      s_tls_worker.sleep_fiber_wait_value = 0;
    }
    // if the previous fiber asked to be freed, do so now
    if (s_tls_worker.free_fiber_id != INVALID_FIBER_ID) {
      fiber_free(s_tls_worker.free_fiber_id);
      s_tls_worker.free_fiber_id = INVALID_FIBER_ID;
    }
  }

  // printf("[%d] worker is done\n", worker_id);
}

void legwork_config_init(legwork_config_t* config) {
  config->fiber_count = DEFAULT_FIBER_COUNT;
  config->fiber_stack_size_bytes = DEFAULT_FIBER_STACK_SIZE_BYTES;
  config->worker_thread_count = std::thread::hardware_concurrency();
  config->worker_thread_spin_count_before_wait = DEFAULT_WORKER_THREAD_SPIN_COUNT_BEFORE_WAIT;
  config->alloc = &default_alloc;
  config->free = &default_free;
  config->alloc_user_data = NULL;
  config->assert_func = &default_assert;
}

void legwork_lib_init(const legwork_config_t* config) {
  if (config) {
    s_config = *config;
  }
  else {
    legwork_config_init(&s_config);
  }

  // create the wait counters
  s_counters = (legwork_counter_t*)legwork_alloc(sizeof(legwork_counter_t) * s_config.fiber_count);
  for (int index = s_config.fiber_count - 1; index >= 0; --index) {
    new (s_counters + index) legwork_counter_t;
    s_counter_free_pool.push(index);
  }

  // create the fibers
  s_fiber_stack_memory = (char*)legwork_alloc(s_config.fiber_stack_size_bytes * s_config.fiber_count);
  s_fibers = (fiber_t*)legwork_alloc(sizeof(fiber_t) * s_config.fiber_count);
  for (int index = s_config.fiber_count - 1; index >= 1; --index) {
    fiber_t* fiber = s_fibers + index;
    fiber->id = index;
    fiber->fcontext = nullptr;
    fiber->task_desc.func = nullptr;
    fiber->task_desc.task = nullptr;
    s_fiber_free_pool.push(index);
  }

  // start the worker threads
  s_shutdown.store(false, std::memory_order_seq_cst);
  s_threads = (std::thread*)legwork_alloc(sizeof(std::thread) * s_config.worker_thread_count);
  for (unsigned int index = 0; index < s_config.worker_thread_count; ++index) {
    new (s_threads + index) std::thread(&worker_thread_proc, index);
  }
}

void legwork_lib_shutdown() {
  // stop the worker threads
  s_shutdown.store(true, std::memory_order_seq_cst);
  worker_thread_notify_all();
  for (unsigned int index = 0; index < s_config.worker_thread_count; ++index) {
    s_threads[index].join();
    s_threads[index].~thread();
  }
  legwork_free(s_threads);
  s_threads = nullptr;

  // destroy the fibers
  legwork_free(s_fiber_stack_memory);
  s_fiber_stack_memory = nullptr;
  legwork_free(s_fibers);
  s_fibers = nullptr;
  while (!s_fiber_free_pool.empty()) {
    s_fiber_free_pool.pop();
  }

  // destroy the wait counters
  legwork_free(s_counters);
  s_counters = nullptr;
  while (!s_counter_free_pool.empty()) {
    s_counter_free_pool.pop();
  }
}

void legwork_task_add(const legwork_task_desc_t* task_descs, unsigned int task_desc_count, legwork_counter_t** counter) {
  legwork_assert(task_descs != NULL, "task_descs cannot be null");
  legwork_assert(task_desc_count > 0, "task_desc_count must be greater than zero");

  legwork_counter_t* wait_counter = nullptr;
  if (counter != nullptr) {
    wait_counter = counter_alloc();
    if (wait_counter == nullptr) {
      // spin wait for a counter to become available
      while (wait_counter == nullptr) {
        wait_counter = counter_alloc();
      }
    }
    *counter = wait_counter;

    // add to the wait counter
    wait_counter->value.store(task_desc_count, std::memory_order_seq_cst);
  }

  // queue the task_descs
  for (unsigned int index = 0; index < task_desc_count; ++index) {
    task_queue_push(task_descs + index, wait_counter);
  }
}

void legwork_wait(legwork_counter_t* counter) {
  legwork_wait_value(counter, 0);
}

void legwork_wait_value(legwork_counter_t* counter, unsigned int value) {
  legwork_assert(counter != nullptr, "counter must be a valid pointer");

  // check if the requirement is already satisfied
  // TODO: is it worth it to force this API to *ALWAYS* be async?
  if (counter->value.load(std::memory_order_relaxed) <= value) {
    // if all the tasks have completed, free the conuter
    if (value == 0) {
      counter_free(counter);
    }
    return;
  }

  // if the current thread isn't running a fiber, then we'll need to spin wait
  const int self_fiber_id = s_tls_worker.active_fiber_id;
  if (self_fiber_id == INVALID_FIBER_ID) {
    while (counter->value.load(std::memory_order_relaxed) > value) {
      // spin
    }
    return;
  }

  // tell the next fiber to put us to sleep
  s_tls_worker.sleep_fiber_id = self_fiber_id;
  s_tls_worker.sleep_fiber_counter = counter;
  s_tls_worker.sleep_fiber_wait_value = value;

  // switch to the worker's fiber
  fiber_switch_to(s_tls_worker.worker_fiber_id);

  // and we're back. should be good to go now.
  // if all the tasks have completed, free the counter
  if (value == 0) {
    counter_free(counter);
  }
}

bool legwork_is_complete(legwork_counter_t* counter) {
  legwork_assert(counter != nullptr, "counter must be a valid pointer");

  if (counter->value.load(std::memory_order_relaxed) == 0) {
    return true;
  }

  return false;
}

int legwork_get_fiber_id() {
  return s_tls_worker.active_fiber_id;
}
