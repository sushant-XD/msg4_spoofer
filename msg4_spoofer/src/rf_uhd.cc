
#include "rf_uhd.h"
#include <algorithm>
#include <complex>
#include <fstream>
#include <iostream>
#include <vector>

void transmission(uhd::usrp::multi_usrp::sptr usrp) {

  // Configure the USRP transmission stream
  uhd::stream_args_t stream_args("fc32",
                                 "sc16"); // Complex float to short conversion
  uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

  uhd::tx_metadata_t metadata;
  metadata.start_of_burst =
      true; // First packet should have start_of_burst = true
  metadata.end_of_burst = false;
  metadata.has_time_spec = false;

  while (true) {

    // Transmit samples
    tx_stream->send(samples.data(), samples.size(), metadata);
    std::cout << "Transmitting...." << std::endl;

    // After the first packet, set `start_of_burst = false`
    metadata.start_of_burst = false;
  }

  // We will never reach this point unless we manually break the loop
}
