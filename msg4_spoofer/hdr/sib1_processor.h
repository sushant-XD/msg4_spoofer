/**
 * SIB1 Processor Header
 * 
 * This class handles the complete SIB1 (System Information Block Type 1) 
 * decoding process including DCI search, PDSCH decoding, and ASN.1 parsing.
 */

#pragma once

#include "rf_base.h"
#include "ssb_decoder.h"
#include "logging.h"
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/phy/ue/ue_dl_nr.h"
#include "srsran/srsran.h"
#include "srsran/asn1/rrc_nr.h"
#include <memory>
#include <vector>

struct SIB1SearchResult {
  bool found;
  asn1::rrc_nr::sib1_s sib1_data;
  uint32_t slot_found;
  std::string json_output;
};

class SIB1Processor {
public:
  /**
   * Constructor
   * @param srate_hz Sample rate in Hz
   * @param nof_prb Number of Physical Resource Blocks
   * @param pci Physical Cell ID
   * @param freq_hz Center frequency in Hz
   */
  SIB1Processor(double srate_hz, uint32_t nof_prb, uint32_t pci, double freq_hz);
  
  /**
   * Destructor
   */
  ~SIB1Processor();

  /**
   * Initialize the SIB1 processor
   * @return true if initialization successful, false otherwise
   */
  bool init();

  /**
   * Search for and decode SIB1
   * @param rf_dev RF device instance for receiving samples
   * @param ue_dl Shared UE DL instance (pre-configured)
   * @param phy_cfg Physical layer configuration
   * @param ssb_result SSB search result containing MIB info
   * @param max_attempts Maximum number of slots to search (default: 1600)
   * @return SIB1SearchResult containing the search outcome
   */
  SIB1SearchResult search_and_decode(
      RFBase* rf_dev,
      srsran_ue_dl_nr_t& ue_dl,
      srsran::phy_cfg_nr_t& phy_cfg,
      const SsbSearchResult& ssb_result,
      uint32_t max_attempts = 1600);

private:
  // Configuration parameters
  double srate_hz_;
  uint32_t nof_prb_;
  uint32_t pci_;
  double freq_hz_;
  
  // Buffer for RF samples
  std::vector<cf_t> sample_buffer_;
  uint32_t slot_len_;
  
  // Initialization state
  bool initialized_;

  /**
   * Search for DCI (Downlink Control Information) in a slot
   * @param ue_dl UE DL instance
   * @param phy_cfg Physical layer configuration  
   * @param slot_cfg Slot configuration
   * @param rnti RNTI to search for
   * @param rnti_type Type of RNTI
   * @param found_dci Output DCI if found
   * @return true if DCI found, false otherwise
   */
  bool search_dci(srsran_ue_dl_nr_t& ue_dl,
                  srsran::phy_cfg_nr_t& phy_cfg,
                  srsran_slot_cfg_t& slot_cfg,
                  uint16_t rnti,
                  srsran_rnti_type_t rnti_type,
                  srsran_dci_dl_nr_t& found_dci);

  /**
   * Decode PDSCH (Physical Downlink Shared Channel) 
   * @param ue_dl UE DL instance
   * @param pdsch_cfg PDSCH configuration
   * @param slot_cfg Slot configuration
   * @param pdsch_res PDSCH decode result
   * @param softbuffer_rx Soft buffer for reception
   * @return true if decode successful, false otherwise
   */
  bool decode_pdsch(srsran_ue_dl_nr_t& ue_dl,
                   srsran_sch_cfg_nr_t& pdsch_cfg,
                   srsran_slot_cfg_t& slot_cfg,
                   srsran_pdsch_res_nr_t& pdsch_res,
                   srsran_softbuffer_rx_t& softbuffer_rx);

  /**
   * Parse SIB1 ASN.1 data
   * @param data Raw message data
   * @param data_len Length of data in bytes
   * @param sib1_out Parsed SIB1 structure
   * @param json_out JSON representation of SIB1
   * @return true if parsing successful, false otherwise
   */
  bool parse_sib1_asn1(const uint8_t* data,
                       uint32_t data_len,
                       asn1::rrc_nr::sib1_s& sib1_out,
                       std::string& json_out);

  /**
   * Decode SIB1 PDSCH and parse the result
   * @param ue_dl UE DL instance
   * @param sib1_dci DCI information for SIB1
   * @param slot_cfg Slot configuration
   * @param result Output result structure
   * @param slot_idx Current slot index
   * @return true if decode and parse successful, false otherwise
   */
  bool decode_sib1_pdsch(srsran_ue_dl_nr_t& ue_dl,
                         const srsran_dci_dl_nr_t& sib1_dci,
                         srsran_slot_cfg_t& slot_cfg,
                         SIB1SearchResult& result,
                         uint32_t slot_idx);
};