#include "legwork.h"
#include <atomic>
#define lest_FEATURE_AUTO_REGISTER 1
#include "lest.hpp"

#define CASE(name) lest_CASE(specs, name)

// init/shutdown helper if an exception gets thrown
struct init_t {
  init_t(const legwork_config_t* config) {
    legwork_init(config);
  }
  ~init_t() {
    legwork_shutdown();
  }
};

static lest::tests specs;

CASE("it runs one simple job") {
  init_t init(nullptr);

  int value = 0;
  auto func = [](void* arg) { *((int*)arg) = 10; };

  legwork_counter_t* counter;
  legwork_task_t task;
  task.func = func;
  task.arg = &value;
  legwork_task_add(&task, 1, &counter);
  legwork_wait(counter, 0);
  EXPECT(value == 10);
}

CASE("one job will wait for another") {
  init_t init(nullptr);

  int value = 0;
  auto func1 = [](void* arg) {
    *((int*)arg) += 1;

    auto func2 = [](void* arg) { *((int*)arg) += 2; };

    legwork_counter_t* counter;
    legwork_task_t task;
    task.func = func2;
    task.arg = arg;
    legwork_task_add(&task, 1, &counter);
    legwork_wait(counter, 0);

    *((int*)arg) += 4;
  };

  legwork_counter_t* counter;
  legwork_task_t task;
  task.func = func1;
  task.arg = &value;
  legwork_task_add(&task, 1, &counter);
  legwork_wait(counter, 0);
  EXPECT(value == 7);
}

CASE("run many jobs") {
  init_t init(nullptr);

  std::atomic<uint32_t> value(0);
  auto func = [](void* arg) { ((std::atomic<uint32_t>*)arg)->fetch_add(1, std::memory_order_relaxed); };

  legwork_counter_t* counter;
  const int task_count = 1000;
  legwork_task_t* tasks = new legwork_task_t[task_count];
  for (int index = 0; index < task_count; ++index) {
    tasks[index].func = func;
    tasks[index].arg = &value;
  }
  legwork_task_add(tasks, task_count, &counter);
  legwork_wait(counter, 0);
  delete[] tasks;
  EXPECT(value == task_count);
}

int main(int argc, char** argv) {
  int failed_count = lest::run(specs, argc, argv);
  if (failed_count == 0) {
    printf("All tests pass!\n");
  }
  else {
    printf("%d failures\n", failed_count);
  }

  return failed_count;
}
