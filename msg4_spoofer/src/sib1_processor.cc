/**
 * SIB1 Processor Implementation
 *
 * This class handles the complete SIB1 (System Information Block Type 1)
 * decoding process including DCI search, PDSCH decoding, and ASN.1 parsing.
 */

#include "sib1_processor.h"
#include "logging.h"
#include "srsran/common/byte_buffer.h"
#include <array>
#include <cstring>

SIB1Processor::SIB1Processor(double srate_hz, uint32_t nof_prb, uint32_t pci,
                             double freq_hz)
    : srate_hz_(srate_hz), nof_prb_(nof_prb), pci_(pci), freq_hz_(freq_hz),
      initialized_(false) {
  // Calculate samples per slot (1ms for 15kHz SCS)
  slot_len_ = static_cast<uint32_t>(srate_hz_ * 0.001);
}

SIB1Processor::~SIB1Processor() {
  // Cleanup is handled automatically by RAII
}

bool SIB1Processor::init() {
  // Initialize sample buffer
  sample_buffer_.resize(slot_len_);

  LOG_INFO("SIB1 Processor initialized (srate=%.2f MHz, PRBs=%u, PCI=%u)",
           srate_hz_ / 1e6, nof_prb_, pci_);

  initialized_ = true;
  return true;
}

SIB1SearchResult SIB1Processor::search_and_decode(
    RFBase *rf_dev, srsran_ue_dl_nr_t &ue_dl, srsran::phy_cfg_nr_t &phy_cfg,
    const SsbSearchResult &ssb_result, uint32_t max_attempts) {

  SIB1SearchResult result = {};
  result.found = false;

  if (!initialized_) {
    LOG_ERROR("SIB1 Processor not initialized");
    return result;
  }

  LOG_INFO("Starting SIB1 search and decode (max_attempts=%u)...",
           max_attempts);

  uint32_t start_sfn = ssb_result.mib.sfn;
  uint32_t slot_idx = start_sfn * 10; // 10 slots per frame for 15kHz SCS

  for (uint32_t i = 0; i < max_attempts; ++i) {
    // Debug logging every 100 attempts
    if (i % 100 == 0) {
      LOG_INFO("SIB1 search progress: %u/%u attempts (slot %u)", i,
               max_attempts, slot_idx);
    }

    // 1. Receive one slot worth of samples
    if (!rf_dev->receive(
            reinterpret_cast<std::complex<float> *>(sample_buffer_.data()),
            slot_len_)) {
      LOG_ERROR("RF receive failed during SIB1 search at attempt %u", i);
      break;
    }

    // 2. Set up slot configuration
    srsran_slot_cfg_t slot_cfg = {.idx = slot_idx};

    // 3. Estimate channel for this slot
    srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);

    // 4. Search for SI-RNTI DCI (System Information RNTI)
    srsran_dci_dl_nr_t sib1_dci = {};
    if (search_dci(ue_dl, phy_cfg, slot_cfg, SRSRAN_SIRNTI, srsran_rnti_type_si,
                   sib1_dci)) {

      LOG_INFO(
          "SIB1 DCI found in slot %u (attempt %u)! Attempting PDSCH decode...",
          slot_idx, i);

      // 5. Decode PDSCH that carries SIB1 data
      if (decode_sib1_pdsch(ue_dl, sib1_dci, slot_cfg, result, slot_idx)) {
        result.found = true;
        result.slot_found = slot_idx;
        LOG_INFO("SIB1 successfully decoded in slot %u", slot_idx);
        return result;
      }
    }

    // Advance to the next slot
    slot_idx = (slot_idx + 1) % (1024 * 10); // 1024 SFN * 10 slots per frame
  }

  if (!result.found) {
    LOG_ERROR("Failed to decode SIB1 after %u attempts", max_attempts);
  }

  return result;
}

