#pragma once

#include "config.h"
#include <memory>

class RFBase {
public:
  virtual ~RFBase() = default;
  virtual spoofer_error_e
  transmit(const spoofer_config_t &args,
           const std::vector<std::complex<float>> &tx_data) = 0;
};

// Factory function declaration
std::unique_ptr<RFBase> create_rf_instance(const spoofer_config_t &config);
