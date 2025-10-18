// 5G NR Random Access Response (RAR) decoder

#include "config.h"
#include "msg2_decoder_standalone.h"
#include "rar_decoder.h"
#include "rf_base.h"
#include "srsran/srslog/srslog.h"
#include "ssb_decoder.h"
#include <csignal>
#include <cstring>
#include <iostream>

static volatile bool keep_running = true;
void print_config(RARSearchConfig &config, srslog::basic_logger &logger);
void signal_handler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    keep_running = false;
  }
}

srslog::basic_logger &
init_logger(srslog::basic_levels level = srslog::basic_levels::info) {
  srslog::init();
  srslog::sink *sink = srslog::create_stdout_sink();
  srslog::log_channel *chan = srslog::create_log_channel("rar_search", *sink);
  srslog::set_default_sink(*sink);
  srslog::basic_logger &logger =
      srslog::fetch_basic_logger("rar_search", false);
  logger.set_level(level);
  return logger;
}

RARSearchConfig config_from_toml(const spoofer_config_t &conf) {
  RARSearchConfig config;

  config.band = conf.rf.band;
  config.nof_prb = conf.rf.nof_prb;
  config.ncellid = conf.rf.N_id;
  config.dl_freq = conf.rf.dl_frequency;
  config.ul_freq = conf.rf.ul_frequency;
  config.ssb_freq = conf.rf.ssb_frequency;
  config.sample_rate = conf.rf.srate;

  config.scs_common = conf.ssb.scs;
  config.scs_ssb = conf.ssb.scs;
  config.duplex_mode = conf.ssb.duplex_mode;
  config.ssb_pattern = conf.ssb.pattern;
  config.ssb_period_ms = conf.ssb.period_ms;
  config.ssb_period = conf.ssb.period_ms;

  // Auto-calculate RA-RNTI from PRACH config
  config.ra_rnti_list =
      calculate_ra_rnti_list(conf.prach.config_idx, conf.ssb.scs, 0, 0);

  config.coreset0_idx = conf.rar.coreset0_idx;
  config.ss0_idx = conf.rar.ss0_idx;
  config.offset_to_carrier = conf.rar.offset_to_carrier;
  config.ssb_offset = conf.rar.ssb_offset;
  config.nof_rx_antennas = conf.rar.nof_rx_antennas;

  config.dmrs_typeA_pos = (conf.rar.dmrs_typeA_pos == 2)
                              ? srsran_dmrs_sch_typeA_pos_2
                              : srsran_dmrs_sch_typeA_pos_3;

  config.pdcch_cfg_scs = conf.ssb.scs;
  config.cell_barred = false;
  config.intra_freq_reselection = true;
  config.hrf = false;
  config.sfn = 0;

  return config;
}

