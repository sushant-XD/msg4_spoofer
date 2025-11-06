#include "config.h"
#include "influxdb.hpp"
#include "logging.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// Function to read MIB data from TOML config
std::vector<mib_data_t> load_mib_test_data(const std::string &config_path) {
  std::vector<mib_data_t> mib_list;

  printf("Loading MIB test data from: %s\n", config_path.c_str());
  toml::table toml = toml::parse_file(config_path);

  // Get array of MIB data entries
  if (auto mib_array = toml["mib_data"].as_array()) {
    for (auto &elem : *mib_array) {
      if (auto mib_table = elem.as_table()) {
        mib_data_t mib;

        mib.sfn = mib_table->get("sfn")->value_or(0);
        mib.ssb_idx = mib_table->get("ssb_idx")->value_or(0);
        mib.hrf = mib_table->get("hrf")->value_or(false);
        mib.scs_common = mib_table->get("scs_common")->value_or(1);
        mib.ssb_offset = mib_table->get("ssb_offset")->value_or(0);
        mib.dmrs_typeA_pos = mib_table->get("dmrs_typeA_pos")->value_or(2);
        mib.coreset0_idx = mib_table->get("coreset0_idx")->value_or(0);
        mib.ss0_idx = mib_table->get("ss0_idx")->value_or(0);
        mib.cell_barred = mib_table->get("cell_barred")->value_or(false);
        mib.intra_freq_reselection =
            mib_table->get("intra_freq_reselection")->value_or(true);
        mib.spare = mib_table->get("spare")->value_or(0);

        mib_list.push_back(mib);

        printf("  Loaded MIB entry: SFN=%lld, SSB_idx=%lld, HRF=%d\n", mib.sfn,
               mib.ssb_idx, mib.hrf);
      }
    }
  }

  return mib_list;
}

// Function to load InfluxDB config from TOML
influxdb_config_t load_influxdb_config(const std::string &config_path) {
  toml::table toml = toml::parse_file(config_path);

  influxdb_config_t conf;

  conf.enabled = toml["influxdb"]["enabled"].value_or(true);
  conf.host = toml["influxdb"]["host"].value_or("localhost");
  conf.port = toml["influxdb"]["port"].value_or(8086);
  conf.org = toml["influxdb"]["org"].value_or("myorg");
  conf.bucket = toml["influxdb"]["bucket"].value_or("mybucket");
  conf.token = toml["influxdb"]["token"].value_or("");

  return conf;
}

// Function to load test configuration
struct test_config_t {
  std::string data_id;
  int publish_interval_ms;
  bool continuous;
};

test_config_t load_test_config(const std::string &config_path) {
  toml::table toml = toml::parse_file(config_path);
  test_config_t conf;

  conf.data_id = toml["test"]["data_id"].value_or("test_device");
  conf.publish_interval_ms = toml["test"]["publish_interval_ms"].value_or(1000);
  conf.continuous = toml["test"]["continuous"].value_or(false);

  return conf;
}

// Function to publish MIB data to InfluxDB
bool publish_mib_to_influxdb(const influxdb_cpp::server_info &si,
                             const mib_data_t &mib,
                             const std::string &data_id) {
  std::string response_text;

  int ret =
      influxdb_cpp::builder()
          .meas("rtue_carrier_metric")
          .tag("sni5gect_data_id", data_id)
          .field("sfn", mib.sfn)
          .field("ssb_idx", mib.ssb_idx)
          .field("hrf", mib.hrf)
          .field("scs_common", mib.scs_common)
          .field("ssb_offset", mib.ssb_offset)
          .field("dmrs_typeA_pos", mib.dmrs_typeA_pos)
          .field("coreset0_idx", mib.coreset0_idx)
          .field("ss0_idx", mib.ss0_idx)
          .field("cell_barred", mib.cell_barred)
          .field("intra_freq_reselection", mib.intra_freq_reselection)
          .field("spare", mib.spare)
          .timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count())
          .post_http(si, &response_text);

  if (ret == 0) {
    printf(" Published MIB: SFN=%lld, SSB_idx=%lld, HRF=%d\n", mib.sfn,
           mib.ssb_idx, mib.hrf);
    return true;
  } else {
    printf(" Failed to publish MIB. Error code: %d\n", ret);
    if (!response_text.empty()) {
      printf("  Response: %s\n", response_text.c_str());
    }
    return false;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <config_file.toml>\n", argv[0]);
    printf("\nExample:\n");
    printf("  %s test/mib_test_data.toml\n", argv[0]);
    return EXIT_FAILURE;
  }

  std::string config_path(argv[1]);

  printf("===========================================\n");
  printf("  InfluxDB MIB Data Generator\n");
  printf("===========================================\n\n");

  // Load configuration
  influxdb_config_t influx_conf;
  test_config_t test_conf;
  std::vector<mib_data_t> mib_list;

  try {
    influx_conf = load_influxdb_config(config_path);
    test_conf = load_test_config(config_path);
    mib_list = load_mib_test_data(config_path);
  } catch (const std::exception &e) {
    printf("Error loading config: %s\n", e.what());
    return EXIT_FAILURE;
  }

  if (mib_list.empty()) {
    printf("Error: No MIB data entries found in config file!\n");
    return EXIT_FAILURE;
  }

  printf("Configuration loaded:\n");
  printf("  InfluxDB: %s:%d\n", influx_conf.host.c_str(), influx_conf.port);
  printf("  Organization: %s\n", influx_conf.org.c_str());
  printf("  Bucket: %s\n", influx_conf.bucket.c_str());
  printf("  Data ID: %s\n", test_conf.data_id.c_str());
  printf("  MIB entries: %zu\n", mib_list.size());
  printf("  Continuous mode: %s\n", test_conf.continuous ? "Yes" : "No");
  printf("  Publish interval: %d ms\n\n", test_conf.publish_interval_ms);

  // Initialize InfluxDB connection
  printf("Connecting to InfluxDB...\n");
  influxdb_cpp::server_info influx_server(influx_conf.host, influx_conf.port,
                                          influx_conf.org, influx_conf.token,
                                          influx_conf.bucket);

  // Check if connection initialized properly
  if (influx_server.resp_ != 0) {
    printf(
        "Failed to resolve InfluxDB host. Please check your configuration.\n");
    return EXIT_FAILURE;
  }

  printf("Connected!\n\n");

  // Publish data
  printf("Starting data publication...\n");
  printf("-------------------------------------------\n");

  int published_count = 0;
  int failed_count = 0;

  do {
    for (const auto &mib : mib_list) {
      if (publish_mib_to_influxdb(influx_server, mib, test_conf.data_id)) {
        published_count++;
      } else {
        failed_count++;
      }

      // Sleep between publishes
      if (test_conf.publish_interval_ms > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(test_conf.publish_interval_ms));
      }
    }

    if (test_conf.continuous) {
      printf("\n--- Completed one cycle. Starting again... ---\n\n");
    }

  } while (test_conf.continuous);

  printf("-------------------------------------------\n");
  printf("\nData publication complete!\n");
  printf("  Published: %d\n", published_count);
  printf("  Failed: %d\n", failed_count);
  printf("  Total: %d\n", published_count + failed_count);
  printf("\nYou can now run your main application to query this data.\n");

  return EXIT_SUCCESS;
}
