#pragma once

#include "logging.h"
#include "srsran/phy/common/phy_common_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "toml.h"
#include <cstring>

constexpr static long int FREQ_DEFAULT = 627750000;
constexpr static long int SRATE_DEFAULT = 23040000;
constexpr static long int GAIN_DEFAULT = 50.0;
constexpr static long int PRB_DEFAULT = 106;

constexpr static int PRACH_CONFIG_IDX_DEFAULT = 1;
constexpr static int PRACH_ROOT_SEQ_IDX_DEFUALT = 1;
constexpr static int PRACH_ZERO_CORR_ZONE_DEFUALT = 0;
constexpr static int PRACH_NUM_RA_PREAMBLES_DEFAULT = 64;
constexpr static int PRACH_FREQ_OFFSET_DEFAULT = 0;

typedef enum spoofer_error_t {
  SUCCESS = 0,
  INIT_ERROR,
  CONFIG_ERROR,
  FILE_ERROR,
  UHD_ERROR,
  ZMQ_ERROR,
  SAMPLE_ERROR
} spoofer_error_e;

typedef struct influxdb_config_s {
  bool enabled;
  std::string host;
  int port;
  std::string org;
  std::string bucket;
  std::string token;
} influxdb_config_t;

typedef struct mib_data_s {
  long long sfn;               // System Frame Number
  long long ssb_idx;           // SSB index
  bool hrf;                    // Half Radio Frame
  int scs_common;              // Subcarrier Spacing Common
  long long ssb_offset;        // SSB offset
  int dmrs_typeA_pos;          // DMRS Type A position
  long long coreset0_idx;      // CORESET0 index
  long long ss0_idx;           // SS0 index
  bool cell_barred;            // Cell barred indicator
  bool intra_freq_reselection; // Intra-frequency reselection
  long long spare;             // Spare bits
  std::string timestamp;       // Timestamp from InfluxDB
  bool valid;                  // Data validity flag
} mib_data_t;

// typedef struct prach_config_s {
//   int config_idx;        // PRACH configuration index
//   int root_seq_idx;      // Root sequence index
//   int zero_corr_zone;    // Zero correlation zone
//   int freq_offset;       // Frequency offset (msg1_freq_start)
//   int num_ra_preambles;  // Number of RA preambles
//   std::string timestamp; // Timestamp from InfluxDB
//   bool valid;            // Data validity flag
// } prach_config_t;

/* struct to hold prach configuration
 * configured by data from influxdb if available, otherwise populated with
 * default values or data provided in the config
 * */
typedef struct prach_config_s {
  bool is_nr = true;         // Set to true if NR
  uint32_t config_idx;       // PRACH configuration index
  uint32_t root_seq_idx;     // Root sequence index
  uint32_t zero_corr_zone;   // Zero correlation zone
  uint32_t freq_offset;      // Frequency offset (msg1_freq_start)
  uint32_t num_ra_preambles; // Numbeer of RA preambles
  std::string timestamp; // timestamp (from influxdb, 0 otherwise if populating
                         // from the config)
  bool valid = true;     // from influxdb, true by default for config
  bool hs_flag;          // highspeed sampling flag
  uint64_t time_delay;   // TODO:
} prach_config_t;

typedef struct band_report_s {
  int band;                // Band number
  int nof_prb;             // Number of PRBs
  int offset_to_carrier;   // Offset to carrier
  std::string scs_common;  // Subcarrier spacing common (string)
  std::string scs_ssb;     // Subcarrier spacing SSB (string)
  long long dl_arfcn;      // Downlink ARFCN
  long long ul_arfcn;      // Uplink ARFCN
  long long ssb_arfcn;     // SSB ARFCN
  double ul_freq;          // Uplink frequency
  double dl_freq;          // Downlink frequency
  double ssb_freq;         // SSB frequency
  std::string ssb_pattern; // SSB pattern (string)
  double sample_rate;      // Sample rate
  double uplink_cfo;       // Uplink CFO
  double downlink_cfo;     // Downlink CFO
  std::string timestamp;   // Timestamp from InfluxDB
  bool valid;              // Data validity flag
} band_report_t;

// thisinfo is populated with influxdb data if available
typedef struct cell_info_s {
  mib_data_t mib;       // MIB data
  prach_config_t prach; // PRACH configuration
  band_report_t band;   // Band information
} cell_info_t;

