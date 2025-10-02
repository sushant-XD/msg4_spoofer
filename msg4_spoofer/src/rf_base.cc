#include "rf_base.h"
#include "rf_uhd.h"
#include <iostream>

std::unique_ptr<RFBase> create_rf_instance(const spoofer_config_t &config) {
  if (config.rf.device_name == "uhd") {
    return std::make_unique<RF_UHD>(config);
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
