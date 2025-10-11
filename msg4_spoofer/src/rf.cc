#include "rf.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

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

int RF::receive(std::complex<float>* buffer, uint32_t nsamples) {
    void* buffers[1] = {buffer};
    int samples_received = srsran_rf_recv_with_time(&rf_device, buffers, nsamples, true, nullptr, nullptr);
    return samples_received;
}

int RF::transmit(const std::complex<float>* buffer, uint32_t nsamples, 
                bool start_of_burst, bool end_of_burst) {
    void* buffers[1] = {const_cast<std::complex<float>*>(buffer)};
    int samples_sent = srsran_rf_send_timed(&rf_device, buffers, nsamples, 0, 0.0);
    return samples_sent;
}

// File-based RF class for testing/simulation
RFFile::RFFile(const std::string& rx_file, const std::string& tx_file) 
    : rx_filename(rx_file), tx_filename(tx_file), rx_file_handle(nullptr), tx_file_handle(nullptr) {
    
    if (!rx_filename.empty()) {
        rx_file_handle = fopen(rx_filename.c_str(), "rb");
        if (!rx_file_handle) {
            throw std::runtime_error("Failed to open RX file: " + rx_filename);
        }
    }
    
    if (!tx_filename.empty()) {
        tx_file_handle = fopen(tx_filename.c_str(), "wb");
        if (!tx_file_handle) {
            throw std::runtime_error("Failed to open TX file: " + tx_filename);
        }
    }
    
    std::cout << "File-based RF initialized (RX: " << rx_filename 
              << ", TX: " << tx_filename << ")" << std::endl;
}

RFFile::~RFFile() {
    if (rx_file_handle) {
        fclose(rx_file_handle);
    }
    if (tx_file_handle) {
        fclose(tx_file_handle);
    }
}

int RFFile::receive(std::complex<float>* buffer, uint32_t nsamples) {
    if (!rx_file_handle) {
        // If no file, generate zeros using proper initialization
        std::fill(buffer, buffer + nsamples, std::complex<float>(0.0f, 0.0f));
        return nsamples;
    }
    
    size_t samples_read = fread(buffer, sizeof(std::complex<float>), nsamples, rx_file_handle);
    
    // If reached end of file, loop back to beginning
    if (samples_read < nsamples) {
        fseek(rx_file_handle, 0, SEEK_SET);
        size_t remaining = nsamples - samples_read;
        size_t additional = fread(buffer + samples_read, sizeof(std::complex<float>), 
                                remaining, rx_file_handle);
        samples_read += additional;
    }
    
    return samples_read;
}

int RFFile::transmit(const std::complex<float>* buffer, uint32_t nsamples,
                    bool start_of_burst, bool end_of_burst) {
    if (!tx_file_handle) {
        return nsamples; // Pretend we transmitted
    }
    
    size_t samples_written = fwrite(buffer, sizeof(std::complex<float>), nsamples, tx_file_handle);
    fflush(tx_file_handle);
    return samples_written;
}
