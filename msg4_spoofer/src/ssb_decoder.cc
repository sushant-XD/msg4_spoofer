/**
 * SSB Decoder Implementation
 */

#include "ssb_decoder.h"
#include "logging.h"
// #include "sib1_decoder.h"  // Not needed for synchronization only
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>

SSBDecoder::SSBDecoder()
    : ssb_initialized_(false), sib1_decoder_initialized_(false) {
  std::memset(&ssb_, 0, sizeof(srsran_ssb_t));
  std::memset(&ssb_result_, 0, sizeof(SsbSearchResult));
}

SSBDecoder::~SSBDecoder() {
  if (ssb_initialized_) {
    srsran_ssb_free(&ssb_);
    ssb_initialized_ = false;
  }
}

bool SSBDecoder::init(RARSearchConfig &config) {
  // Initialize SSB
  srsran_ssb_args_t args = {};
  args.max_srate_hz = config.sample_rate;
  args.min_scs = srsran_subcarrier_spacing_15kHz;  // Use 15kHz as minimum
  args.enable_search = true;
  args.enable_measure = true;
  args.enable_encode = false;
  args.enable_decode = true;
  args.disable_polar_simd = false;
  // Use default threshold for better detection
  args.pbch_dmrs_thr = 0.0f;  // Use default threshold

  if (srsran_ssb_init(&ssb_, &args) != SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to initialize SSB");
    return false;
  }

  ssb_initialized_ = true;
  return configure_ssb(config);
}

bool SSBDecoder::configure_ssb(RARSearchConfig &config) {
  if (!ssb_initialized_) {
    LOG_ERROR("SSB not initialized");
    return false;
  }

  srsran_ssb_cfg_t ssb_cfg = {};
  ssb_cfg.srate_hz = config.sample_rate;
  ssb_cfg.center_freq_hz = config.dl_freq;
  ssb_cfg.ssb_freq_hz = config.ssb_freq;
  ssb_cfg.scs = config.scs_ssb;
  ssb_cfg.pattern = config.ssb_pattern;
  ssb_cfg.duplex_mode = config.duplex_mode;
  ssb_cfg.periodicity_ms = config.ssb_period_ms;
  ssb_cfg.beta_pss = 1.0f;
  ssb_cfg.beta_sss = 1.0f;
  ssb_cfg.beta_pbch = 1.0f;
  ssb_cfg.beta_pbch_dmrs = 1.0f;
  ssb_cfg.scaling = 0.0f;

  LOG_INFO("Configuring SSB: pattern=%s, scs=%u kHz, freq=%.2f MHz, center_freq=%.2f MHz",
           (ssb_cfg.pattern == SRSRAN_SSB_PATTERN_A) ? "A" : "Other",
           (ssb_cfg.scs == srsran_subcarrier_spacing_15kHz) ? 15 : 30,
           ssb_cfg.ssb_freq_hz / 1e6, ssb_cfg.center_freq_hz / 1e6);

  if (srsran_ssb_set_cfg(&ssb_, &ssb_cfg) != SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to configure SSB");
    return false;
  }

  return true;
}

