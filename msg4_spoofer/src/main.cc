// 5G NR Random Access Response (RAR) decoder

#include "config.h"
#include "msg2_decoder_standalone.h"
#include "rar_decoder.h"
#include "rf_base.h"
#include "srsran/srslog/srslog.h"
#include "ssb_decoder.h"
#include "sib1_processor.h"
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

  // SSB Detection Parameters
  config.ssb_window_size_ms = conf.ssb.window_size_ms;
  config.ssb_step_size_ms = conf.ssb.step_size_ms;
  config.ssb_overlap_ms = conf.ssb.overlap_ms;
  config.ssb_max_search_steps = conf.ssb.max_search_steps;

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

  // Single-pass SSB detection with configurable sliding window
  uint32_t ssb_sample_len = sf_len; // 1ms worth of samples
  uint32_t window_size =
      ssb_sample_len * config.ssb_window_size_ms; // total reading window size
  uint32_t step_size =
      ssb_sample_len * config.ssb_step_size_ms; // step size per reading
  uint32_t overlap_len =
      ssb_sample_len *
      config.ssb_overlap_ms; // overlap size from one reading to the next

  cf_t *window_buffer = srsran_vec_cf_malloc(window_size + overlap_len);
  SsbSearchResult ssb_result;
  bool ssb_found = false;

  logger.info("=== SSB Detection Configuration ===");
  logger.info("Window: %u samples (%u ms)", window_size,
              config.ssb_window_size_ms);
  logger.info("Step: %u samples (%u ms)", step_size, config.ssb_step_size_ms);
  logger.info("Overlap: %u samples (%u ms)", overlap_len,
              config.ssb_overlap_ms);
  logger.info("Max steps: %u", config.ssb_max_search_steps);
  logger.info("===================================");

  // Initialize with first window of data
  logger.info("Reading initial %u samples (%u ms) for SSB detection...",
              window_size, config.ssb_window_size_ms);
  if (!rf_dev->receive(window_buffer, window_size)) {
    logger.error("Failed to receive initial data for SSB search");
    return EXIT_FAILURE;
  }

  // Single-pass sliding window search
  uint32_t total_samples_processed = window_size;
  uint32_t step_count = 0;

  while (!ssb_found && step_count < config.ssb_max_search_steps) {
    // Search in current window (with overlap from previous step)
    logger.info(
        "Step %u: Searching window [%u-%u] samples (%.1f ms)", step_count,
        total_samples_processed - window_size, total_samples_processed,
        (total_samples_processed - window_size) * 1000.0 / config.sample_rate);

    ssb_result = ssb_decoder.scan_ssb(window_buffer, window_size + overlap_len,
                                      config.ncellid);

    if (ssb_result.found) {
      logger.info("  SSB found at step %u!", step_count);
      logger.info(
          "  Detection time: %.1f ms",
          (total_samples_processed - window_size + ssb_result.t_offset) *
              1000.0 / config.sample_rate);
      ssb_found = true;
      break;
    }

    // Shift window: move overlap to beginning
    memcpy(window_buffer, window_buffer + window_size,
           overlap_len * sizeof(cf_t));

    // Read next step of data
    if (!rf_dev->receive(window_buffer + overlap_len, step_size)) {
      logger.warning("End of file reached at step %u without finding SSB",
                     step_count);
      break;
    }

    total_samples_processed += step_size;
    step_count++;
  }

  if (!ssb_found) {
    if (step_count >= config.ssb_max_search_steps) {
      logger.error(
          "SSB search timeout - no SSB found in %u steps (%.1f seconds)",
          config.ssb_max_search_steps,
          config.ssb_max_search_steps * config.ssb_step_size_ms / 1000.0);
    } else {
      logger.error("SSB search failed - no SSB found in the data");
    }
    return EXIT_FAILURE;
  }

  logger.info("=== SSB Detection Results ===");
  logger.info("PCI:        %u", ssb_result.pci);
  logger.info("SSB Index:  %u", ssb_result.ssb_idx);
  logger.info("SNR:        %.1f dB", ssb_result.snr_db);
  logger.info("RSRP:       %.1f dBm", ssb_result.rsrp_dbm);
  logger.info("SFN:        %u", ssb_result.mib.sfn);
  logger.info("Time Offset: %u samples", ssb_result.t_offset);
  logger.info("=============================");

  // Clean up SSB detection buffer
  free(window_buffer);

  // ============================================================================
  // SIB1 DECODING PHASE
  // ============================================================================
  logger.info("");
  logger.info("=== Starting SIB1 Decoding ===");
  
  // Initialize SIB1 processor
  SIB1Processor sib1_processor(config.sample_rate, config.nof_prb, config.ncellid, config.dl_freq);
  if (!sib1_processor.init()) {
    logger.error("Failed to initialize SIB1 processor");
    return EXIT_FAILURE;
  }
  
  // Set up UE DL for SIB1 decoding
  srsran_ue_dl_nr_t ue_dl = {};
  srsran::phy_cfg_nr_t phy_cfg;
  
  // Initialize physical configuration from SSB results
  phy_cfg.carrier.dl_center_frequency_hz = config.dl_freq;
  phy_cfg.carrier.ul_center_frequency_hz = config.ul_freq;
  phy_cfg.carrier.ssb_center_freq_hz = config.ssb_freq;
  phy_cfg.carrier.offset_to_carrier = 0; // Will be updated from MIB
  phy_cfg.carrier.scs = config.scs_common;
  phy_cfg.carrier.nof_prb = config.nof_prb;
  phy_cfg.carrier.pci = config.ncellid;
  phy_cfg.carrier.max_mimo_layers = 1;
  
  phy_cfg.duplex.mode = config.duplex_mode;
  phy_cfg.ssb.periodicity_ms = config.ssb_period_ms;
  phy_cfg.ssb.position_in_burst[0] = true;
  phy_cfg.ssb.scs = config.scs_ssb;
  phy_cfg.ssb.pattern = config.ssb_pattern;
  
  // Configure CORESET0 and SearchSpace0 for SIB1 using MIB data
  // Extract CORESET0 and SearchSpace0 indices from MIB
  uint8_t coreset0_idx = ssb_result.mib.coreset0_idx;
  uint8_t ss0_idx = ssb_result.mib.ss0_idx;
  
  logger.info("MIB CORESET0 index: %u, SearchSpace0 index: %u", coreset0_idx, ss0_idx);
  
  // Calculate SSB to PointA frequency offset (critical for CORESET0)
  double pointA_abs_freq_Hz = config.dl_freq - config.nof_prb * SRSRAN_NRE * SRSRAN_SUBC_SPACING_NR(config.scs_common) / 2;
  double ssb_abs_freq_Hz = config.ssb_freq;
  uint32_t ssb_pointA_freq_offset_Hz = (ssb_abs_freq_Hz > pointA_abs_freq_Hz) ? 
                                       (uint32_t)(ssb_abs_freq_Hz - pointA_abs_freq_Hz) : 0;
  
  logger.info("PointA frequency: %.2f MHz", pointA_abs_freq_Hz / 1e6);
  logger.info("SSB frequency: %.2f MHz", ssb_abs_freq_Hz / 1e6);
  logger.info("SSB-PointA offset: %u Hz", ssb_pointA_freq_offset_Hz);
  
  // Configure CORESET0 using MIB data
  srsran_coreset_t *coreset0 = &phy_cfg.pdcch.coreset[0];
  if (srsran_coreset_zero(config.ncellid, ssb_pointA_freq_offset_Hz, config.scs_ssb, config.scs_common, 
                          coreset0_idx, coreset0) < SRSRAN_SUCCESS) {
    logger.error("Failed to configure CORESET0 for SIB1 (coreset0_idx=%u)", coreset0_idx);
    return EXIT_FAILURE;
  }
  
  coreset0->id = 0;
  coreset0->mapping_type = srsran_coreset_mapping_type_interleaved; // Use interleaved as per 5G Sniffer
  coreset0->precoder_granularity = srsran_coreset_precoder_granularity_reg_bundle;
  coreset0->reg_bundle_size = srsran_coreset_bundle_size_n6; // As per 5G Sniffer (6)
  coreset0->interleaver_size = srsran_coreset_bundle_size_n2; // As per 5G Sniffer (2)
  coreset0->shift_index = config.ncellid; // nshift = cell_id as per 5G Sniffer
  phy_cfg.pdcch.coreset_present[0] = true;
  
  logger.info("CORESET0 configured: %u RBs, %u symbols, duration=%u, offset_rb=%u", 
              coreset0->offset_rb, coreset0->duration, coreset0->duration, coreset0->offset_rb);
  logger.info("CORESET0 mapping: interleaved, bundle_size=%u, interleaver_size=%u, shift_index=%u", 
              coreset0->reg_bundle_size, coreset0->interleaver_size, coreset0->shift_index);
  
  // Configure SearchSpace0 using standard srsRAN configuration
  srsran_search_space_t *ss0 = &phy_cfg.pdcch.search_space[0];
  ss0->id = 0;
  ss0->coreset_id = 0;
  ss0->type = srsran_search_space_type_common_0;
  
  // Use standard SearchSpace0 configuration from 3GPP TS 38.213 Table 10.1-1
  // This is critical for SIB1 decoding
  ss0->nof_candidates[0] = 0;  // AL1: 0 candidates
  ss0->nof_candidates[1] = 0;  // AL2: 0 candidates  
  ss0->nof_candidates[2] = 1;  // AL4: 1 candidate
  ss0->nof_candidates[3] = 0;  // AL8: 0 candidates
  ss0->nof_candidates[4] = 0;  // AL16: 0 candidates
  
  ss0->duration = 2;  // 2 consecutive slots
  ss0->nof_formats = 1;
  ss0->formats[0] = srsran_dci_format_nr_1_0;
  
  phy_cfg.pdcch.search_space_present[0] = true;
  
  logger.info("SearchSpace0 configured: AL4=1 candidate, duration=2 slots");
  
  // Initialize UE DL
  srsran_ue_dl_nr_args_t ue_dl_args = {};
  ue_dl_args.nof_max_prb = config.nof_prb;
  ue_dl_args.nof_rx_antennas = 1;
  ue_dl_args.pdcch.measure_evm = false;
  ue_dl_args.pdcch.measure_time = false;
  ue_dl_args.pdcch.disable_simd = false;
  ue_dl_args.pdsch.sch.disable_simd = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded = false;
  ue_dl_args.pdsch.sch.decoder_scaling_factor = 0;
  ue_dl_args.pdsch.sch.max_nof_iter = 10;
  
  // Allocate buffer for UE DL
  cf_t *ue_dl_buffer = srsran_vec_cf_malloc(slot_len);
  std::array<cf_t *, SRSRAN_MAX_PORTS> rx_buffer = {};
  rx_buffer[0] = ue_dl_buffer;
  
  if (srsran_ue_dl_nr_init(&ue_dl, rx_buffer.data(), &ue_dl_args) != 0) {
    logger.error("Failed to initialize UE DL for SIB1");
    free(ue_dl_buffer);
    return EXIT_FAILURE;
  }
  
  if (srsran_ue_dl_nr_set_carrier(&ue_dl, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    logger.error("Failed to set carrier for SIB1");
    srsran_ue_dl_nr_free(&ue_dl);
    free(ue_dl_buffer);
    return EXIT_FAILURE;
  }
  
  srsran_dci_cfg_nr_t dci_cfg = phy_cfg.get_dci_cfg();
  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl, &phy_cfg.pdcch, &dci_cfg) != SRSRAN_SUCCESS) {
    logger.error("Failed to set PDCCH config for SIB1");
    srsran_ue_dl_nr_free(&ue_dl);
    free(ue_dl_buffer);
    return EXIT_FAILURE;
  }
  
  logger.info("SIB1 processor and UE DL initialized successfully");
  logger.info("Searching for SIB1 (SI-RNTI=0x%x)...", SRSRAN_SIRNTI);
  
  // Search for SIB1
  SIB1SearchResult sib1_result = sib1_processor.search_and_decode(rf_dev.get(), ue_dl, phy_cfg, ssb_result, 1600);
  
  if (sib1_result.found) {
    logger.info("=== SIB1 Decoding Results ===");
    logger.info("SIB1 found in slot: %u", sib1_result.slot_found);
    logger.info("SIB1 JSON data length: %zu bytes", sib1_result.json_output.length());
    logger.info("=============================");
    
    // Log some key SIB1 information
    if (!sib1_result.json_output.empty()) {
      logger.info("SIB1 data preview (first 200 chars):");
      logger.info("%s", sib1_result.json_output.substr(0, 200).c_str());
    }
  } else {
    logger.error("SIB1 decoding failed - no SIB1 found");
    srsran_ue_dl_nr_free(&ue_dl);
    free(ue_dl_buffer);
    return EXIT_FAILURE;
  }
  
  // Clean up UE DL
  srsran_ue_dl_nr_free(&ue_dl);
  free(ue_dl_buffer);
  
  logger.info("SIB1 decoding completed successfully!");
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
