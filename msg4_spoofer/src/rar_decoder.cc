// RAR decoder implementation

#include "rar_decoder.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

// ============================================================================
// PHY STATE CLASS IMPLEMENTATION
// ============================================================================

void phy_state_nr::set_ul_pending_grant(const srsran::phy_cfg_nr_t &cfg,
                                        const srsran_slot_cfg_t &slot_rx,
                                        const srsran_dci_ul_nr_t &dci_ul) {
  srsran_sch_cfg_nr_t pusch_cfg = {};
  if (!cfg.get_pusch_cfg(slot_rx, dci_ul, pusch_cfg)) {
    return;
  }

  uint32_t tti_tx = (slot_rx.idx + pusch_cfg.grant.k) % 10240;

  std::lock_guard<std::mutex> lock(ul_mutex);
  pending_ul_grant_t &grant = pending_ul_grants[tti_tx];
  grant.sch_cfg = pusch_cfg;
  grant.pid = dci_ul.pid;
  grant.enable = true;
}

bool phy_state_nr::get_ul_pending_grant(uint32_t tti_tx,
                                        srsran_sch_cfg_nr_t &pusch_cfg,
                                        uint32_t &pid) {
  std::lock_guard<std::mutex> lock(ul_mutex);

  auto it = pending_ul_grants.find(tti_tx);
  if (it == pending_ul_grants.end() || !it->second.enable) {
    return false;
  }

  pusch_cfg = it->second.sch_cfg;
  pid = it->second.pid;
  it->second.enable = false;

  return true;
}

void phy_state_nr::set_dl_pending_grant(const srsran::phy_cfg_nr_t &cfg,
                                        const srsran_slot_cfg_t &slot,
                                        const srsran_dci_dl_nr_t &dci_dl) {
  srsran_sch_cfg_nr_t pdsch_cfg = {};
  if (!cfg.get_pdsch_cfg(slot, dci_dl, pdsch_cfg)) {
    return;
  }

  srsran_harq_ack_resource_t ack_resource = {};
  if (!cfg.get_pdsch_ack_resource(dci_dl, ack_resource)) {
    return;
  }

  uint32_t tti_rx = (slot.idx + pdsch_cfg.grant.k) % 10240;

  std::lock_guard<std::mutex> lock(dl_mutex);
  pending_dl_grant_t &grant = pending_dl_grants[tti_rx];
  grant.sch_cfg = pdsch_cfg;
  grant.ack_resource = ack_resource;
  grant.pid = dci_dl.pid;
  grant.enable = true;
}

bool phy_state_nr::get_dl_pending_grant(uint32_t tti_rx,
                                        srsran_sch_cfg_nr_t &pdsch_cfg,
                                        srsran_harq_ack_resource_t &ack_resource,
                                        uint32_t &pid) {
  std::lock_guard<std::mutex> lock(dl_mutex);

  auto it = pending_dl_grants.find(tti_rx);
  if (it == pending_dl_grants.end() || !it->second.enable) {
    return false;
  }

  pdsch_cfg = it->second.sch_cfg;
  ack_resource = it->second.ack_resource;
  pid = it->second.pid;
  it->second.enable = false;

  return true;
}

void phy_state_nr::clear_pending_grants() {
  std::lock_guard<std::mutex> lock_ul(ul_mutex);
  std::lock_guard<std::mutex> lock_dl(dl_mutex);
  pending_ul_grants.clear();
  pending_dl_grants.clear();
}

// ============================================================================
// RAR DECODER CLASS IMPLEMENTATION
// ============================================================================

RARDecoder::RARDecoder(const RARSearchConfig& config)
    : config_(config),
      logger_(srslog::fetch_basic_logger("rar_search")),
      rar_count_(0),
      slot_number_(0) {
  
  // Calculate derived parameters
  slot_per_subframe_ = 1 << config_.scs_common;
  uint32_t sf_len = static_cast<uint32_t>(config_.sample_rate / 1000.0);
  slot_len_ = sf_len / slot_per_subframe_;
  
  logger_.info("RAR Decoder initialized");
  logger_.info("  Slot length: %u samples", slot_len_);
  logger_.info("  Slots per subframe: %u", slot_per_subframe_);
}

