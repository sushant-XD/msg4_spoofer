# SIB1 Decoding Process in Sni5Gect-5GNR-sniffing-and-exploitation

## Overview

This document provides a comprehensive analysis of how the Sni5Gect-5GNR-sniffing-and-exploitation repository decodes MIB (Master Information Block) and SIB1 (System Information Block 1) across different processes: cell_search, sync, broadcast_worker, and scheduler. The SIB1 decoding process is crucial for 5G NR network sniffing as it contains essential system information needed for network analysis and exploitation.

## Architecture Overview

The SIB1 decoding process involves four main components working together:

1. **Cell Search Process** - Initial cell discovery and MIB decoding
2. **Sync Process** - Continuous synchronization and SSB tracking
3. **Broadcast Worker** - PDSCH decoding and SIB1 extraction
4. **Scheduler** - Coordination and configuration management

## 1. Cell Search Process

### Location
- **File**: `srsue/src/phy/nr/cell_search.cc`
- **Header**: `srsue/hdr/phy/nr/cell_search.h`

### Key Functions

#### `cell_search::init()`
```cpp
bool cell_search::init(const args_t& args)
```
- **Purpose**: Initialize the SSB (Synchronization Signal Block) search object
- **srsRAN Functions Called**:
  - `srsran_ssb_init(&ssb, &ssb_args)` - Initialize SSB search with configuration
- **Configuration**: Sets up SSB search parameters including max sample rate, minimum SCS, and enables search/decode modes

#### `cell_search::run_slot()`
```cpp
cell_search::ret_t cell_search::run_slot(const cf_t* buffer, uint32_t slot_sz)
```
- **Purpose**: Search for SSB in received samples and decode MIB
- **srsRAN Functions Called**:
  - `srsran_ssb_search(&ssb, buffer, slot_sz + ssb.ssb_sz, &ret.ssb_res)` - Perform SSB search
- **Process**:
  1. Searches for SSB in the input buffer
  2. Checks if SNR >= -10.0 dB and PBCH CRC is valid
  3. Returns `CELL_FOUND` if conditions are met, otherwise `CELL_NOT_FOUND`

### MIB Decoding Flow
1. **SSB Search**: Uses `srsran_ssb_search()` to locate SSB in time and frequency
2. **PBCH Decoding**: Automatically extracts and decodes PBCH message
3. **MIB Extraction**: MIB information is contained within the PBCH message structure
4. **Validation**: Checks CRC and signal quality metrics

## 2. Sync Process (Syncer)

### Location
- **File**: `shadower/comp/sync/syncer.cc`
- **Header**: `shadower/comp/sync/syncer.h`

### Key Functions

#### `Syncer::run_cell_search()`
```cpp
bool Syncer::run_cell_search()
```
- **Purpose**: Main cell search loop that continues until a cell is found
- **srsRAN Functions Called**:
  - `srsran_ssb_search(&ssb, samples->dl_buffer[0]->data(), sf_len, &cs_result)` - Search for SSB
  - `srsran_pbch_msg_nr_mib_unpack(&pbch_msg_, &tmp_mib)` - Unpack MIB from PBCH
- **Process**:
  1. Continuously receives samples from source
  2. Performs SSB search on each subframe
  3. Validates SNR and CRC conditions
  4. Extracts MIB information from PBCH message
  5. Updates timing and CFO measurements

#### `Syncer::handle_pbch()`
```cpp
bool Syncer::handle_pbch(srsran_pbch_msg_nr_t& pbch_msg_)
```
- **Purpose**: Process PBCH message and extract MIB information
- **srsRAN Functions Called**:
  - `srsran_pbch_msg_nr_mib_unpack(&pbch_msg_, &tmp_mib)` - Extract MIB from PBCH
  - `srsran_ssb_candidate_sf_idx(&ssb, pbch_msg_.ssb_idx, pbch_msg_.hrf)` - Calculate subframe index
- **Process**:
  1. Unpacks MIB from PBCH message
  2. Checks if cell is barred
  3. Updates MIB structure and calculates TTI

#### `Syncer::run_sync_track()`
```cpp
bool Syncer::run_sync_track(cf_t* buffer)
```
- **Purpose**: Track SSB and maintain synchronization
- **srsRAN Functions Called**:
  - `srsran_ssb_track(&ssb, buffer, ncellid, pbch_msg_tmp.ssb_idx, half_frame, &measurements_tmp, &pbch_msg_tmp)` - Track SSB