bool SIB1Processor::search_dci(srsran_ue_dl_nr_t &ue_dl,
                               srsran::phy_cfg_nr_t &phy_cfg,
                               srsran_slot_cfg_t &slot_cfg, uint16_t rnti,
                               srsran_rnti_type_t rnti_type,
                               srsran_dci_dl_nr_t &found_dci) {

  // Estimate PDCCH channel for every configured CORESET
  uint32_t coresets_processed = 0;
  for (uint32_t i = 0; i < SRSRAN_UE_DL_NR_MAX_NOF_CORESET; i++) {
    if (ue_dl.cfg.coreset_present[i]) {
      srsran_dmrs_pdcch_estimate(&ue_dl.dmrs_pdcch[i], &slot_cfg,
                                 ue_dl.sf_symbols[0]);
      coresets_processed++;
    }
  }

  std::array<srsran_dci_dl_nr_t, SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR>
      dci_dl = {};
  int num_dci_dl =
      srsran_ue_dl_nr_find_dl_dci(&ue_dl, &slot_cfg, rnti, rnti_type,
                                  dci_dl.data(), (uint32_t)dci_dl.size());

  // Enhanced debug logging for comprehensive blind decoding analysis
  if (slot_cfg.idx % 100 == 0) { // Log every 100 slots
    LOG_DEBUG("DCI search slot %u: processed %u coresets, found %d DCIs",
              slot_cfg.idx, coresets_processed, num_dci_dl);
    LOG_DEBUG("SearchSpace candidates: AL1=%u, AL2=%u, AL4=%u, AL8=%u, AL16=%u",
              phy_cfg.pdcch.search_space[0].nof_candidates[0],
              phy_cfg.pdcch.search_space[0].nof_candidates[1],
              phy_cfg.pdcch.search_space[0].nof_candidates[2],
              phy_cfg.pdcch.search_space[0].nof_candidates[3],
              phy_cfg.pdcch.search_space[0].nof_candidates[4]);
  }

  if (num_dci_dl > 0) {
    LOG_INFO("Found %d DCIs with RA-RNTI 0x%x in slot %u", num_dci_dl, rnti,
             slot_cfg.idx);
    found_dci = dci_dl[0];
    return true;
  }

  return false;
}

bool SIB1Processor::decode_pdsch(srsran_ue_dl_nr_t &ue_dl,
                                 srsran_sch_cfg_nr_t &pdsch_cfg,
                                 srsran_slot_cfg_t &slot_cfg,
                                 srsran_pdsch_res_nr_t &pdsch_res,
                                 srsran_softbuffer_rx_t &softbuffer_rx) {

  // Initialize soft buffer
  srsran_softbuffer_rx_reset(&softbuffer_rx);
  pdsch_cfg.grant.tb[0].softbuffer.rx = &softbuffer_rx;

  // Call srsRAN API to decode PDSCH message
  if (srsran_ue_dl_nr_decode_pdsch(&ue_dl, &slot_cfg, &pdsch_cfg, &pdsch_res) !=
      0) {
    LOG_ERROR("Error in srsran_ue_dl_nr_decode_pdsch");
    return false;
  }

  return true;
}

bool SIB1Processor::parse_sib1_asn1(const uint8_t *data, uint32_t data_len,
                                    asn1::rrc_nr::sib1_s &sib1_out,
                                    std::string &json_out) {

  // Parse ASN.1 SIB1 data
  asn1::cbit_ref bref(data, data_len);
  if (sib1_out.unpack(bref) != asn1::SRSASN_SUCCESS) {
    LOG_ERROR("Failed to unpack SIB1 ASN.1 data");
    return false;
  }

  // Convert to JSON for logging/debugging
  asn1::json_writer json_writer;
  sib1_out.to_json(json_writer);
  json_out = json_writer.to_string();

  return true;
}