RARDecoder::~RARDecoder() {
  logger_.info("RAR Decoder destroyed");
}

bool RARDecoder::init() {
  // Initialize PHY configuration
  init_phy_cfg();
  
  // Configure CORESET0 and SS0
  if (!configure_phy_cfg_basic()) {
    logger_.error("Failed to configure PHY");
    return false;
  }
  
  // Initialize PHY state
  phy_state_.clear_pending_grants();
  
  logger_.info("RAR Decoder initialization complete");
  return true;
}

void RARDecoder::init_phy_cfg() {
  phy_cfg_.carrier.dl_center_frequency_hz = config_.dl_freq;
  phy_cfg_.carrier.ul_center_frequency_hz = config_.ul_freq;
  phy_cfg_.carrier.ssb_center_freq_hz = config_.ssb_freq;
  phy_cfg_.carrier.offset_to_carrier = config_.offset_to_carrier;
  phy_cfg_.carrier.scs = config_.scs_common;
  phy_cfg_.carrier.nof_prb = config_.nof_prb;
  phy_cfg_.carrier.pci = config_.ncellid;
  phy_cfg_.carrier.max_mimo_layers = 1;
  
  phy_cfg_.duplex.mode = config_.duplex_mode;
  
  phy_cfg_.ssb.periodicity_ms = config_.ssb_period_ms;
  phy_cfg_.ssb.position_in_burst[0] = true;
  phy_cfg_.ssb.scs = config_.scs_ssb;
  phy_cfg_.ssb.pattern = config_.ssb_pattern;
}

bool RARDecoder::configure_phy_cfg_basic() {
  srsran_coreset_t *coreset0 = &phy_cfg_.pdcch.coreset[0];

  if (srsran_coreset_zero(config_.ncellid, config_.ssb_offset, config_.scs_ssb,
                          config_.scs_common, config_.coreset0_idx,
                          coreset0) < SRSRAN_SUCCESS) {
    return false;
  }

  coreset0->id = 0;
  coreset0->mapping_type = srsran_coreset_mapping_type_non_interleaved;
  coreset0->precoder_granularity = srsran_coreset_precoder_granularity_reg_bundle;

  phy_cfg_.pdcch.coreset_present[0] = true;

  srsran_search_space_t *ss0 = &phy_cfg_.pdcch.search_space[0];
  ss0->id = 0;
  ss0->coreset_id = 0;
  ss0->type = srsran_search_space_type_common_1;
  ss0->nof_candidates[0] = 0;
  ss0->nof_candidates[1] = 0;
  ss0->nof_candidates[2] = 4;
  ss0->nof_candidates[3] = 2;
  ss0->nof_candidates[4] = 1;
  ss0->duration = 1;
  ss0->nof_formats = 1;
  ss0->formats[0] = srsran_dci_format_nr_1_0;

  phy_cfg_.pdcch.search_space_present[0] = true;
  phy_cfg_.pdcch.ra_search_space_present = true;
  phy_cfg_.pdcch.ra_search_space = *ss0;

  phy_cfg_.t_offset = 0;

  return true;
}

bool RARDecoder::process_slot(cf_t *data_buffer, uint32_t slot_number) {
  if (search_rar_in_slot(data_buffer, slot_number)) {
    rar_count_++;
    logger_.info("RAR found in slot %u (Total: %u)", slot_number, rar_count_);
    return true;
  }
  return false;
}

