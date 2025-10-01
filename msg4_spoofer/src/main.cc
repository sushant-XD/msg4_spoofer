#include "config.h"
#include "data_source.h"
#include "logging.h"
#include "srsran/srsran.h"
#include <iostream>
#include <srsran/phy/utils/vector.h>
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

  uint32_t fft_size = srsran_symbol_sz(conf.rf.nof_prb);
  if (fft_size == 0) {
    LOG_ERROR("Invalid number of PRBs for PRACH.");
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

  if (prach.N_seq == 0) {
    LOG_ERROR("Invalid number of sequences after configuration");
  }
  cf_t *preamble_buffer = srsran_vec_cf_malloc(prach.N_seq + prach.N_cp);
  uint32_t indices[64];
  memset(indices, 0, sizeof(indices));
  for (int seq_index = 0; seq_index < 64; seq_index++) {
    LOG_INFO("GENERATING PRACH...");
    srsran_prach_gen(&prach, seq_index, 0, preamble);

    // Transmit the generated prach sequence we generate
  }

  rf_handler rf_dev = rf_handler();
  uint32_t nof_channels = 1;
  const uhd::device_addr_t dev_addr = uhd::device_addr_t(args.rf.device_args);
  handle_uhd_error(rf_dev.usrp_make(dev_addr, nof_channels));

  size_t channel_no = 0;
  handle_uhd_error(rf_dev.set_tx_gain(channel_no, args.rf.tx_gain));
  handle_uhd_error(rf_dev.set_tx_rate(args.sampling_freq));
  float actual_frequency = 0.0;
  handle_uhd_error(
      rf_dev.set_tx_freq(0, args.center_frequency, actual_frequency));

  // uhd::stream_args_t stream_args;
  //   tx_stream = rf_dev.get_tx_stream(stream_args);
  transmission(rf_dev.usrp, args);
}
