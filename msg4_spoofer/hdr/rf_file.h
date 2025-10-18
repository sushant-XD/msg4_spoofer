#pragma once

#include "rf_base.h"
#include <cstdio>
#include <string>

class RFFile : public RFBase {
public:
  RFFile(const std::string &rx_file, const std::string &tx_file);
  ~RFFile();

  bool receive(cf_t *buffer, uint32_t nsamples) override;
  bool transmit(const cf_t *buffer, uint32_t nsamples,
                bool start_of_burst = false,
                bool end_of_burst = false) override;

private:
  FILE *rx_file_handle;
  FILE *tx_file_handle;
  std::string rx_filename;
  std::string tx_filename;
};