SsbSearchResult SSBDecoder::scan_ssb(cf_t *cf_buffer, uint32_t nsamples,
                                     uint32_t target_pci) {
  SsbSearchResult result = {};
  result.found = false;

  if (!ssb_initialized_) {
    LOG_ERROR("SSB not initialized");
    return result;
  }

  // Perform SSB search
  srsran_ssb_search_res_t search_res = {};

  // First perform CSI search to find the PCI
  srsran_csi_trs_measurements_t meas = {};
  uint32_t N_id = 0;
  if (srsran_ssb_csi_search(&ssb_, cf_buffer, nsamples, &N_id, &meas) <
      SRSRAN_SUCCESS) {
    // CSI search failed, but continue with regular search
    LOG_DEBUG("SSB CSI search failed, continuing with regular search");
  }

  // Perform the actual SSB search
  if (srsran_ssb_search(&ssb_, cf_buffer, nsamples, &search_res) !=
      SRSRAN_SUCCESS) {
    LOG_ERROR("SSB search function failed");
    return result;
  }

  char str[512] = {};
  srsran_pbch_msg_info(&search_res.pbch_msg, str, sizeof(str));
  LOG_DEBUG("SSB search result: PCI=%u, t_offset=%u, crc=%s, %s", 
            search_res.N_id, search_res.t_offset, 
            search_res.pbch_msg.crc ? "OK" : "FAIL", str);
  
  // Check if PBCH was successfully decoded (CRC must pass)
  if (!search_res.pbch_msg.crc) {
    LOG_DEBUG("SSB search completed but PBCH CRC failed - no valid SSB found");
    return result;
  }

  // Validate measurement quality to avoid false positives
  // RSRP should be reasonable (typically > -120 dBm for valid signal)
  // SNR should be positive for reliable decoding
  if (search_res.measurements.rsrp_dB < -120.0f) {
    LOG_WARN("SSB RSRP too low (%.1f dBm) - likely noise, rejecting",
             search_res.measurements.rsrp_dB);
    return result;
  }

  if (search_res.measurements.snr_dB < -5.0f) {
    LOG_WARN("SSB SNR too low (%.1f dB) - likely noise, rejecting",
             search_res.measurements.snr_dB);
    return result;
  }

  // If target PCI specified, check if it matches
  if (search_res.N_id != target_pci) {
    LOG_INFO("SSB found but PCI mismatch: found=%u, expected=%u",
             search_res.N_id, target_pci);
    return result;
  }

  // Decode MIB immediately while search_res is still valid
  srsran_mib_nr_t mib = {};
  if (srsran_pbch_msg_nr_mib_unpack(&search_res.pbch_msg, &mib) !=
      SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to unpack MIB from PBCH message");
    return result;
  }

  // Validate MIB contents - SFN should be in valid range (0-1023)
  if (mib.sfn > 1023) {
    LOG_WARN("Invalid MIB: SFN=%u out of range (0-1023), rejecting", mib.sfn);
    return result;
  }

  // Fill result structure with only the data we need (no pointers)
  result.found = true;
  result.pci = search_res.N_id;
  result.ssb_idx = search_res.pbch_msg.ssb_idx;
  result.t_offset =
      search_res.t_offset; // Store timing offset for synchronization
  result.snr_db = search_res.measurements.snr_dB;
  result.rsrp_dbm = search_res.measurements.rsrp_dB;
  result.mib = mib; // Copy the decoded MIB

  ssb_result_ = result;

  LOG_INFO("SSB successfully decoded: PCI=%u, SSB_idx=%u, SNR=%.1f dB, "
           "RSRP=%.1f dBm, SFN=%u",
           result.pci, result.ssb_idx, result.snr_db, result.rsrp_dbm, mib.sfn);

  return result;
}

bool SSBDecoder::decode_mib(const srsran_pbch_msg_nr_t &pbch_msg,
                            srsran_mib_nr_t &mib) {
  if (srsran_pbch_msg_nr_mib_unpack(&pbch_msg, &mib) != SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to unpack MIB");
    return false;
  }
  return true;
}

