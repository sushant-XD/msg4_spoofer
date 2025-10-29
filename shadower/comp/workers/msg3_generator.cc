#include "shadower/comp/workers/msg3_generator.h"
#include "srsran/mac/mac_sch_pdu_nr.h"
#include "srsran/phy/utils/vector.h"
#include <random>

Msg3Generator::Msg3Generator(ShadowerConfig& config_) :
  logger(srslog::fetch_basic_logger("Msg3Gen")), config(config_), tc_rnti(0), target_slot_idx(0)
{
  logger.set_level(config.worker_log_level);
  dci_msg = {};
}

bool Msg3Generator::generate_msg3_packets(uint16_t                                          rnti,
                                          std::array<uint8_t, SRSRAN_RAR_UL_GRANT_NBITS>& rar_grant,
                                          uint32_t                                          slot_idx,
                                          uint32_t                                          num_packets)
{
  tc_rnti = rnti;
  
  // Parse RAR grant to extract UL grant information
  if (!parse_rar_grant(rar_grant)) {
    logger.error("Failed to parse RAR grant");
    return false;
  }

  // Calculate target slot for msg3 transmission (typically k2 slots after RAR)
  // For NR, k2 is usually configured in the grant, but we'll use a default of 4 slots
  target_slot_idx = slot_idx + 4;

  logger.info(YELLOW "Generating %u malformed msg3 packets for TC-RNTI=0x%x at slot %u" RESET, 
              num_packets, tc_rnti, target_slot_idx);

  // Clear any existing packets
  while (!msg3_queue.empty()) {
    msg3_queue.pop();
  }

  // Generate multiple msg3 packets with different malformations
  for (uint32_t i = 0; i < num_packets; i++) {
    srsran::unique_byte_buffer_t msg3_pkt = create_msg3_packet(i % 10, i);
    if (msg3_pkt != nullptr) {
      msg3_queue.push(std::move(msg3_pkt));
    }
  }

  logger.info("Generated %zu msg3 packets in queue", msg3_queue.size());
  return !msg3_queue.empty();
}

srsran::unique_byte_buffer_t Msg3Generator::get_next_msg3()
{
  if (msg3_queue.empty()) {
    return nullptr;
  }

  srsran::unique_byte_buffer_t pkt = std::move(msg3_queue.front());
  msg3_queue.pop();
  return pkt;
}

srsran::unique_byte_buffer_t Msg3Generator::create_msg3_packet(uint32_t malformation_type, uint32_t packet_index)
{
  // Create different types of malformed packets
  switch (malformation_type) {
    case 0:
      // Normal msg3 with correct C-RNTI MAC CE
      return create_normal_msg3(tc_rnti);
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
      // Various malformed msg3 packets
      return create_malformed_msg3(malformation_type);
    default:
      return create_normal_msg3(tc_rnti);
  }
}

srsran::unique_byte_buffer_t Msg3Generator::create_normal_msg3(uint16_t rnti)
{
  srsran::unique_byte_buffer_t pdu = srsran::make_byte_buffer();
  if (pdu == nullptr) {
    logger.error("Failed to allocate buffer for msg3");
    return nullptr;
  }

  // Create MAC PDU with C-RNTI MAC CE
  srsran::mac_sch_pdu_nr tx_pdu;
  
  // Initialize with maximum possible size (will be adjusted)
  uint32_t max_pdu_len = 256; // Reasonable size for msg3
  tx_pdu.init_tx(pdu.get(), max_pdu_len, true);

  // Add C-RNTI MAC CE (this is mandatory in msg3 for contention resolution)
  if (tx_pdu.add_crnti_ce(rnti) != SRSRAN_SUCCESS) {
    logger.error("Failed to add C-RNTI MAC CE");
    return nullptr;
  }

  // Create proper RRC Setup Request
  uint8_t rrc_setup_req[32] = {0};
  rrc_setup_req[0] = 0x00; // RRC Setup Request
  rrc_setup_req[1] = 0x12; // ue-Identity (random part)
  rrc_setup_req[2] = 0x34;
  rrc_setup_req[3] = 0x56;
  rrc_setup_req[4] = 0x78;
  rrc_setup_req[5] = 0x9A;
  rrc_setup_req[6] = 0xBC;
  rrc_setup_req[7] = 0xDE;
  rrc_setup_req[8] = 0xF0;
  rrc_setup_req[9] = 0x11;
  rrc_setup_req[10] = 0x00; // establishmentCause: mo-Data (normal)
  rrc_setup_req[11] = 0x00; // spare
  
  // Add SDU with LCID 1 (CCCH) containing RRC Setup Request
  tx_pdu.add_sdu(1, rrc_setup_req, sizeof(rrc_setup_req));

  // Pack the PDU
  tx_pdu.pack();

  logger.debug("Created normal msg3: %u bytes", pdu->N_bytes);
  return pdu;
}

