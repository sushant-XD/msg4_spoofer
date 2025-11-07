#include "shadower/comp/prach_flooder.h"
#include "srsran/common/band_helper.h"
#include "srsran/common/standard_streams.h"
#include "srsran/phy/common/timestamp.h"
#include <algorithm>
#include <chrono>

using namespace std::chrono_literals;

PrachFlooder::PrachFlooder(srslog::basic_logger& logger_) : logger(logger_)
{
  srsran_timestamp_init(&next_timestamp, 0, 0.0);
}

PrachFlooder::~PrachFlooder()
{
  stop();
  if (prach_ready) {
    srsran_prach_free(&prach_ctx);
    prach_ready = false;
  }
}

bool PrachFlooder::configure(const srsran::phy_cfg_nr_t& phy_cfg, const ShadowerConfig& config)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (running.load()) {
    logger.error("Cannot configure PRACH flooder while running");
    return false;
  }

  sample_rate_hz = config.sample_rate;
  if (sample_rate_hz <= 0.0) {
    logger.error("Invalid sample rate %.3f Hz", sample_rate_hz);
    return false;
  }
  nof_channels = std::max<uint32_t>(1, config.nof_channels);
  preamble_index = config.prach_flood_preamble;
  tx_period_s    = std::max(config.prach_flood_period_ms * 1e-3, 0.0);

  if (!init_prach(phy_cfg)) {
    return false;
  }

  double min_period = static_cast<double>(waveform.size()) / sample_rate_hz;
  if (tx_period_s <= 0.0 || tx_period_s < min_period) {
    tx_period_s = min_period;
  }

  configured = true;
  logger.info("PRACH flooder configured: preamble=%u, period=%.3f ms", preamble_index, tx_period_s * 1e3);
  return true;
}

bool PrachFlooder::start(Source* source_)
{
  if (source_ == nullptr) {
    logger.error("Invalid source pointer supplied to PRACH flooder");
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex);
  if (!configured || !prach_ready) {
    logger.error("PRACH flooder not configured");
    return false;
  }
  if (running.load()) {
    return true;
  }

  source = source_;
  channel_ptrs.assign(nof_channels, nullptr);
  channel_ptrs[0] = waveform.data();
  for (uint32_t i = 1; i < nof_channels; i++) {
    channel_ptrs[i] = null_waveform.data();
  }

  stop_requested.store(false);
  running.store(true);
  srsran_timestamp_init(&next_timestamp, 0, 0.0);
  worker_thread = std::thread(&PrachFlooder::worker_loop, this);
  logger.info("PRACH flooder started");
  return true;
}

void PrachFlooder::stop()
{
  stop_requested.store(true);
  if (worker_thread.joinable()) {
    worker_thread.join();
  }
  running.store(false);
  stop_requested.store(false);
}

bool PrachFlooder::init_prach(const srsran::phy_cfg_nr_t& phy_cfg)
{
  if (prach_ready) {
    srsran_prach_free(&prach_ctx);
    prach_ready = false;
  }

  srsran_cell_t cell = {};
  if (srsran_carrier_to_cell(&phy_cfg.carrier, &cell) < SRSRAN_SUCCESS) {
    logger.error("Failed to convert carrier configuration to cell");
    return false;
  }

  prach_cfg                 = phy_cfg.prach;
  prach_cfg.is_nr           = true;
  prach_cfg.tdd_config.configured = (phy_cfg.duplex.mode == SRSRAN_DUPLEX_MODE_TDD);

  uint32_t freq_offset_adjust = (phy_cfg.carrier.nof_prb - cell.nof_prb) / 2;
  if (prach_cfg.freq_offset < freq_offset_adjust) {
    logger.error("PRACH frequency offset %u smaller than adjustment %u", prach_cfg.freq_offset, freq_offset_adjust);
    return false;
  }
  prach_cfg.freq_offset -= freq_offset_adjust;

  if (srsran_prach_init(&prach_ctx, srsran_symbol_sz(cell.nof_prb))) {
    logger.error("Failed to initialise PRACH context");
    return false;
  }

  if (srsran_prach_set_cfg(&prach_ctx, &prach_cfg, cell.nof_prb)) {
    logger.error("Failed to set PRACH configuration");
    srsran_prach_free(&prach_ctx);
    return false;
  }

  prach_ready = true;
  waveform.resize(prach_ctx.N_cp + prach_ctx.N_seq);
  null_waveform.assign(waveform.size(), cf_t{0.0f, 0.0f});
  channel_ptrs.clear();
  return true;
}

bool PrachFlooder::generate_waveform(uint32_t preamble)
{
  if (!prach_ready) {
    return false;
  }

  uint32_t available = std::max<uint32_t>(prach_cfg.num_ra_preambles, 1);
  uint32_t selected  = preamble % available;
  if (srsran_prach_gen(&prach_ctx, selected, prach_cfg.freq_offset, waveform.data()) != SRSRAN_SUCCESS) {
    logger.error("Failed to generate PRACH preamble (index=%u)", selected);
    return false;
  }
  return true;
}

void PrachFlooder::worker_loop()
{
  while (!stop_requested.load()) {
    srsran_timestamp_t tx_timestamp = {};
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!generate_waveform(preamble_index)) {
        break;
      }
      channel_ptrs[0] = waveform.data();
      tx_timestamp    = next_timestamp;
    }

    uint32_t nof_samples = static_cast<uint32_t>(waveform.size());
    int      ret         = source->send(channel_ptrs.data(), nof_samples, tx_timestamp);
    if (ret < 0) {
      logger.error("Failed to send PRACH burst");
      break;
    }

    {
      std::lock_guard<std::mutex> lock(mutex);
      srsran_timestamp_add(&next_timestamp, 0, tx_period_s);
    }

    if (stop_requested.load()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(tx_period_s));
  }

  running.store(false);
}

