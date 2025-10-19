#pragma once

#define FREQ_DEFAULT 627750000
#define SRATE_DEFAULT 23040000
#define GAIN_DEFAULT 50.0
#define PRB_DEFAULT 106

#define PRACH_CONFIG_IDX_DEFAULT 1
#define PRACH_ROOT_SEQ_IDX_DEFUALT 1
#define PRACH_ZERO_CORR_ZONE_DEFUALT 0
#define PRACH_NUM_RA_PREAMBLES_DEFAULT 64
#define PRACH_FREQ_OFFSET_DEFAULT 0

#include "logging.h"
#include "srsran/phy/sync/ssb.h"
#include "toml.h"
#include <cstring>

typedef enum spoofer_error_t {
  SUCCESS = 0,
  INIT_ERROR,
  CONFIG_ERROR,
  FILE_ERROR,
  UHD_ERROR,
  ZMQ_ERROR,
  SAMPLE_ERROR
} spoofer_error_e;

typedef struct rf_config_s {
  uint32_t freq_offset;
  float rx_gain;
  float tx_gain;
  double srate;

  double dl_frequency;
  double ul_frequency;
  double ssb_frequency;
  uint32_t nof_prb;
  uint32_t N_id;
  uint32_t band;

  std::string device_name;
  std::string device_args;

  std::string file_path;
} rf_config_t;

typedef struct ssb_config_s {
  srsran_ssb_pattern_t pattern = SRSRAN_SSB_PATTERN_A;
  srsran_subcarrier_spacing_t scs = srsran_subcarrier_spacing_15kHz;
  srsran_duplex_mode_t duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
  double scan_duration;
  uint32_t period_ms;
  
  // SSB Detection Parameters
  uint32_t window_size_ms = 20;
  uint32_t step_size_ms = 1;
  uint32_t overlap_ms = 1;
  uint32_t max_search_steps = 1000;
} ssb_config_t;

/* struct available in prach.h*/
typedef struct prach_config_s {
  bool is_nr; // Set to true if NR
  uint32_t config_idx;
  uint32_t root_seq_idx;
  uint32_t zero_corr_zone;
  uint32_t freq_offset;
  uint32_t num_ra_preambles;
  bool hs_flag;
  uint64_t time_delay;
  // srsran_tdd_config_t tdd_config; // leave these to default
  // bool enable_successive_cancellation;
  // bool enable_freq_domain_offset_calc;
} prach_config_t;

/* RAR (MSG2) decoder configuration */
typedef struct rar_config_s {
  std::vector<uint16_t> ra_rnti_list;
  uint8_t coreset0_idx;
  uint8_t ss0_idx;
  uint32_t offset_to_carrier;
  uint32_t ssb_offset;
  uint8_t dmrs_typeA_pos;
  uint32_t nof_rx_antennas;
} rar_config_t;

typedef struct spoofer_config_s {
  rf_config_t rf;
  ssb_config_t ssb;
  prach_config_t prach;
  rar_config_t rar;
} spoofer_config_t;

