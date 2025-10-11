#pragma once

#include "rf_base.h"
#include <string>
#include <cstdio>

class RFFile : public RFBase {
public:
    RFFile(const std::string& rx_file, const std::string& tx_file);
    ~RFFile();

    bool receive(std::complex<float>* buffer, uint32_t nsamples) override;
    bool transmit(const std::complex<float>* buffer, uint32_t nsamples,
                 bool start_of_burst = false, bool end_of_burst = false) override;

private:
    FILE* rx_file_handle;
    FILE* tx_file_handle;
    std::string rx_filename;
    std::string tx_filename;
};