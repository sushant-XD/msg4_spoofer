/**
 * MSG2 Configuration Header
 * 
 * This module provides fallback configuration for MSG2 decoding when SIB1 is not available.
 * It creates PHY configurations based on MIB + defaults or TOML file parameters.
 */

#pragma once

#include "config.h"
#include "ssb_decoder.h"
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/srsran.h"
#include "srsran/asn1/rrc_nr.h"
#include <vector>

/**
 * MSG2 Configuration Source
 */
enum class MSG2ConfigSource {
  SIB1_AVAILABLE,    // Use actual SIB1 data
  MIB_DEFAULTS,      // Use MIB + 3GPP defaults
  TOML_OVERRIDE      // Use TOML file overrides
};

/**
 * MSG2 Configuration Structure
 */
struct MSG2Config {
  // Source of configuration
  MSG2ConfigSource source;
  
  // PDCCH Configuration
  struct {
    bool configured;
    uint8_t coreset0_idx;
    uint8_t searchspace0_idx;
    uint32_t nof_candidates_al4;  // Aggregation level 4
    uint32_t nof_candidates_al8;  // Aggregation level 8
  } pdcch_config;
  
  // PRACH Configuration for RA-RNTI calculation
  struct {
    uint32_t config_idx;
    uint32_t root_seq_idx;
    uint32_t zero_corr_zone;
    uint32_t num_preambles;
    bool is_nr;
    bool hs_flag;
    std::vector<uint32_t> prach_slots;  // Slots where PRACH can occur
  } prach_config;
  
  // Cell configuration
  struct {
    uint32_t pci;
    uint32_t nof_prb;
    srsran_subcarrier_spacing_t scs;
    srsran_duplex_mode_t duplex_mode;
    double dl_freq_hz;
    double ul_freq_hz;
  } cell_config;
  
  // RA-RNTI list for blind decoding
  std::vector<uint16_t> ra_rnti_list;
  
  // Timing configuration
  struct {
    uint32_t msg2_window_slots;      // MSG2 window length
    uint32_t max_msg2_attempts;      // Max attempts to find MSG2
  } timing_config;
};

/**
 * MSG2 Configuration Builder Class
 */
class MSG2ConfigBuilder {
public:
  /**
   * Create MSG2 config from SIB1 (when available)
   */
  static MSG2Config from_sib1(const asn1::rrc_nr::sib1_s& sib1_data,
                              const SsbSearchResult& ssb_result,
                              const spoofer_config_t& toml_config);

  /**
   * Create MSG2 config from MIB + defaults (when SIB1 unavailable)
   */
  static MSG2Config from_mib_defaults(const SsbSearchResult& ssb_result,
                                     const spoofer_config_t& toml_config);

  /**
   * Create MSG2 config from TOML overrides only
   */
  static MSG2Config from_toml_only(const spoofer_config_t& toml_config);

  /**
   * Update PHY configuration based on MSG2 config
   */
  static bool configure_phy_for_msg2(srsran::phy_cfg_nr_t& phy_cfg,
                                     const MSG2Config& msg2_config,
                                     const SsbSearchResult& ssb_result);

  /**
   * Calculate RA-RNTI list based on PRACH configuration
   */
  static std::vector<uint16_t> calculate_ra_rnti_list(const MSG2Config& config);

private:
  /**
   * Apply 3GPP default configurations for initial access
   */
  static void apply_3gpp_defaults(MSG2Config& config);
  
  /**
   * Calculate PRACH slots based on configuration index
   */
  static std::vector<uint32_t> get_prach_slots(uint32_t config_idx, 
                                               srsran_subcarrier_spacing_t scs);
};

/**
 * Helper functions for MSG2 configuration validation
 */
namespace msg2_config_utils {
  
  /**
   * Validate MSG2 configuration
   */
  bool validate_config(const MSG2Config& config);
  
  /**
   * Print MSG2 configuration for debugging
   */
  void print_config(const MSG2Config& config);
  
  /**
   * Get configuration source name as string
   */
  std::string source_to_string(MSG2ConfigSource source);
}