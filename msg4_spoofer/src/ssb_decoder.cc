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

SSBDecoder::SSBDecoder(std::unique_ptr<RFBase> *rf_dev)
    : ssb_initialized_(false), sib1_decoder_initialized_(false),
      rf_dev(rf_dev) {
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
  args.min_scs = srsran_subcarrier_spacing_15kHz; // Use 15kHz as minimum
  args.enable_search = true;
  args.enable_measure = true;
  args.enable_decode = true;

  sf_len_ = config.sample_rate * SF_DURATION;
  buffer_pool = std::make_unique<SharedBufferPool>(sf_len_, 10);

  if (srsran_ssb_init(&ssb_, &args) != SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to initialize SSB");
    return false;
  }

  ssb_initialized_ = true;
  return configure_ssb(config);
}

void SSBDecoder::run_tti() {
  /* update the slot index if it is greater than 1 */
  srsran_timestamp_t temp = {};
  srsran_timestamp_copy(&temp, &timestamp_new);
  srsran_timestamp_sub(&temp, timestamp_prev.full_secs,
                       timestamp_prev.frac_secs);
  int32_t tti_jump =
      static_cast<int32_t>(srsran_timestamp_uint64(&temp, 1e3)) * slot_per_sf;
  if (tti_jump != 0) {
    srsran_timestamp_copy(&timestamp_prev, &timestamp_new);
    tti = (tti + tti_jump) % (10240 * slot_per_sf);
  }
}

void SSBDecoder::get_tti(uint32_t *idx, srsran_timestamp_t *ts) {
  std::lock_guard<std::mutex> lock(time_mtx);
  *idx = tti;
  srsran_timestamp_copy(ts, &timestamp_new);
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

  LOG_INFO("Configuring SSB: pattern=%s, scs=%u kHz, freq=%.2f MHz, "
           "center_freq=%.2f MHz",
           (ssb_cfg.pattern == SRSRAN_SSB_PATTERN_A) ? "A" : "Other",
           (ssb_cfg.scs == srsran_subcarrier_spacing_15kHz) ? 15 : 30,
           ssb_cfg.ssb_freq_hz / 1e6, ssb_cfg.center_freq_hz / 1e6);

  if (srsran_ssb_set_cfg(&ssb_, &ssb_cfg) != SRSRAN_SUCCESS) {
    LOG_ERROR("Failed to configure SSB");
    return false;
  }

  return true;
}

bool SSBDecoder::listen(std::shared_ptr<samples_t> &samples) {
  /* receive data */
  cf_t *buffer[SRSRAN_MAX_CHANNELS];
  for (int i = 0; i < SRSRAN_MAX_CHANNELS; i++) {
    if (i < num_channels) {
      buffer[i] = samples->dl_buffer[i]->data();
    } else {
      buffer[i] = nullptr; // Fill the rest with nullptr if fewer channels
    }
  }
  uint32_t offset = 0;
  uint32_t to_receive = sf_len;
  int32_t limit = 2.4e-6 * srate; // 500 samples
  if (samples_delayed > limit) {
    /* If there's still a lot of samples belong to last subframe not processed,
      we receive the remaining samples and make it complete */
    /* if current frame to receive still contain last frame, and the offset is
    larger than 500 Then copy the last sf from the correct start to current
    buffer, we re-do some decoding on the same sf we already processed before */
    std::shared_ptr<samples_t> history = history_samples_queue.back();
    /* Remaining correctly aligned samples in the last slot */
    uint32_t remaining = sf_len - samples_delayed;
    /* read from history queue and fill current subframe with last subframe data
     */
    for (uint32_t i = 0; i < num_channels; i++) {
      srsran_vec_cf_copy(buffer[i],
                         history->dl_buffer[i]->data() + samples_delayed,
                         remaining);
    }
    offset = remaining;
    to_receive = samples_delayed;
  } else if (samples_delayed > 0) {
    /* If the offset is too small, just ignore */
    srsran_timestamp_t ts;
    rf_dev->recv(buffer, samples_delayed, &ts);
  } else {
    /* if part of new frame is already occupied in last frame */
    offset = (uint32_t)(-samples_delayed);
    to_receive = (sf_len + samples_delayed);
    if (offset > limit) {
      std::shared_ptr<samples_t> history = history_samples_queue.back();
      for (uint32_t i = 0; i < num_channels; i++) {
        srsran_vec_cf_copy(buffer[i],
                           history->dl_buffer[i]->data() + to_receive, offset);
      }
    } else {
      for (uint32_t i = 0; i < num_channels; i++) {
        srsran_vec_cf_zero(buffer[i], offset);
      }
    }
  }
  /* reset next sample offset */
  samples_delayed = 0;
  srsran_timestamp_t ts;
  cf_t *tmp[SRSRAN_MAX_CHANNELS];
  for (int i = 0; i < num_channels; i++) {
    tmp[i] = buffer[i] + offset;
  }
  /* receive the remaining samples of the subframe */
  if (rf_dev->recv(tmp, to_receive, &ts) == -1) {
    LOG_ERROR("Error rf_dev->receive");
    return false;
  }

  /* maintain the history queue size to 11 */
  if (history_samples_queue.size() < 11) {
    history_samples_queue.push(samples);
  } else {
    history_samples_queue.pop();
    history_samples_queue.push(samples);
  }

  for (uint32_t i = 0; i < num_channels; i++) {
    srsran_vec_apply_cfo(buffer[i] + offset, -cfo_hz / srate,
                         samples->dl_buffer[i]->data() + offset, to_receive);
  }

  std::lock_guard<std::mutex> lock(time_mtx);
  /* update the new received sample timer */
  srsran_timestamp_copy(&timestamp_new, &ts);
  /* update the slot index */
  run_tti();
  return true;
}