bool SIB1Processor::decode_sib1_pdsch(srsran_ue_dl_nr_t &ue_dl,
                                      const srsran_dci_dl_nr_t &sib1_dci,
                                      srsran_slot_cfg_t &slot_cfg,
                                      SIB1SearchResult &result,
                                      uint32_t slot_idx) {

  // Convert DCI to PDSCH configuration
  srsran_sch_cfg_nr_t pdsch_cfg = {};
  srsran_harq_ack_resource_t ack_resource = {};

  // Convert DCI to PDSCH grant - this is the critical step I was missing
  if (srsran_ra_dl_dci_to_grant_nr(&ue_dl.carrier, &slot_cfg, nullptr,
                                   &sib1_dci, &pdsch_cfg,
                                   &pdsch_cfg.grant) < SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to convert DCI to PDSCH grant");
    return false;
  }

  // Initialize the buffer for SIB1 data
  uint32_t tbs_bytes = pdsch_cfg.grant.tb[0].tbs / 8U;
  std::vector<uint8_t> sib1_data(tbs_bytes);

  // Initialize PDSCH result
  srsran_pdsch_res_nr_t pdsch_res = {};
  pdsch_res.tb[0].payload = sib1_data.data();

  // Initialize soft buffer
  srsran_softbuffer_rx_t softbuffer_rx = {};
  if (srsran_softbuffer_rx_init_guru(
          &softbuffer_rx, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC,
          SRSRAN_LDPC_MAX_LEN_ENCODED_CB) != SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to initialize soft buffer");
    return false;
  }

  // Reset soft buffer and link to PDSCH config
  srsran_softbuffer_rx_reset(&softbuffer_rx);
  pdsch_cfg.grant.tb[0].softbuffer.rx = &softbuffer_rx;

  // Decode PDSCH
  if (srsran_ue_dl_nr_decode_pdsch(&ue_dl, &slot_cfg, &pdsch_cfg, &pdsch_res) !=
      SRSRAN_SUCCESS) {
    LOG_ERROR("Error in srsran_ue_dl_nr_decode_pdsch");
    srsran_softbuffer_rx_free(&softbuffer_rx);
    return false;
  }

  // Check CRC
  if (!pdsch_res.tb[0].crc) {
    LOG_DEBUG("PDSCH CRC failed for SIB1 in slot %u", slot_idx);
    srsran_softbuffer_rx_free(&softbuffer_rx);
    return false;
  }

  LOG_INFO("SIB1 PDSCH decoded successfully! TBS=%u bytes", tbs_bytes);

  // Parse SIB1 ASN.1 data
  if (!parse_sib1_asn1(sib1_data.data(), tbs_bytes, result.sib1_data,
                       result.json_output)) {
    LOG_ERROR("Failed to parse SIB1 ASN.1 data");
    srsran_softbuffer_rx_free(&softbuffer_rx);
    return false;
  }

  LOG_INFO("SIB1 ASN.1 parsing successful");

  // Cleanup
  srsran_softbuffer_rx_free(&softbuffer_rx);

  return true;
}

bool SIB1Processor::configure_search_space_0(srsran_search_space_t &ss0_ref) {
  // Configure SearchSpace0 for comprehensive SIB1 blind decoding
  // Per 3GPP TS 38.213 Section 10.1, SearchSpace0 must try all ALs:
  // {1,2,4,8,16}

  ss0_ref.id = 0;
  ss0_ref.coreset_id = 0;
  ss0_ref.type = srsran_search_space_type_common_0;

  // CRITICAL: Include ALL aggregation levels for comprehensive blind decoding
  ss0_ref.nof_candidates[0] = 4; // AL1: 4 candidates
  ss0_ref.nof_candidates[1] = 4; // AL2: 4 candidates
  ss0_ref.nof_candidates[2] = 4; // AL4: 4 candidates
  ss0_ref.nof_candidates[3] = 2; // AL8: 2 candidates
  ss0_ref.nof_candidates[4] = 1; // AL16: 1 candidate

  ss0_ref.duration = 1;
  ss0_ref.nof_formats = 1;
  ss0_ref.formats[0] = srsran_dci_format_nr_1_0;

  // Store reference for internal use (though it points to the same location as
  // ss0_ref)
  ss0 = ss0_ref;

  // Log the comprehensive blind decoding configuration
  LOG_INFO("SIB1 SearchSpace0 configured for comprehensive blind decoding:");
  LOG_INFO("  - AL1: %u candidates, AL2: %u candidates, AL4: %u candidates",
           ss0_ref.nof_candidates[0], ss0_ref.nof_candidates[1],
           ss0_ref.nof_candidates[2]);
  LOG_INFO("  - AL8: %u candidates, AL16: %u candidates",
           ss0_ref.nof_candidates[3], ss0_ref.nof_candidates[4]);
  LOG_INFO("  - Total candidates per slot: %u",
           ss0_ref.nof_candidates[0] + ss0_ref.nof_candidates[1] +
               ss0_ref.nof_candidates[2] + ss0_ref.nof_candidates[3] +
               ss0_ref.nof_candidates[4]);

  return true;
}
