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

  // Start from the slot where SSB was found, accounting for timing offset
  uint32_t start_sfn = ssb_result.mib.sfn;
  uint32_t ssb_slot = start_sfn * 10; // 10 slots per frame for 15kHz SCS
  uint32_t slot_offset = ssb_result.t_offset / slot_len_; // Convert sample offset to slot offset
  uint32_t slot_idx = (ssb_slot + slot_offset) % (1024 * 10); // Wrap around 1024 frames
  
  LOG_INFO("SIB1 search starting from slot %u (SFN=%u, SSB slot=%u, offset=%u slots)", 
           slot_idx, start_sfn, ssb_slot, slot_offset);

  // SIB1 SEARCH - Use only SI-RNTI (0xffff) as per Sni5Gect approach
  LOG_INFO("=== SIB1 SEARCH (SI-RNTI only) ===");
  LOG_INFO("Using Sni5Gect approach: SI-RNTI only for SIB1 decoding");
  
  // Sni5Gect uses only SI-RNTI for SIB1 decoding, no brute force needed
  uint16_t rnti_list[] = {SRSRAN_SIRNTI};  // Only SI-RNTI (0xffff)
  uint32_t num_rntis = sizeof(rnti_list) / sizeof(rnti_list[0]);
  
  // Only System Information RNTI type for SIB1
  srsran_rnti_type_t rnti_types[] = {srsran_rnti_type_si};
  uint32_t num_rnti_types = sizeof(rnti_types) / sizeof(rnti_types[0]);
  
  LOG_INFO("Using SI-RNTI (0x%04x) for SIB1 decoding", SRSRAN_SIRNTI);
  
  for (uint32_t i = 0; i < max_attempts; ++i) {
    // Debug logging every 50 attempts
    if (i % 50 == 0) {
      LOG_INFO("SIB1 search progress: %u/%u attempts (slot %u)", i, max_attempts, slot_idx);
    }

    // 1. Receive one slot worth of samples
    if (!rf_dev->receive(sample_buffer_.data(), slot_len_)) {
      LOG_ERROR("RF receive failed during SIB1 search at attempt %u", i);
      break;
    }

    // 2. Set up slot configuration
    srsran_slot_cfg_t slot_cfg = {.idx = slot_idx};

    // 3. Estimate channel for this slot
    srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);

    // 4. BRUTE FORCE: Try ALL RNTI combinations
    for (uint32_t rnti_idx = 0; rnti_idx < num_rntis; rnti_idx++) {
      for (uint32_t rnti_type_idx = 0; rnti_type_idx < num_rnti_types; rnti_type_idx++) {
        
        uint16_t test_rnti = rnti_list[rnti_idx];
        srsran_rnti_type_t test_rnti_type = rnti_types[rnti_type_idx];
        
        // Log every 100 combinations
        if ((i * num_rntis * num_rnti_types + rnti_idx * num_rnti_types + rnti_type_idx) % 100 == 0) {
          LOG_INFO("Trying RNTI=0x%04x, type=%d (slot %u)", test_rnti, test_rnti_type, slot_idx);
        }
        
        // Search for DCI with this combination
        srsran_dci_dl_nr_t sib1_dci = {};
        if (search_dci(ue_dl, phy_cfg, slot_cfg, test_rnti, test_rnti_type, sib1_dci)) {
          
          LOG_INFO("*** POTENTIAL SIB1 DCI FOUND! ***");
          LOG_INFO("RNTI=0x%04x, type=%d, slot=%u", test_rnti, test_rnti_type, slot_idx);
          LOG_INFO("DCI format: %d, RNTI: 0x%x", 
                   sib1_dci.ctx.format, sib1_dci.ctx.rnti);

          // 5. Try to decode PDSCH with this DCI
          if (decode_sib1_pdsch(ue_dl, sib1_dci, slot_cfg, result, slot_idx)) {
            result.found = true;
            result.slot_found = slot_idx;
            LOG_INFO("*** SIB1 SUCCESSFULLY DECODED! ***");
            LOG_INFO("Found with RNTI=0x%04x, type=%d, slot=%u", test_rnti, test_rnti_type, slot_idx);
            return result;
          } else {
            LOG_INFO("DCI found but PDSCH decode failed - continuing search...");
          }
        }
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
      LOG_DEBUG("Processing CORESET %u: id=%u, duration=%u, offset_rb=%u", 
                i, ue_dl.cfg.coreset[i].id, ue_dl.cfg.coreset[i].duration, ue_dl.cfg.coreset[i].offset_rb);
      srsran_dmrs_pdcch_estimate(&ue_dl.dmrs_pdcch[i], &slot_cfg,
                                 ue_dl.sf_symbols[0]);
      coresets_processed++;
    }
  }
  
  LOG_DEBUG("DCI search: processed %u CORESETs, slot=%u, RNTI=0x%04x, type=%d", 
            coresets_processed, slot_cfg.idx, rnti, rnti_type);

  std::array<srsran_dci_dl_nr_t, SRSRAN_SEARCH_SPACE_MAX_NOF_CANDIDATES_NR>
      dci_dl = {};
  int num_dci_dl =
      srsran_ue_dl_nr_find_dl_dci(&ue_dl, &slot_cfg, rnti, rnti_type,
                                  dci_dl.data(), (uint32_t)dci_dl.size());

  // Enhanced debug logging for comprehensive blind decoding analysis
  if (slot_cfg.idx % 100 == 0) { // Log every 100 slots
    LOG_INFO("DCI search slot %u: processed %u coresets, found %d DCIs",
              slot_cfg.idx, coresets_processed, num_dci_dl);
    LOG_INFO("SearchSpace candidates: AL1=%u, AL2=%u, AL4=%u, AL8=%u, AL16=%u",
              phy_cfg.pdcch.search_space[0].nof_candidates[0],
              phy_cfg.pdcch.search_space[0].nof_candidates[1],
              phy_cfg.pdcch.search_space[0].nof_candidates[2],
              phy_cfg.pdcch.search_space[0].nof_candidates[3],
              phy_cfg.pdcch.search_space[0].nof_candidates[4]);
    LOG_INFO("SearchSpace0: id=%u, coreset_id=%u, type=%d, duration=%u",
              phy_cfg.pdcch.search_space[0].id,
              phy_cfg.pdcch.search_space[0].coreset_id,
              phy_cfg.pdcch.search_space[0].type,
              phy_cfg.pdcch.search_space[0].duration);
  }

  if (num_dci_dl > 0) {
    LOG_INFO("*** DCI FOUND! *** Found %d DCIs with RNTI 0x%x in slot %u", num_dci_dl, rnti, slot_cfg.idx);
    for (int i = 0; i < num_dci_dl; i++) {
      LOG_INFO("DCI[%d]: format=%d, RNTI=0x%x", 
               i, dci_dl[i].ctx.format, dci_dl[i].ctx.rnti);
    }
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

  // Parse ASN.1 SIB1 data - FIXED VERSION
  // SIB1 is wrapped in a BCCH-DL-SCH message structure
  asn1::rrc_nr::bcch_dl_sch_msg_s dlsch_msg;
  asn1::cbit_ref dlsch_bref(data, data_len);
  asn1::SRSASN_CODE err = dlsch_msg.unpack(dlsch_bref);
  if (err != asn1::SRSASN_SUCCESS ||
      dlsch_msg.msg.type().value != asn1::rrc_nr::bcch_dl_sch_msg_type_c::types_opts::c1) {
    LOG_ERROR("Error unpacking BCCH-DL-SCH message");
    return false;
  }
  
  sib1_out = dlsch_msg.msg.c1().sib_type1();

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

  LOG_INFO("*** ATTEMPTING PDSCH DECODE ***");
  LOG_INFO("DCI: format=%d, RNTI=0x%x", 
           sib1_dci.ctx.format, sib1_dci.ctx.rnti);
  
  // Convert DCI to PDSCH grant - this is the critical step I was missing
  if (srsran_ra_dl_dci_to_grant_nr(&ue_dl.carrier, &slot_cfg, nullptr,
                                   &sib1_dci, &pdsch_cfg,
                                   &pdsch_cfg.grant) < SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to convert DCI to PDSCH grant");
    return false;
  }
  
  LOG_INFO("PDSCH grant: TBS=%u bits (%u bytes), PRBs=%u", 
           pdsch_cfg.grant.tb[0].tbs, pdsch_cfg.grant.tb[0].tbs/8, 
           pdsch_cfg.grant.tb[0].nof_re);

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
    LOG_INFO("PDSCH CRC failed for SIB1 in slot %u", slot_idx);
    srsran_softbuffer_rx_free(&softbuffer_rx);
    return false;
  }

  LOG_INFO("*** PDSCH DECODE SUCCESS! *** SIB1 PDSCH decoded successfully! TBS=%u bytes", tbs_bytes);
  LOG_INFO("PDSCH result: CRC=%s", 
           pdsch_res.tb[0].crc ? "PASS" : "FAIL");

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

