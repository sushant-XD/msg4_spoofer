/**
 * MSG2 Configuration Implementation
 *
 * This module provides fallback configuration for MSG2 decoding when SIB1 is
 * not available.
 */

#include "msg2_config.h"
#include "logging.h"
#include <algorithm>
#include <iomanip>
#include <iostream>

MSG2Config MSG2ConfigBuilder::from_sib1(const asn1::rrc_nr::sib1_s &sib1_data,
                                        const SsbSearchResult &ssb_result,
                                        const spoofer_config_t &toml_config) {
  MSG2Config config = {};
  config.source = MSG2ConfigSource::SIB1_AVAILABLE;

  // Extract PDCCH configuration from SIB1
  config.pdcch_config.configured = true;
  config.pdcch_config.coreset0_idx = ssb_result.mib.coreset0_idx;
  config.pdcch_config.searchspace0_idx = ssb_result.mib.ss0_idx;
  config.pdcch_config.nof_candidates_al4 = 4;
  config.pdcch_config.nof_candidates_al8 = 2;

  // Use PRACH config from TOML (SIB1 PRACH parsing is complex)
  config.prach_config.config_idx = toml_config.prach.config_idx;
  config.prach_config.root_seq_idx = toml_config.prach.root_seq_idx;
  config.prach_config.zero_corr_zone = toml_config.prach.zero_corr_zone;
  config.prach_config.num_preambles = toml_config.prach.num_ra_preambles;
  config.prach_config.is_nr = toml_config.prach.is_nr;
  config.prach_config.hs_flag = toml_config.prach.hs_flag;

  // Cell configuration from TOML and SSB
  config.cell_config.pci = ssb_result.pci;
  config.cell_config.nof_prb = toml_config.rf.nof_prb;
  config.cell_config.scs = ssb_result.mib.scs_common;
  config.cell_config.duplex_mode = toml_config.ssb.duplex_mode;
  config.cell_config.dl_freq_hz = toml_config.rf.dl_frequency;
  config.cell_config.ul_freq_hz = toml_config.rf.ul_frequency;

  // Timing configuration
  config.timing_config.msg2_window_slots = 40; // 4ms window for 15kHz SCS
  config.timing_config.max_msg2_attempts = 500;

  // Calculate PRACH slots and RA-RNTIs
  config.prach_config.prach_slots =
      get_prach_slots(config.prach_config.config_idx, config.cell_config.scs);
  config.ra_rnti_list = calculate_ra_rnti_list(config);

  LOG_INFO("MSG2 config created from SIB1 (PRACH config_idx=%u, %zu RA-RNTIs)",
           config.prach_config.config_idx, config.ra_rnti_list.size());

  return config;
}

MSG2Config
MSG2ConfigBuilder::from_mib_defaults(const SsbSearchResult &ssb_result,
                                     const spoofer_config_t &toml_config) {
  MSG2Config config = {};
  config.source = MSG2ConfigSource::MIB_DEFAULTS;

  // PDCCH configuration from MIB
  config.pdcch_config.configured = true;
  config.pdcch_config.coreset0_idx = ssb_result.mib.coreset0_idx;
  config.pdcch_config.searchspace0_idx = ssb_result.mib.ss0_idx;
  config.pdcch_config.nof_candidates_al4 = 4;
  config.pdcch_config.nof_candidates_al8 = 2;

  // PRACH configuration from TOML
  config.prach_config.config_idx = toml_config.prach.config_idx;
  config.prach_config.root_seq_idx = toml_config.prach.root_seq_idx;
  config.prach_config.zero_corr_zone = toml_config.prach.zero_corr_zone;
  config.prach_config.num_preambles = toml_config.prach.num_ra_preambles;
  config.prach_config.is_nr = toml_config.prach.is_nr;
  config.prach_config.hs_flag = toml_config.prach.hs_flag;

  // Cell configuration
  config.cell_config.pci = ssb_result.pci;
  config.cell_config.nof_prb = toml_config.rf.nof_prb;
  config.cell_config.scs = ssb_result.mib.scs_common;
  config.cell_config.duplex_mode = toml_config.ssb.duplex_mode;
  config.cell_config.dl_freq_hz = toml_config.rf.dl_frequency;
  config.cell_config.ul_freq_hz = toml_config.rf.ul_frequency;

  // Apply 3GPP defaults for missing SIB1 parameters
  apply_3gpp_defaults(config);

  // Calculate PRACH slots and RA-RNTIs
  config.prach_config.prach_slots =
      get_prach_slots(config.prach_config.config_idx, config.cell_config.scs);
  config.ra_rnti_list = calculate_ra_rnti_list(config);

  LOG_INFO("MSG2 config created from MIB+defaults (PRACH config_idx=%u, %zu "
           "RA-RNTIs)",
           config.prach_config.config_idx, config.ra_rnti_list.size());

  return config;
}

