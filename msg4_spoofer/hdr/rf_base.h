#pragma once

#include "config.h"
#include <complex>
#include <memory>
#include <vector>
#include <srsran/phy/common/timestamp.h>

class RFBase {
public:
  virtual ~RFBase() = default;

  virtual bool is_sdr() const = 0;
  virtual int send(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t& ts, uint32_t slot = 0) = 0;
  virtual int recv(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t* ts) = 0;
  virtual void close() = 0;
  virtual void set_tx_gain(double gain) = 0;
  virtual void set_rx_gain(double gain) = 0;
  virtual void set_tx_srate(double sample_rate) = 0;
  virtual void set_rx_srate(double sample_rate) = 0;
  virtual void set_tx_freq(double freq) = 0;
  virtual void set_rx_freq(double freq) = 0;

protected:
  uint32_t nof_channels = 1;
  void set_num_channels(uint32_t num_channels) { nof_channels = num_channels; }
};

// Factory function declaration
std::unique_ptr<RFBase> create_rf_instance(const spoofer_config_t &config);