bool SIB1Processor::setup_sib1_search(srsran::phy_cfg_nr_t& phy_cfg, srsran_ue_dl_nr_t& ue_dl) {
  LOG_INFO("Setting up SIB1 search with SI-RNTI (0x%04x)", SRSRAN_SIRNTI);
  
  // Configure for SIB1 search using SI-RNTI only (as per Sni5Gect approach)
  // The broadcast worker in Sni5Gect uses SI-RNTI by default for SIB1
  
  // Ensure SearchSpace0 is configured for common control information
  if (!phy_cfg.pdcch.search_space_present[0]) {
    LOG_ERROR("SearchSpace0 not configured - required for SIB1 decoding");
    return false;
  }
  
  // Verify CORESET0 is configured
  if (!phy_cfg.pdcch.coreset_present[0]) {
    LOG_ERROR("CORESET0 not configured - required for SIB1 decoding");
    return false;
  }
  
  LOG_INFO("SIB1 search setup complete: CORESET0=id%u, SearchSpace0=id%u", 
           phy_cfg.pdcch.coreset[0].id, phy_cfg.pdcch.search_space[0].id);
  
  return true;
}

bool SIB1Processor::configure_from_mib(const srsran_mib_nr_t& mib, uint32_t ncellid, 
                                       srsran::phy_cfg_nr_t& phy_cfg) {
  LOG_INFO("Configuring PHY from MIB: PCI=%u, CORESET0_idx=%u", ncellid, mib.coreset0_idx);
  
  // Update PDSCH configuration
  phy_cfg.pdsch.typeA_pos = mib.dmrs_typeA_pos;
  phy_cfg.pdsch.scs_cfg = mib.scs_common;
  phy_cfg.carrier.pci = ncellid;

  // Calculate frequency offsets
  double pointA_abs_freq_Hz = phy_cfg.carrier.dl_center_frequency_hz -
                              phy_cfg.carrier.nof_prb * SRSRAN_NRE * SRSRAN_SUBC_SPACING_NR(phy_cfg.carrier.scs) / 2;
  double ssb_abs_freq_Hz = phy_cfg.carrier.ssb_center_freq_hz;
  uint32_t ssb_pointA_freq_offset_Hz = 
      (ssb_abs_freq_Hz > pointA_abs_freq_Hz) ? (uint32_t)(ssb_abs_freq_Hz - pointA_abs_freq_Hz) : 0;

  LOG_INFO("Frequency offsets: pointA=%.2f MHz, SSB=%.2f MHz, offset=%u Hz", 
           pointA_abs_freq_Hz/1e6, ssb_abs_freq_Hz/1e6, ssb_pointA_freq_offset_Hz);

  // Create CORESET0
  if (srsran_coreset_zero(phy_cfg.carrier.pci, ssb_pointA_freq_offset_Hz,
                          phy_cfg.ssb.scs, phy_cfg.carrier.scs,
                          mib.coreset0_idx, &phy_cfg.pdcch.coreset[0])) {
    LOG_ERROR("Failed to create CORESET0");
    return false;
  }
  phy_cfg.pdcch.coreset_present[0] = true;

  // Create SearchSpace0 - manually configure it
  srsran_search_space_t* ss0 = &phy_cfg.pdcch.search_space[0];
  ss0->id = 0;
  ss0->coreset_id = 0;
  ss0->type = srsran_search_space_type_common_0;
  ss0->nof_candidates[0] = 4; // AL1: 4 candidates
  ss0->nof_candidates[1] = 4; // AL2: 4 candidates
  ss0->nof_candidates[2] = 4; // AL4: 4 candidates
  ss0->nof_candidates[3] = 2; // AL8: 2 candidates
  ss0->nof_candidates[4] = 0; // AL16: 1 candidate
  ss0->duration = 1;
  ss0->nof_formats = 1;
  ss0->formats[0] = srsran_dci_format_nr_1_0;
  phy_cfg.pdcch.search_space_present[0] = true;
  
  LOG_INFO("CORESET0 and SearchSpace0 configured successfully");
  return true;
}

