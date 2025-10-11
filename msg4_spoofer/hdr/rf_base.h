#pragma once

#include "config.h"
#include <memory>
#include <vector>
#include <complex>

class RFBase {
public:
  virtual ~RFBase() = default;
  
  // Simple receive/transmit methods - return true on success, false on failure
  virtual bool receive(std::complex<float>* buffer, uint32_t nsamples) = 0;
  virtual bool transmit(const std::complex<float>* buffer, uint32_t nsamples,
                       bool start_of_burst = false, bool end_of_burst = false) = 0;
};

// Factory function declaration
std::unique_ptr<RFBase> create_rf_instance(const spoofer_config_t& config);