bool RARDecoder::search_rar_in_slot(cf_t *data_buffer, uint32_t slot_number) {
  // Initialize UE DL
  srsran_ue_dl_nr_t ue_dl = {};
  cf_t *buffer = srsran_vec_cf_malloc(slot_len_);
  if (!init_ue_dl(ue_dl, buffer)) {
    logger_.error("Failed to init UE DL");
    free(buffer);
    return false;
  }

  // Copy samples to processing buffer
  srsran_vec_cf_copy(buffer, data_buffer, slot_len_);

  // Initialize slot configuration
  srsran_slot_cfg_t slot_cfg = {.idx = slot_number};

  // Run FFT to get OFDM symbols
  srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);

  bool rar_found = false;

  // Try each RA-RNTI in the list
  for (uint16_t ra_rnti : config_.ra_rnti_list) {
    logger_.debug("Searching for RA-RNTI: 0x%04x (%u)", ra_rnti, ra_rnti);

    // Clear previous grants
    phy_state_.clear_pending_grants();

    // Search for DCI with this RA-RNTI
    ue_dl_dci_search(ue_dl, slot_cfg, ra_rnti, srsran_rnti_type_ra);

    // Check if we found a grant
    uint32_t pid = 0;
    srsran_sch_cfg_nr_t pdsch_cfg = {};
    srsran_harq_ack_resource_t ack_resource = {};

    if (!phy_state_.get_dl_pending_grant(slot_cfg.idx, pdsch_cfg, ack_resource, pid)) {
      logger_.debug("No grant found for RA-RNTI 0x%04x", ra_rnti);
      continue;
    }

    logger_.info("Found DCI for RA-RNTI 0x%04x, attempting PDSCH decode", ra_rnti);

    // Initialize buffer for decoded data
    srsran::unique_byte_buffer_t data = srsran::make_byte_buffer();
    if (data == nullptr) {
      logger_.error("Error creating byte buffer");
      continue;
    }
    data->N_bytes = pdsch_cfg.grant.tb[0].tbs / 8U;

    // Initialize PDSCH result
    srsran_pdsch_res_nr_t pdsch_res = {};
    pdsch_res.tb[0].payload = data->msg;

    // Initialize softbuffer
    srsran_softbuffer_rx_t softbuffer_rx = {};
    if (srsran_softbuffer_rx_init_guru(&softbuffer_rx,
                                       SRSRAN_SCH_NR_MAX_NOF_CB_LDPC,
                                       SRSRAN_LDPC_MAX_LEN_ENCODED_CB) != 0) {
      logger_.error("Couldn't allocate softbuffer");
      continue;
    }

    // Decode PDSCH
    if (!ue_dl_pdsch_decode(ue_dl, pdsch_cfg, slot_cfg, pdsch_res, softbuffer_rx)) {
      srsran_softbuffer_rx_free(&softbuffer_rx);
      continue;
    }

    // Check CRC
    if (!pdsch_res.tb[0].crc) {
      logger_.debug("PDSCH CRC failed for RA-RNTI 0x%04x", ra_rnti);
      srsran_softbuffer_rx_free(&softbuffer_rx);
      continue;
    }

    logger_.info("Successfully decoded PDSCH for RA-RNTI 0x%04x", ra_rnti);

    // Process RAR PDU (store decoded grants)
    if (process_rar_pdu(data->msg, data->N_bytes, ra_rnti, slot_number)) {
      rar_found = true;

      // Save to file
      std::string filename = "rar_ra_rnti_" + std::to_string(ra_rnti) +
                             "_slot_" + std::to_string(slot_number) + ".bin";
      std::ofstream rar_file(filename, std::ios::binary);
      if (rar_file) {
        rar_file.write(reinterpret_cast<char *>(data->msg), data->N_bytes);
        logger_.info("Saved RAR to %s", filename.c_str());
      }
    }

    srsran_softbuffer_rx_free(&softbuffer_rx);

    if (rar_found) {
      break;
    }
  }

  // Cleanup
  srsran_ue_dl_nr_free(&ue_dl);
  free(buffer);

  return rar_found;
}

bool RARDecoder::init_ue_dl(srsran_ue_dl_nr_t &ue_dl, cf_t *buffer) {
  srsran_ue_dl_nr_args_t ue_dl_args = {};
  ue_dl_args.nof_max_prb = phy_cfg_.carrier.nof_prb;
  ue_dl_args.nof_rx_antennas = config_.nof_rx_antennas;
  ue_dl_args.pdcch.measure_evm = false;
  ue_dl_args.pdcch.measure_time = false;
  ue_dl_args.pdcch.disable_simd = false;
  ue_dl_args.pdsch.sch.disable_simd = false;
  ue_dl_args.pdsch.sch.decoder_use_flooded = false;
  ue_dl_args.pdsch.sch.decoder_scaling_factor = 0;
  ue_dl_args.pdsch.sch.max_nof_iter = 10;

  std::array<cf_t *, SRSRAN_MAX_PORTS> rx_buffer = {};
  rx_buffer[0] = buffer;

  if (srsran_ue_dl_nr_init(&ue_dl, rx_buffer.data(), &ue_dl_args) != 0) {
    return false;
  }

  if (srsran_ue_dl_nr_set_carrier(&ue_dl, &phy_cfg_.carrier) != SRSRAN_SUCCESS) {
    return false;
  }

  srsran_dci_cfg_nr_t dci_cfg = phy_cfg_.get_dci_cfg();
  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl, &phy_cfg_.pdcch, &dci_cfg) !=
      SRSRAN_SUCCESS) {
    return false;
  }

  return true;
}

