#pragma once

#include "config.h"
#include "rf_base.h"
#include <memory>
#include <srsran/phy/rf/rf.h>
#include <string>
#include <mutex>
#include <srsran/phy/common/timestamp.h>

class RF : public RFBase {
public:
  RF(const spoofer_config_t &config);
  ~RF();

  bool is_sdr() const override { return true; }
  int send(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t& ts, uint32_t slot = 0) override;
  int recv(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t* ts) override;
  void close() override;
  void set_tx_gain(double gain) override;
  void set_rx_gain(double gain) override;
  void set_tx_srate(double sample_rate) override;
  void set_rx_srate(double sample_rate) override;
  void set_tx_freq(double freq) override;
  void set_rx_freq(double freq) override;

private:
  srsran_rf_t rf_device;
  std::mutex mutex;
  void configure_device(const spoofer_config_t &config);
  std::string device_args;
};