int main(int argc, char *argv[]) {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <config_file.toml>\n", argv[0]);
    return EXIT_FAILURE;
  }

  std::string config_path(argv[1]);
  spoofer_config_t conf;

  try {
    conf = load(config_path);
  } catch (const std::exception &e) {
    fprintf(stderr, "Error loading configuration: %s\n", e.what());
    return EXIT_FAILURE;
  }

  RARSearchConfig config = config_from_toml(conf);
  srslog::basic_logger &logger = init_logger(srslog::basic_levels::info);

  logger.info("5G NR RAR Search Tool");
  logger.info("Config: %s", config_path.c_str());

  RARDecoder decoder(config);
  if (!decoder.init()) {
    logger.error("Failed to initialize decoder");
    return EXIT_FAILURE;
  }

  print_config(config, logger);

  uint32_t slot_len = decoder.get_slot_len();
  uint32_t slots_per_subframe = decoder.get_slots_per_subframe();
  uint32_t sf_len = SRSRAN_SF_LEN_PRB(config.nof_prb);

  std::unique_ptr<RFBase> rf_dev = create_rf_instance(conf);
  if (!rf_dev) {
    logger.error("Failed to create RF device");
    return EXIT_FAILURE;
  }

  // === SSB SYNCHRONIZATION ===
  logger.info("Searching for SSB to synchronize...");
  SSBDecoder ssb_decoder;

  if (!ssb_decoder.init(config)) {
    logger.error("Failed to initialize SSB decoder");
    return EXIT_FAILURE;
  }

  // Single-pass SSB detection with sliding window approach
  uint32_t ssb_sample_len = sf_len;  // 1ms worth of samples
  uint32_t window_size = ssb_sample_len * 20;  // 20ms window
  uint32_t step_size = ssb_sample_len;  // 1ms step
  uint32_t overlap_len = ssb_sample_len;  // 1ms overlap
  
  cf_t *window_buffer = srsran_vec_cf_malloc(window_size + overlap_len);  // 21ms total
  SsbSearchResult ssb_result;
  bool ssb_found = false;
  
  logger.info("Single-pass SSB detection with sliding window:");
  logger.info("  Window size: %u samples (20ms)", window_size);
  logger.info("  Step size: %u samples (1ms)", step_size);
  logger.info("  Overlap: %u samples (1ms)", overlap_len);
  logger.info("  Total buffer: %u samples (21ms)", window_size + overlap_len);
  
  // Initialize with first 20ms of data
  if (!rf_dev->receive(window_buffer, window_size)) {
    logger.error("Failed to receive initial data for SSB search");
    return EXIT_FAILURE;
  }
  
  // Single-pass sliding window search
  uint32_t total_samples_processed = window_size;
  uint32_t step_count = 0;
  
  while (!ssb_found) {
    // Search in current window (with overlap from previous step)
    logger.info("Step %u: Searching in window [%u-%u] samples", 
                step_count, total_samples_processed - window_size, total_samples_processed);
    
    ssb_result = ssb_decoder.scan_ssb(window_buffer, window_size + overlap_len, config.ncellid);
    
    if (ssb_result.found) {
      logger.info("SSB found at step %u!", step_count);
      ssb_found = true;
      break;
    }
    
    // Shift window: move overlap to beginning
    memcpy(window_buffer, window_buffer + window_size, overlap_len * sizeof(cf_t));
    
    // Read next 1ms of data
    if (!rf_dev->receive(window_buffer + overlap_len, step_size)) {
      logger.error("End of file reached without finding SSB");
      return EXIT_FAILURE;
    }
    
    total_samples_processed += step_size;
    step_count++;
    
    // Safety check to prevent infinite loop
    if (step_count > 1000) {
      logger.error("SSB search timeout - no SSB found in 1000 steps");
      return EXIT_FAILURE;
    }
  }
  logger.info("SSB FOUND!");
  logger.info("  PCI:       %u", ssb_result.pci);
  logger.info("  SSB Index: %u", ssb_result.ssb_idx);
  logger.info("  SNR:       %.1f dB", ssb_result.snr_db);
  logger.info("  RSRP:      %.1f dBm", ssb_result.rsrp_dbm);
  logger.info("  SFN:       %u", ssb_result.mib.sfn);
  logger.info("  Time Offset: %u samples", ssb_result.t_offset);
  logger.info("");

  // Calculate slot alignment from SSB timing
  // SSB appears at specific slots, we can use this to align our slot counter
  uint32_t samples_per_slot = slot_len;
  uint32_t slot_offset = ssb_result.t_offset / samples_per_slot;

  logger.info("Synchronized! Starting RAR search from slot %u...", slot_offset);
  logger.info("");

  std::vector<cf_t> data_buffer(sf_len);
  uint32_t slot_number = slot_offset;

  // Main RX loop: read samples and process
  while (keep_running && rf_dev->receive(data_buffer.data(), sf_len)) {

    for (uint32_t slot_in_sf = 0; slot_in_sf < slots_per_subframe;
         slot_in_sf++) {
      cf_t *slot_buffer = data_buffer.data() + slot_in_sf * slot_len;
      if (decoder.process_slot(slot_buffer, slot_number) == true) {
        logger.info("RAR found in slot %u", slot_number);
        break;
      }
      if (slot_number % 100 == 0) {
        logger.info("Processed upto slot: %d", slot_number);
      }
      slot_number++;
    }
  }

  logger.info("");
  logger.info("Search completed");
  logger.info("  Slots processed: %u", decoder.get_slot_count());
  logger.info("  RARs found:      %u", decoder.get_rar_count());

  // Display stored RAR grants
  const auto &grants = decoder.get_rar_grants();
  if (!grants.empty()) {
    logger.info("");
    logger.info("Decoded RAR Grants (%zu total):", grants.size());
    logger.info("%-6s %-10s %-6s %-10s %-8s %-12s", "Slot", "RA-RNTI", "RAPID",
                "TC-RNTI", "TA", "TA(us)");
    logger.info(
        "----------------------------------------------------------------");

    for (const auto &grant : grants) {
      logger.info("%-6u 0x%04x     %-6u 0x%04x     %-8u %.3f",
                  grant.slot_number, grant.ra_rnti, grant.rapid, grant.tc_rnti,
                  grant.ta, grant.ta_time_us);
    }
  }

  return EXIT_SUCCESS;
}

void print_config(RARSearchConfig &config, srslog::basic_logger &logger) {
  logger.info("");
  logger.info("Cell Config:");
  logger.info("  Band:       %u", config.band);
  logger.info("  PRBs:       %u", config.nof_prb);
  logger.info("  Cell ID:    %u", config.ncellid);
  logger.info("  DL Freq:    %.2f MHz", config.dl_freq / 1e6);
  logger.info("  UL Freq:    %.2f MHz", config.ul_freq / 1e6);
  logger.info("  SSB Freq:   %.2f MHz", config.ssb_freq / 1e6);
  logger.info("  SCS:        %u kHz", 15 << config.scs_common);
  logger.info("  Samp Rate:  %.2f MHz", config.sample_rate / 1e6);
  logger.info("  Duplex:     %s",
              config.duplex_mode == SRSRAN_DUPLEX_MODE_FDD ? "FDD" : "TDD");

  logger.info("");
  logger.info("PRACH Config:");
  logger.info("  RA-RNTIs:   %zu values", config.ra_rnti_list.size());
  for (size_t i = 0; i < config.ra_rnti_list.size(); i++) {
    logger.info("    [%zu] %u (0x%04x)", i, config.ra_rnti_list[i],
                config.ra_rnti_list[i]);
  }
}
