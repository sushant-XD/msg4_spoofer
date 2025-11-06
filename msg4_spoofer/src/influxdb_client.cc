#include "influxdb_client.h"
#include "logging.h"
#include <iostream>

InfluxDBClient::InfluxDBClient(const influxdb_config_t &config)
    : bucket_(config.bucket), org_(config.org), connected_(false) {

  server_info_ = std::make_unique<influxdb_cpp::server_info>(
      config.host, config.port, config.org, config.token, config.bucket);

  // Check if connection was established successfully
  if (server_info_->resp_ == 0) {
    connected_ = true;
    LOG_INFO("InfluxDB client connected to %s:%d", config.host.c_str(),
             config.port);
    LOG_INFO("Organization: %s, Bucket: %s", config.org.c_str(),
             config.bucket.c_str());
  } else {
    connected_ = false;
    LOG_ERROR("Failed to connect to InfluxDB at %s:%d", config.host.c_str(),
              config.port);
  }
}

InfluxDBClient::~InfluxDBClient() { LOG_INFO("InfluxDB client disconnected"); }

bool InfluxDBClient::is_connected() const { return connected_; }

bool InfluxDBClient::execute_query(const std::string &query,
                                   std::string &response) {
  if (!connected_) {
    LOG_ERROR("Cannot execute query: not connected to InfluxDB");
    return false;
  }

  int ret = influxdb_cpp::query(response, query, *server_info_);

  if (ret == 0) {
    return true;
  } else {
    LOG_ERROR("Query failed with error code: %d", ret);
    if (ret == -1) {
      LOG_ERROR("Connection failed. Check if InfluxDB is running");
    }
    return false;
  }
}

bool InfluxDBClient::parse_mib_data(const std::string &response,
                                    mib_data_t &mib) {
  try {
    JSON::Value root;
    size_t parsed = root.read(response);

    if (parsed != response.length()) {
      LOG_ERROR("Failed to parse complete JSON response");
      return false;
    }

    JSON::Array &results = root["results"].a();
    if (results.size() == 0)
      return false;

    JSON::Object &result = const_cast<JSON::Value &>(results[0]).o();
    if (result.find("series") == result.end())
      return false;

    JSON::Array &series = result["series"].a();
    if (series.size() == 0)
      return false;

    JSON::Object &serie = const_cast<JSON::Value &>(series[0]).o();
    JSON::Array &columns = serie["columns"].a();
    JSON::Array &values = serie["values"].a();

    if (values.size() == 0)
      return false;

    // Get the last (most recent) value
    JSON::Array &row = const_cast<JSON::Value &>(values[values.size() - 1]).a();

    // Map column names to values
    for (size_t i = 0; i < columns.size(); i++) {
      std::string col_name = columns[i].s();
      if (col_name == "time")
        mib.timestamp = row[i].s();
      else if (col_name == "sfn")
        mib.sfn = row[i].i();
      else if (col_name == "ssb_idx")
        mib.ssb_idx = row[i].i();
      else if (col_name == "hrf")
        mib.hrf = row[i].b();
      else if (col_name == "scs_common")
        mib.scs_common = row[i].i();
      else if (col_name == "ssb_offset")
        mib.ssb_offset = row[i].i();
      else if (col_name == "dmrs_typeA_pos")
        mib.dmrs_typeA_pos = row[i].i();
      else if (col_name == "coreset0_idx")
        mib.coreset0_idx = row[i].i();
      else if (col_name == "ss0_idx")
        mib.ss0_idx = row[i].i();
      else if (col_name == "cell_barred")
        mib.cell_barred = row[i].b();
      else if (col_name == "intra_freq_reselection")
        mib.intra_freq_reselection = row[i].b();
      else if (col_name == "spare")
        mib.spare = row[i].i();
    }

    mib.valid = true;
    return true;
  } catch (std::exception &e) {
    LOG_ERROR("Error parsing MIB data: %s", e.what());
    return false;
  }
}

bool InfluxDBClient::parse_prach_config(const std::string &response,
                                        prach_config_t &prach) {
  try {
    JSON::Value root;
    size_t parsed = root.read(response);

    if (parsed != response.length()) {
      LOG_ERROR("Failed to parse complete JSON response");
      return false;
    }

    JSON::Array &results = root["results"].a();
    if (results.size() == 0)
      return false;

    JSON::Object &result = const_cast<JSON::Value &>(results[0]).o();
    if (result.find("series") == result.end())
      return false;

    JSON::Array &series = result["series"].a();
    if (series.size() == 0)
      return false;

    JSON::Object &serie = const_cast<JSON::Value &>(series[0]).o();
    JSON::Array &columns = serie["columns"].a();
    JSON::Array &values = serie["values"].a();

    if (values.size() == 0)
      return false;

    // Get the last (most recent) value
    JSON::Array &row = const_cast<JSON::Value &>(values[values.size() - 1]).a();

    // Map column names to values
    for (size_t i = 0; i < columns.size(); i++) {
      std::string col_name = columns[i].s();
      if (col_name == "time")
        prach.timestamp = row[i].s();
      else if (col_name == "config_idx")
        prach.config_idx = row[i].i();
      else if (col_name == "root_seq_idx")
        prach.root_seq_idx = row[i].i();
      else if (col_name == "zero_corr_zone")
        prach.zero_corr_zone = row[i].i();
      else if (col_name == "freq_offset")
        prach.freq_offset = row[i].i();
      else if (col_name == "num_ra_preambles")
        prach.num_ra_preambles = row[i].i();
    }

    prach.valid = true;
    return true;
  } catch (std::exception &e) {
    LOG_ERROR("Error parsing PRACH config: %s", e.what());
    return false;
  }
}

