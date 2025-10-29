#include "shadower/comp/workers/msg3_ul_worker.h"
#include "srsran/srsran.h"

Msg3ULWorker::Msg3ULWorker(srslog::basic_logger& logger_, Source* source_, ShadowerConfig& config_) :
  logger(logger_), source(source_), config(config_)
{
}

Msg3ULWorker::~Msg3ULWorker()
{
  if (ul_buffer) {
    free(ul_buffer);
    ul_buffer = nullptr;
  }
  if (tx_buffer) {
    free(tx_buffer);
    tx_buffer = nullptr;
  }
  if (data_tx[0]) {
    free(data_tx[0]);
    data_tx[0] = nullptr;
  }
  srsran_softbuffer_tx_free(&softbuffer_tx);
  srsran_ue_ul_nr_free(&ue_ul);
}

bool Msg3ULWorker::init()
{
  std::lock_guard<std::mutex> lock(mutex);
  sf_len      = config.sample_rate * SF_DURATION;
  slot_per_sf = 1 << config.scs_common;
  slot_len    = sf_len / slot_per_sf;
  nof_sc      = config.nof_prb * SRSRAN_NRE;
  nof_re      = nof_sc * SRSRAN_NSYMB_PER_SLOT_NR;
  numerology  = (uint32_t)config.scs_common;

  /* Init ul_buffer and tx_buffer */
  ul_buffer = srsran_vec_cf_malloc(sf_len);
  if (!ul_buffer) {
    logger.error("Error allocating UL buffer");
    return false;
  }
  
  tx_buffer = srsran_vec_cf_malloc(sf_len * 2);
  if (!tx_buffer) {
    logger.error("Error allocating TX buffer");
    return false;
  }

  /* buffer for data to send */
  data_tx[0] = srsran_vec_u8_malloc(SRSRAN_SLOT_MAX_NOF_BITS_NR);
  if (data_tx[0] == nullptr) {
    logger.error("Error allocating data buffer");
    return false;
  }

  /* Initialize softbuffer tx */
  if (srsran_softbuffer_tx_init_guru(&softbuffer_tx, SRSRAN_SCH_NR_MAX_NOF_CB_LDPC, SRSRAN_LDPC_MAX_LEN_ENCODED_CB) <
      SRSRAN_SUCCESS) {
    logger.error("Error initializing softbuffer_tx");
    return false;
  }

  /* Initialize UE UL NR for PUSCH encoding */
  srsran_ue_ul_nr_args_t ue_ul_args = {};
  ue_ul_args.nof_max_prb            = config.nof_prb;
  ue_ul_args.scs                    = config.scs_common;
  
  if (srsran_ue_ul_nr_init(&ue_ul, tx_buffer, &ue_ul_args) != SRSRAN_SUCCESS) {
    logger.error("Error initializing UE UL NR");
    return false;
  }

  logger.info("Msg3 UL Worker initialized");
  return true;
}

bool Msg3ULWorker::update_cfg(srsran::phy_cfg_nr_t& phy_cfg_)
{
  std::lock_guard<std::mutex> lock(mutex);
  phy_cfg = phy_cfg_;
  
  /* Set carrier configuration for UE UL */
  if (srsran_ue_ul_nr_set_carrier(&ue_ul, &phy_cfg.carrier) != SRSRAN_SUCCESS) {
    logger.error("Error setting UE UL carrier");
    return false;
  }
  
  return true;
}

void Msg3ULWorker::set_context(msg3_ul_task_t& task_)
{
  std::lock_guard<std::mutex> lock(mutex);
  msg3_task = std::move(task_);
}

void Msg3ULWorker::work_imp()
{
  std::lock_guard<std::mutex> lock(mutex);
  
  if (!msg3_task.msg3_pdu || msg3_task.msg3_pdu->N_bytes == 0) {
    logger.warning("Empty msg3 PDU in task");
    return;
  }

  logger.info(GREEN "Transmitting msg3: RNTI=0x%x, slot=%u, size=%u bytes" RESET,
              msg3_task.rnti,
              msg3_task.slot_idx,
              msg3_task.msg3_pdu->N_bytes);

  // Send the msg3 using PUSCH
  if (!send_pusch_msg3()) {
    logger.error("Failed to send msg3 on PUSCH");
    return;
  }

  logger.debug("Msg3 transmitted successfully");
}

