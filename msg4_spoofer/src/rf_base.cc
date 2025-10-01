#include "rf_base.h"
#include "rf_uhd.h"
#include "rf_zmq.h"
#include <iostream>

std::unique_ptr<RFBase> create_rf_instance(const std::string &rf_type) {
  if (rf_type == "uhd") {
    return std::make_unique<RF_UHD>();
  } else if (rf_type == "zmq") {
    return std::make_unique<RF_ZMQ>();
  } else {
    std::cerr << "Unknown/Unsupported RF type: " << rf_type << std::endl;
    return nullptr;
  }
}
