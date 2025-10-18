#pragma once

#include "config.h"
#include <complex>
#include <memory>
#include <vector>

class RFBase {
public:
  virtual ~RFBase() = default;

  // Simple receive/transmit methods - return true on success, false on failure
  virtual bool receive(cf_t *buffer, uint32_t nsamples) = 0;
  virtual bool transmit(const cf_t *buffer, uint32_t nsamples,
                        bool start_of_burst = false,
                        bool end_of_burst = false) = 0;
};

// Factory function declaration
std::unique_ptr<RFBase> create_rf_instance(const spoofer_config_t &config);
