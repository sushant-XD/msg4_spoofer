#ifndef CONFIG_H
#define CONFIG_H

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

typedef enum spoofer_error_t {
  SUCCESS = 0,
  FILE_ERROR,
  UHD_ERROR,
  ZMQ_ERROR,
  SAMPLE_ERROR,
  INVALID_RF_TYPE
} spoofer_error_e;

typedef struct rf_config_s {
  std::string file_path;
  uint64_t sample_rate;
  double frequency;
  const char *rf_args;
  double gain;
  uint64_t nof_prb;
  uint32_t N_id;
} rf_config_t;

typedef struct ssb_config_s {
  srsran_ssb_pattern_t ssb_pattern = SRSRAN_SSB_PATTERN_A;
  srsran_subcarrier_spacing_t ssb_scs = srsran_subcarrier_spacing_15kHz;
  srsran_duplex_mode_t duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
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
  // srsran_tdd_config_t tdd_config; // leave these to default
  // bool enable_successive_cancellation;
  // bool enable_freq_domain_offset_calc;
} prach_config_t;

typedef struct spoofer_config_s {
  rf_config_t rf;
  ssb_config_t ssb;
  prach_config_t prach;
} spoofer_config_t;

static spoofer_config_t load(std::string config_path) {
  printf("Loading config from path: %s\n", config_path.c_str());
  toml::table toml = toml::parse_file(config_path);
  spoofer_config_t conf;
  conf.rf.file_path = toml["rf"]["file_path"].value_or("");
  conf.rf.sample_rate = toml["rf"]["sample_rate"].value_or(SRATE_DEFAULT);
  conf.rf.frequency = toml["rf"]["frequency"].value_or(FREQ_DEFAULT);
  conf.rf.gain = toml["rf"]["gain"].value_or(GAIN_DEFAULT);
  conf.rf.nof_prb = toml["rf"]["nof_prb"].value_or(PRB_DEFAULT);
  conf.rf.N_id = toml["rf"]["N_id"].value_or(0);
  conf.rf.rf_args = toml["rf"]["rf_args"].value_or(
      ""); // rf args, default to type: uhd and device: b210 if not available

  conf.ssb.ssb_pattern = SRSRAN_SSB_PATTERN_A;
  conf.ssb.ssb_scs = srsran_subcarrier_spacing_15kHz;
  conf.ssb.duplex_mode = SRSRAN_DUPLEX_MODE_FDD;

  conf.prach.config_idx =
      toml["prach"]["config_idx"].value_or(PRACH_CONFIG_IDX_DEFAULT);
  conf.prach.is_nr = toml["prach"]["is_nr"].value_or(true);
  conf.prach.hs_flag = toml["prach"]["hs_flag"].value_or(false);
  conf.prach.freq_offset =
      toml["prach"]["freq_offset"].value_or(PRACH_FREQ_OFFSET_DEFAULT);
  conf.prach.root_seq_idx =
      toml["prach"]["root_sequence_index"].value_or(PRACH_ROOT_SEQ_IDX_DEFUALT);
  conf.prach.zero_corr_zone = toml["prach"]["zero_correlation_zone"].value_or(
      PRACH_ZERO_CORR_ZONE_DEFUALT);
  conf.prach.num_ra_preambles = toml["prach"]["num_ra_preambles"].value_or(
      PRACH_NUM_RA_PREAMBLES_DEFAULT);

  std::string log_level_str = toml["log"]["level"].value_or("debug");

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

#endif // CONFIG_H