MSG2Config
MSG2ConfigBuilder::from_toml_only(const spoofer_config_t &toml_config) {
  MSG2Config config = {};
  config.source = MSG2ConfigSource::TOML_OVERRIDE;

  // PDCCH configuration - use defaults
  config.pdcch_config.configured = true;
  config.pdcch_config.coreset0_idx = 12; // Common default
  config.pdcch_config.searchspace0_idx = 0;
  config.pdcch_config.nof_candidates_al4 = 2;
  config.pdcch_config.nof_candidates_al8 = 0;

  // PRACH configuration from TOML
  config.prach_config.config_idx = toml_config.prach.config_idx;
  config.prach_config.root_seq_idx = toml_config.prach.root_seq_idx;
  config.prach_config.zero_corr_zone = toml_config.prach.zero_corr_zone;
  config.prach_config.num_preambles = toml_config.prach.num_ra_preambles;
  config.prach_config.is_nr = toml_config.prach.is_nr;
  config.prach_config.hs_flag = toml_config.prach.hs_flag;

  // Cell configuration from TOML
  config.cell_config.pci = toml_config.rf.N_id;
  config.cell_config.nof_prb = toml_config.rf.nof_prb;
  config.cell_config.scs = toml_config.ssb.scs;
  config.cell_config.duplex_mode = toml_config.ssb.duplex_mode;
  config.cell_config.dl_freq_hz = toml_config.rf.dl_frequency;
  config.cell_config.ul_freq_hz = toml_config.rf.ul_frequency;

  // Apply 3GPP defaults
  apply_3gpp_defaults(config);

  // Calculate PRACH slots and RA-RNTIs
  config.prach_config.prach_slots =
      get_prach_slots(config.prach_config.config_idx, config.cell_config.scs);
  config.ra_rnti_list = calculate_ra_rnti_list(config);

  LOG_INFO(
      "MSG2 config created from TOML only (PRACH config_idx=%u, %zu RA-RNTIs)",
      config.prach_config.config_idx, config.ra_rnti_list.size());

  return config;
}

bool MSG2ConfigBuilder::configure_phy_for_msg2(
    srsran::phy_cfg_nr_t &phy_cfg, const MSG2Config &msg2_config,
    const SsbSearchResult &ssb_result) {

  // Basic carrier configuration
  phy_cfg.carrier.pci = msg2_config.cell_config.pci;
  phy_cfg.carrier.dl_center_frequency_hz = msg2_config.cell_config.dl_freq_hz;
  phy_cfg.carrier.ul_center_frequency_hz = msg2_config.cell_config.ul_freq_hz;
  phy_cfg.carrier.nof_prb = msg2_config.cell_config.nof_prb;
  phy_cfg.carrier.scs = msg2_config.cell_config.scs;
  phy_cfg.carrier.max_mimo_layers = 1;
  phy_cfg.carrier.offset_to_carrier = 0;

  // SSB configuration
  phy_cfg.ssb.scs = ssb_result.mib.scs_common;
  phy_cfg.ssb.pattern = SRSRAN_SSB_PATTERN_A; // Default for sub-6GHz

  // PDSCH configuration from MIB
  phy_cfg.pdsch.typeA_pos = ssb_result.mib.dmrs_typeA_pos;
  phy_cfg.pdsch.scs_cfg = ssb_result.mib.scs_common;

  // Duplex configuration
  phy_cfg.duplex.mode = msg2_config.cell_config.duplex_mode;

  // CORESET0 configuration
  double pointA_abs_freq_Hz = phy_cfg.carrier.dl_center_frequency_hz -
                              phy_cfg.carrier.nof_prb * SRSRAN_NRE *
                                  SRSRAN_SUBC_SPACING_NR(phy_cfg.carrier.scs) /
                                  2;
  double ssb_abs_freq_Hz = phy_cfg.carrier.ssb_center_freq_hz;
  uint32_t ssb_pointA_freq_offset_Hz =
      (ssb_abs_freq_Hz > pointA_abs_freq_Hz)
          ? (uint32_t)(ssb_abs_freq_Hz - pointA_abs_freq_Hz)
          : 0;

  if (srsran_coreset_zero(phy_cfg.carrier.pci, ssb_pointA_freq_offset_Hz,
                          phy_cfg.ssb.scs, phy_cfg.carrier.scs,
                          msg2_config.pdcch_config.coreset0_idx,
                          &phy_cfg.pdcch.coreset[0])) {
    LOG_ERROR("Failed to configure CORESET0");
    return false;
  }
  phy_cfg.pdcch.coreset_present[0] = true;

  // SearchSpace0 configuration for MSG2
  srsran_search_space_t &ss0 = phy_cfg.pdcch.search_space[0];
  ss0.id = 0;
  ss0.coreset_id = 0;
  ss0.type = srsran_search_space_type_common_1; // Type1-PDCCH for RA
  ss0.nof_candidates[0] = 4;
  ss0.nof_candidates[1] = 4;
  ss0.nof_candidates[2] = msg2_config.pdcch_config.nof_candidates_al4; // AL4
  ss0.nof_candidates[3] = msg2_config.pdcch_config.nof_candidates_al8; // AL8
  ss0.nof_candidates[4] = 1;
  ss0.duration = 1;
  ss0.nof_formats = 1;
  ss0.formats[0] = srsran_dci_format_nr_1_0;
  phy_cfg.pdcch.search_space_present[0] = true;

  // RA search space configuration
  phy_cfg.pdcch.ra_search_space_present = true;
  phy_cfg.pdcch.ra_search_space = ss0;

  LOG_INFO("PHY configuration updated for MSG2 decoding (source: %s)",
           msg2_config_utils::source_to_string(msg2_config.source).c_str());

  return true;
}