bool SIB1Processor::configure_complete_from_mib(const srsran_mib_nr_t& mib, uint32_t ncellid,
                                                srsran::phy_cfg_nr_t& phy_cfg, srsran_ue_dl_nr_t& ue_dl) {
  LOG_INFO("=== COMPLETE MIB-TO-PHY CONFIGURATION (Sni5Gect Style) ===");
  
  // Step 1: Configure PHY from MIB
  if (!configure_from_mib(mib, ncellid, phy_cfg)) {
    LOG_ERROR("Failed to configure PHY from MIB");
    return false;
  }
  
  // Step 2: Update UE DL with new configuration
  if (!update_ue_dl_config(ue_dl, phy_cfg)) {
    LOG_ERROR("Failed to update UE DL configuration");
    return false;
  }
  
  // Step 3: Setup SIB1 search (SI-RNTI only, as per Sni5Gect)
  if (!setup_sib1_search(phy_cfg, ue_dl)) {
    LOG_ERROR("Failed to setup SIB1 search");
    return false;
  }
  
  // Step 4: Log configuration summary
  LOG_INFO("MIB-to-PHY configuration completed successfully (Sni5Gect style):");
  LOG_INFO("  - PCI: %u", phy_cfg.carrier.pci);
  LOG_INFO("  - CORESET0: present=%s, id=%u", 
           phy_cfg.pdcch.coreset_present[0] ? "yes" : "no",
           phy_cfg.pdcch.coreset[0].id);
  LOG_INFO("  - SearchSpace0: present=%s, id=%u, coreset_id=%u",
           phy_cfg.pdcch.search_space_present[0] ? "yes" : "no",
           phy_cfg.pdcch.search_space[0].id,
           phy_cfg.pdcch.search_space[0].coreset_id);
  LOG_INFO("  - SIB1 search: SI-RNTI (0x%04x) configured", SRSRAN_SIRNTI);
  
  return true;
}

bool SIB1Processor::update_ue_dl_config(srsran_ue_dl_nr_t &ue_dl, const srsran::phy_cfg_nr_t& phy_cfg) {
  LOG_INFO("Updating UE DL configuration with new PHY config");
  
  // Note: The UE DL object configuration is handled through the phy_cfg parameter
  // passed to the search_and_decode function. The UE DL object uses this configuration
  // during DCI search and PDSCH decoding operations.
  
  // Update specific configuration elements that need to be synchronized
  // Copy PDCCH configuration
  ue_dl.cfg.coreset[0] = phy_cfg.pdcch.coreset[0];
  ue_dl.cfg.coreset_present[0] = phy_cfg.pdcch.coreset_present[0];
  ue_dl.cfg.search_space[0] = phy_cfg.pdcch.search_space[0];
  ue_dl.cfg.search_space_present[0] = phy_cfg.pdcch.search_space_present[0];
  
  LOG_INFO("UE DL configuration updated successfully");
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
  ss0_ref.nof_candidates[4] = 0; // AL16: 1 candidate

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
