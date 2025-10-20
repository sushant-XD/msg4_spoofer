#pragma once

#include "rf_base.h"
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <chrono>
#include <srsran/phy/common/timestamp.h>

class RFFile : public RFBase {
public:
  RFFile(std::vector<std::string> filenames, uint32_t num_channels, double sample_rate);
  ~RFFile();

  bool is_sdr() const override { return false; }

  int send(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t& ts, uint32_t slot = 0) override;
  int recv(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t* ts) override;
  void close() override;
  void set_tx_gain(double gain) override {};
  void set_rx_gain(double gain) override {};
  void set_tx_srate(double sample_rate) override {};
  void set_rx_srate(double sample_rate) override {};
  void set_tx_freq(double freq) override {};
  void set_rx_freq(double freq) override {};

private:
  std::vector<std::ifstream> ifiles;
  double srate;
  srsran_timestamp_t timestamp_prev{};
  uint32_t nof_channels;
};
