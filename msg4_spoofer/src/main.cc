// 5G NR Random Access Response (RAR) decoder

#include "config.h"
#include "msg2_decoder_standalone.h"
#include "rar_decoder.h"
#include "rf_base.h"
#include "srsran/srslog/srslog.h"
#include <csignal>
#include <iostream>

static volatile bool keep_running = true;

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
  logger.info("=====================");
  logger.info("Config: %s", config_path.c_str());

  RARDecoder decoder(config);
  if (!decoder.init()) {
    logger.error("Failed to initialize decoder");
    return EXIT_FAILURE;
  }

  uint32_t slot_len = decoder.get_slot_len();
  uint32_t slots_per_subframe = decoder.get_slots_per_subframe();
  uint32_t sf_len = slot_len * slots_per_subframe;

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
  logger.info("  Slot Len:   %u samples", slot_len);
  logger.info("  Duplex:     %s",
              config.duplex_mode == SRSRAN_DUPLEX_MODE_FDD ? "FDD" : "TDD");
  logger.info("  Device:     %s", conf.rf.device_name.c_str());

  logger.info("");
  logger.info("PRACH Config:");
  logger.info("  Index:      %u", conf.prach.config_idx);
  logger.info("  RA-RNTIs:   %zu values", config.ra_rnti_list.size());
  for (size_t i = 0; i < config.ra_rnti_list.size(); i++) {
    logger.info("    [%zu] %u (0x%04x)", i, config.ra_rnti_list[i],
                config.ra_rnti_list[i]);
  }

  logger.info("");
  logger.info("Starting search (Ctrl+C to stop)...");
  logger.info("");

  std::unique_ptr<RFBase> rf_dev = create_rf_instance(conf);
  if (!rf_dev) {
    logger.error("Failed to create RF device");
    return EXIT_FAILURE;
  }

  std::vector<cf_t> data_buffer(sf_len);
  uint32_t slot_number = 0;

  // Main RX loop: read samples and process
  while (keep_running &&
         rf_dev->receive(
             reinterpret_cast<std::complex<float> *>(data_buffer.data()),
             sf_len)) {

    for (uint32_t slot_in_sf = 0; slot_in_sf < slots_per_subframe;
         slot_in_sf++) {
      cf_t *slot_buffer = data_buffer.data() + slot_in_sf * slot_len;
      decoder.process_slot(slot_buffer, slot_number);
      logger.info("Processed slot: %d", slot_number);
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
