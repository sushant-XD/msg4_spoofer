#include "data_source.h"
#include <srsran/phy/io/filesource.h>
#include <srsran/phy/io/format.h>

data_source::data_source(char *file_path, srsran_datatype_t datatype)
    : radio_init(false), file_init(true) {
  file_src = {};
  srsran_filesource_init(&file_src, file_path, datatype);
}

data_source::data_source(char *rf_args, double rf_gain, double rf_freq,
                         double srate)
    : radio_init(true), file_init(false) {
  if (srsran_rf_open(&radio_src, rf_args)) {
    fprintf(stderr, "Error opening rf\n");
    return;
  }

  srsran_rf_set_rx_gain(&radio_src, rf_gain);
  printf("Set RX rate: %.2f MHz\n",
         srsran_rf_set_rx_srate(&radio_src, srate) / 1000000);
  printf("Set RX gain: %.1f dB\n", srsran_rf_get_rx_gain(&radio_src));
  printf("Set RX freq: %.2f MHz\n",
         srsran_rf_set_rx_freq(&radio_src, 0, rf_freq) / 1000000);

  srsran_rf_start_rx_stream(&radio_src, false);
  radio_init = true;
}

data_source::~data_source() {
  if (file_init) {
    srsran_filesource_free(&file_src);
  }

  if (radio_init) {
    srsran_rf_close(&radio_src);
  }
}

bool data_source::read(cf_t *output, int nof_samples) {
  if (!file_init && !radio_init)
    return false;
  if (file_init) {
    if (srsran_filesource_read(&file_src, output, nof_samples) <
        SRSRAN_SUCCESS) {
      printf("Error reading from file\n");
      return false;
    }
    return true;
  }

  printf(" ----  Receive %d samples  ---- ", nof_samples);
  srsran_rf_recv(&radio_src, output, nof_samples, 0);
  return 0;
}
