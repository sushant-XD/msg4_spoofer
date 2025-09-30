#include "main.h"
#include "config.h"
#include "data_source.h"
#include "logging.h"
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char *argv[]) {
  std::cout << "Hello world" << std::endl;
  std::string config_path(argv[1]);

  agent_config_t conf = load(config_path);

  if (argc != 2) {
    LOG_ERROR("Usage: pdcch-agent <config file>\n");
    return EXIT_FAILURE;
  }

  data_source *src;
  if (!conf.rf.file_path.empty())
    src = new data_source(strdup(conf.rf.file_path.c_str()),
                          SRSRAN_COMPLEX_FLOAT_BIN);
  else
    src = new data_source(strdup(conf.rf.rf_args), conf.rf.gain,
                          conf.rf.frequency, conf.rf.sample_rate);
}
