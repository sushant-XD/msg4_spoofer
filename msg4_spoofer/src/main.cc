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
#define MAX_LEN 70176

spoofer_error_e check_config_validity(spoofer_config_t &config) {
  if (config.rf.device_name != "uhd" && config.rf.device_name != "zmq" && config.rf.device_name != "file") {
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

  LOG_INFO("Creating RF instance for device: %s", conf.rf.device_name.c_str());
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

  LOG_INFO("Generated %zu preambles", preambles.size());

  // Simple continuous RF reading using srsRAN RF API (supports both UHD and ZMQ)
  LOG_INFO("Starting continuous RF reading with %s device using srsRAN RF API...", 
           conf.rf.device_name.c_str());
  
  const uint32_t samples_per_read = 1920; // One LTE slot
  size_t total_samples_processed = 0;
  size_t iteration_count = 0;
  
  // Allocate buffer for samples
  std::vector<std::complex<float>> data_buffer(samples_per_read);
  
  // Simple receive loop using srsRAN RF API - auto-starts RX stream on first call
  int nrecv;
  while ((nrecv = rf_dev->receive(data_buffer.data(), samples_per_read)) > 0) {
    iteration_count++;
    total_samples_processed += nrecv;
    
    // TODO: Process the received samples here
    // - PRACH detection
    // - Signal analysis  
    // - MSG4 spoofing logic
    
    // Log progress periodically
    if (iteration_count % 1000 == 0) {
      LOG_INFO("Processed %zu samples in %zu iterations using %s", 
               total_samples_processed, iteration_count, conf.rf.device_name.c_str());
    }
    
    // Demo exit condition - remove this in actual implementation
    if (iteration_count >= 10000) {
      LOG_INFO("Demo: Processed enough iterations (%zu), stopping...", iteration_count);
      break;
    }
  }
  
  // Check if we exited due to error
  if (nrecv < 0) {
    LOG_ERROR("Receive loop exited due to error: %d", nrecv);
  }
  
  LOG_INFO("RF receive loop completed. Total: %zu samples in %zu iterations using %s", 
           total_samples_processed, iteration_count, conf.rf.device_name.c_str());

  for (auto &preamble : preambles) {
    free(preamble);
  }
}