srsran::unique_byte_buffer_t Msg3Generator::create_malformed_msg3(uint32_t malformation_type)
{
  srsran::unique_byte_buffer_t pdu = srsran::make_byte_buffer();
  if (pdu == nullptr) {
    logger.error("Failed to allocate buffer for msg3");
    return nullptr;
  }

  srsran::mac_sch_pdu_nr tx_pdu;
  uint32_t               max_pdu_len = 256;
  std::random_device     rd;
  std::mt19937           gen(rd());
  std::uniform_int_distribution<> dis(0, 255);

  switch (malformation_type) {
    case 1: {
      // Different UE IDs with same TC-RNTI
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      tx_pdu.add_crnti_ce(tc_rnti); // Correct TC-RNTI
      
      // Create RRC Setup Request with different UE identity
      uint8_t rrc_setup_req[32] = {0};
      rrc_setup_req[0] = 0x00; // RRC Setup Request
      rrc_setup_req[1] = 0x00; // ue-Identity (random part)
      rrc_setup_req[2] = 0x00;
      rrc_setup_req[3] = 0x00;
      rrc_setup_req[4] = 0x00;
      rrc_setup_req[5] = 0x00;
      rrc_setup_req[6] = 0x00;
      rrc_setup_req[7] = 0x00;
      rrc_setup_req[8] = 0x00;
      rrc_setup_req[9] = 0x00;
      rrc_setup_req[10] = 0x00; // establishmentCause
      rrc_setup_req[11] = 0x00; // spare
      
      // Fill with different UE identity
      for (int i = 0; i < 8; i++) {
        rrc_setup_req[i + 1] = dis(gen);
      }
      
      tx_pdu.add_sdu(1, rrc_setup_req, sizeof(rrc_setup_req));
      tx_pdu.pack();
      logger.debug("Created msg3 with different UE ID but same TC-RNTI");
      break;
    }
    
    case 2: {
      // Same UE ID but different RRC establishment causes
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      tx_pdu.add_crnti_ce(tc_rnti);
      
      uint8_t rrc_setup_req[32] = {0};
      rrc_setup_req[0] = 0x00; // RRC Setup Request
      rrc_setup_req[1] = 0x12; // ue-Identity (fixed)
      rrc_setup_req[2] = 0x34;
      rrc_setup_req[3] = 0x56;
      rrc_setup_req[4] = 0x78;
      rrc_setup_req[5] = 0x9A;
      rrc_setup_req[6] = 0xBC;
      rrc_setup_req[7] = 0xDE;
      rrc_setup_req[8] = 0xF0;
      rrc_setup_req[9] = 0x11;
      rrc_setup_req[10] = 0x01; // establishmentCause: emergency
      rrc_setup_req[11] = 0x00; // spare
      
      tx_pdu.add_sdu(1, rrc_setup_req, sizeof(rrc_setup_req));
      tx_pdu.pack();
      logger.debug("Created msg3 with emergency establishment cause");
      break;
    }
    
    case 3: {
      // Same UE ID, high priority access
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      tx_pdu.add_crnti_ce(tc_rnti);
      
      uint8_t rrc_setup_req[32] = {0};
      rrc_setup_req[0] = 0x00; // RRC Setup Request
      rrc_setup_req[1] = 0x12; // ue-Identity (same as case 2)
      rrc_setup_req[2] = 0x34;
      rrc_setup_req[3] = 0x56;
      rrc_setup_req[4] = 0x78;
      rrc_setup_req[5] = 0x9A;
      rrc_setup_req[6] = 0xBC;
      rrc_setup_req[7] = 0xDE;
      rrc_setup_req[8] = 0xF0;
      rrc_setup_req[9] = 0x11;
      rrc_setup_req[10] = 0x02; // establishmentCause: highPriorityAccess
      rrc_setup_req[11] = 0x00; // spare
      
      tx_pdu.add_sdu(1, rrc_setup_req, sizeof(rrc_setup_req));
      tx_pdu.pack();
      logger.debug("Created msg3 with high priority access");
      break;
    }
    
    case 4: {
      // Same UE ID, mt-Access
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      tx_pdu.add_crnti_ce(tc_rnti);
      
      uint8_t rrc_setup_req[32] = {0};
      rrc_setup_req[0] = 0x00; // RRC Setup Request
      rrc_setup_req[1] = 0x12; // ue-Identity (same as case 2)
      rrc_setup_req[2] = 0x34;
      rrc_setup_req[3] = 0x56;
      rrc_setup_req[4] = 0x78;
      rrc_setup_req[5] = 0x9A;
      rrc_setup_req[6] = 0xBC;
      rrc_setup_req[7] = 0xDE;
      rrc_setup_req[8] = 0xF0;
      rrc_setup_req[9] = 0x11;
      rrc_setup_req[10] = 0x03; // establishmentCause: mt-Access
      rrc_setup_req[11] = 0x00; // spare
      
      tx_pdu.add_sdu(1, rrc_setup_req, sizeof(rrc_setup_req));
      tx_pdu.pack();
      logger.debug("Created msg3 with mt-Access establishment cause");
      break;
    }
    
    case 5: {
      // Same UE ID, mo-Signalling
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      tx_pdu.add_crnti_ce(tc_rnti);
      
      uint8_t rrc_setup_req[32] = {0};
      rrc_setup_req[0] = 0x00; // RRC Setup Request
      rrc_setup_req[1] = 0x12; // ue-Identity (same as case 2)
      rrc_setup_req[2] = 0x34;
      rrc_setup_req[3] = 0x56;
      rrc_setup_req[4] = 0x78;
      rrc_setup_req[5] = 0x9A;
      rrc_setup_req[6] = 0xBC;
      rrc_setup_req[7] = 0xDE;
      rrc_setup_req[8] = 0xF0;
      rrc_setup_req[9] = 0x11;
      rrc_setup_req[10] = 0x04; // establishmentCause: mo-Signalling
      rrc_setup_req[11] = 0x00; // spare
      
      tx_pdu.add_sdu(1, rrc_setup_req, sizeof(rrc_setup_req));
      tx_pdu.pack();
      logger.debug("Created msg3 with mo-Signalling establishment cause");
      break;
    }
    
    case 6: {
      // Same UE ID, mo-Data
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      tx_pdu.add_crnti_ce(tc_rnti);
      
      uint8_t rrc_setup_req[32] = {0};
      rrc_setup_req[0] = 0x00; // RRC Setup Request
      rrc_setup_req[1] = 0x12; // ue-Identity (same as case 2)
      rrc_setup_req[2] = 0x34;
      rrc_setup_req[3] = 0x56;
      rrc_setup_req[4] = 0x78;
      rrc_setup_req[5] = 0x9A;
      rrc_setup_req[6] = 0xBC;
      rrc_setup_req[7] = 0xDE;
      rrc_setup_req[8] = 0xF0;
      rrc_setup_req[9] = 0x11;
      rrc_setup_req[10] = 0x05; // establishmentCause: mo-Data
      rrc_setup_req[11] = 0x00; // spare
      
      tx_pdu.add_sdu(1, rrc_setup_req, sizeof(rrc_setup_req));
      tx_pdu.pack();
      logger.debug("Created msg3 with mo-Data establishment cause");
      break;
    }
    
    case 7: {
      // Wrong RNTI in C-RNTI MAC CE
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      tx_pdu.add_crnti_ce((tc_rnti + 1) % 0xFFF0); // Wrong RNTI
      tx_pdu.pack();
      logger.debug("Created msg3 with wrong RNTI");
      break;
    }
    
    case 8: {
      // Duplicate C-RNTI MAC CE
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      tx_pdu.add_crnti_ce(tc_rnti);
      tx_pdu.add_crnti_ce(tc_rnti); // Duplicate - invalid
      tx_pdu.pack();
      logger.debug("Created msg3 with duplicate C-RNTI MAC CE");
      break;
    }
    
    case 9: {
      // Empty msg3 (no C-RNTI MAC CE)
      tx_pdu.init_tx(pdu.get(), max_pdu_len, true);
      uint8_t dummy[10] = {0};
      tx_pdu.add_sdu(1, dummy, sizeof(dummy));
      tx_pdu.pack();
      logger.debug("Created msg3 without C-RNTI MAC CE");
      break;
    }
    
    default: {
      // Random data
      pdu->N_bytes = 32 + (dis(gen) % 32);
      for (uint32_t i = 0; i < pdu->N_bytes; i++) {
        pdu->msg[i] = dis(gen);
      }
      logger.debug("Created random msg3");
      break;
    }
  }

  return pdu;
}

bool Msg3Generator::parse_rar_grant(std::array<uint8_t, SRSRAN_RAR_UL_GRANT_NBITS>& rar_grant)
{
  // Setup DCI message from RAR grant
  dci_msg.ctx.format          = srsran_dci_format_nr_rar; /* MAC RAR grant shall be unpacked as DCI 0_0 format */
  dci_msg.ctx.ss_type         = srsran_search_space_type_rar;
  dci_msg.ctx.rnti            = tc_rnti;
  dci_msg.ctx.rnti_type       = srsran_rnti_type_ra;
  dci_msg.nof_bits            = SRSRAN_RAR_UL_GRANT_NBITS;
  
  // Copy grant bits
  srsran_vec_u8_copy(dci_msg.payload, rar_grant.data(), SRSRAN_RAR_UL_GRANT_NBITS);

  logger.debug("Parsed RAR grant: TC-RNTI=0x%x, grant_bits=%u", tc_rnti, dci_msg.nof_bits);
  return true;
}