static spoofer_config_t load(std::string config_path) {
  printf("Loading config from path: %s\n", config_path.c_str());
  toml::table toml = toml::parse_file(config_path);
  spoofer_config_t conf;

  // RF Configuration
  conf.rf.freq_offset = 0; // Note: freq_offset is for PRACH, not RF device
  conf.rf.rx_gain = toml["rf"]["rx_gain"].value_or(50.0);
  conf.rf.tx_gain = toml["rf"]["tx_gain"].value_or(50.0);
  conf.rf.srate = toml["rf"]["srate"].value_or(23.04e6);

  conf.rf.dl_frequency = toml["rf"]["dl_frequency"].value_or(1865.0e6);
  conf.rf.ul_frequency = toml["rf"]["ul_frequency"].value_or(1770.0e6);
  conf.rf.ssb_frequency = toml["rf"]["ssb_frequency"].value_or(1857.65e6);
  conf.rf.nof_prb = toml["rf"]["nof_prb"].value_or(106);
  conf.rf.N_id = toml["rf"]["cell_id"].value_or(1);
  conf.rf.band = toml["rf"]["band"].value_or(3);

  // Debug: Print loaded RF configuration
  printf("\n=== Loaded RF Config from TOML ===\n");
  printf("Device:       %s\n", conf.rf.device_name.c_str());
  printf("Device Args:  %s\n", conf.rf.device_args.c_str());
  printf("File Path:    %s\n", conf.rf.file_path.c_str());
  printf("DL Frequency: %.2f MHz\n", conf.rf.dl_frequency / 1e6);
  printf("UL Frequency: %.2f MHz\n", conf.rf.ul_frequency / 1e6);
  printf("SSB Frequency:%.2f MHz\n", conf.rf.ssb_frequency / 1e6);
  printf("Sample Rate:  %.2f MHz\n", conf.rf.srate / 1e6);
  printf("RX Gain:      %.1f dB\n", conf.rf.rx_gain);
  printf("TX Gain:      %.1f dB\n", conf.rf.tx_gain);
  printf("Num PRBs:     %u\n", conf.rf.nof_prb);
  printf("Cell ID:      %u\n", conf.rf.N_id);
  printf("Band:         %u\n", conf.rf.band);
  printf("==================================\n\n");

  conf.rf.device_name = toml["rf"]["device_name"].value_or("uhd");
  conf.rf.device_args = toml["rf"]["device_args"].value_or("type=b200");
  conf.rf.file_path = toml["rf"]["file_path"].value_or("");

  // SSB Configuration
  std::string pattern_str = toml["ssb"]["pattern"].value_or("A");
  if (pattern_str == "A") {
    conf.ssb.pattern = SRSRAN_SSB_PATTERN_A;
  } else if (pattern_str == "B") {
    conf.ssb.pattern = SRSRAN_SSB_PATTERN_B;
  } else if (pattern_str == "C") {
    conf.ssb.pattern = SRSRAN_SSB_PATTERN_C;
  } else {
    conf.ssb.pattern = SRSRAN_SSB_PATTERN_A;
  }

  uint32_t scs_khz = toml["ssb"]["scs"].value_or(15);
  if (scs_khz == 15) {
    conf.ssb.scs = srsran_subcarrier_spacing_15kHz;
  } else if (scs_khz == 30) {
    conf.ssb.scs = srsran_subcarrier_spacing_30kHz;
  } else {
    conf.ssb.scs = srsran_subcarrier_spacing_15kHz;
  }

  std::string duplex_str = toml["ssb"]["duplex_mode"].value_or("FDD");
  if (duplex_str == "FDD") {
    conf.ssb.duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
  } else if (duplex_str == "TDD") {
    conf.ssb.duplex_mode = SRSRAN_DUPLEX_MODE_TDD;
  } else {
    conf.ssb.duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
  }

  conf.ssb.scan_duration = toml["ssb"]["scan_duration"].value_or(1000.0);
  conf.ssb.period_ms = toml["ssb"]["period_ms"].value_or(10);
  
  // SSB Detection Parameters
  conf.ssb.window_size_ms = toml["ssb"]["window_size_ms"].value_or(20);
  conf.ssb.step_size_ms = toml["ssb"]["step_size_ms"].value_or(1);
  conf.ssb.overlap_ms = toml["ssb"]["overlap_ms"].value_or(1);
  conf.ssb.max_search_steps = toml["ssb"]["max_search_steps"].value_or(1000);

  // PRACH Configuration
  conf.prach.config_idx =
      toml["prach"]["config_idx"].value_or(PRACH_CONFIG_IDX_DEFAULT);
  conf.prach.is_nr = toml["prach"]["is_nr"].value_or(true);
  conf.prach.hs_flag = toml["prach"]["hs_flag"].value_or(false);
  conf.prach.root_seq_idx =
      toml["prach"]["root_sequence_index"].value_or(PRACH_ROOT_SEQ_IDX_DEFUALT);
  conf.prach.zero_corr_zone = toml["prach"]["zero_correlation_zone"].value_or(
      PRACH_ZERO_CORR_ZONE_DEFUALT);
  conf.prach.num_ra_preambles = toml["prach"]["num_ra_preambles"].value_or(
      PRACH_NUM_RA_PREAMBLES_DEFAULT);
  conf.prach.freq_offset = toml["prach"]["freq_offset"].value_or(
      PRACH_FREQ_OFFSET_DEFAULT);
  conf.prach.time_delay = toml["prach"]["time_delay"].value_or(1);

  // RAR Configuration
  if (toml["rar"]["ra_rnti_list"].is_array()) {
    auto rnti_array = toml["rar"]["ra_rnti_list"].as_array();
    for (const auto& elem : *rnti_array) {
      if (elem.is_integer()) {
        int64_t val = elem.as_integer()->get();
        conf.rar.ra_rnti_list.push_back(static_cast<uint16_t>(val));
      }
    }
  }
  if (conf.rar.ra_rnti_list.empty()) {
    conf.rar.ra_rnti_list = {1, 2, 3, 4}; // Default
  }

  conf.rar.coreset0_idx = toml["rar"]["coreset0_idx"].value_or(0);
  conf.rar.ss0_idx = toml["rar"]["ss0_idx"].value_or(0);
  conf.rar.offset_to_carrier = toml["rar"]["offset_to_carrier"].value_or(0);
  conf.rar.ssb_offset = toml["rar"]["ssb_offset"].value_or(0);
  conf.rar.dmrs_typeA_pos = toml["rar"]["dmrs_typeA_pos"].value_or(2);
  conf.rar.nof_rx_antennas = toml["rar"]["nof_rx_antennas"].value_or(1);

  // Logging Configuration
  std::string log_level_str = toml["log"]["level"].value_or("info");

  if (log_level_str == "error")
    log_level = ERROR;
  else if (log_level_str == "info")
    log_level = INFO;
  else if (log_level_str == "warning")
    log_level = WARNING;
  else if (log_level_str == "debug")
    log_level = DEBUG;

  return conf;
}
