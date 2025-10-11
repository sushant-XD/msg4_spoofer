// msg2_decoder.cpp
#include "msg2_decoder.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include <cstring>

MSG2Decoder::MSG2Decoder( 
                         uint32_t sample_rate_, 
                         uint32_t nof_prb_, 
                         uint32_t pci_,
                         double carrier_freq_) :
    sample_rate(sample_rate_),
    nof_prb(nof_prb_),
    pci(pci_),
    carrier_freq(carrier_freq_),
    buffer(nullptr)
{
    // Initialize structures to zero
    memset(&ue_dl, 0, sizeof(ue_dl));
    memset(&softbuffer_rx, 0, sizeof(softbuffer_rx));
    
    // Calculate timing parameters
    sf_len = sample_rate * 0.001; // 1ms subframe
    slot_per_sf = 1; // For SCS 15kHz (numerology 0)
    slot_per_frame = 10; // 10 slots per frame for SCS 15kHz
    slot_len = sf_len / slot_per_sf;
}

MSG2Decoder::~MSG2Decoder() {
    if (buffer) {
        free(buffer);
    }
    srsran_ue_dl_nr_free(&ue_dl);
    srsran_softbuffer_rx_free(&softbuffer_rx);
}

bool MSG2Decoder::init() {
    // Allocate buffer
    buffer = srsran_vec_cf_malloc(sf_len);
    if (!buffer) {
        // /logger.error("Failed to allocate buffer");
        return false;
    }
    
    // Initialize PHY configuration with SIB1-like defaults
    init_phy_cfg_from_sib1();
    
    // Initialize UE DL
    srsran_ue_dl_nr_args_t ue_dl_args = {};
    ue_dl_args.nof_rx_antennas = 1;
    ue_dl_args.nof_max_prb = nof_prb;
    
    cf_t* input_ptrs[SRSRAN_MAX_PORTS] = {buffer, nullptr, nullptr, nullptr};
    if (srsran_ue_dl_nr_init(&ue_dl, input_ptrs, &ue_dl_args) < SRSRAN_SUCCESS) {
        // logger.error("Failed to initialize ue_dl_nr");
        return false;
    }
    
    // Set carrier configuration
    srsran_carrier_nr_t carrier = {};
    carrier.nof_prb = nof_prb;
    carrier.max_mimo_layers = 1;
    carrier.pci = pci;
    carrier.scs = srsran_subcarrier_spacing_15kHz;
    carrier.dl_center_frequency_hz = carrier_freq;
    
    if (srsran_ue_dl_nr_set_carrier(&ue_dl, &carrier) < SRSRAN_SUCCESS) {
        // logger.error("Failed to set carrier");
        return false;
    }
    
    // Initialize softbuffer
    if (srsran_softbuffer_rx_init_guru(&softbuffer_rx, 
                                      SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, 
                                      SRSRAN_LDPC_MAX_LEN_ENCODED_CB) != SRSRAN_SUCCESS) {
        // logger.error("Failed to initialize softbuffer");
        return false;
    }
    
    // Calculate RA-RNTI list based on PRACH configuration
    calculate_ra_rnti_list();
    
    // logger.info("MSG2 Decoder initialized with %d RA-RNTIs to monitor", ra_rnti_list.size());
    return true;
}

void MSG2Decoder::set_prach_config(const PrachConfig& cfg) {
    prach_cfg = cfg;
    calculate_ra_rnti_list();
}

std::vector<MSG2Result> MSG2Decoder::process_slot(cf_t* iq_samples, uint32_t slot_idx) {
    std::vector<MSG2Result> results;
    
    // Copy samples to internal buffer
    srsran_vec_cf_copy(buffer, iq_samples, slot_len);
    
    // Perform FFT
    srsran_slot_cfg_t slot_cfg = {.idx = slot_idx};
    srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);
    
    // Calculate frame and slot within frame
    uint32_t sfn = (slot_idx / slot_per_frame) % 1024;
    uint32_t slot_in_frame = slot_idx % slot_per_frame;
    
    // logger.debug("Processing SFN=%d, Slot=%d (absolute slot %d)", 
    //             sfn, slot_in_frame, slot_idx);
    
    // Try to decode with each RA-RNTI
    for (uint16_t ra_rnti : ra_rnti_list) {
        MSG2Result result = try_decode_msg2(slot_cfg, ra_rnti);
        if (result.valid) {
            result.sfn = sfn;
            result.slot_in_frame = slot_in_frame;
            // logger.info("MSG2 decoded! SFN=%d, Slot=%d, RA-RNTI: 0x%04x, TC-RNTI: 0x%04x, RAPID: %d", 
            //            sfn, slot_in_frame, result.ra_rnti, result.tc_rnti, result.rapid);
            results.push_back(result);
        }
    }
    
    return results;
}

