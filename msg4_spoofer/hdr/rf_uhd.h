#include "rf.h"
#include "rf_base.h"
#include <uhd/stream.hpp>
#include <uhd/usrp/multi_usrp.hpp>

class RF_UHD : public RFBase {
public:
  RF_UHD(const spoofer_config_t &config);
  spoofer_error_e
  transmit(const spoofer_config_t &args,
           const std::vector<std::complex<float>> &tx_data) override;
  ~RF_UHD() override = default;

private:
  void handle_uhd_error(uhd_error err);
  rf_handler rf_dev;
};
