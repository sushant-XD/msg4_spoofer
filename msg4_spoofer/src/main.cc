#include "config.h"
#include "data_source.h"
#include "logging.h"
#include "rf_base.h"
#include "srsran/srsran.h"
#include <chrono>
#include <iostream>
#include <srsran/phy/utils/vector.h>
#include <string>
#include <thread>
#include <uhd/error.h>
#include <uhd/stream.hpp>
#include <uhd/types/device_addr.hpp>
#define MAX_LEN 70176

spoofer_error_e check_config_validity(spoofer_config_t &config) {
  if (config.rf.device_name != "uhd" && config.rf.device_name != "zmq") {
    LOG_ERROR("invalid device name");
    return CONFIG_ERROR;
  }
  if (config.prach.num_ra_preambles == 0 ||
      config.prach.num_ra_preambles > 64) {

    LOG_ERROR("invalid  number of preambles");
    return CONFIG_ERROR;
  }
  return SUCCESS;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    LOG_ERROR("Usage: msg4_spoofer <config file>\n");
    return EXIT_FAILURE;
  }

  std::string config_path(argv[1]);
  spoofer_config_t conf = load(config_path);

  if (check_config_validity(conf) != SUCCESS)
    return CONFIG_ERROR;

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

  uint32_t fft_size = srsran_symbol_sz(conf.rf.nof_prb);
  if (fft_size == 0) {
    LOG_ERROR("Invalid number of PRBs");
    return INIT_ERROR;
  }

  if (srsran_prach_init(&prach, srsran_symbol_sz(nof_prb))) {
    LOG_ERROR("Failed to initialize PRACH");
    return INIT_ERROR;
  }
  if (srsran_prach_set_cfg(&prach, &prach_cfg, nof_prb)) {
    LOG_ERROR("Error configuring PRACH\n");
    return CONFIG_ERROR;
  }

  LOG_INFO("PRACH CONFIGURED");

  LOG_INFO("Creating RF instance for: %s", conf.rf.device_name.c_str());
  std::unique_ptr<RFBase> rf_dev = create_rf_instance(conf);
  if (!rf_dev) {
    LOG_ERROR("Failed to create RF instance. Exiting.");
    return EXIT_FAILURE;
  }

  size_t preamble_len = prach.N_seq + prach.N_cp;

  std::vector<cf_t *> preambles(conf.prach.num_ra_preambles);
  for (int i = 0; i < preambles.size(); ++i) {
    preambles[i] = srsran_vec_cf_malloc(preamble_len);
    srsran_prach_gen(&prach, i, conf.rf.freq_offset, preambles[i]);
  }

  uint32_t current_seq_idx = 0;

  while (true) {

    cf_t *tx_buffer = preambles[current_seq_idx];
    std::vector<std::complex<float>> tx_vector(tx_buffer,
                                               tx_buffer + preamble_len);

    if (rf_dev->transmit(conf, tx_vector) != SUCCESS) {
      LOG_ERROR("Error during transmission.");
      return CONFIG_ERROR;
    }

    current_seq_idx = (current_seq_idx + 1) % conf.prach.num_ra_preambles;
    if (conf.prach.time_delay > 0) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(conf.prach.time_delay));
    }
  }

  for (auto &preamble : preambles) {
    free(preamble);
  }
}
