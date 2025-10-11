# MSG2 Decoder for 5G RACH Security Research

This document describes the MSG2 decoder implementation for 5G RACH (Random Access Channel) procedure security research at Mississippi State University.

## Overview

The MSG2 decoder is designed to intercept and decode MSG2 (Random Access Response) messages in the 5G RACH procedure. This tool is specifically built for security research purposes to identify vulnerabilities in the 5G random access procedure.

## RA-RNTI Calculation

The RA-RNTI (Random Access Radio Network Temporary Identifier) is calculated using the 3GPP formula:

```
RA-RNTI = 1 + s_id + 14 × t_id + 14 × 80 × f_id + 14 × 80 × 8 × ul_carrier_id
```

Where:
- `s_id`: First OFDM symbol index (0-13)
- `t_id`: First slot index within 10ms frame (0-79 for 15kHz SCS)
- `f_id`: PRACH frequency occasion index (0-7)
- `ul_carrier_id`: UL carrier ID (0 for normal, 1 for SUL)

## Key Features

### 1. Smart RA-RNTI Generation
- Generates comprehensive sets of RA-RNTI values for the 4ms MSG2 window
- Supports multiple subcarrier spacings (15, 30, 60, 120 kHz)
- Tracks up to 1000 concurrent RA-RNTI candidates

### 2. Real-time MSG2 Decoding
- PDCCH monitoring with DCI format 1_0 decoding
- PDSCH decoding for RAR (Random Access Response) payloads
- CRC validation for reliable decoding

### 3. Comprehensive Coverage
- Handles multiple UE scenarios simultaneously
- Supports both normal and SUL (Supplemental Uplink) carriers
- Adaptive timing window management

### 4. Research-oriented Design
- Detailed logging for security analysis
- Statistics tracking for vulnerability assessment
- Modular design for easy extension

## Configuration

### TOML Configuration File

Update your `test.toml` file to include MSG2-specific parameters:

```toml
[log]
level = "debug"

[rf]
freq_offset = 0
rx_gain = 40.0
tx_gain = 40.0
srate = 23.04e6
frequency = 1842.5e6
nof_prb = 106
N_id = 1
ssb_numerology = 0  # 0=15kHz, 1=30kHz, 2=60kHz, 3=120kHz

# Device configuration
device_name = "zmq"
device_args = "tx_port=tcp://*:2000,rx_port=tcp://localhost:2001,id=enb"

[prach]
config_idx = 1
is_nr = true
hs_flag = false
root_sequence_index = 1
zero_correlation_zone = 0
num_ra_preambles = 64
time_delay = 1

# MSG2 specific configuration (optional)
[msg2]
max_rnti_candidates = 200
search_space_id = 1
coreset_id = 0
enable_multiple_rnti = true
```

## Usage

### 1. Build the Project

```bash
cd /home/sushant/Wireless/msg4_spoofer
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 2. Run MSG2 Decoder Demo

```bash
# Run MSG2 decoding mode
./msg4_spoofer/msg4_spoofer ../test.toml msg2

# Run RA-RNTI calculation demonstration
./msg4_spoofer/msg4_spoofer ../test.toml rnti

# Run default PRACH detection mode
./msg4_spoofer/msg4_spoofer ../test.toml
```

### 3. Example Output

```
[INFO] Starting MSG2 decoding mode for RACH security research
[INFO] MSG2 Decoder initialized successfully
[INFO] - Carrier: 106 PRBs, PCI=1, SCS=15 kHz
[INFO] - CORESET#0: 48 PRBs, duration=1 symbols
[INFO] - Max RA-RNTI candidates: 200
[INFO] Generated 8960 RA-RNTI candidates for comprehensive coverage
[INFO] === DECODED MSG2 ===
[INFO] RA-RNTI: 1234
[INFO] RAPID: 15
[INFO] TA Command: 127
[INFO] UL Grant: 0x1A2B3C
[INFO] Temp C-RNTI: 4567
[INFO] Reception Time: 1634567890123 μs
[INFO] ==================
```

## API Reference

### MSG2Decoder Class

#### Initialization
```cpp
MSG2Decoder decoder;
bool success = decoder.init(config);
```

#### Adding PRACH Occasions
```cpp
// Add detected PRACH occasion
decoder.add_prach_occasion(s_id, t_id, f_id, ul_carrier_id, timestamp);

