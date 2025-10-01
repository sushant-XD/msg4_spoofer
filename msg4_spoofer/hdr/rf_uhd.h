#ifndef RF_UHD_H
#define RF_UHD_H

#include "rf_base.h"
#include <uhd/stream.hpp>
#include <uhd/usrp/multi_usrp.hpp>

class RF_UHD : public RFBase {
public:
  uuagent_error_e transmit(const spoofer_config_t &args) override;
};

#endif // !RF_UHD_H
