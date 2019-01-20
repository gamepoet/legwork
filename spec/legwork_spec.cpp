#include <atomic>
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

    auto func2 = [](void* task) { *((int*)task) += 2; };

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