bool Msg3ULWorker::send_pusch_msg3()
{
  // Configure PUSCH for msg3 transmission
  srsran_sch_cfg_nr_t pusch_cfg = {};
  if (!configure_pusch_for_msg3(pusch_cfg)) {
    logger.error("Failed to configure PUSCH for msg3");
    return false;
  }

  // Copy msg3 PDU to transmission buffer
  if (msg3_task.msg3_pdu->N_bytes > SRSRAN_SLOT_MAX_NOF_BITS_NR / 8) {
    logger.error("Msg3 PDU too large: %u bytes", msg3_task.msg3_pdu->N_bytes);
    return false;
  }
  
  memcpy(data_tx[0], msg3_task.msg3_pdu->msg, msg3_task.msg3_pdu->N_bytes);

  // Prepare PUSCH data structure
  srsran_pusch_data_nr_t pusch_data = {};
  pusch_data.payload[0]             = data_tx[0];
  pusch_data.uci                    = {}; // No UCI for msg3

  // Configure slot
  srsran_slot_cfg_t slot_cfg = {.idx = msg3_task.slot_idx};

  // Encode PUSCH using srsRAN UE UL encoder
  if (srsran_ue_ul_nr_encode_pusch(&ue_ul, &slot_cfg, &pusch_cfg, &pusch_data) != SRSRAN_SUCCESS) {
    logger.error("Failed to encode PUSCH msg3");
    return false;
  }

  // Calculate transmission timestamp
  srsran_timestamp_t tx_time       = msg3_task.rx_time;
  double             slot_duration = SF_DURATION / slot_per_sf;
  double             time_advance  = (msg3_task.slot_idx - msg3_task.rx_tti) * slot_duration;
  srsran_timestamp_add(&tx_time, 0, time_advance);

  // Transmit using Source interface
  if (source && source->is_sdr()) {
    uint32_t tx_nsamples = slot_len;
    
    // Apply CFO if configured
    if (config.uplink_cfo != 0.0f) {
      srsran_vec_apply_cfo(tx_buffer, -config.uplink_cfo, tx_buffer, tx_nsamples);
    }
    
    // Transmit the encoded PUSCH
    cf_t* tx_buffers[SRSRAN_MAX_PORTS] = {tx_buffer};
    int   ret = source->send(tx_buffers, tx_nsamples, tx_time, msg3_task.slot_idx);
    if (ret < 0) {
      logger.error("Failed to transmit msg3 via Source");
      return false;
    }
    
    logger.info(GREEN "Transmitted msg3 PUSCH: slot=%u, size=%u bytes, rnti=0x%x" RESET,
                msg3_task.slot_idx,
                msg3_task.msg3_pdu->N_bytes,
                msg3_task.rnti);
  } else {
    logger.warning("Source not available or not SDR, msg3 not transmitted (dry-run mode)");
  }

  return true;
}

bool Msg3ULWorker::configure_pusch_for_msg3(srsran_sch_cfg_nr_t& pusch_cfg)
{
  // Parse DCI to get PUSCH configuration
  srsran_dci_ul_nr_t dci_ul = {};
  
  // Unpack RAR grant (DCI format 0_0) to get UL grant parameters
  if (srsran_dci_nr_ul_unpack(nullptr, &msg3_task.dci_msg, &dci_ul) != SRSRAN_SUCCESS) {
    logger.error("Failed to unpack RAR UL grant");
    return false;
  }
                                              
  // Convert DCI UL to SCH grant
                                                                                                                                            srsran_slot_cfg_t slot_cfg = {.idx = msg3_task.slot_idx};
  if (srsran_ra_ul_dci_to_grant_nr(&phy_cfg.carrier,
                                    &slot_cfg,
                                    &phy_cfg.pusch,
                                    &dci_ul,
                                    &pusch_cfg,
                                    &pusch_cfg.grant) != SRSRAN_SUCCESS) {
    logger.error("Failed to convert DCI to grant");
    return false;
  }

  // Override TBS with actual msg3 size
  pusch_cfg.grant.tb[0].tbs       = msg3_task.msg3_pdu->N_bytes * 8;
  pusch_cfg.grant.tb[0].softbuffer.tx = &softbuffer_tx;
  pusch_cfg.grant.tb[0].enabled   = true;
  
  // RNTI configuration
  pusch_cfg.grant.rnti      = msg3_task.rnti;
  pusch_cfg.grant.rnti_type = msg3_task.rnti_type;
  
  // Enable DMRS
  pusch_cfg.dmrs.scrambling_id0 = phy_cfg.carrier.pci;
  pusch_cfg.dmrs.type           = srsran_dmrs_sch_type_1;
  pusch_cfg.dmrs.typeA_pos      = srsran_dmrs_sch_typeA_pos_2;
  pusch_cfg.dmrs.additional_pos = srsran_dmrs_sch_add_pos_2;

  // Reset softbuffer
  srsran_softbuffer_tx_reset(&softbuffer_tx);

  logger.debug("Configured PUSCH: TBS=%u bits, nof_prb=%u, rnti=0x%x",
               pusch_cfg.grant.tb[0].tbs,
               pusch_cfg.grant.nof_prb,
               pusch_cfg.grant.rnti);

  return true;
}

void Msg3ULWorker::execute_work()
{
  work_imp();
}

