#ifndef RAR_CONFIG_H
#define RAR_CONFIG_H

#include "srsran/phy/common/phy_common_nr.h"
#include <cstdint>
#include <string>
#include <vector>

struct RARSearchConfig {
  // Cell parameters
  uint16_t band = 3;
  uint32_t nof_prb = 106;
  uint32_t ncellid = 1;

  // Frequencies (Hz)
  double dl_freq = 1865e6;
  double ul_freq = 1770e6;
  double ssb_freq = 1857.65e6;
  double sample_rate = 23.04e6;

  // Subcarrier spacing
  srsran_subcarrier_spacing_t scs_common = srsran_subcarrier_spacing_15kHz;
  srsran_subcarrier_spacing_t scs_ssb = srsran_subcarrier_spacing_15kHz;

  // Duplex config
  srsran_duplex_mode_t duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
  srsran_ssb_pattern_t ssb_pattern = SRSRAN_SSB_PATTERN_A;

  // Timing
  uint32_t ssb_period_ms = 10;
  uint32_t ssb_period = 10;
  uint32_t offset_to_carrier = 0;

  // RA-RNTI list (auto-calculated from PRACH config)
  std::vector<uint16_t> ra_rnti_list = {1, 2, 3, 4};

  // CORESET0 config
  uint8_t coreset0_idx = 0;
  uint8_t ss0_idx = 0;

  // Other params
  srsran_dmrs_sch_typeA_pos_t dmrs_typeA_pos = srsran_dmrs_sch_typeA_pos_2;
  srsran_subcarrier_spacing_t pdcch_cfg_scs = srsran_subcarrier_spacing_30kHz;
  bool cell_barred = false;
  bool intra_freq_reselection = true;
  bool hrf = false;
  uint32_t sfn = 0;
  uint32_t ssb_offset = 0;
  uint32_t nof_rx_antennas = 1;
};

// FDD Band 3 default
inline RARSearchConfig get_default_config() {
  RARSearchConfig config;
  config.band = 3;
  config.nof_prb = 106;
  config.ncellid = 1;
  config.dl_freq = 1865e6;
  config.ul_freq = 1770e6;
  config.ssb_freq = 1857.65e6;
  config.sample_rate = 23.04e6;

  // Subcarrier spacing from SIB1: "kHz15"
  config.scs_common = srsran_subcarrier_spacing_15kHz;
  config.scs_ssb = srsran_subcarrier_spacing_15kHz;

  // Band 3 is FDD
  config.duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
  config.ssb_pattern = SRSRAN_SSB_PATTERN_A;

  // SSB periodicity from SIB1: "ms10"
  config.ssb_period_ms = 10;
  config.ssb_period = 10; // 10 slots for 15kHz SCS

  // From SIB1: offsetToPointA = 40
  config.offset_to_carrier = 0;

  // PRACH config from SIB1: prach-ConfigurationIndex = 1
  // For FDD, this typically gives RA-RNTI values starting from 1
  // ra-ResponseWindow = "sl10" means RAR comes within 10 slots
  config.ra_rnti_list = {1, 2, 3, 4};

  // CORESET0 and SS0 (will be from MIB, using defaults)
  config.coreset0_idx = 0;
  config.ss0_idx = 0;

  // Other parameters from SIB1
  config.dmrs_typeA_pos = srsran_dmrs_sch_typeA_pos_2;
  config.pdcch_cfg_scs = srsran_subcarrier_spacing_15kHz;
  config.cell_barred = false;
  config.intra_freq_reselection = true;

  // SSB offset from SIB1: offsetToCarrier = 0
  config.ssb_offset = 0;
  config.nof_rx_antennas = 1;

  return config;
}

/**
 * Get configuration for FDD deployment (Band 3, 20 MHz)
 */
inline RARSearchConfig get_fdd_band3_20mhz_config() {
  RARSearchConfig config;
  config.band = 3;
  config.nof_prb = 106;
  config.ncellid = 1;
  config.dl_freq = 1865e6;
  config.ul_freq = 1770e6;
  config.ssb_freq = 1857.65e6;
  config.sample_rate = 23.04e6;
  config.scs_common = srsran_subcarrier_spacing_15kHz;
  config.scs_ssb = srsran_subcarrier_spacing_15kHz;
  config.duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
  config.ssb_pattern = SRSRAN_SSB_PATTERN_A;
  config.ssb_period_ms = 10;
  config.ssb_period = 10;
  config.ra_rnti_list = {1, 2};
  return config;
}

// TDD Band 78 config
inline RARSearchConfig get_tdd_band78_20mhz_config() {
  RARSearchConfig config;
  config.band = 78;
  config.nof_prb = 51;
  config.ncellid = 1;
  config.dl_freq = 3427.5e6;
  config.ul_freq = 3427.5e6;
  config.ssb_freq = 3421.92e6;
  config.sample_rate = 23.04e6;
  config.scs_common = srsran_subcarrier_spacing_30kHz;
  config.scs_ssb = srsran_subcarrier_spacing_30kHz;
  config.duplex_mode = SRSRAN_DUPLEX_MODE_TDD;
  config.ssb_pattern = SRSRAN_SSB_PATTERN_C;
  config.ssb_period_ms = 10;
  config.ssb_period = 20;
  config.ra_rnti_list = {2};
  return config;
}

// Calculate RA-RNTI from PRACH config
// Formula: RA-RNTI = 1 + s_id + 14*t_id + 14*80*f_id + 14*80*8*ul_carrier_id
inline std::vector<uint16_t> calculate_ra_rnti_list(
    uint32_t prach_config_idx,
    srsran_subcarrier_spacing_t scs,
    uint32_t f_id = 0,
    uint32_t ul_carrier_id = 0) {
  
  std::vector<uint16_t> ra_rnti_list;
  std::vector<uint32_t> prach_slots;
  uint32_t s_id = 0;  // PRACH start symbol
  
  // Map PRACH config index to time slots
  switch (prach_config_idx) {
    case 0:  prach_slots = {0}; break;
    case 1:  prach_slots = {0, 2, 4, 6, 8}; break;
    case 2:  prach_slots = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18}; break;
    case 3:  prach_slots = {0, 1, 2}; break;
    case 16: prach_slots = {1}; s_id = 2; break;
    case 17: prach_slots = {1, 4, 7}; s_id = 2; break;
    case 27: prach_slots = {4}; break;
    case 52: prach_slots = {1}; s_id = 4; break;
    case 67: prach_slots = {8}; break;
    default: prach_slots = {0, 2, 4, 6, 8}; break;  // Fallback
  }
  
  // Calculate RA-RNTI for each slot
  for (uint32_t t_id : prach_slots) {
    uint16_t ra_rnti = 1 + s_id + 14 * t_id + 14 * 80 * f_id + 14 * 80 * 8 * ul_carrier_id;
    ra_rnti_list.push_back(ra_rnti);
  }
  
  return ra_rnti_list;
}

#endif // RAR_CONFIG_H
