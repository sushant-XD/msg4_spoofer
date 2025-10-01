#include "config.h"
#include "data_source.h"
#include "logging.h"
#include "srsran/srsran.h"
#include <iostream>
#include <string>
#include <vector>

#define MAX_LEN 70176

int main(int argc, char *argv[]) {
  std::string config_path(argv[1]);

  spoofer_config_t conf = load(config_path);

  if (argc != 2) {
    LOG_ERROR("Usage: msg4_spoofer <config file>\n");
    return EXIT_FAILURE;
  }

  srsran_prach_t prach;
  srsran_prach_cfg_t prach_cfg;

  int nof_prb = conf.rf.nof_prb;

  prach_cfg.is_nr = conf.prach.is_nr;
  prach_cfg.config_idx = conf.prach.config_idx;
  prach_cfg.hs_flag = conf.prach.hs_flag;
  prach_cfg.freq_offset = conf.prach.freq_offset;
  prach_cfg.root_seq_idx = conf.prach.root_seq_idx;
  prach_cfg.zero_corr_zone = conf.prach.zero_corr_zone;
  prach_cfg.num_ra_preambles = conf.prach.num_ra_preambles;

  if (srsran_prach_init(&prach, srsran_symbol_sz(nof_prb))) {
    LOG_ERROR("Failed to initialize PRACH");
    return -1;
  }
  if (srsran_prach_set_cfg(&prach, &prach_cfg, nof_prb)) {
    LOG_ERROR("Error configuring PRACH\n");
    return -1;
  }

  LOG_INFO("PRACH CONFIGURED");

  cf_t preamble[MAX_LEN];
  memset(preamble, 0, sizeof(cf_t) * MAX_LEN);
  uint32_t indices[64];
  memset(indices, 0, sizeof(indices));
  for (int seq_index = 0; seq_index < 64; seq_index++) {
    LOG_INFO("GENERATING PRACH...");
    srsran_prach_gen(&prach, seq_index, 0, preamble);

    // Transmit the generated prach sequence we generate
  }
}
