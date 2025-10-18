/**
 * SSB and SIB1 Extractor Header
 *
 * This class:
 * 1. Scans for SSB (Synchronization Signal Block)
 * 2. Decodes MIB from the SSB
 * 3. Uses MIB information to decode SIB1
 * 4. Extracts critical system information
 */

#ifndef SSB_DECODER_H
#define SSB_DECODER_H

#include "msg2_decoder_standalone.h"
#include "srsran/srsran.h"
#include <complex>
#include <memory>
#include <optional>
#include <vector>

// SSB Search Result structure
struct SsbSearchResult {
  bool found;
  uint32_t pci;
  uint32_t ssb_idx;
  uint32_t t_offset; // Time offset in samples where SSB was found
  float snr_db;
  float rsrp_dbm;
  srsran_mib_nr_t mib;
};

// Forward declaration - SIB1Result is defined in sib1_decoder.h
struct SIB1Result;

class SSBDecoder {
public:
  SSBDecoder();
  ~SSBDecoder();

  /**
   * Initialize the extractor
   */
  bool init(RARSearchConfig &config);

  /**
   * Configure SSB parameters
   */
  bool configure_ssb(RARSearchConfig &config);

  /**
   * Scan for SSB and decode MIB
   * @param buffer Input samples buffer
   * @param nsamples Number of samples
   * @param target_pci Optional target PCI (if nullopt, scan all PCIs)
   * @return SSB search result with MIB information
   */
  SsbSearchResult scan_ssb(cf_t *cf_buffer, uint32_t nsamples,
                           uint32_t target_pci);

  /**
   * Process slot for SIB1 decoding
   * Must be called after successful SSB scan
   * @param buffer Input samples for one slot
   * @param slot_idx Current slot index (0-10239)
   * @return SIB1 result if decoded, otherwise invalid result
   */
  SIB1Result process_slot_for_sib1(const cf_t *buffer, uint32_t slot_idx);

  /**
   * Set MIB information for SIB1 decoding
   * Called internally after SSB scan, but can be set manually
   */
  void set_mib_info(uint8_t coreset0_idx, uint8_t ss0_idx);

  /**
   * Get the detected SSB result (call after successful scan)
   */
  const SsbSearchResult &get_ssb_result() const { return ssb_result_; }

  /**
   * Check if SSB has been detected
   */
  bool has_ssb() const { return ssb_result_.found; }

  /**
   * Print MIB information
   */
  void print_mib(const srsran_mib_nr_t &mib);

  /**
   * Print SIB1 information
   */
  void print_sib1(const SIB1Result &sib1);

private:
  // SSB-related members
  srsran_ssb_t ssb_;
  bool ssb_initialized_;
  double srate_hz_;
  double center_freq_hz_;
  uint32_t nof_prb_;
  uint32_t pci_;

  // SIB1-related members
  // Add your SIB1 decoder structure here
  // srsran_ue_dl_nr_t ue_dl_; // Example
  bool sib1_decoder_initialized_;

  // Stored results
  SsbSearchResult ssb_result_;

  // Helper functions
  srsran_ssb_pattern_t pattern_from_string(const std::string &pattern);
  srsran_subcarrier_spacing_t scs_from_khz(uint32_t scs_khz);
  bool decode_mib(const srsran_pbch_msg_nr_t &pbch_msg, srsran_mib_nr_t &mib);
};

#endif // SSB_DECODER_H