void SSBDecoder::print_mib(const srsran_mib_nr_t &mib) {
  std::cout << "\n=== MIB Information ===" << std::endl;
  std::cout << "  SFN                    : " << mib.sfn << std::endl;
  std::cout << "  SSB Index              : "
            << static_cast<uint32_t>(mib.ssb_idx) << std::endl;
  std::cout << "  Half Radio Frame       : " << (mib.hrf ? "Yes" : "No")
            << std::endl;
  std::cout << "  Subcarrier Spacing     : ";
  switch (mib.scs_common) {
  case srsran_subcarrier_spacing_15kHz:
    std::cout << "15 kHz";
    break;
  case srsran_subcarrier_spacing_30kHz:
    std::cout << "30 kHz";
    break;
  case srsran_subcarrier_spacing_60kHz:
    std::cout << "60 kHz";
    break;
  case srsran_subcarrier_spacing_120kHz:
    std::cout << "120 kHz";
    break;
  case srsran_subcarrier_spacing_240kHz:
    std::cout << "240 kHz";
    break;
  default:
    std::cout << "Unknown";
    break;
  }
  std::cout << std::endl;
  std::cout << "  SSB Offset             : " << mib.ssb_offset << std::endl;
  std::cout << "  DMRS TypeA Position    : "
            << static_cast<uint32_t>(mib.dmrs_typeA_pos) << std::endl;
  std::cout << "  CORESET0 Index         : "
            << static_cast<uint32_t>(mib.coreset0_idx) << std::endl;
  std::cout << "  SearchSpace0 Index     : "
            << static_cast<uint32_t>(mib.ss0_idx) << std::endl;

  // Calculate and display pdcch-ConfigSIB1 parameter
  // pdcch-ConfigSIB1 = (coreset0_idx << 4) | ss0_idx
  uint32_t pdcch_config_sib1 = (static_cast<uint32_t>(mib.coreset0_idx) << 4) |
                               static_cast<uint32_t>(mib.ss0_idx);
  std::cout << "  pdcch-ConfigSIB1       : " << pdcch_config_sib1 << " (0x"
            << std::hex << pdcch_config_sib1 << std::dec << ")" << std::endl;

  std::cout << "  Cell Barred            : " << (mib.cell_barred ? "Yes" : "No")
            << std::endl;
  std::cout << "  Intra-Freq Reselection : "
            << (mib.intra_freq_reselection ? "Allowed" : "Not Allowed")
            << std::endl;
  std::cout << "=======================" << std::endl;
}

// void SSBDecoder::print_sib1(const SIB1Result &sib1) {
//   std::cout << "\n=== SIB1 Information ===" << std::endl;
//   std::cout << "  SFN                    : " << sib1.sfn << std::endl;
//   std::cout << "  Slot in Frame          : " << sib1.slot_in_frame <<
//   std::endl; std::cout << "  Valid                  : " << (sib1.valid ?
//   "Yes" : "No")
//             << std::endl;
//
//   if (sib1.valid) {
//     std::cout << "  PDCCH Config           : "
//               << (sib1.pdcch_config.configured ? "Configured"
//                                                : "Not Configured")
//               << std::endl;
//     std::cout << "  CORESET0 Index         : " <<
//     sib1.pdcch_config.coreset0_idx
//               << std::endl;
//     std::cout << "  SearchSpace0 Index     : "
//               << sib1.pdcch_config.searchspace0_idx << std::endl;
//   }
//
//   std::cout << "=======================" << std::endl;
// }

srsran_ssb_pattern_t
SSBDecoder::pattern_from_string(const std::string &pattern) {
  if (pattern == "A")
    return SRSRAN_SSB_PATTERN_A;
  if (pattern == "B")
    return SRSRAN_SSB_PATTERN_B;
  if (pattern == "C")
    return SRSRAN_SSB_PATTERN_C;
  if (pattern == "D")
    return SRSRAN_SSB_PATTERN_D;
  if (pattern == "E")
    return SRSRAN_SSB_PATTERN_E;

  LOG_WARN("Unknown SSB pattern '%s', defaulting to C", pattern.c_str());
  return SRSRAN_SSB_PATTERN_C;
}

srsran_subcarrier_spacing_t SSBDecoder::scs_from_khz(uint32_t scs_khz) {
  switch (scs_khz) {
  case 15:
    return srsran_subcarrier_spacing_15kHz;
  case 30:
    return srsran_subcarrier_spacing_30kHz;
  case 60:
    return srsran_subcarrier_spacing_60kHz;
  case 120:
    return srsran_subcarrier_spacing_120kHz;
  case 240:
    return srsran_subcarrier_spacing_240kHz;
  default:
    LOG_WARN("Unknown SCS %u kHz, defaulting to 30 kHz", scs_khz);
    return srsran_subcarrier_spacing_30kHz;
  }
}
