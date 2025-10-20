#include "rf_file.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <sstream>

RFFile::RFFile(std::vector<std::string> filenames, uint32_t num_channels, double sample_rate) 
    : srate(sample_rate), nof_channels(num_channels) {
  set_num_channels(num_channels);
  for (uint32_t i = 0; i < num_channels; i++) {
    std::string filename = filenames[i];
    if (filename.empty()) {
      throw std::runtime_error("Error opening file, filename is empty");
    }
    std::ifstream ifile(filename, std::ios::binary);
    if (!ifile.is_open()) {
      throw std::runtime_error("Error opening file");
    }
    ifiles.push_back(std::move(ifile));
    printf("[INFO] Using source file: %s\n", filename.c_str());
  }
  timestamp_prev = {0, 0};
}

RFFile::~RFFile() {
  close();
}

int RFFile::send(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t& ts, uint32_t slot) {
  for (uint32_t i = 0; i < nof_channels; i++) {
    char filename[256];
    sprintf(filename, "tx_slot_ch_%u_%u", i, slot);
    // Note: write_record_to_file function would need to be implemented
    // For now, just return the number of samples
  }
  return nof_samples;
}

int RFFile::recv(cf_t** buffer, uint32_t nof_samples, srsran_timestamp_t* ts) {
  for (uint32_t i = 0; i < nof_channels; i++) {
    if (ifiles[i].eof()) {
      return -1;
    }
    ifiles[i].read(reinterpret_cast<char*>(buffer[i]), nof_samples * sizeof(cf_t));
    if (ifiles[i].eof()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      return -1;
    }
  }
  srsran_timestamp_add(&timestamp_prev, 0, nof_samples / srate);
  srsran_timestamp_copy(ts, &timestamp_prev);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return nof_samples;
}

void RFFile::close() {
  for (uint32_t i = 0; i < nof_channels; i++) {
    std::ifstream& ifile = ifiles[i];
    if (ifile.is_open()) {
      ifile.close();
    }
  }
}
