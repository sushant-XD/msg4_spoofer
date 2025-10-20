/**
 * SSB and SIB1 Extractor Header
 *
 * This class:
 * 1. Scans for SSB (Synchronization Signal Block)
 * 2. Decodes MIB from the SSB
 * 3. Uses MIB information to decode SIB1
 * 4. Extracts critical system information
 */
#pragma once

#include "buffer_pool.h"
#include "msg2_decoder_standalone.h"
#include "rf_base.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/sync/ssb.h"
#include "srsran/srsran.h"
#include <atomic>
#include <complex>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

#define SF_DURATION 1e-3

struct samples_t {
  uint32_t slot_idx;
  srsran_timestamp_t ts;
  std::shared_ptr<std::vector<cf_t>> dl_buffer[SRSRAN_MAX_CHANNELS];
  std::shared_ptr<std::vector<cf_t>> ul_buffer[SRSRAN_MAX_CHANNELS];
};

struct SsbSearchResult {
  bool found;
  uint32_t pci;
  uint32_t ssb_idx;
  uint32_t t_offset;
  float snr_db;
  float rsrp_dbm;
  srsran_mib_nr_t mib;
};

class SSBDecoder {
public:
  SSBDecoder(std::unique_ptr<RFBase> &rf_dev);
  ~SSBDecoder();

  bool init(RARSearchConfig &config);

  bool run_cell_search();
  bool configure_ssb(RARSearchConfig &config);

  void set_mib_info(uint8_t coreset0_idx, uint8_t ss0_idx);

  const SsbSearchResult &get_ssb_result() const { return ssb_result_; }

  void print_mib(const srsran_mib_nr_t &mib);

  bool listen(std::shared_ptr<samples_t> &samples);

  void run_tti();

  bool handle_pbch(srsran_pbch_msg_nr_t &pbch_msg_);
  void handle_measurements(srsran_csi_trs_measurements_t &feedback);

  void get_tti(uint32_t *idx, srsran_timestamp_t *ts);

private:
  // SSB-related members
  srsran_ssb_t ssb_;
  bool ssb_initialized_;
  double srate_hz_;
  double center_freq_hz_;
  uint32_t nof_prb_;
  uint32_t pci_;
  uint32_t sf_len_;

  uint32_t slot_per_sf;
  uint32_t num_channels = 1;

  float cfo_hz = 0;
  // used for sync
  int32_t samples_delayed = 0;

  std::atomic<uint32_t> tti{0};
  srsran_timestamp_t timestamp_new{};
  srsran_timestamp_t timestamp_prev{};
  srsran_ssb_t ssb = {};
  srsran_mib_nr_t mib = {};
  srsran_pbch_msg_nr_t pbch_msg = {};
  srsran_csi_trs_measurements_t measurements = {};

  std::queue<std::shared_ptr<samples_t>> history_samples_queue;
  bool sib1_decoder_initialized_;

  // Stored results
  SsbSearchResult ssb_result_;

  std::unique_ptr<SharedBufferPool> buffer_pool = nullptr;

  std::mutex time_mtx;
  std::atomic<bool> running{false};
  std::atomic<bool> cell_found{false};

  std::unique_ptr<RFBase> rf_dev;
  // Helper functions
  srsran_ssb_pattern_t pattern_from_string(const std::string &pattern);
  srsran_subcarrier_spacing_t scs_from_khz(uint32_t scs_khz);
  bool decode_mib(const srsran_pbch_msg_nr_t &pbch_msg, srsran_mib_nr_t &mib);
};