bool InfluxDBClient::parse_band_report(const std::string &response,
                                       band_report_t &band) {
  try {
    JSON::Value root;
    size_t parsed = root.read(response);

    if (parsed != response.length()) {
      LOG_ERROR("Failed to parse complete JSON response");
      return false;
    }

    JSON::Array &results = root["results"].a();
    if (results.size() == 0)
      return false;

    JSON::Object &result = const_cast<JSON::Value &>(results[0]).o();
    if (result.find("series") == result.end())
      return false;

    JSON::Array &series = result["series"].a();
    if (series.size() == 0)
      return false;

    JSON::Object &serie = const_cast<JSON::Value &>(series[0]).o();
    JSON::Array &columns = serie["columns"].a();
    JSON::Array &values = serie["values"].a();

    if (values.size() == 0)
      return false;

    // Get the last (most recent) value
    JSON::Array &row = const_cast<JSON::Value &>(values[values.size() - 1]).a();

    // Map column names to values
    for (size_t i = 0; i < columns.size(); i++) {
      std::string col_name = columns[i].s();
      if (col_name == "time")
        band.timestamp = row[i].s();
      else if (col_name == "band")
        band.band = row[i].i();
      else if (col_name == "nof_prb")
        band.nof_prb = row[i].i();
      else if (col_name == "offset_to_carrier")
        band.offset_to_carrier = row[i].i();
      else if (col_name == "scs_common")
        band.scs_common = row[i].s();
      else if (col_name == "scs_ssb")
        band.scs_ssb = row[i].s();
      else if (col_name == "dl_arfcn")
        band.dl_arfcn = row[i].i();
      else if (col_name == "ul_arfcn")
        band.ul_arfcn = row[i].i();
      else if (col_name == "ssb_arfcn")
        band.ssb_arfcn = row[i].i();
      else if (col_name == "ul_freq")
        band.ul_freq = row[i].f();
      else if (col_name == "dl_freq")
        band.dl_freq = row[i].f();
      else if (col_name == "ssb_freq")
        band.ssb_freq = row[i].f();
      else if (col_name == "ssb_pattern")
        band.ssb_pattern = row[i].s();
      else if (col_name == "sample_rate")
        band.sample_rate = row[i].f();
      else if (col_name == "uplink_cfo")
        band.uplink_cfo = row[i].f();
      else if (col_name == "downlink_cfo")
        band.downlink_cfo = row[i].f();
    }

    band.valid = true;
    return true;
  } catch (std::exception &e) {
    LOG_ERROR("Error parsing band report: %s", e.what());
    return false;
  }
}

bool InfluxDBClient::query_mib(mib_data_t &mib) {
  mib.valid = false;

  LOG_INFO("Querying MIB data from InfluxDB...");
  std::string query = "select * from mib order by time desc limit 1";
  std::string response;

  if (!execute_query(query, response)) {
    return false;
  }

  if (parse_mib_data(response, mib)) {
    LOG_INFO(" MIB data retrieved successfully");
    LOG_INFO("  SFN: %lld, SSB Index: %lld, HRF: %d", mib.sfn, mib.ssb_idx,
             mib.hrf);
    return true;
  } else {
    LOG_WARN("No MIB data available");
    return false;
  }
}

bool InfluxDBClient::query_prach_config(prach_config_t &prach) {
  prach.valid = false;

  LOG_INFO("Querying PRACH configuration from InfluxDB...");
  std::string query = "select * from prach_cfg order by time desc limit 1";
  std::string response;

  if (!execute_query(query, response)) {
    return false;
  }

  if (parse_prach_config(response, prach)) {
    LOG_INFO("✓ PRACH config retrieved successfully");
    LOG_INFO("  Config Index: %d, Root Seq: %d, Zero Corr Zone: %d",
             prach.config_idx, prach.root_seq_idx, prach.zero_corr_zone);
    return true;
  } else {
    LOG_WARN("No PRACH config available");
    return false;
  }
}

