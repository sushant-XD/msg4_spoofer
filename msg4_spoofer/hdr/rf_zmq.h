#pragma once

#include "rf_base.h"
#include "srsran/phy/rf/rf.h"
#include <complex>
#include <cstring>

class RF_ZMQ : public RFBase {
public:
  RF_ZMQ(const spoofer_config_t& config);
  ~RF_ZMQ() override;

  // Simple receive/transmit methods only
  int receive(std::complex<float>* buffer, uint32_t nsamples) override;
  int transmit(const std::complex<float>* buffer, uint32_t nsamples,
              bool start_of_burst = false, bool end_of_burst = false) override;

private:
  srsran_rf_t rf_device_;
  bool initialized_;
  bool rx_stream_started_;
  spoofer_config_t config_;

  // Private initialization and configuration methods
  bool init_device();
  bool start_rx();
  bool stop_rx();
  bool start_tx();
  bool stop_tx();

  // Disable copy
  RF_ZMQ(const RF_ZMQ&) = delete;
  RF_ZMQ& operator=(const RF_ZMQ&) = delete;
};
