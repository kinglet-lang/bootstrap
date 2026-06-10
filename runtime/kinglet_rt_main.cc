#include "runtime/kinglet_rt.h"

int main(int argc, char **argv) {
  kl_set_program_args(argc, const_cast<const char **>(argv));
  return kinglet_main();
}
