#include "rf.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

// Single RF class that handles both UHD and ZMQ
RF::RF(const spoofer_config_t &config) {
  // Initialize the srsRAN RF device
  memset(&rf_device, 0, sizeof(srsran_rf_t));

  configure_device(config);

  // Log the configuration being used
  std::cout << "\n====== RF Configuration ======" << std::endl;
  std::cout << "Device:      " << config.rf.device_name << std::endl;
  std::cout << "Args:        " << device_args << std::endl;
  std::cout << "Sample Rate: " << config.rf.srate / 1e6 << " MHz" << std::endl;
  std::cout << "RX Freq:     " << config.rf.dl_frequency / 1e6 << " MHz (DL)"
            << std::endl;
  std::cout << "TX Freq:     " << config.rf.ul_frequency / 1e6 << " MHz (UL)"
            << std::endl;
  std::cout << "RX Gain:     " << config.rf.rx_gain << " dB" << std::endl;
  std::cout << "TX Gain:     " << config.rf.tx_gain << " dB" << std::endl;
  std::cout << "=======================" << std::endl;

  // Initialize the RF device
  std::string device_name = config.rf.device_name;
  // Make a non-const copy for the API
  std::string device_args_copy = device_args;
  if (srsran_rf_open_devname(&rf_device, device_name.c_str(),
                             const_cast<char *>(device_args_copy.c_str()),
                             1) != SRSRAN_SUCCESS) {
    throw std::runtime_error("Failed to open RF device: " + device_name);
  }

  // Set sample rate
  srsran_rf_set_rx_srate(&rf_device, config.rf.srate);
  srsran_rf_set_tx_srate(&rf_device, config.rf.srate);

  // Set center frequency (RX = DL from base station, TX = UL to base station)
  srsran_rf_set_rx_freq(&rf_device, 0, config.rf.dl_frequency);
  srsran_rf_set_tx_freq(&rf_device, 0, config.rf.ul_frequency);

  // Set gains
  srsran_rf_set_rx_gain(&rf_device, config.rf.rx_gain);
  srsran_rf_set_tx_gain(&rf_device, config.rf.tx_gain);

  // Start RX and TX streams
  srsran_rf_start_rx_stream(&rf_device, false);

  std::cout << "RF device (" << device_name << ") initialized successfully"
            << std::endl;
}

RF::~RF() { srsran_rf_close(&rf_device); }

void RF::configure_device(const spoofer_config_t &config) {
  std::string device_name = config.rf.device_name;

  if (device_name == "uhd") {
    // UHD specific arguments
    device_args = config.rf.device_args;
    if (device_args.empty()) {
      device_args = "type=b200"; // Default UHD args
    }
  } else if (device_name == "zmq") {
    // ZMQ specific arguments
    device_args = config.rf.device_args;
    if (device_args.empty()) {
      device_args = "tx_port=tcp://*:2000,rx_port=tcp://localhost:2001,id=enb";
    }
  }
}

int RF::send(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t& ts, uint32_t slot) {
  std::lock_guard<std::mutex> lock(mutex);
  try {
    int samples_sent = srsran_rf_send_timed_multi(
        &rf_device, (void**)buffer, nof_samples, ts.full_secs, ts.frac_secs, true, true, true);
    return samples_sent;
  } catch (const std::exception& e) {
    return -1;
  }
}

int RF::recv(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t* ts) {
  try {
    int samples_recv =
        srsran_rf_recv_with_time_multi(&rf_device, (void**)buffer, nof_samples, false, &ts->full_secs, &ts->frac_secs);
    if (samples_recv == SRSRAN_ERROR) {
      return -1;
    }
    return samples_recv;
  } catch (const std::exception& e) {
    return -1;
  }
}

void RF::close() {
  if (rf_device.dev) {
    srsran_rf_close(&rf_device);
  }
}

void RF::set_tx_gain(double gain) {
  srsran_rf_set_tx_gain(&rf_device, gain);
}

void RF::set_rx_gain(double gain) {
  srsran_rf_set_rx_gain(&rf_device, gain);
}

void RF::set_tx_srate(double sample_rate) {
  srsran_rf_set_tx_srate(&rf_device, sample_rate);
}

void RF::set_rx_srate(double sample_rate) {
  srsran_rf_set_rx_srate(&rf_device, sample_rate);
}

void RF::set_tx_freq(double freq) {
  srsran_rf_set_tx_freq(&rf_device, 0, freq);
}

void RF::set_rx_freq(double freq) {
  srsran_rf_set_rx_freq(&rf_device, 0, freq);
}
