/**
 * SSB Decoder Implementation
 */

#include "ssb_decoder.h"
#include "logging.h"
#include "sib1_decoder.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>

SSBDecoder::SSBDecoder(double srate_hz, uint32_t nof_prb, uint32_t pci,
                       double freq_hz)
    : ssb_initialized_(false), sib1_decoder_initialized_(false),
      srate_hz_(srate_hz), center_freq_hz_(freq_hz), nof_prb_(nof_prb),
      pci_(pci) {
  std::memset(&ssb_, 0, sizeof(srsran_ssb_t));
  std::memset(&ssb_result_, 0, sizeof(SsbSearchResult));
}

SSBDecoder::~SSBDecoder() {
  if (ssb_initialized_) {
    srsran_ssb_free(&ssb_);
    ssb_initialized_ = false;
  }
}

bool SSBDecoder::init() {
  // Initialize SSB
  srsran_ssb_args_t args = {};
  args.max_srate_hz = srate_hz_;
  args.min_scs = srsran_subcarrier_spacing_15kHz;
  args.enable_search = true;
  args.enable_measure = true;
  args.enable_encode = false;
  args.enable_decode = true;
  args.disable_polar_simd = false;
  args.pbch_dmrs_thr = 0.0f;

  LOG_INFO("Initializing SSB processor (srate=%.2f MHz)...", srate_hz_ / 1e6);

  if (srsran_ssb_init(&ssb_, &args) != SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to initialize SSB");
    return false;
  }

  ssb_initialized_ = true;
  return true;
}

bool SSBDecoder::configure_ssb(const std::string &pattern, uint32_t scs_khz,
                               double ssb_freq_offset_hz) {
  if (!ssb_initialized_) {
    LOG_ERROR("SSB not initialized");
    return false;
  }

  double ssb_freq_hz = center_freq_hz_ + ssb_freq_offset_hz;

  srsran_ssb_cfg_t ssb_cfg = {};
  ssb_cfg.srate_hz = srate_hz_;
  ssb_cfg.center_freq_hz = center_freq_hz_;
  ssb_cfg.ssb_freq_hz = ssb_freq_hz;
  ssb_cfg.scs = scs_from_khz(scs_khz);
  ssb_cfg.pattern = pattern_from_string(pattern);
  ssb_cfg.duplex_mode = SRSRAN_DUPLEX_MODE_FDD;
  ssb_cfg.periodicity_ms = 20;
  ssb_cfg.beta_pss = 0.0f;
  ssb_cfg.beta_sss = 0.0f;
  ssb_cfg.beta_pbch = 0.0f;
  ssb_cfg.beta_pbch_dmrs = 0.0f;
  ssb_cfg.scaling = 0.0f;

  LOG_INFO("Configuring SSB: pattern=%s, scs=%u kHz, freq=%.2f MHz",
           pattern.c_str(), scs_khz, ssb_freq_hz / 1e6);

  if (srsran_ssb_set_cfg(&ssb_, &ssb_cfg) != SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to configure SSB");
    return false;
  }

  return true;
}

SsbSearchResult SSBDecoder::scan_ssb(const std::complex<float> *buffer,
                                     uint32_t nsamples,
                                     std::optional<uint32_t> target_pci) {
  SsbSearchResult result = {};
  result.found = false;

  if (!ssb_initialized_) {
    LOG_ERROR("SSB not initialized");
    return result;
  }

  // Perform SSB search
  srsran_ssb_search_res_t search_res = {};
  const cf_t *cf_buffer = reinterpret_cast<const cf_t *>(buffer);

  if (srsran_ssb_search(&ssb_, cf_buffer, nsamples, &search_res) !=
      SRSRAN_SUCCESS) {
    return result;
  }

  // Check if PBCH was successfully decoded
  if (!search_res.pbch_msg.crc) {
    return result;
  }

  // If target PCI specified, check if it matches
  if (target_pci.has_value() && search_res.N_id != target_pci.value()) {
    return result;
  }

  // Decode MIB immediately while search_res is still valid
  srsran_mib_nr_t mib = {};
  if (srsran_pbch_msg_nr_mib_unpack(&search_res.pbch_msg, &mib) !=
      SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to unpack MIB");
    return result;
  }

  // Fill result structure with only the data we need (no pointers)
  result.found = true;
  result.pci = search_res.N_id;
  result.ssb_idx = search_res.pbch_msg.ssb_idx;
  result.snr_db = search_res.measurements.snr_dB;
  result.rsrp_dbm = search_res.measurements.rsrp_dB;
  result.mib = mib; // Copy the decoded MIB

  ssb_result_ = result;

  LOG_INFO("SSB found: PCI=%u, SSB_idx=%u, SNR=%.1f dB, RSRP=%.1f dBm",
           result.pci, result.ssb_idx, result.snr_db, result.rsrp_dbm);

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
