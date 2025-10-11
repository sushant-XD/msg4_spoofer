/**
 * MSG4 Spoofer ZMQ RF Handler using srsRAN RF API
 */

#include "rf_zmq.h"
#include "logging.h"
#include <iostream>
#include <cstring>

RF_ZMQ::RF_ZMQ(const spoofer_config_t& config) 
    : initialized_(false), rx_stream_started_(false), config_(config) {
  // Properly zero-initialize the RF device struct
  std::memset(&rf_device_, 0, sizeof(srsran_rf_t));
  
  // Initialize the device in constructor
  if (!init_device()) {
    throw std::runtime_error("Failed to initialize ZMQ RF device");
  }
}

RF_ZMQ::~RF_ZMQ() {
  if (rx_stream_started_) {
    stop_rx();
  }
  if (initialized_) {
    srsran_rf_close(&rf_device_);
  }
}

bool RF_ZMQ::init_device() {
  if (initialized_) {
    LOG_ERROR("ZMQ RF Handler already initialized");
    return false;
  }
  
  // Build ZMQ device args string 
  std::string args;
  
  // Use device_args if provided, otherwise build default config
  if (strlen(config_.rf.device_args) > 0) {
    args = config_.rf.device_args;
  } else if (!config_.rf.file_path.empty()) {
    // File-based ZMQ (reading from IQ file)
    args = "file,filename=" + config_.rf.file_path + ",sample_format=fc32";
  } else {
    // Network-based ZMQ (default configuration)
    args = "tx_port=tcp://*:2000,rx_port=tcp://localhost:2001,id=enb,base_srate=" + 
           std::to_string((uint32_t)config_.rf.srate);
  }
  
  char args_cstr[512];
  std::snprintf(args_cstr, sizeof(args_cstr), "%s", args.c_str());
  
  // Load RF plugins
  srsran_rf_load_plugins();
  
  // Open ZMQ RF device using srsran_rf_open
  LOG_INFO("Opening ZMQ RF device with args: %s", args_cstr);
  
  if (srsran_rf_open(&rf_device_, args_cstr) != SRSRAN_SUCCESS) {
    LOG_ERROR("Error opening ZMQ RF device");
    return false;
  }
  
  // Set RX gain 
  LOG_INFO("Setting ZMQ RX gain: %.1f dB", config_.rf.rx_gain);
  if (srsran_rf_set_rx_gain(&rf_device_, config_.rf.rx_gain) != SRSRAN_SUCCESS) {
    LOG_ERROR("Error setting ZMQ RX gain");
    srsran_rf_close(&rf_device_);
    return false;
  }
  
  // Set sample rates
  LOG_INFO("Setting ZMQ sample rate: %.2f MHz", config_.rf.srate / 1e6);
  double actual_srate_rx = srsran_rf_set_rx_srate(&rf_device_, config_.rf.srate);
  LOG_INFO("Actual ZMQ RX sample rate: %.2f MHz", actual_srate_rx / 1e6);
  
  double actual_srate_tx = srsran_rf_set_tx_srate(&rf_device_, config_.rf.srate);
  LOG_INFO("Actual ZMQ TX sample rate: %.2f MHz", actual_srate_tx / 1e6);
  
  // Set frequencies
  LOG_INFO("Setting ZMQ RX frequency: %.2f MHz", config_.rf.frequency / 1e6);
  double actual_rx_freq = srsran_rf_set_rx_freq(&rf_device_, 0, config_.rf.frequency);
  LOG_INFO("Actual ZMQ RX frequency: %.2f MHz", actual_rx_freq / 1e6);
  
  LOG_INFO("Setting ZMQ TX frequency: %.2f MHz", config_.rf.frequency / 1e6);
  double actual_tx_freq = srsran_rf_set_tx_freq(&rf_device_, 0, config_.rf.frequency);
  LOG_INFO("Actual ZMQ TX frequency: %.2f MHz", actual_tx_freq / 1e6);
  
  // Set TX gain
  LOG_INFO("Setting ZMQ TX gain: %.1f dB", config_.rf.tx_gain);
  if (srsran_rf_set_tx_gain(&rf_device_, config_.rf.tx_gain) != SRSRAN_SUCCESS) {
    LOG_ERROR("Error setting ZMQ TX gain");
    srsran_rf_close(&rf_device_);
    return false;
  }
  
  initialized_ = true;
  LOG_INFO("ZMQ RF device initialized successfully");
  
  return true;
}

