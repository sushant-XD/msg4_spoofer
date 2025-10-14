// 5G NR RAR decoder class

#ifndef RAR_DECODER_H
#define RAR_DECODER_H

#include "msg2_decoder_standalone.h"
#include "config.h"
#include "rf_base.h"
#include "srsran/common/buffer_pool.h"
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/mac/mac_rar_pdu_nr.h"
#include "srsran/phy/phch/pbch_msg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "srsran/srslog/srslog.h"
#include <array>
#include <complex>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// Decoded RAR grant information
struct rar_grant_t {
  uint32_t slot_number;      // Slot where RAR was received
  uint16_t ra_rnti;          // RA-RNTI used
  uint8_t rapid;             // Random Access Preamble ID
  uint16_t tc_rnti;          // Temporary C-RNTI assigned
  uint32_t ta;               // Timing Advance command
  double ta_time_us;         // TA in microseconds
  std::array<uint8_t, 27> ul_grant;  // UL grant (27 bytes)
  uint64_t timestamp_ms;     // System timestamp
};

// PHY state management for UL/DL grants
class phy_state_nr {
private:
  struct pending_ul_grant_t {
    bool enable = false;
    uint32_t pid = 0;
    srsran_sch_cfg_nr_t sch_cfg = {};
  };

  struct pending_dl_grant_t {
    bool enable = false;
    uint32_t pid = 0;
    srsran_sch_cfg_nr_t sch_cfg = {};
    srsran_harq_ack_resource_t ack_resource = {};
  };

  std::map<uint32_t, pending_ul_grant_t> pending_ul_grants;
  std::map<uint32_t, pending_dl_grant_t> pending_dl_grants;
  std::mutex ul_mutex;
  std::mutex dl_mutex;

public:
  void set_ul_pending_grant(const srsran::phy_cfg_nr_t &cfg,
                            const srsran_slot_cfg_t &slot_rx,
                            const srsran_dci_ul_nr_t &dci_ul);

  bool get_ul_pending_grant(uint32_t tti_tx, srsran_sch_cfg_nr_t &pusch_cfg,
                            uint32_t &pid);

  void set_dl_pending_grant(const srsran::phy_cfg_nr_t &cfg,
                            const srsran_slot_cfg_t &slot,
                            const srsran_dci_dl_nr_t &dci_dl);

  bool get_dl_pending_grant(uint32_t tti_rx, srsran_sch_cfg_nr_t &pdsch_cfg,
                            srsran_harq_ack_resource_t &ack_resource,
                            uint32_t &pid);

  void clear_pending_grants();
};

// RAR decoder (processes IQ samples from main)
class RARDecoder {
public:
  RARDecoder(const RARSearchConfig& config);
  ~RARDecoder();

  bool init();
  bool process_slot(cf_t *data_buffer, uint32_t slot_number);

  uint32_t get_rar_count() const { return rar_count_; }
  uint32_t get_slot_count() const { return slot_number_; }
  uint32_t get_slot_len() const { return slot_len_; }
  uint32_t get_slots_per_subframe() const { return slot_per_subframe_; }
  
  // Get decoded RAR grants
  const std::vector<rar_grant_t>& get_rar_grants() const { return rar_grants_; }
  void clear_rar_grants() { rar_grants_.clear(); }

private:
  RARSearchConfig config_;
  srsran::phy_cfg_nr_t phy_cfg_;
  phy_state_nr phy_state_;
  srslog::basic_logger& logger_;
  
  uint32_t rar_count_;
  uint32_t slot_number_;
  uint32_t slot_len_;
  uint32_t slot_per_subframe_;
  
  // Storage for decoded RAR grants
  std::vector<rar_grant_t> rar_grants_;

  void init_phy_cfg();
  bool configure_phy_cfg_basic();
  bool search_rar_in_slot(cf_t *data_buffer, uint32_t slot_number);
  bool init_ue_dl(srsran_ue_dl_nr_t &ue_dl, cf_t *buffer);
  bool update_ue_dl(srsran_ue_dl_nr_t &ue_dl);
  void ue_dl_dci_search(srsran_ue_dl_nr_t &ue_dl, srsran_slot_cfg_t &slot_cfg,
                        uint16_t rnti, srsran_rnti_type_t rnti_type);
  bool ue_dl_pdsch_decode(srsran_ue_dl_nr_t &ue_dl,
                          srsran_sch_cfg_nr_t &pdsch_cfg,
                          srsran_slot_cfg_t &slot_cfg,
                          srsran_pdsch_res_nr_t &pdsch_res,
                          srsran_softbuffer_rx_t &softbuffer_rx);
  bool process_rar_pdu(uint8_t *data, uint32_t len, uint16_t ra_rnti, uint32_t slot_number);
  void write_record_to_file(cf_t *buffer, uint32_t length, const char *name);
  std::string buffer_to_hex_string(const uint8_t *buffer, uint32_t len);
};

#endif // RAR_DECODER_H
