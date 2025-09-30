#ifndef CONFIG_H
#define CONFIG_H

#define FREQ_DEFAULT 627750000
#define SRATE_DEFAULT 23040000
#define GAIN_DEFAULT 50.0
#define PRB_DEFAULT 106

#include "logging.h"
#include "toml.hpp"

typedef struct pdcch_config_s {
  bool interleaved;
  uint64_t rnti;
} pdcch_config_t;

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

typedef struct agent_config_s {
  influxdb_config_t influx;
  rf_config_t rf;
  ssb_config_t ssb;
  pdcch_config_t pdcch;
} agent_config_t;

static agent_config_t load(std::string config_path) {
  printf("Loading config from path: %s\n", config_path.c_str());
  toml::table toml = toml::parse_file(config_path);
  agent_config_t conf;
  conf.rf.file_path = toml["rf"]["file_path"].value_or("");
  conf.rf.sample_rate = toml["rf"]["sample_rate"].value_or(SRATE_DEFAULT);
  conf.rf.frequency = toml["rf"]["frequency"].value_or(FREQ_DEFAULT);
  conf.rf.gain = toml["rf"]["gain"].value_or(GAIN_DEFAULT);
  conf.rf.nof_prb = toml["rf"]["nof_prb"].value_or(PRB_DEFAULT);
  conf.rf.N_id = toml["rf"]["N_id"].value_or(0);
  conf.rf.rf_args = toml["rf"]["rf_args"].value_or("");

  conf.pdcch.interleaved = toml["pdcch"]["interleaved"].value_or(false);
  conf.pdcch.rnti = toml["pdcch"]["rnti"].value_or(0);

  conf.ssb.ssb_pattern = SRSRAN_SSB_PATTERN_A;
  conf.ssb.ssb_scs = srsran_subcarrier_spacing_15kHz;
  conf.ssb.duplex_mode = SRSRAN_DUPLEX_MODE_FDD;

  std::string log_level_str = toml["log"]["level"].value_or("error");

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
