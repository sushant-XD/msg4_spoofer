#ifndef MSG3_GENERATOR_H
#define MSG3_GENERATOR_H

#include "shadower/utils/arg_parser.h"
#include "shadower/utils/constants.h"
#include "srsran/common/buffer_pool.h"
#include "srsran/mac/mac_rar_pdu_nr.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include "srsran/phy/phch/dci_nr.h"
#include "srsran/srslog/srslog.h"
#include <array>
#include <memory>
#include <queue>
#include <vector>

/**
 * @brief Msg3Generator creates malformed msg3 packets for flooding attacks
 * 
 * This class generates multiple malformed msg3 packets based on RAR information
 * and queues them for uplink transmission.
 */
class Msg3Generator
{
public:
  explicit Msg3Generator(ShadowerConfig& config_);
  ~Msg3Generator() = default;

  /**
   * @brief Generate malformed msg3 packets from RAR information
   * @param rnti Temporary C-RNTI from RAR
   * @param rar_grant RAR UL grant information
   * @param slot_idx Slot index when RAR was received
   * @param num_packets Number of msg3 packets to generate (5-10 recommended)
   * @return true if successful, false otherwise
   */
  bool generate_msg3_packets(uint16_t                                               rnti,
                             std::array<uint8_t, SRSRAN_RAR_UL_GRANT_NBITS>&       rar_grant,
                             uint32_t                                               slot_idx,
                             uint32_t                                               num_packets = 10);

  /**
   * @brief Get the next msg3 packet from the queue
   * @return unique_byte_buffer_t containing msg3 packet, or nullptr if queue is empty
   */
  srsran::unique_byte_buffer_t get_next_msg3();

  /**
   * @brief Check if there are msg3 packets in the queue
   * @return true if queue has packets, false otherwise
   */
  bool has_msg3_packets() const { return !msg3_queue.empty(); }

  /**
   * @brief Get the number of msg3 packets in the queue
   * @return Number of packets in queue
   */
  size_t get_queue_size() const { return msg3_queue.size(); }

  /**
   * @brief Get the RAR grant information for PUSCH configuration
   * @return DCI message containing grant information
   */
  const srsran_dci_msg_nr_t& get_grant_info() const { return dci_msg; }

  /**
   * @brief Get the target slot index for transmission
   * @return Slot index
   */
  uint32_t get_target_slot_idx() const { return target_slot_idx; }

  /**
   * @brief Get the RNTI for transmission
   * @return RNTI value
   */
  uint16_t get_rnti() const { return tc_rnti; }

private:
  srslog::basic_logger& logger;
  ShadowerConfig&       config;

  // RAR information
  uint16_t              tc_rnti;
  srsran_dci_msg_nr_t   dci_msg;
  uint32_t              target_slot_idx;
  
  // Queue of generated msg3 packets
  std::queue<srsran::unique_byte_buffer_t> msg3_queue;

  /**
   * @brief Create a single msg3 packet (normal or malformed)
   * @param malformation_type Type of malformation to apply
   * @param packet_index Index of this packet in the generation sequence
   * @return unique_byte_buffer_t containing the msg3 packet
   */
  srsran::unique_byte_buffer_t create_msg3_packet(uint32_t malformation_type, uint32_t packet_index);

  /**
   * @brief Create a normal msg3 packet with C-RNTI MAC CE
   * @param rnti RNTI to include in the packet
   * @return unique_byte_buffer_t containing the packet
   */
  srsran::unique_byte_buffer_t create_normal_msg3(uint16_t rnti);

  /**
   * @brief Create a malformed msg3 packet
   * @param malformation_type Type of malformation (0-9 for different types)
   * @return unique_byte_buffer_t containing the malformed packet
   */
  srsran::unique_byte_buffer_t create_malformed_msg3(uint32_t malformation_type);

  /**
   * @brief Parse RAR grant into DCI message format
   * @param rar_grant RAR UL grant bits
   * @return true if successful, false otherwise
   */
  bool parse_rar_grant(std::array<uint8_t, SRSRAN_RAR_UL_GRANT_NBITS>& rar_grant);
};

#endif // MSG3_GENERATOR_H

