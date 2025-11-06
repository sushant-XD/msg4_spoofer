#pragma once

#include "config.h"
#include "influxdb.hpp"
#include "xpjson.hpp"
#include <memory>
#include <string>

/**
 * @class InfluxDBClient
 * @brief Client for querying cell information from InfluxDB
 *
 * This class provides a clean interface for querying MIB, PRACH configuration,
 * and band information from InfluxDB. It handles connection management,
 * query execution, and data parsing.
 */
class InfluxDBClient {
public:
  /**
   * @brief Constructor
   * @param config InfluxDB configuration
   */
  explicit InfluxDBClient(const influxdb_config_t &config);

  /**
   * @brief Destructor
   */
  ~InfluxDBClient();

  /**
   * @brief Check if client is connected to InfluxDB
   * @return true if connected, false otherwise
   */
  bool is_connected() const;

  /**
   * @brief Query MIB data from InfluxDB
   * @param mib Output parameter to store MIB data
   * @return true if query successful, false otherwise
   */
  bool query_mib(mib_data_t &mib);

  /**
   * @brief Query PRACH configuration from InfluxDB
   * @param prach Output parameter to store PRACH config
   * @return true if query successful, false otherwise
   */
  bool query_prach_config(prach_config_t &prach);

  /**
   * @brief Query band report from InfluxDB
   * @param band Output parameter to store band information
   * @return true if query successful, false otherwise
   */
  bool query_band_report(band_report_t &band);

  /**
   * @brief Query complete cell information (MIB, PRACH, band)
   * @return Complete cell information structure
   */
  cell_info_t query_cell_info();

  /**
   * @brief Display complete cell information in formatted output
   * @param cell_info Cell information to display
   */
  static void display_cell_info(const cell_info_t &cell_info);

  /**
   * @brief Get the bucket name
   * @return Bucket name string
   */
  std::string get_bucket() const { return bucket_; }

  /**
   * @brief Get the organization name
   * @return Organization name string
   */
  std::string get_org() const { return org_; }

private:
  /**
   * @brief Execute a query and return response
   * @param query InfluxQL query string
   * @param response Output parameter for response
   * @return true if query successful, false otherwise
   */
  bool execute_query(const std::string &query, std::string &response);

  /**
   * @brief Parse MIB data from JSON response
   * @param response JSON response string
   * @param mib Output parameter to store parsed MIB data
   * @return true if parsing successful, false otherwise
   */
  bool parse_mib_data(const std::string &response, mib_data_t &mib);

  /**
   * @brief Parse PRACH config from JSON response
   * @param response JSON response string
   * @param prach Output parameter to store parsed PRACH config
   * @return true if parsing successful, false otherwise
   */
  bool parse_prach_config(const std::string &response, prach_config_t &prach);

  /**
   * @brief Parse band report from JSON response
   * @param response JSON response string
   * @param band Output parameter to store parsed band report
   * @return true if parsing successful, false otherwise
   */
  bool parse_band_report(const std::string &response, band_report_t &band);

  std::unique_ptr<influxdb_cpp::server_info> server_info_;
  std::string bucket_;
  std::string org_;
  bool connected_;
};
