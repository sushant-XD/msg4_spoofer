#include "rf_uhd.h"
#include <uhd/usrp/multi_usrp.hpp>

void RF_UHD::handle_uhd_error(uhd_error err) {
  if (err != UHD_ERROR_NONE) {
    fprintf(stderr, "UHD ERROR: %s\n", strerror(err));
    exit(EXIT_FAILURE);
  }
}

RF_UHD::RF_UHD(const spoofer_config_t &config) {
  std::cout << "Initializing RF_UHD device..." << std::endl;

  uint32_t nof_channels = 1;
  const uhd::device_addr_t dev_addr(config.rf.device_args);

  // The core initialization call
  handle_uhd_error(rf_dev.usrp_make(dev_addr, nof_channels));

  // --- 2. Configuration (Gain, Rate, Frequency) ---
  size_t channel_no = 0;
  float actual_frequency = 0.0;

  handle_uhd_error(rf_dev.set_tx_gain(channel_no, config.rf.tx_gain));
  handle_uhd_error(rf_dev.set_tx_rate(config.rf.srate));
  handle_uhd_error(
      rf_dev.set_tx_freq(0, config.rf.frequency, actual_frequency));

  float actual_rx_frequency = 0.0;
  handle_uhd_error(rf_dev.set_rx_gain(channel_no, config.rf.rx_gain));
  handle_uhd_error(rf_dev.set_rx_rate(config.rf.srate));
  handle_uhd_error(
      rf_dev.set_rx_freq(channel_no, config.rf.frequency, actual_rx_frequency));

  // --- 4. Streamer Setup ---
  size_t max_tx_samps = 0;
  handle_uhd_error(rf_dev.get_tx_stream(max_tx_samps));
  size_t max_rx_samps = 0;
  handle_uhd_error(rf_dev.get_rx_stream(max_rx_samps));
  std::cout << "RF_UHD device initialized and configured." << std::endl;
}

spoofer_error_e
RF_UHD::transmit(const spoofer_config_t &args,
                 const std::vector<std::complex<float>> &tx_data_buffer) {
  uhd::tx_streamer::sptr tx_stream = rf_dev.tx_stream;

  if (!tx_stream) {
    std::cerr << "RF_UHD Error: Transmit streamer not initialized."
              << std::endl;
    return CONFIG_ERROR;
  }

  size_t samples_to_send = tx_data_buffer.size();

  if (samples_to_send == 0) {
    std::cerr << "RF_UHD Warning: Data buffer is empty, nothing to transmit."
              << std::endl;
    return SUCCESS;
  }

  uhd::tx_metadata_t metadata;
  metadata.start_of_burst = true;
  metadata.end_of_burst = true;
  metadata.has_time_spec = false;

  try {
    size_t num_tx_samps =
        tx_stream->send(tx_data_buffer.data(), samples_to_send, metadata);

    if (num_tx_samps != samples_to_send) {
      return CONFIG_ERROR;
    }

  } catch (const uhd::exception &e) {
    std::cerr << "UHD TX Exception: " << e.what() << std::endl;
    return CONFIG_ERROR;
  }

  std::cout << "RF_UHD: Transmission complete." << std::endl;
  return SUCCESS;
}