std::vector<MSG2Result> MSG2Decoder::process_slot_with_prach_slot(cf_t* iq_samples, 
                                                                   uint32_t slot_idx, 
                                                                   uint32_t prach_slot_in_frame) {
    std::vector<MSG2Result> results;
    
    // Validate PRACH slot
    if (prach_slot_in_frame % 2 != 0) {
        // logger.warning("Invalid PRACH slot %d (must be even: 0,2,4,6,8)", prach_slot_in_frame);
        return results;
    }
    
    // Calculate the specific RA-RNTI for this PRACH slot
    uint16_t ra_rnti = 1 + 0 + 14 * prach_slot_in_frame;
    
    // Copy samples and perform FFT
    srsran_vec_cf_copy(buffer, iq_samples, slot_len);
    srsran_slot_cfg_t slot_cfg = {.idx = slot_idx};
    
    srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg);
    
    uint32_t sfn = (slot_idx / slot_per_frame) % 1024;
    uint32_t slot_in_frame = slot_idx % slot_per_frame;
    
    // logger.debug("Processing SFN=%d, Slot=%d with specific RA-RNTI=0x%04x (PRACH slot %d)", 
    //             sfn, slot_in_frame, ra_rnti, prach_slot_in_frame);
    
    // Only try to decode with the specific RA-RNTI
    MSG2Result result = try_decode_msg2(slot_cfg, ra_rnti);
    if (result.valid) {
        result.sfn = sfn;
        result.slot_in_frame = slot_in_frame;
        // logger.info("MSG2 decoded! SFN=%d, Slot=%d, RA-RNTI: 0x%04x, TC-RNTI: 0x%04x, RAPID: %d", 
        //            sfn, slot_in_frame, result.ra_rnti, result.tc_rnti, result.rapid);
        results.push_back(result);
    }
    
    return results;
}

void MSG2Decoder::init_phy_cfg_from_sib1() {
    // Initialize with default SIB1 configuration for PDCCH
    // This sets up CORESET0 and SearchSpace0 for SIB1/MSG2
    
    // CORESET 0 (Common CORESET for initial access)
    srsran_coreset_t& coreset0 = phy_cfg.pdcch.coreset[0];
    coreset0.id = 0;
    coreset0.duration = 1; // 1 symbol
    coreset0.mapping_type = srsran_coreset_mapping_type_non_interleaved;
    
    // Frequency allocation - all PRBs
    for (int i = 0; i < SRSRAN_CORESET_FREQ_DOMAIN_RES_SIZE; i++) {
        coreset0.freq_resources[i] = (i < (nof_prb / 6)) ? true : false;
    }
    
    coreset0.precoder_granularity = srsran_coreset_precoder_granularity_reg_bundle;
    phy_cfg.pdcch.coreset_present[0] = true;
    
    // Search Space 0 (for RA-RNTI)
    srsran_search_space_t& ss0 = phy_cfg.pdcch.search_space[0];
    ss0.id = 0;
    ss0.coreset_id = 0;
    ss0.type = srsran_search_space_type_common_1; // Type1-PDCCH for RA
    ss0.nof_candidates[0] = 0; // Aggregation level 1
    ss0.nof_candidates[1] = 0; // Aggregation level 2
    ss0.nof_candidates[2] = 4; // Aggregation level 4
    ss0.nof_candidates[3] = 2; // Aggregation level 8
    ss0.nof_candidates[4] = 1; // Aggregation level 16
    ss0.duration = 1;
    ss0.nof_formats = 1;
    ss0.formats[0] = srsran_dci_format_nr_1_0; // DCI format 1_0 for MSG2
    phy_cfg.pdcch.search_space_present[0] = true;
    
    // RA search space configuration
    phy_cfg.pdcch.ra_search_space_present = true;
    phy_cfg.pdcch.ra_search_space = ss0;
}

void MSG2Decoder::calculate_ra_rnti_list() {
    // RA-RNTI = 1 + s_id + 14 × t_id + 14 × 80 × f_id + 14 × 80 × 8 × ul_carrier_id
    // 
    // For your configuration:
    // - PRACH config_idx = 1 (Format 0, Preamble format 0)
    // - SCS = 15 kHz (numerology 0), so 10 slots per frame
    // - f_id = 0 (single PRACH frequency resource)
    // - ul_carrier_id = 0 (normal FDD/TDD operation)
    //
    // For config_idx = 1 in FR1 with 15kHz SCS:
    // - Format 0 occupies specific slots in a frame
    // - According to TS 38.211 Table 6.3.3.2-2 and 6.3.3.2-3
    // - Config index 1: PRACH in slot 0, 2, 4, 6, 8 (even slots)
    // - Starting symbol s_id depends on format (Format 0 starts at symbol 0)
    
    ra_rnti_list.clear();
    
    // For config_idx = 1 with 15kHz SCS:
    // PRACH occasions occur in even-numbered slots (0, 2, 4, 6, 8)
    std::vector<uint32_t> prach_slots = {0, 2, 4, 6, 8};
    
    // Format 0 starting symbol
    uint32_t s_id = 0; // PRACH Format 0 starts at symbol 0
    
    for (uint32_t t_id : prach_slots) {
        // RA-RNTI formula with f_id=0, ul_carrier_id=0
        uint16_t ra_rnti = 1 + s_id + 14 * t_id;
        ra_rnti_list.push_back(ra_rnti);
        // logger.debug("PRACH slot %d -> RA-RNTI: %d (0x%04x)", t_id, ra_rnti, ra_rnti);
    }
    
    // logger.info("Generated %d RA-RNTI values for config_idx=%d: %s", 
    //            ra_rnti_list.size(), 
    //            prach_cfg.config_idx,
    //            "1, 29, 57, 85, 113");
}