int RF_ZMQ::receive(std::complex<float>* buffer, uint32_t nsamples) {
  if (!initialized_) {
    LOG_ERROR("ZMQ RF Handler not initialized");
    return -1;
  }
  
  // Auto-start RX stream on first receive call
  if (!rx_stream_started_) {
    if (!start_rx()) {
      LOG_ERROR("Failed to start ZMQ RX stream");
      return -1;
    }
  }
  
  // Use blocking mode (1) for reliable sample reception
  int nrecv = srsran_rf_recv(&rf_device_, buffer, nsamples, 1);
  
  if (nrecv < 0) {
    LOG_ERROR("ZMQ receive failed with error code: %d", nrecv);
    return nrecv;
  }
  
  return nrecv;
}

int RF_ZMQ::transmit(const std::complex<float>* buffer, uint32_t nsamples,
                    bool start_of_burst, bool end_of_burst) {
  if (!initialized_) {
    LOG_ERROR("ZMQ RF Handler not initialized");
    return -1;
  }
  
  // For non-const buffer
  void* buffers[1] = {const_cast<std::complex<float>*>(buffer)};
  
  // Using srsran_rf_send_multi for non-timed transmission
  int nsent = srsran_rf_send_multi(&rf_device_, buffers, nsamples, 
                                   true, start_of_burst, end_of_burst);
  
  if (nsent < 0) {
    LOG_ERROR("ZMQ srsran_rf_send_multi failed with error code: %d", nsent);
    LOG_ERROR("Parameters: nsamples=%u, start_of_burst=%d, end_of_burst=%d", 
              nsamples, start_of_burst, end_of_burst);
    return nsent;
  }
  
  // Warn if we didn't transmit all samples
  if (nsent != (int)nsamples) {
    LOG_WARN("ZMQ transmitted %d samples, expected %u", nsent, nsamples);
  }
  
  return nsent;
}

// Private methods for internal use only

bool RF_ZMQ::start_rx() {
  if (!initialized_) {
    LOG_ERROR("ZMQ RF Handler not initialized");
    return false;
  }
  
  if (srsran_rf_start_rx_stream(&rf_device_, false) != SRSRAN_SUCCESS) {
    LOG_ERROR("Error starting ZMQ RX stream");
    return false;
  }
  
  rx_stream_started_ = true;
  LOG_INFO("ZMQ RX stream started");
  return true;
}

bool RF_ZMQ::stop_rx() {
  if (!initialized_) {
    LOG_ERROR("ZMQ RF Handler not initialized");
    return false;
  }
  
  if (srsran_rf_stop_rx_stream(&rf_device_) != SRSRAN_SUCCESS) {
    LOG_ERROR("Error stopping ZMQ RX stream");
    return false;
  }
  
  rx_stream_started_ = false;
  LOG_INFO("ZMQ RX stream stopped");
  return true;
}

bool RF_ZMQ::start_tx() {
  if (!initialized_) {
    LOG_ERROR("ZMQ RF Handler not initialized");
    return false;
  }
  
  LOG_INFO("ZMQ TX stream ready");
  return true;
}

bool RF_ZMQ::stop_tx() {
  if (!initialized_) {
    LOG_ERROR("ZMQ RF Handler not initialized");
    return false;
  }
  
  LOG_INFO("ZMQ TX stream stopped");
  return true;
}