- **Process**:
  1. Tracks SSB at periodic intervals
  2. Validates PBCH CRC and timing
  3. Updates measurements and timing

### Synchronization Flow
1. **Initial Search**: Find cell and decode MIB
2. **Sync Find**: Get back in sync if lost
3. **Sync Track**: Maintain synchronization
4. **Measurement Update**: Continuously update CFO and timing

## 3. Broadcast Worker Process

### Location
- **File**: `shadower/comp/workers/broadcast_worker.cc`
- **Header**: `shadower/comp/workers/broadcast_worker.h`

### Key Functions

#### `BroadCastWorker::work()`
```cpp
bool BroadCastWorker::work(const std::shared_ptr<Task>& task)
```
- **Purpose**: Main processing function for each task/slot
- **srsRAN Functions Called**:
  - `srsran_ue_dl_nr_estimate_fft(&ue_dl, &slot_cfg)` - Perform FFT estimation
  - `ue_dl_dci_search()` - Search for DCI (Downlink Control Information)
  - `pdsch_decode()` - Decode PDSCH data
- **Process**:
  1. Processes each slot in the subframe
  2. Performs FFT estimation
  3. Searches for DCI
  4. Decodes PDSCH if grant is available

#### `BroadCastWorker::pdsch_decode()`
```cpp
bool BroadCastWorker::pdsch_decode(uint32_t slot_idx, uint32_t task_idx)
```
- **Purpose**: Decode PDSCH symbols into bytes
- **srsRAN Functions Called**:
  - `ue_dl_pdsch_decode(ue_dl, pdsch_cfg, slot_cfg, pdsch_res, softbuffer_rx, logger, task_idx)` - Decode PDSCH
- **Process**:
  1. Gets pending DL grant from DCI search
  2. Allocates buffer for decoded data
  3. Decodes PDSCH symbols
  4. Validates CRC
  5. Routes to SIB1 or RAR decoder based on RNTI type

#### `BroadCastWorker::decode_sib1()`
```cpp
bool BroadCastWorker::decode_sib1(srsran::unique_byte_buffer_t& data)
```
- **Purpose**: Decode SIB1 bytes into ASN.1 structure
- **Functions Called**:
  - `parse_to_sib1(data->msg, data->N_bytes, sib1_)` - Parse SIB1 from bytes
- **Process**:
  1. Calls utility function to parse SIB1
  2. Updates internal SIB1 structure
  3. Triggers callback for configuration update

### SIB1 Decoding Flow
1. **FFT Processing**: Convert time domain to frequency domain
2. **DCI Search**: Find downlink control information
3. **PDSCH Decoding**: Decode physical downlink shared channel
4. **SIB1 Parsing**: Extract SIB1 from decoded bytes
5. **Configuration Update**: Apply SIB1 configuration

## 4. Scheduler Process

### Location
- **File**: `shadower/comp/scheduler.cc`
- **Header**: `shadower/comp/scheduler.h`

### Key Functions

#### `Scheduler::handle_mib()`
```cpp
void Scheduler::handle_mib(srsran_mib_nr_t& mib_, uint32_t ncellid_)
```
- **Purpose**: Apply MIB configuration to all workers
- **Process**:
  1. Stores MIB and cell ID
  2. Applies MIB configuration to broadcast worker
  3. Applies MIB configuration to all UE trackers
  4. Logs successful application

#### `Scheduler::handle_sib1()`
```cpp
void Scheduler::handle_sib1(asn1::rrc_nr::sib1_s& sib1_)
```
- **Purpose**: Apply SIB1 configuration to all workers
- **Process**:
  1. Stores SIB1 configuration
  2. Applies SIB1 to broadcast worker
  3. Applies SIB1 to all UE trackers
  4. Extracts and logs cell information (MCC, MNC, TAC)
  5. Creates additional broadcast workers for RA-RNTI tracking

### Coordination Flow
1. **MIB Application**: Distribute MIB configuration to workers
2. **SIB1 Application**: Distribute SIB1 configuration to workers
3. **Worker Management**: Create and manage multiple broadcast workers
4. **Task Distribution**: Coordinate task processing across workers

## 5. SIB1 Parsing Implementation

### Location
- **File**: `shadower/utils/src/phy_cfg_utils.cc`
- **Function**: `parse_to_sib1()`