MSG2Result MSG2Decoder::try_decode_msg2(srsran_slot_cfg_t& slot_cfg, uint16_t ra_rnti) {
    MSG2Result result;
    result.ra_rnti = ra_rnti;
    result.slot_idx = slot_cfg.idx;
    
    // Search for DCI with this RA-RNTI
    srsran_dci_dl_nr_t dci_dl_list[SRSRAN_MAX_DCI_MSG_NR];
    int nof_dcis = srsran_ue_dl_nr_find_dl_dci(&ue_dl, 
                                                &slot_cfg, 
                                                ra_rnti, 
                                                srsran_rnti_type_ra, 
                                                dci_dl_list, 
                                                SRSRAN_MAX_DCI_MSG_NR);
    
    if (nof_dcis <= 0) {
        return result; // No DCI found
    }
    
    // Try to decode PDSCH for each DCI found
    for (int i = 0; i < nof_dcis; i++) {
        srsran_sch_cfg_nr_t pdsch_cfg = {};
        
        // Convert DCI to PDSCH config
        if (srsran_ra_dl_dci_to_grant_nr(&ue_dl.carrier,
                                         &slot_cfg,
                                         &phy_cfg.pdsch,
                                         &dci_dl_list[i],
                                         &pdsch_cfg,
                                         &pdsch_cfg.grant) < SRSRAN_SUCCESS) {
            continue;
        }
        
        // Decode PDSCH
        srsran_pdsch_res_nr_t pdsch_res = {};
        uint8_t data[SRSRAN_SLOT_MAX_NOF_BITS_NR / 8];
        pdsch_res.tb[0].payload = data;
        
        srsran_softbuffer_rx_reset(&softbuffer_rx);
        
        if (srsran_ue_dl_nr_decode_pdsch(&ue_dl, 
                                        &slot_cfg, 
                                        &pdsch_cfg, 
                                        &pdsch_res) < SRSRAN_SUCCESS) {
            continue;
        }
        
        // Check CRC
        if (!pdsch_res.tb[0].crc) {
            continue;
        }
        
        uint32_t data_len = pdsch_cfg.grant.tb[0].tbs / 8;
        
        // Parse MAC PDU to extract RAR
        if (parse_rar_pdu(data, data_len, result)) {
            result.valid = true;
            return result;
        }
    }
    
    return result;
}

bool MSG2Decoder::parse_rar_pdu(uint8_t* data, uint32_t len, MSG2Result& result) {
    // MAC RAR PDU structure:
    // - MAC subheader (1 byte): E/T/RAPID
    // - MAC RAR payload (7 bytes): R/TA(12)/UL_Grant(27)/TC-RNTI(16)
    
    if (len < 8) {
        return false; // Too short for RAR
    }
    
    uint32_t idx = 0;
    
    while (idx < len) {
        uint8_t subheader = data[idx++];
        uint8_t E = (subheader >> 7) & 0x01; // Extension
        uint8_t T = (subheader >> 6) & 0x01; // Type (0=RAR, 1=Backoff)
        
        if (T == 1) {
            // Backoff Indicator, skip
            continue;
        }
        
        // RAR subheader
        result.rapid = subheader & 0x3F; // RAPID (6 bits)
        
        if (idx + 6 > len) {
            return false;
        }
        
        // Parse RAR payload (7 bytes)
        // Byte 0: R(1) | TA[11:4] (7 bits)
        // Byte 1: TA[3:0] (4 bits) | UL_Grant[26:23] (4 bits)
        // Bytes 2-4: UL_Grant[22:0]
        // Bytes 5-6: TC-RNTI (16 bits)
        
        uint8_t rar[7];
        memcpy(rar, &data[idx], 7);
        idx += 7;
        
        // Extract Timing Advance (12 bits)
        result.timing_advance = ((rar[0] & 0x7F) << 4) | ((rar[1] >> 4) & 0x0F);
        
        // Extract UL Grant (27 bits)
        result.ul_grant = ((rar[1] & 0x0F) << 23) | 
                         (rar[2] << 15) | 
                         (rar[3] << 7) | 
                         (rar[4] >> 1);
        
        // Extract TC-RNTI (16 bits) - THIS IS WHAT WE NEED!
        result.tc_rnti = (rar[5] << 8) | rar[6];
        
        // logger.debug("Parsed RAR: RAPID=%d, TA=%d, TC-RNTI=0x%04x", 
        //             result.rapid, result.timing_advance, result.tc_rnti);
        
        return true;
    }
    
    return false;
}