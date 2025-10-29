#ifndef MSG3_UL_WORKER_H
#define MSG3_UL_WORKER_H

// Include srsran.h first to ensure extern "C" guards are applied
#include "srsran/srsran.h"

#include "shadower/comp/workers/msg3_generator.h"
#include "shadower/source/source.h"
#include "shadower/utils/arg_parser.h"
#include "shadower/utils/utils.h"
#include "srsran/common/phy_cfg_nr.h"
#include "srsran/common/thread_pool.h"
#include "srsran/phy/ue/ue_ul_nr.h"
#include "srsran/srslog/srslog.h"
#include <memory>
#include <mutex>

/**
 * @brief Msg3ULWorker handles uplink transmission of msg3 packets
 * 
 * This worker encodes and transmits msg3 packets (including malformed ones)
 * to the base station on the uplink channel.
 */
class Msg3ULWorker : public srsran::thread_pool::worker
{
public:
  Msg3ULWorker(srslog::basic_logger& logger_, Source* source_, ShadowerConfig& config_);
  ~Msg3ULWorker() override;

  struct msg3_ul_task_t {
    uint16_t                         rnti;
    srsran_rnti_type_t               rnti_type;
    uint32_t                         slot_idx;
    uint32_t                         rx_tti;
    srsran_timestamp_t               rx_time;
    srsran::unique_byte_buffer_t     msg3_pdu;
    srsran_dci_msg_nr_t              dci_msg;  // RAR grant information
  };

  /**
   * @brief Initialize the msg3 uplink worker
   * @return true if successful, false otherwise
   */
  bool init();

  /**
   * @brief Update carrier configuration
   * @param phy_cfg_ Physical layer configuration
   * @return true if successful, false otherwise
   */
  bool update_cfg(srsran::phy_cfg_nr_t& phy_cfg_);

  /**
   * @brief Set the task context for msg3 transmission
   * @param task_ Task containing msg3 packet and transmission parameters
   */
  void set_context(msg3_ul_task_t& task_);

  /**
   * @brief Encode and transmit msg3 on PUSCH
   * @return true if successful, false otherwise
   */
  bool send_pusch_msg3();

  /**
   * @brief Execute the worker task
   */
  void execute_work();

  cf_t* tx_buffer = nullptr;
  cf_t* ul_buffer = nullptr;

private:
  srslog::basic_logger& logger;
  std::mutex            mutex;
  ShadowerConfig&       config;
  srsran::phy_cfg_nr_t  phy_cfg = {};

  uint32_t sf_len      = 0;
  uint32_t slot_per_sf = 1;
  uint32_t slot_len    = 0;
  uint32_t nof_sc      = 0;
  uint32_t nof_re      = 0;
  uint32_t numerology  = 0;

  Source*                source       = nullptr;
  srsran_ue_ul_nr_t      ue_ul        = {};
  srsran_softbuffer_tx_t softbuffer_tx = {};
  uint8_t*               data_tx[SRSRAN_MAX_TB] = {};

  // Current work information
  msg3_ul_task_t msg3_task = {};

  // Worker implementation
  void work_imp() override;

  /**
   * @brief Configure PUSCH for msg3 transmission
   * @param pusch_cfg PUSCH configuration to fill
   * @return true if successful, false otherwise
   */
  bool configure_pusch_for_msg3(srsran_sch_cfg_nr_t& pusch_cfg);
};

#endif // MSG3_UL_WORKER_H