bool SSBDecoder::run_cell_search() {
  srsran_ssb_search_res_t cs_result = {};
  while (!cell_found.load()) {
    /* Initialize the buffer */
    std::shared_ptr<samples_t> samples = std::make_shared<samples_t>();
    for (int i = 0; i < config.nof_channels; i++) {
      samples->dl_buffer[i] = buffer_pool->get_buffer();
    }
    /* receive the samples */
    if (!listen(samples)) {
      LOG_ERROR("Error receive samples for cell search");
      error_handler();
      return false;
    }

    /* run ssb search on new subframe received */
    if (srsran_ssb_search(&ssb, samples->dl_buffer[0]->data(), sf_len,
                          &cs_result) < SRSRAN_SUCCESS) {
      LOG_ERROR("Error srsran_ssb_search");
      continue;
    }
    /* if snr too low or crc error, skip the current subframe */
    if (cs_result.measurements.snr_dB < -10.0f || !cs_result.pbch_msg.crc) {
      samples_delayed = -0.01 * sf_len;
      LOG_ERROR("SNR too small or crc error");
      continue;
    }
    /* extract mib from the pbch msg */
    if (!handle_pbch(cs_result.pbch_msg)) {
      LOG_ERROR("Error handle_pbch");
      continue;
    }
    /* update the offset and the cfo */
    handle_measurements(cs_result.measurements);
    /* log out the cell information */
    std::array<char, 512> mib_info_str = {};
    srsran_pbch_msg_nr_mib_info(&mib, mib_info_str.data(),
                                (uint32_t)mib_info_str.size());
    ncellid = cs_result.N_id;
    return true;
  }
  return false;
}

bool SSBDecoder::handle_pbch(srsran_pbch_msg_nr_t &pbch_msg_) {
  srsran_mib_nr_t tmp_mib = {};
  /* unpack the mib */
  if (srsran_pbch_msg_nr_mib_unpack(&pbch_msg_, &tmp_mib) != 0) {
    LOG_ERROR("Error srsran_pbch_msg_nr_mib_unpack");
    return false;
  }
  if (tmp_mib.cell_barred) {
    return false;
  } /* update the class mib record */
  mib = tmp_mib; /* update the subframe index */
  uint32_t sf_idx =
      srsran_ssb_candidate_sf_idx(&ssb, pbch_msg_.ssb_idx, pbch_msg_.hrf);
  /* Update the TTI value */
  tti = (mib.sfn * 10 * slot_per_sf + sf_idx) % (10240 * slot_per_sf);
  return true;
}

/* update the sample offset and cfo for receiving the samples next time. */
void SSBDecoder::handle_measurements(srsran_csi_trs_measurements_t &feedback) {
  srsran_vec_zero((void *)&measurements, sizeof(srsran_csi_trs_measurements_t));
  srsran_combine_csi_trs_measurements(&measurements, &feedback, &measurements);
  samples_delayed = (uint32_t)round((double)feedback.delay_us * (srate * 1e-6));
  cfo_hz = feedback.cfo_hz;
  measurements = feedback;
  // LOG_ERROR("CFO: %f SNR: %f", feedback.cfo_hz, feedback.snr_dB);
  tracer_status.send_string(
      fmt::format("{{\"CFO\": {:.2f}, \"SNR\": {:.2f}, \"RSRP\": {:.2f}}}",
                  feedback.cfo_hz, feedback.snr_dB, feedback.rsrp_dB));
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