// rf hardware configuration
typedef struct rf_config_s {
  uint32_t freq_offset;
  float rx_gain;
  float tx_gain;
  double srate;

  double frequency;
  uint32_t nof_prb;
  uint32_t N_id;
  uint32_t ssb_numerology;

  std::string device_name;
  const char *device_args;

  std::string file_path;
} rf_config_t;

// ssb configuration
typedef struct ssb_config_s {
  srsran_ssb_pattern_t ssb_pattern = SRSRAN_SSB_PATTERN_A;
  srsran_subcarrier_spacing_t ssb_scs = srsran_subcarrier_spacing_15kHz;
  srsran_duplex_mode_t duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
} ssb_config_t;

typedef struct spoofer_config_s {
  rf_config_t rf;
  ssb_config_t ssb;
  prach_config_t prach;
  influxdb_config_t influxdb;
} spoofer_config_t;

static spoofer_config_t load(std::string config_path) {
  printf("Loading config from path: %s\n", config_path.c_str());
  toml::table toml = toml::parse_file(config_path);
  spoofer_config_t conf;

  conf.rf.freq_offset = toml["rf"]["freq_offset"].value_or(0);
  conf.rf.rx_gain = toml["rf"]["rx_gain"].value_or(0.0);
  conf.rf.tx_gain = toml["rf"]["tx_gain"].value_or(0.0);
  conf.rf.srate = toml["rf"]["srate"].value_or(23.04e6);

  conf.rf.frequency = toml["rf"]["frequency"].value_or(1842.5e6);
  conf.rf.nof_prb = toml["rf"]["nof_prb"].value_or(106);
  conf.rf.N_id = toml["rf"]["N_id"].value_or(1);
  conf.rf.ssb_numerology = toml["rf"]["ssb_numerology"].value_or(0);

  conf.rf.device_name = toml["rf"]["device_name"].value_or("uhd");
  conf.rf.device_args = toml["rf"]["device_args"].value_or("type=b200");
  conf.rf.file_path = toml["rf"]["file_path"].value_or("");

  // Preconfigured if influxdb is not setup
  conf.ssb.ssb_pattern = SRSRAN_SSB_PATTERN_A;
  conf.ssb.ssb_scs = srsran_subcarrier_spacing_15kHz;
  conf.ssb.duplex_mode = SRSRAN_DUPLEX_MODE_FDD;

  conf.prach.time_delay = toml["prach"]["time_delay"].value_or(1);
  std::string log_level_str = toml["log"]["level"].value_or("debug");

  if (log_level_str == "error")
    log_level = ERROR;
  else if (log_level_str == "info")
    log_level = INFO;
  else if (log_level_str == "warning")
    log_level = WARNING;
  else if (log_level_str == "debug")
    log_level = DEBUG;

  // InfluxDB configuration
  // If influxdb is enabled, then we get the ssb information, prach config, etc.
  // from influxdb
  conf.influxdb.enabled = toml["influxdb"]["enabled"].value_or(false);

  if (conf.influxdb.enabled) {
    conf.influxdb.host = toml["influxdb"]["host"].value_or("localhost");
    conf.influxdb.port = toml["influxdb"]["port"].value_or(8086);
    conf.influxdb.org = toml["influxdb"]["org"].value_or("myorg");
    conf.influxdb.bucket = toml["influxdb"]["bucket"].value_or("mybucket");
    conf.influxdb.token = toml["influxdb"]["token"].value_or("");
    
    // Set default PRACH values (will be overridden by InfluxDB data)
    conf.prach.config_idx = PRACH_CONFIG_IDX_DEFAULT;
    conf.prach.is_nr = true;
    conf.prach.hs_flag = false;
    conf.prach.root_seq_idx = PRACH_ROOT_SEQ_IDX_DEFUALT;
    conf.prach.zero_corr_zone = PRACH_ZERO_CORR_ZONE_DEFUALT;
    conf.prach.num_ra_preambles = PRACH_NUM_RA_PREAMBLES_DEFAULT;
    conf.prach.freq_offset = PRACH_FREQ_OFFSET_DEFAULT;
    conf.prach.time_delay = toml["prach"]["time_delay"].value_or(1);
    
    return conf;
  }

  // if influxdb is not setup correctly, use the default config or default
  // values
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

  return conf;
}
