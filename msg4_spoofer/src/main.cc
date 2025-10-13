#include "config.h"
#include "data_source.h"
#include "logging.h"
#include "msg2_config.h"
#include "msg2_decoder.h"
#include "rf_base.h"
#include "sib1_decoder.h"
#include "sib1_processor.h"
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "srsran/phy/utils/vector.h"
#include "srsran/srsran.h"
#include "ssb_decoder.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#define MAX_LEN 70176
#define SSB

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

  // Create RF instance
  std::unique_ptr<RFBase> rf_dev = create_rf_instance(conf);
  if (!rf_dev) {
    LOG_ERROR("Failed to create RF instance");
    return EXIT_FAILURE;
  }

  // Calculate samples per slot (1ms for 15kHz SCS)
  uint32_t slot_len = static_cast<uint32_t>(conf.rf.srate * 0.001);
  std::vector<cf_t> data_buffer(slot_len);

  srsran::phy_cfg_nr_t phy_cfg = {};
  phy_cfg.carrier.dl_center_frequency_hz = conf.rf.dl_frequency;
  phy_cfg.carrier.ul_center_frequency_hz = conf.rf.ul_frequency;
  phy_cfg.carrier.offset_to_carrier = 0;
  phy_cfg.carrier.scs = conf.ssb.scs; // TODO: split rf scs and ssb scs
  phy_cfg.carrier.nof_prb = conf.rf.nof_prb;
  phy_cfg.carrier.max_mimo_layers = 1;

  phy_cfg.duplex.mode = conf.ssb.duplex_mode;

  phy_cfg.ssb.scs = conf.ssb.scs;
  phy_cfg.ssb.pattern = conf.ssb.pattern;

  phy_cfg.pdsch.typeA_pos = ssb_result.mib.dmrs_typeA_pos;
  phy_cfg.pdsch.scs_cfg = ssb_result.mib.scs_common;
  phy_cfg.carrier.pci = 1;

  /* Get pointA and SSB absolute frequencies */
  double pointA_abs_freq_Hz = phy_cfg.carrier.dl_center_frequency_hz -
                              phy_cfg.carrier.nof_prb * SRSRAN_NRE *
                                  SRSRAN_SUBC_SPACING_NR(phy_cfg.carrier.scs) /
                                  2;
  double ssb_abs_freq_Hz = phy_cfg.carrier.ssb_center_freq_hz;
  /* Calculate integer SSB to pointA frequency offset in Hz */
  uint32_t ssb_pointA_freq_offset_Hz =
      (ssb_abs_freq_Hz > pointA_abs_freq_Hz)
          ? (uint32_t)(ssb_abs_freq_Hz - pointA_abs_freq_Hz)
          : 0;
  if (srsran_coreset_zero(phy_cfg.carrier.pci, ssb_pointA_freq_offset_Hz,
                          phy_cfg.ssb.scs, phy_cfg.carrier.scs,
                          ssb_result.mib.coreset0_idx,
                          &phy_cfg.pdcch.coreset[0])) {
    return false;
  }
  phy_cfg.pdcch.coreset_present[0] = true;

  /* Create SearchSpace0 */
  // SearchSpace0 will be configured by SIB1Processor to ensure proper reference
  // handling
  phy_cfg.pdcch.search_space_present[0] = true;

  /**************** Initialization of UE DL buffer ************************/
  // Allocate shared buffer for UE DL
  uint32_t sf_len = static_cast<uint32_t>(conf.rf.srate * 0.001);
  cf_t *ue_buffer = srsran_vec_cf_malloc(sf_len);
  if (!ue_buffer) {
    LOG_ERROR("Failed to allocate shared buffer");
    return EXIT_FAILURE;
  }

  srsran_ue_dl_nr_t ue_dl;
  srsran_ue_dl_nr_args_t ue_dl_args = {};
  memset(&ue_dl, 0, sizeof(ue_dl));

  ue_dl_args.nof_rx_antennas = 1;
  ue_dl_args.nof_max_prb = conf.rf.nof_prb;
  ue_dl_args.pdcch.measure_evm = false;
  ue_dl_args.pdcch.measure_time = false;
  ue_dl_args.pdcch.disable_simd = false;
  ue_dl_args.pdsch.sch.disable_simd = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded = false;
  ue_dl_args.pdsch.sch.decoder_scaling_factor = 0;
  ue_dl_args.pdsch.sch.max_nof_iter = 10;
  // ue_dl_args.scs = phy_cfg.carrier.scs;
  // ue_dl_args.sample_rate_hz = phy_cfg.carrier.sample_rate_hz;

  cf_t *input_ptrs[SRSRAN_MAX_PORTS] = {ue_buffer, nullptr, nullptr, nullptr};
  if (srsran_ue_dl_nr_init(&ue_dl, input_ptrs, &ue_dl_args) < SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to initialize shared UE DL");
    if (ue_buffer)
      free(ue_buffer);
    return EXIT_FAILURE;
  }

  if (srsran_ue_dl_nr_set_carrier(&ue_dl, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    return false;
  }

  // Create MSG2 configuration based on SIB1 availability
  MSG2Config msg2_config;
  msg2_config = MSG2ConfigBuilder::from_toml_only(conf);

  // Validate and print MSG2 configuration
  if (!msg2_config_utils::validate_config(msg2_config)) {
    LOG_ERROR("MSG2 configuration validation failed");
    return EXIT_FAILURE;
  }

  msg2_config_utils::print_config(msg2_config);

  // Update PHY configuration for MSG2 decoding
  if (!MSG2ConfigBuilder::configure_phy_for_msg2(phy_cfg, msg2_config,
                                                 ssb_result)) {
    LOG_ERROR("Failed to configure PHY for MSG2 decoding");
    return EXIT_FAILURE;
  }

  LOG_INFO(
      "MSG2 Decoder - Sample Rate: %.2f MHz, Freq: %.2f MHz, PRBs: %u, PCI: %u",
      conf.rf.srate / 1e6, conf.rf.dl_frequency / 1e6, conf.rf.nof_prb,
      conf.rf.N_id);

  // Initialize MSG2 decoder with shared UE DL and MSG2 config
  MSG2Decoder decoder(conf.rf.srate, conf.rf.nof_prb, conf.rf.N_id,
                      conf.rf.dl_frequency, &ue_dl);

  // Configure MSG2 decoder with our computed configuration
  PrachConfig prach_cfg;
  prach_cfg.config_idx = msg2_config.prach_config.config_idx;
  prach_cfg.is_nr = msg2_config.prach_config.is_nr;
  prach_cfg.hs_flag = msg2_config.prach_config.hs_flag;
  prach_cfg.root_seq_idx = msg2_config.prach_config.root_seq_idx;
  prach_cfg.zero_corr_zone = msg2_config.prach_config.zero_corr_zone;
  prach_cfg.num_preambles = msg2_config.prach_config.num_preambles;
  decoder.set_prach_config(prach_cfg);

  if (!decoder.init()) {
    LOG_ERROR("Failed to initialize MSG2 decoder");
    return EXIT_FAILURE;
  }

  LOG_INFO("MSG2 decoder initialized with %zu RA-RNTIs for blind decoding",
           msg2_config.ra_rnti_list.size());

  uint32_t total_msg2_found = 0;
  uint32_t current_slot_idx = 0;

  while (rf_dev->receive(
      reinterpret_cast<std::complex<float> *>(data_buffer.data()), slot_len)) {
    std::vector<MSG2Result> results =
        decoder.process_slot(data_buffer.data(), current_slot_idx);

    for (const auto &msg2 : results) {
      total_msg2_found++;
      LOG_INFO("MSG2 [#%u] SFN=%u Slot=%u RA-RNTI=0x%04x TC-RNTI=0x%04x "
               "RAPID=%u TA=%u",
               total_msg2_found, msg2.sfn, msg2.slot_in_frame, msg2.ra_rnti,
               msg2.tc_rnti, msg2.rapid, msg2.timing_advance);

      // Print detailed MSG2 information
      MSG2Decoder::print_msg2_details(msg2, total_msg2_found);
    }

    current_slot_idx = (current_slot_idx + 1) % 10240;
  }

  // Cleanup ue resources
  srsran_ue_dl_nr_free(&ue_dl);
  if (ue_buffer) {
    free(ue_buffer);
  }

  return EXIT_SUCCESS;
}
