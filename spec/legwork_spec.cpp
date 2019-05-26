#include <atomic>
#include <condition_variable>
#include <mutex>
#include "catch.hpp"
#include "legwork.h"

// init/shutdown helper if an exception gets thrown
struct init_t {
  init_t(const legwork_config_t* config) {
    legwork_lib_init(config);
  }
  ~init_t() {
    legwork_lib_shutdown();
  }
};

TEST_CASE("it runs one simple job") {
  init_t init(nullptr);

  int value = 0;
  auto func = [](void* task) { *((int*)task) = 10; };

  legwork_counter_t* counter;
  legwork_task_desc_t task_desc;
  task_desc.func = func;
  task_desc.task = &value;
  legwork_task_add(&task_desc, 1, &counter);
  legwork_wait(counter);
  CHECK(value == 10);
}

TEST_CASE("one job will wait for another") {
  init_t init(nullptr);

  int value = 0;
  auto func1 = [](void* task) {
    *((int*)task) += 1;

    auto func2 = [](void* task2) { *((int*)task2) += 2; };

    legwork_counter_t* counter;
    legwork_task_desc_t task_desc;
    task_desc.func = func2;
    task_desc.task = task;
    legwork_task_add(&task_desc, 1, &counter);
    legwork_wait(counter);

    *((int*)task) += 4;
  };

  legwork_counter_t* counter;
  legwork_task_desc_t task_desc;
  task_desc.func = func1;
  task_desc.task = &value;
  legwork_task_add(&task_desc, 1, &counter);
  legwork_wait(counter);
  CHECK(value == 7);
}

TEST_CASE("run many jobs") {
  init_t init(nullptr);

  std::atomic<uint32_t> value(0);
  auto func = [](void* task) { ((std::atomic<uint32_t>*)task)->fetch_add(1, std::memory_order_relaxed); };

  legwork_counter_t* counter;
  const int task_count = 1000;
  legwork_task_desc_t* task_descs = new legwork_task_desc_t[task_count];
  for (int index = 0; index < task_count; ++index) {
    task_descs[index].func = func;
    task_descs[index].task = &value;
  }
  legwork_task_add(task_descs, task_count, &counter);
  legwork_wait(counter);
  delete[] task_descs;
  CHECK(value == task_count);
}

TEST_CASE("you can run a fire-and-forget task") {
  init_t init(nullptr);

  bool is_complete;
  std::mutex complete_mutex;
  std::condition_variable complete_cv;

  struct task_t {
    std::atomic<uint32_t>* val;
    bool* is_complete;
    std::mutex* complete_mutex;
    std::condition_variable* complete_cv;
  };

  std::atomic<uint32_t> value(0);
  auto func = [](void* in_task) {
    task_t* task = (task_t*)in_task;
    task->val->fetch_add(1, std::memory_order_relaxed);
    {
      *task->is_complete = true;
      std::lock_guard<std::mutex>(*task->complete_mutex);
      task->complete_cv->notify_all();
    }
  };

  task_t task = {};
  task.val = &value;
  task.is_complete = &is_complete;
  task.complete_mutex = &complete_mutex;
  task.complete_cv = &complete_cv;
  legwork_task_desc_t task_desc = {};
  task_desc.func = func;
  task_desc.task = &task;
  legwork_task_add(&task_desc, 1, NULL);

  {
    std::unique_lock<std::mutex> lock(complete_mutex);
    complete_cv.wait(lock, [&is_complete] { return is_complete; });
  }

  CHECK(value == 1);
}
