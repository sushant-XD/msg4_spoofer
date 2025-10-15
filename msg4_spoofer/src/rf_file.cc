#include "rf_file.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>

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

bool RFFile::receive(std::complex<float>* buffer, uint32_t nsamples) {
    if (!rx_file_handle) {
        // If no file, generate zeros using proper initialization
        std::fill(buffer, buffer + nsamples, std::complex<float>(0.0f, 0.0f));
        return true;
    }
    
    size_t samples_read = fread(buffer, sizeof(std::complex<float>), nsamples, rx_file_handle);
    
    // If reached end of file, stop processing (don't loop)
    if (samples_read < nsamples) {
        // Fill remaining buffer with zeros
        std::fill(buffer + samples_read, buffer + nsamples, std::complex<float>(0.0f, 0.0f));
        // Return false to signal end of file
        return false;
    }
    
    return samples_read > 0;
}

bool RFFile::transmit(const std::complex<float>* buffer, uint32_t nsamples,
                     bool start_of_burst, bool end_of_burst) {
    if (!tx_file_handle) {
        return true; // Pretend we transmitted successfully
    }
    
    size_t samples_written = fwrite(buffer, sizeof(std::complex<float>), nsamples, tx_file_handle);
    fflush(tx_file_handle);
    return samples_written == nsamples;
}