// Set current timing
decoder.set_timing(sfn, slot, timestamp);
```

#### Decoding MSG2
```cpp
// Decode all possible MSG2s in the signal
std::vector<msg2_pdu_t> msg2s = decoder.decode_msg2(rx_signal, signal_len);

// Decode MSG2 for specific RA-RNTI
msg2_pdu_t msg2;
bool success = decoder.decode_msg2_for_rnti(rx_signal, signal_len, ra_rnti, &msg2);
```

#### Utility Functions
```cpp
// Get active RA-RNTIs
std::vector<uint32_t> active_rntis = decoder.get_active_ra_rntis();

// Generate comprehensive RA-RNTI set
std::vector<uint32_t> all_rntis = decoder.generate_comprehensive_ra_rnti_set(1000);

// Print status
decoder.print_ra_rnti_status();
```

### RA-RNTI Utilities

```cpp
// Calculate RA-RNTI
uint32_t ra_rnti = ra_rnti_utils::calculate_ra_rnti(s_id, t_id, f_id, ul_carrier_id);

// Generate all possible RA-RNTIs for given SCS
std::vector<uint32_t> all_rntis = ra_rnti_utils::generate_all_possible_ra_rntis(15);

// Get maximum slots per frame for SCS
uint32_t max_slots = ra_rnti_utils::get_max_slots_per_frame(15);
```

## Security Research Applications

### 1. RACH Vulnerability Analysis
- Monitor MSG2 responses for timing analysis
- Identify patterns in RA-RNTI allocation
- Detect unauthorized MSG2 transmissions

### 2. DoS Attack Detection
- Track excessive MSG2 responses
- Identify resource exhaustion attempts
- Monitor abnormal PRACH patterns

### 3. Spoofing Detection
- Validate MSG2 authenticity
- Detect replay attacks
- Identify rogue base stations

### 4. Performance Analysis
- Measure MSG2 success rates
- Analyze timing advance distributions
- Study frequency occasion utilization

## Technical Details

### MSG2 Message Structure

The MSG2 contains Random Access Response (RAR) with:
- **RAPID** (6 bits): Random Access Preamble Identifier
- **TA Command** (12 bits): Timing Advance adjustment
- **UL Grant** (20 bits): Uplink resource allocation
- **Temp C-RNTI** (16 bits): Temporary Cell RNTI

### PDCCH Monitoring

The decoder monitors PDCCH in:
- **CORESET#0**: Control Resource Set for initial access
- **Type1-PDCCH CSS**: Common Search Space for MSG2
- **DCI format 1_0**: Downlink Control Information for MSG2

### Timing Considerations

- **MSG2 Window**: 4ms maximum response time
- **Frame Structure**: Depends on subcarrier spacing
- **Slot Timing**: Critical for RA-RNTI calculation

## Troubleshooting

### Common Issues

1. **No MSG2 Decoded**
   - Check RF configuration and signal strength
   - Verify PRACH occasions are being detected
   - Ensure RA-RNTI calculations are correct

2. **High False Positive Rate**
   - Reduce RA-RNTI candidate count
   - Improve CRC validation
   - Check timing synchronization

3. **Performance Issues**
   - Optimize buffer sizes
   - Reduce logging verbosity
   - Limit concurrent RA-RNTI tracking

### Debug Commands

```bash
# Enable detailed logging
export SRSLOG_LEVEL=debug

# Check RA-RNTI generation
./msg4_spoofer ../test.toml rnti

# Monitor with specific verbosity
./msg4_spoofer ../test.toml msg2 2>&1 | grep "MSG2\|RA-RNTI"
```

## Research Ethics and Legal Notice

This tool is designed exclusively for legitimate security research purposes. Users must:

1. **Obtain proper authorization** before testing on any network
2. **Comply with local regulations** and spectrum licensing
3. **Use only in controlled environments** (test labs, shielded rooms)
4. **Follow responsible disclosure** for any vulnerabilities found
5. **Respect privacy** and avoid intercepting private communications

## References

- 3GPP TS 38.211: Physical channels and modulation
- 3GPP TS 38.213: Physical layer procedures for control
- 3GPP TS 38.321: Medium Access Control (MAC) protocol specification
- srsRAN 4G/5G software radio suite documentation

## Contributing

For questions or contributions related to this research tool:

1. Follow responsible disclosure practices
2. Document security findings appropriately
3. Test thoroughly in controlled environments
4. Submit pull requests with detailed explanations

---

**Disclaimer**: This tool is for educational and research purposes only. Users are responsible for ensuring compliance with all applicable laws and regulations.