std::vector<uint16_t>
MSG2ConfigBuilder::calculate_ra_rnti_list(const MSG2Config &config) {
  std::vector<uint16_t> ra_rnti_list;

  // RA-RNTI = 1 + s_id + 14 × t_id + 14 × 80 × f_id + 14 × 80 × 8 ×
  // ul_carrier_id For single carrier, f_id = 0, ul_carrier_id = 0

  uint32_t s_id = 0; // Starting symbol (usually 0 for format 0)

  for (uint32_t t_id : config.prach_config.prach_slots) {
    uint16_t ra_rnti = 1 + s_id + 14 * t_id;
    ra_rnti_list.push_back(ra_rnti);
  }

  return ra_rnti_list;
}

void MSG2ConfigBuilder::apply_3gpp_defaults(MSG2Config &config) {
  // MSG2 window: 4ms for 15kHz SCS (40 slots)
  if (config.cell_config.scs == srsran_subcarrier_spacing_15kHz) {
    config.timing_config.msg2_window_slots = 40;
  } else if (config.cell_config.scs == srsran_subcarrier_spacing_30kHz) {
    config.timing_config.msg2_window_slots = 80;
  } else {
    config.timing_config.msg2_window_slots = 40; // Default
  }

  config.timing_config.max_msg2_attempts = 500;

  LOG_DEBUG("Applied 3GPP defaults: MSG2 window = %u slots",
            config.timing_config.msg2_window_slots);
}

std::vector<uint32_t>
MSG2ConfigBuilder::get_prach_slots(uint32_t config_idx,
                                   srsran_subcarrier_spacing_t scs) {
  std::vector<uint32_t> prach_slots;

  // For config_idx = 1 with 15kHz SCS (most common case)
  if (config_idx == 1 && scs == srsran_subcarrier_spacing_15kHz) {
    // PRACH format 0 in even slots: 0, 2, 4, 6, 8
    prach_slots = {0, 2, 4, 6, 8};
  } else {
    // For other configurations, add more cases as needed
    LOG_WARN(
        "PRACH config_idx %u with SCS %d not fully implemented, using defaults",
        config_idx, scs);
    prach_slots = {0, 2, 4, 6, 8}; // Default fallback
  }

  return prach_slots;
}

// Utility functions
namespace msg2_config_utils {

bool validate_config(const MSG2Config &config) {
  if (config.ra_rnti_list.empty()) {
    LOG_ERROR("MSG2 config validation failed: no RA-RNTIs");
    return false;
  }

  if (config.cell_config.nof_prb == 0) {
    LOG_ERROR("MSG2 config validation failed: invalid PRB count");
    return false;
  }

  if (!config.pdcch_config.configured) {
    LOG_ERROR("MSG2 config validation failed: PDCCH not configured");
    return false;
  }

  return true;
}

void print_config(const MSG2Config &config) {
  std::cout << "\n=== MSG2 Configuration ===" << std::endl;
  std::cout << "  Source: " << source_to_string(config.source) << std::endl;
  std::cout << "  Cell PCI: " << config.cell_config.pci << std::endl;
  std::cout << "  PRBs: " << config.cell_config.nof_prb << std::endl;
  std::cout << "  SCS: "
            << (config.cell_config.scs == srsran_subcarrier_spacing_15kHz
                    ? "15"
                    : "30")
            << " kHz" << std::endl;
  std::cout << "  PRACH config_idx: " << config.prach_config.config_idx
            << std::endl;
  std::cout << "  PRACH slots: ";
  for (size_t i = 0; i < config.prach_config.prach_slots.size(); ++i) {
    if (i > 0)
      std::cout << ", ";
    std::cout << config.prach_config.prach_slots[i];
  }
  std::cout << std::endl;
  std::cout << "  RA-RNTIs (" << config.ra_rnti_list.size() << "): ";
  for (size_t i = 0; i < std::min(config.ra_rnti_list.size(), size_t(5)); ++i) {
    if (i > 0)
      std::cout << ", ";
    std::cout << config.ra_rnti_list[i];
  }
  if (config.ra_rnti_list.size() > 5) {
    std::cout << "...";
  }
  std::cout << std::endl;
  std::cout << "  MSG2 window: " << config.timing_config.msg2_window_slots
            << " slots" << std::endl;
  std::cout << "=========================" << std::endl;
}

std::string source_to_string(MSG2ConfigSource source) {
  switch (source) {
  case MSG2ConfigSource::SIB1_AVAILABLE:
    return "SIB1";
  case MSG2ConfigSource::MIB_DEFAULTS:
    return "MIB+Defaults";
  case MSG2ConfigSource::TOML_OVERRIDE:
    return "TOML Only";
  default:
    return "Unknown";
  }
}

} // namespace msg2_config_utils