bool InfluxDBClient::query_band_report(band_report_t &band) {
  band.valid = false;

  LOG_INFO("Querying band information from InfluxDB...");
  std::string query = "select * from band_report order by time desc limit 1";
  std::string response;

  if (!execute_query(query, response)) {
    return false;
  }

  if (parse_band_report(response, band)) {
    LOG_INFO(" Band info retrieved successfully");
    LOG_INFO("  Band: %d, PRBs: %d, DL Freq: %.2f MHz, UL Freq: %.2f MHz",
             band.band, band.nof_prb, band.dl_freq / 1e6, band.ul_freq / 1e6);
    return true;
  } else {
    LOG_WARN("No band information available");
    return false;
  }
}

cell_info_t InfluxDBClient::query_cell_info() {
  cell_info_t cell_info = {};

  // Query all three measurements
  query_mib(cell_info.mib);
  query_prach_config(cell_info.prach);
  query_band_report(cell_info.band);

  return cell_info;
}

void InfluxDBClient::display_cell_info(const cell_info_t &cell_info) {
  LOG_INFO("========================================");
  LOG_INFO("       COMPLETE CELL INFORMATION       ");
  LOG_INFO("========================================");

  if (cell_info.mib.valid) {
    LOG_INFO("\n MIB Data:");
    LOG_INFO("  Time: %s", cell_info.mib.timestamp.c_str());
    LOG_INFO("  SFN: %lld", cell_info.mib.sfn);
    LOG_INFO("  SSB Index: %lld", cell_info.mib.ssb_idx);
    LOG_INFO("  Half Radio Frame: %s", cell_info.mib.hrf ? "true" : "false");
    LOG_INFO("  SCS Common: %d", cell_info.mib.scs_common);
    LOG_INFO("  SSB Offset: %lld", cell_info.mib.ssb_offset);
    LOG_INFO("  DMRS Type A Pos: %d", cell_info.mib.dmrs_typeA_pos);
    LOG_INFO("  CORESET0 Index: %lld", cell_info.mib.coreset0_idx);
    LOG_INFO("  SS0 Index: %lld", cell_info.mib.ss0_idx);
    LOG_INFO("  Cell Barred: %s", cell_info.mib.cell_barred ? "true" : "false");
    LOG_INFO("  Intra-freq Reselection: %s",
             cell_info.mib.intra_freq_reselection ? "allowed" : "not allowed");
    LOG_INFO("  Spare: %lld", cell_info.mib.spare);
  } else {
    LOG_WARN("\nMIB Data: Not available");
  }

  if (cell_info.prach.valid) {
    LOG_INFO("\nPRACH Configuration:");
    LOG_INFO("  Time: %s", cell_info.prach.timestamp.c_str());
    LOG_INFO("  Config Index: %d", cell_info.prach.config_idx);
    LOG_INFO("  Root Sequence Index: %d", cell_info.prach.root_seq_idx);
    LOG_INFO("  Zero Correlation Zone: %d", cell_info.prach.zero_corr_zone);
    LOG_INFO("  Frequency Offset: %d", cell_info.prach.freq_offset);
    LOG_INFO("  Number of RA Preambles: %d", cell_info.prach.num_ra_preambles);
  } else {
    LOG_WARN("\n PRACH Configuration: Not available");
  }

  if (cell_info.band.valid) {
    LOG_INFO("\nBand Information:");
    LOG_INFO("  Time: %s", cell_info.band.timestamp.c_str());
    LOG_INFO("  Band: %d", cell_info.band.band);
    LOG_INFO("  Number of PRBs: %d", cell_info.band.nof_prb);
    LOG_INFO("  Offset to Carrier: %d", cell_info.band.offset_to_carrier);
    LOG_INFO("  SCS Common: %s", cell_info.band.scs_common.c_str());
    LOG_INFO("  SCS SSB: %s", cell_info.band.scs_ssb.c_str());
    LOG_INFO("  DL ARFCN: %lld", cell_info.band.dl_arfcn);
    LOG_INFO("  UL ARFCN: %lld", cell_info.band.ul_arfcn);
    LOG_INFO("  SSB ARFCN: %lld", cell_info.band.ssb_arfcn);
    LOG_INFO("  DL Frequency: %.3f MHz", cell_info.band.dl_freq / 1e6);
    LOG_INFO("  UL Frequency: %.3f MHz", cell_info.band.ul_freq / 1e6);
    LOG_INFO("  SSB Frequency: %.3f MHz", cell_info.band.ssb_freq / 1e6);
    LOG_INFO("  SSB Pattern: %s", cell_info.band.ssb_pattern.c_str());
    LOG_INFO("  Sample Rate: %.2f MHz", cell_info.band.sample_rate / 1e6);
    LOG_INFO("  Uplink CFO: %.2f Hz", cell_info.band.uplink_cfo);
    LOG_INFO("  Downlink CFO: %.2f Hz", cell_info.band.downlink_cfo);
  } else {
    LOG_WARN("\n Band Information: Not available");
  }

  LOG_INFO("========================================\n");
}