bool RARDecoder::update_ue_dl(srsran_ue_dl_nr_t &ue_dl) {
  if (srsran_ue_dl_nr_set_carrier(&ue_dl, &phy_cfg_.carrier) != SRSRAN_SUCCESS) {
    return false;
  }
  srsran_dci_cfg_nr_t dci_cfg = phy_cfg_.get_dci_cfg();
  if (srsran_ue_dl_nr_set_pdcch_config(&ue_dl, &phy_cfg_.pdcch, &dci_cfg) !=
      SRSRAN_SUCCESS) {
    return false;
  }
  return true;
}

void RARDecoder::ue_dl_dci_search(srsran_ue_dl_nr_t &ue_dl,
                                  srsran_slot_cfg_t &slot_cfg, uint16_t rnti,
                                  srsran_rnti_type_t rnti_type) {
  char dci_str[256];

  // Estimate PDCCH channel for every configured CORESET
  for (uint32_t i = 0; i < SRSRAN_UE_DL_NR_MAX_NOF_CORESET; i++) {
    if (ue_dl.cfg.coreset_present[i]) {
      srsran_dmrs_pdcch_estimate(&ue_dl.dmrs_pdcch[i], &slot_cfg,
                                 ue_dl.sf_symbols[0]);
    }
  }

  // Search for DL DCI
  std::array<srsran_dci_dl_nr_t, SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR>
      dci_dl = {};
  int num_dci_dl =
      srsran_ue_dl_nr_find_dl_dci(&ue_dl, &slot_cfg, rnti, rnti_type,
                                  dci_dl.data(), (uint32_t)dci_dl.size());

  for (int i = 0; i < num_dci_dl; i++) {
    phy_state_.set_dl_pending_grant(phy_cfg_, slot_cfg, dci_dl[i]);
    if (logger_.debug.enabled()) {
      srsran_dci_dl_nr_to_str(&ue_dl.dci, &dci_dl[i], dci_str, 256);
      logger_.debug("DCI DL slot %u: %s", slot_cfg.idx, dci_str);
    }
  }

  // Search for UL DCI
  std::array<srsran_dci_ul_nr_t, SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR>
      dci_ul = {};
  int num_dci_ul =
      srsran_ue_dl_nr_find_ul_dci(&ue_dl, &slot_cfg, rnti, rnti_type,
                                  dci_ul.data(), (uint32_t)dci_ul.size());

  for (int i = 0; i < num_dci_ul; i++) {
    phy_state_.set_ul_pending_grant(phy_cfg_, slot_cfg, dci_ul[i]);
    if (logger_.debug.enabled()) {
      srsran_dci_ul_nr_to_str(&ue_dl.dci, &dci_ul[i], dci_str, 256);
      logger_.debug("DCI UL slot %u: %s", slot_cfg.idx, dci_str);
    }
  }
}

bool RARDecoder::ue_dl_pdsch_decode(srsran_ue_dl_nr_t &ue_dl,
                                    srsran_sch_cfg_nr_t &pdsch_cfg,
                                    srsran_slot_cfg_t &slot_cfg,
                                    srsran_pdsch_res_nr_t &pdsch_res,
                                    srsran_softbuffer_rx_t &softbuffer_rx) {
  srsran_softbuffer_rx_reset(&softbuffer_rx);
  pdsch_cfg.grant.tb[0].softbuffer.rx = &softbuffer_rx;

  if (srsran_ue_dl_nr_decode_pdsch(&ue_dl, &slot_cfg, &pdsch_cfg, &pdsch_res) != 0) {
    logger_.error("Error srsran_ue_dl_nr_decode_pdsch");
    return false;
  }

  if (logger_.debug.enabled()) {
    char str[256];
    srsran_ue_dl_nr_pdsch_info(&ue_dl, &pdsch_cfg, &pdsch_res, str, 256);
    logger_.debug("PDSCH slot %u: %s", slot_cfg.idx, str);
  }

  return true;
}

