#ifndef PRACH_FLOODER_H
#define PRACH_FLOODER_H

#include "shadower/source/source.h"
#include "shadower/utils/arg_parser.h"
#include "srsran/phy/common/phy_common_nr.h"
#include "srsran/phy/phch/prach.h"
#include "srsran/srsran.h"
#include "srsran/srslog/srslog.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class PrachFlooder
{
public:
  explicit PrachFlooder(srslog::basic_logger& logger_);
  ~PrachFlooder();

  bool configure(const srsran::phy_cfg_nr_t& phy_cfg, const ShadowerConfig& config);
  bool start(Source* source_);
  void stop();

  bool is_running() const { return running.load(); }

private:
  bool init_prach(const srsran::phy_cfg_nr_t& phy_cfg);
  bool generate_waveform(uint32_t preamble);
  void worker_loop();

  srslog::basic_logger& logger;

  std::mutex           mutex;
  srsran_prach_t       prach_ctx = {};
  srsran_prach_cfg_t   prach_cfg = {};
  bool                 prach_ready = false;
  bool                 configured  = false;

  uint32_t preamble_index = 0;
  double   sample_rate_hz = 0.0;
  double   tx_period_s    = 0.0;
  uint32_t nof_channels   = 0;

  std::vector<cf_t>    waveform;
  std::vector<cf_t>    null_waveform;
  std::vector<cf_t*>   channel_ptrs;
  Source*              source = nullptr;

  std::atomic<bool> running{false};
  std::atomic<bool> stop_requested{false};
  std::thread       worker_thread;
  srsran_timestamp_t next_timestamp = {};
};

#endif // PRACH_FLOODER_H

