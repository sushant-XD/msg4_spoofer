#include "rf_base.h"
#include "rf.h"
#include "rf_file.h"
#include "logging.h"

std::unique_ptr<RFBase> create_rf_instance(const spoofer_config_t& config) {
  try {
    std::string device_type = config.rf.device_name;
    
    if (device_type == "uhd" || device_type == "zmq") {
      LOG_INFO("Creating RF instance for device type: %s", device_type.c_str());
      return std::make_unique<RF>(config);
    } else if (device_type == "file") {
      LOG_INFO("Creating file-based RF instance");
      // For file-based, we expect file path in device_args or file_path
      std::string rx_file = config.rf.file_path;
      if (rx_file.empty() && !config.rf.device_args.empty()) {
        rx_file = config.rf.device_args;
      }
      std::string tx_file = rx_file + ".tx"; // Add .tx extension for TX file
      return std::make_unique<RFFile>(rx_file, tx_file);
    } else {
      LOG_ERROR("Unknown/Unsupported RF device type: %s", device_type.c_str());
      return nullptr;
    }
  } catch (const std::exception &e) {
    LOG_ERROR("Failed to create RF object: %s", e.what());
    return nullptr;
  }
}
