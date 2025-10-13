#pragma once
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/srsran.h"
// #include "srslog/srslog.h"
#include <cstdint>
#include <vector>

// Structure to hold MSG2 decode result
struct MSG2Result {
  bool valid = false;
  uint16_t tc_rnti = 0;
  uint16_t ra_rnti = 0;
  uint8_t rapid = 0;
  uint16_t timing_advance = 0;
  uint32_t ul_grant = 0;
  uint32_t slot_idx = 0;
  uint32_t sfn = 0;
  uint32_t slot_in_frame = 0;
};

// PRACH configuration structure
struct PrachConfig {
  uint32_t config_idx = 1;
  bool is_nr = true;
  bool hs_flag = false;
  uint32_t root_seq_idx = 1;
  uint32_t zero_corr_zone = 0;
  uint32_t num_preambles = 64;
};

class MSG2Decoder {
private:
  // PHY configuration
  srsran::phy_cfg_nr_t phy_cfg;
  srsran_ue_dl_nr_t ue_dl;
  srsran_softbuffer_rx_t softbuffer_rx;

  // RF parameters
  uint32_t sample_rate;
  uint32_t nof_prb;
  uint32_t pci;
  double carrier_freq;

  // Timing
  uint32_t slot_len;
  uint32_t sf_len;
  uint32_t slot_per_sf;
  uint32_t slot_per_frame;

  // Buffer for IQ samples
  cf_t *buffer;

  // List of RA-RNTIs to monitor
  std::vector<uint16_t> ra_rnti_list;

  // PRACH configuration
  PrachConfig prach_cfg;

  // Initialization tracking
  bool ue_dl_initialized;
  bool softbuffer_initialized;
  bool owns_ue_dl;  // Whether this instance owns the UE DL (should free it)

public:
  MSG2Decoder(uint32_t sample_rate_, uint32_t nof_prb_, uint32_t pci_,
              double carrier_freq_);

  // Constructor that uses an external UE DL instance
  MSG2Decoder(uint32_t sample_rate_, uint32_t nof_prb_, uint32_t pci_,
              double carrier_freq_, srsran_ue_dl_nr_t* external_ue_dl);

  ~MSG2Decoder();

  // Initialize the decoder
  bool init();

  // Set PRACH configuration
  void set_prach_config(const PrachConfig &cfg);

  // Process one slot worth of IQ samples (monitors all RA-RNTIs)
  std::vector<MSG2Result> process_slot(cf_t *iq_samples, uint32_t slot_idx);

  // Process slot with specific RA-RNTI if you know which PRACH slot was used
  std::vector<MSG2Result>
  process_slot_with_prach_slot(cf_t *iq_samples, uint32_t slot_idx,
                               uint32_t prach_slot_in_frame);

  // Get the list of monitored RA-RNTIs
  const std::vector<uint16_t> &get_ra_rnti_list() const { return ra_rnti_list; }

private:
  // Initialize PHY configuration from SIB1 defaults
  void init_phy_cfg_from_sib1();

  // Calculate RA-RNTI list based on PRACH configuration
  void calculate_ra_rnti_list();

  // Try to decode MSG2 with a specific RA-RNTI
  MSG2Result try_decode_msg2(srsran_slot_cfg_t &slot_cfg, uint16_t ra_rnti);

  // Parse RAR MAC PDU to extract TC-RNTI
  bool parse_rar_pdu(uint8_t *data, uint32_t len, MSG2Result &result);
};
