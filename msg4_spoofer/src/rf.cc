#include "rf.h"
#include <iostream>
#include <stdexcept>
#include <cstring>

// Single RF class that handles both UHD and ZMQ
RF::RF(const spoofer_config_t& config) {
    // Initialize the srsRAN RF device
    memset(&rf_device, 0, sizeof(srsran_rf_t));
    
    configure_device(config);
    
    // Initialize the RF device
    std::string device_name = config.rf.device_name;
    // Make a non-const copy for the API
    std::string device_args_copy = device_args;
    if (srsran_rf_open_devname(&rf_device, device_name.c_str(), const_cast<char*>(device_args_copy.c_str()), 1) != SRSRAN_SUCCESS) {
        throw std::runtime_error("Failed to open RF device: " + device_name);
    }
    
    // Set sample rate
    srsran_rf_set_rx_srate(&rf_device, config.rf.srate);
    srsran_rf_set_tx_srate(&rf_device, config.rf.srate);
    
    // Set center frequency  
    srsran_rf_set_rx_freq(&rf_device, 0, config.rf.frequency);
    srsran_rf_set_tx_freq(&rf_device, 0, config.rf.frequency);
    
    // Set gains
    srsran_rf_set_rx_gain(&rf_device, config.rf.rx_gain);
    srsran_rf_set_tx_gain(&rf_device, config.rf.tx_gain);
    
    // Start RX and TX streams
    srsran_rf_start_rx_stream(&rf_device, false);
    
    std::cout << "RF device (" << device_name << ") initialized successfully" << std::endl;
}

RF::~RF() {
    srsran_rf_close(&rf_device);
}

void RF::configure_device(const spoofer_config_t& config) {
    std::string device_name = config.rf.device_name;
    
    if (device_name == "uhd") {
        // UHD specific arguments
        device_args = config.rf.device_args;
        if (device_args.empty()) {
            device_args = "type=b200";  // Default UHD args
        }
    } else if (device_name == "zmq") {
        // ZMQ specific arguments
        device_args = config.rf.device_args;
        if (device_args.empty()) {
            device_args = "tx_port=tcp://*:2000,rx_port=tcp://localhost:2001,id=enb";
        }
    }
}

bool RF::receive(std::complex<float>* buffer, uint32_t nsamples) {
    int samples_received = srsran_rf_recv(&rf_device, buffer, nsamples, true);
    return samples_received > 0;
}

bool RF::transmit(const std::complex<float>* buffer, uint32_t nsamples, 
                 bool start_of_burst, bool end_of_burst) {
    int samples_sent = srsran_rf_send(&rf_device, const_cast<std::complex<float>*>(buffer), nsamples, true);
    return samples_sent > 0;
}
