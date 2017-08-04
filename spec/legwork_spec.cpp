#include "legwork.h"
#define lest_FEATURE_AUTO_REGISTER 1
#include "lest.hpp"

#define CASE(name) lest_CASE(specs, name)

static lest::tests specs;

CASE("it runs one simple job") {
  // TODO:
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
