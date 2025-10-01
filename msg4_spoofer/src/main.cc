#include "config.h"
#include "data_source.h"
#include "logging.h"
#include "srsran/srsran.h"
#include <iostream>
#include <string>
#include <vector>

#define MAX_LEN 70176

int main(int argc, char *argv[]) {
  // std::string config_path(argv[1]);

  static bool is_nr = false;
  static uint32_t nof_prb = 50;
  static uint32_t config_idx = 3;
  static uint32_t root_seq_idx = 0;
  static uint32_t zero_corr_zone = 15;
  static uint32_t num_ra_preambles = 0; // use default

  // agent_config_t conf = load(config_path);

  if (argc != 2) {
    LOG_ERROR("Usage: msg4_spoofer <config file>\n");
    return EXIT_FAILURE;
  }

  srsran_prach_t prach;

  srsran_prach_cfg_t prach_cfg;

  prach_cfg.is_nr = is_nr;
  prach_cfg.config_idx = config_idx;
  prach_cfg.hs_flag = false;
  prach_cfg.freq_offset = 0;
  prach_cfg.root_seq_idx = root_seq_idx;
  prach_cfg.zero_corr_zone = zero_corr_zone;
  prach_cfg.num_ra_preambles = num_ra_preambles;

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

  uint32_t seq_index = 0;
  uint32_t indices[64];
  uint32_t n_indices = 0;
  for (int i = 0; i < 64; i++)
    indices[i] = 0;

  for (seq_index = 0; seq_index < 64; seq_index++) {
    srsran_prach_gen(&prach, seq_index, 0, preamble);

    uint32_t prach_len = prach.N_seq;

    srsran_prach_detect(&prach, 0, &preamble[prach.N_cp], prach_len, indices,
                        &n_indices);
    if (n_indices != 1 || indices[0] != seq_index)
      return -1;
  }
}
