#include "config.h"
#include "data_source.h"
#include "logging.h"
#include "msg2_decoder.h"
#include "rf_base.h"
#include "srsran/srsran.h"
#include <chrono>
#include <iostream>
#include <srsran/phy/utils/vector.h>
#include <string>
#include <thread>

// Forward declaration for MSG2 spoofer integration
void run_msg2_spoofer_demo(const std::string &config_path);

#define MAX_LEN 70176

spoofer_error_e check_config_validity(spoofer_config_t &config) {
  if (config.rf.device_name != "uhd" && config.rf.device_name != "zmq" &&
      config.rf.device_name != "file") {
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
    LOG_ERROR("Usage: %s <config file>", argv[0]);
    return EXIT_FAILURE;
  }

  std::string config_path(argv[1]);

  // Load and validate configuration
  spoofer_config_t conf = load(config_path);
  if (check_config_validity(conf) != SUCCESS) {
    LOG_ERROR("Configuration validation failed");
    return EXIT_FAILURE;
  }


  LOG_INFO(
      "MSG2 Decoder - Sample Rate: %.2f MHz, Freq: %.2f MHz, PRBs: %u, PCI: %u",
      conf.rf.srate / 1e6, conf.rf.frequency / 1e6, conf.rf.nof_prb,
      conf.rf.N_id);

  // Initialize MSG2 decoder
  MSG2Decoder decoder(conf.rf.srate, conf.rf.nof_prb, conf.rf.N_id,
                      conf.rf.frequency);

  PrachConfig prach_cfg;
  prach_cfg.config_idx = conf.prach.config_idx;
  prach_cfg.is_nr = conf.prach.is_nr;
  prach_cfg.hs_flag = conf.prach.hs_flag;
  prach_cfg.root_seq_idx = conf.prach.root_seq_idx;
  prach_cfg.zero_corr_zone = conf.prach.zero_corr_zone;
  prach_cfg.num_preambles = conf.prach.num_ra_preambles;
  decoder.set_prach_config(prach_cfg);

  if (!decoder.init()) {
    LOG_ERROR("Failed to initialize MSG2 decoder");
    return EXIT_FAILURE;
  }

  // Create RF instance
  std::unique_ptr<RFBase> rf_dev = create_rf_instance(conf);
  if (!rf_dev) {
    LOG_ERROR("Failed to create RF instance");
    return EXIT_FAILURE;
  }

  // Calculate samples per slot (1ms for 15kHz SCS)
  uint32_t slot_len = static_cast<uint32_t>(conf.rf.srate * 0.001);
  std::vector<cf_t> data_buffer(slot_len);

  LOG_INFO("Starting MSG2 monitoring (Ctrl+C to stop)...");

  uint32_t slot_idx = 0;
  size_t total_msg2_found = 0;

  // Main receive loop
  while (rf_dev->receive(reinterpret_cast<std::complex<float>*>(data_buffer.data()), slot_len)) {
    std::vector<MSG2Result> results =
        decoder.process_slot(data_buffer.data(), slot_idx);

    for (const auto &msg2 : results) {
      total_msg2_found++;
      LOG_INFO("MSG2 [#%zu] SFN=%u Slot=%u RA-RNTI=0x%04x TC-RNTI=0x%04x "
               "RAPID=%u TA=%u",
               total_msg2_found, msg2.sfn, msg2.slot_in_frame, msg2.ra_rnti,
               msg2.tc_rnti, msg2.rapid, msg2.timing_advance);
    }

    slot_idx = (slot_idx + 1) % 10240;
  }

  return EXIT_SUCCESS;
}
