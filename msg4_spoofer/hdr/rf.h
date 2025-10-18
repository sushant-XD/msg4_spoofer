#pragma once

#include "config.h"
#include "rf_base.h"
#include <memory>
#include <srsran/phy/rf/rf.h>
#include <string>

class RF : public RFBase {
public:
  RF(const spoofer_config_t &config);
  ~RF();

  bool receive(cf_t *buffer, uint32_t nsamples) override;
  bool transmit(const cf_t *buffer, uint32_t nsamples,
                bool start_of_burst = false,
                bool end_of_burst = false) override;

private:
  srsran_rf_t rf_device;
  void configure_device(const spoofer_config_t &config);
  std::string device_args;
};
