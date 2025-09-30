#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include <srsran/phy/io/filesource.h>
#include <srsran/phy/io/format.h>
#include <srsran/phy/rf/rf.h>

class data_source {
public:
  data_source(char *file_path, srsran_datatype_t datatype);
  data_source(char *rf_args, double rf_gain, double rf_freq, double srate);
  ~data_source();

  bool read(cf_t *output, int nof_samples);

private:
  bool file_init;
  bool radio_init;
  srsran_filesource_t file_src;
  srsran_rf_t radio_src;
};

#endif // DATA_SOURCE_H
