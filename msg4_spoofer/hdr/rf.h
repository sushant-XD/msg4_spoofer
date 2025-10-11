#pragma once

#include "rf_base.h"
#include "config.h"
#include <srsran/phy/rf/rf.h>
#include <memory>
#include <string>

class RF : public RFBase {
public:
    RF(const spoofer_config_t& config);
    ~RF();

    int receive(std::complex<float>* buffer, uint32_t nsamples) override;
    int transmit(const std::complex<float>* buffer, uint32_t nsamples,
                bool start_of_burst = false, bool end_of_burst = false) override;

private:
    srsran_rf_t rf_device;
    void configure_device(const spoofer_config_t& config);
    std::string device_args;
};

// File-based RF class for testing/simulation
class RFFile : public RFBase {
public:
    RFFile(const std::string& rx_file, const std::string& tx_file);
    ~RFFile();

    int receive(std::complex<float>* buffer, uint32_t nsamples) override;
    int transmit(const std::complex<float>* buffer, uint32_t nsamples,
                bool start_of_burst = false, bool end_of_burst = false) override;

private:
    FILE* rx_file_handle;
    FILE* tx_file_handle;
    std::string rx_filename;
    std::string tx_filename;
};