bool RARDecoder::process_rar_pdu(uint8_t *data, uint32_t len, uint16_t ra_rnti, uint32_t slot_number) {
  srsran::mac_rar_pdu_nr rar_pdu;

  if (!rar_pdu.unpack(data, len)) {
    logger_.error("Error decoding RAR PDU");
    return false;
  }

  uint32_t num_subpdus = rar_pdu.get_num_subpdus();
  if (num_subpdus == 0) {
    logger_.warning("No subPDUs in RAR");
    return false;
  }

  logger_.info("RAR PDU for RA-RNTI 0x%04x contains %u subPDU(s)", ra_rnti, num_subpdus);

  for (uint32_t i = 0; i < num_subpdus; i++) {
    const srsran::mac_rar_subpdu_nr subpdu = rar_pdu.get_subpdu(i);

    if (subpdu.has_rapid()) {
      uint16_t tc_rnti = subpdu.get_temp_crnti();
      uint32_t ta = subpdu.get_ta();
      std::array<uint8_t, srsran::mac_rar_subpdu_nr::UL_GRANT_NBITS> ul_grant = subpdu.get_ul_grant();

      logger_.info("  SubPDU %u:", i);
      logger_.info("    RAPID:     %u", subpdu.get_rapid());
      logger_.info("    TC-RNTI:   0x%04x (%u)", tc_rnti, tc_rnti);
      logger_.info("    TA:        %u", ta);
      logger_.info("    UL Grant:  %s", buffer_to_hex_string(ul_grant.data(), ul_grant.size()).c_str());

      uint32_t scs_common_khz = 15 << static_cast<int>(srsran_subcarrier_spacing_15kHz);
      double Tc = 1.0 / (480e3 * 4096);
      uint32_t n_timing_advance = ta * 16 * 64 / (1 << scs_common_khz) + 0;
      double ta_time = static_cast<double>(n_timing_advance) * Tc;

      logger_.info("    TA (time): %.6f us", ta_time * 1e6);
      
      // Store decoded RAR grant
      rar_grant_t grant;
      grant.slot_number = slot_number;
      grant.ra_rnti = ra_rnti;
      grant.rapid = subpdu.get_rapid();
      grant.tc_rnti = tc_rnti;
      grant.ta = ta;
      grant.ta_time_us = ta_time * 1e6;
      grant.ul_grant = ul_grant;
      
      // Get current timestamp
      auto now = std::chrono::system_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
      grant.timestamp_ms = ms.count();
      
      rar_grants_.push_back(grant);
      logger_.debug("Stored RAR grant: TC-RNTI=0x%04x, total grants=%zu", tc_rnti, rar_grants_.size());
      
    } else if (subpdu.has_backoff()) {
      logger_.info("  SubPDU %u: Backoff indicator = %u", i, subpdu.get_backoff());
    }
  }

  return true;
}

void RARDecoder::write_record_to_file(cf_t *buffer, uint32_t length,
                                      const char *name) {
  char filename[256];
  snprintf(filename, sizeof(filename), "records/%s.fc32", name);
  std::ofstream f(filename, std::ios::binary);
  if (f) {
    f.write(reinterpret_cast<char *>(buffer), length * sizeof(cf_t));
    f.close();
    logger_.info("Wrote %u samples to %s", length, filename);
  } else {
    logger_.error("Error opening file: %s", filename);
  }
}

std::string RARDecoder::buffer_to_hex_string(const uint8_t *buffer,
                                             uint32_t len) {
  std::ostringstream oss;
  for (uint32_t i = 0; i < len; i++) {
    if (i == len - 1) {
      oss << "0x" << std::setfill('0') << std::setw(2) << std::hex
          << static_cast<int>(buffer[i]);
    } else {
      oss << "0x" << std::setfill('0') << std::setw(2) << std::hex
          << static_cast<int>(buffer[i]) << ", ";
    }
  }
  return oss.str();
}
