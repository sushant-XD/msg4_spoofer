#include "rf_base.h"
#include "rf_uhd.h"
#include <iostream>

std::unique_ptr<RFBase> create_rf_instance(const spoofer_config_t &config) {
  if (config.rf.device_name == "uhd") {
    try {
      return std::make_unique<RF_UHD>(config);
    } catch (const std::exception &e) {
      LOG_ERROR("Failed to create RF_UHD object: %s", e.what());
      return nullptr;
    }
  }
  // else if (rf_type == "zmq") {
  // return std::make_unique<RF_ZMQ>();
  // }
  else {
    std::cerr << "Unknown/Unsupported RF type: " << config.rf.device_name
              << std::endl;
    return nullptr;
  }
}