### Implementation Details
```cpp
bool parse_to_sib1(uint8_t* data, uint32_t len, asn1::rrc_nr::sib1_s& sib1)
{
  asn1::rrc_nr::bcch_dl_sch_msg_s dlsch_msg;
  asn1::cbit_ref                  dlsch_bref(data, len);
  asn1::SRSASN_CODE               err = dlsch_msg.unpack(dlsch_bref);
  if (err != asn1::SRSASN_SUCCESS ||
      dlsch_msg.msg.type().value != asn1::rrc_nr::bcch_dl_sch_msg_type_c::types_opts::c1) {
    std::cerr << "Error unpacking BCCH-BCH message\n";
    return false;
  }
  sib1 = dlsch_msg.msg.c1().sib_type1();
  return true;
}
```

### Process
1. **ASN.1 Unpacking**: Unpacks BCCH-DL-SCH message using ASN.1 library
2. **Message Validation**: Validates message type and structure
3. **SIB1 Extraction**: Extracts SIB1 from the message structure
4. **Error Handling**: Returns false on parsing errors

## 6. Configuration Updates

### MIB to PHY Configuration
- **Function**: `update_phy_cfg_from_mib()`
- **Process**:
  1. Updates PDSCH configuration (typeA position, SCS)
  2. Sets physical cell ID
  3. Calculates frequency offsets
  4. Creates CORESET0 and SearchSpace0 configurations

### SIB1 to PHY Configuration
- **Function**: `update_phy_cfg_from_sib1()`
- **Process**:
  1. Applies PDSCH common configuration
  2. Applies PUSCH common configuration
  3. Applies PUCCH common configuration
  4. Applies RACH common configuration
  5. Applies PDCCH common configuration
  6. Applies carrier and SSB configurations
  7. Sets timing advance offset

## 7. Key srsRAN Functions Used

### SSB Processing
- `srsran_ssb_init()` - Initialize SSB search object
- `srsran_ssb_set_cfg()` - Configure SSB parameters
- `srsran_ssb_search()` - Search for SSB in samples
- `srsran_ssb_find()` - Find specific SSB
- `srsran_ssb_track()` - Track SSB for synchronization

### PBCH/MIB Processing
- `srsran_pbch_msg_nr_mib_unpack()` - Extract MIB from PBCH message
- `srsran_pbch_msg_nr_mib_info()` - Format MIB information for logging

### PDSCH Processing
- `srsran_ue_dl_nr_estimate_fft()` - Perform FFT estimation
- `srsran_ue_dl_nr_decode_pdsch()` - Decode PDSCH symbols
- `ue_dl_pdsch_decode()` - High-level PDSCH decoding function

### DCI Processing
- `ue_dl_dci_search()` - Search for DCI in PDCCH
- `srsran_dci_nr_ul_unpack()` - Unpack uplink DCI

## 8. Data Flow Summary

```
Radio Samples → Cell Search → MIB Decoding → Sync Process
                    ↓
              Broadcast Worker → PDSCH Decoding → SIB1 Parsing
                    ↓
              Scheduler → Configuration Distribution → All Workers
```

## 9. Error Handling

### Cell Search Errors
- SNR too low (< -10.0 dB)
- PBCH CRC errors
- Cell barred conditions

### Sync Errors
- Lost synchronization
- Timing drift
- CFO estimation errors

### SIB1 Decoding Errors
- ASN.1 parsing errors
- Invalid message structure
- CRC validation failures

## 10. Performance Considerations

### Optimization Features
- **CUDA Support**: GPU acceleration for FFT processing
- **Thread Pool**: Parallel processing of tasks
- **Buffer Pool**: Efficient memory management
- **History Queue**: Sample history for timing correction

### Monitoring
- SNR measurements
- CFO tracking
- Timing advance updates
- Cell information logging

## Conclusion

The SIB1 decoding process in Sni5Gect is a sophisticated multi-stage pipeline that combines cell search, synchronization, PDSCH decoding, and ASN.1 parsing to extract essential system information from 5G NR networks. The implementation leverages srsRAN's comprehensive library of physical layer functions while providing custom optimizations for network sniffing and exploitation scenarios.

The modular design allows for efficient processing across multiple workers while maintaining synchronization and configuration consistency. The extensive use of srsRAN functions ensures compatibility with 3GPP standards and provides robust error handling for real-world network conditions.
