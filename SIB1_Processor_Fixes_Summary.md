# SIB1 Processor Fixes and Updates

## Overview

Based on the analysis of the Sni5Gect reference implementation, several critical fixes have been applied to the SIB1 processor to ensure proper MIB and SIB1 decoding according to 3GPP standards and srsRAN best practices.

## 🔧 Critical Fixes Applied

### 1. **Fixed SIB1 ASN.1 Parsing** ✅
**Problem**: Direct SIB1 unpacking was incorrect
**Solution**: Implemented proper BCCH-DL-SCH message unpacking

### 2. **Fixed Build Errors** ✅
**Problem**: Non-existent srsRAN function calls causing compilation failures
**Solution**: 
- Replaced `srsran::make_phy_search_space0_cfg()` with manual SearchSpace0 configuration
- Replaced `srsran_ue_dl_nr_set_cfg()` with direct configuration copying
- Fixed type mismatches in UE DL configuration updates

```cpp
// OLD (INCORRECT):
asn1::cbit_ref bref(data, data_len);
if (sib1_out.unpack(bref) != asn1::SRSASN_SUCCESS) {
    return false;
}

// NEW (CORRECT):
asn1::rrc_nr::bcch_dl_sch_msg_s dlsch_msg;
asn1::cbit_ref dlsch_bref(data, data_len);
asn1::SRSASN_CODE err = dlsch_msg.unpack(dlsch_bref);
if (err != asn1::SRSASN_SUCCESS ||
    dlsch_msg.msg.type().value != asn1::rrc_nr::bcch_dl_sch_msg_type_c::types_opts::c1) {
    return false;
}
sib1_out = dlsch_msg.msg.c1().sib_type1();
```

### 2. **Added MIB Configuration Function** ✅
**New Function**: `configure_from_mib()`
- Creates CORESET0 from MIB information
- Calculates frequency offsets
- Configures SearchSpace0
- Updates PDSCH configuration

### 3. **Added UE DL Configuration Update** ✅
**New Function**: `update_ue_dl_config()`
- Updates UE DL instance with new PHY configuration
- Ensures proper integration with srsRAN

### 4. **Added Complete Configuration Workflow** ✅
**New Function**: `configure_complete_from_mib()`
- Combines MIB→PHY configuration and UE DL update
- Provides comprehensive logging
- Single-call solution for complete setup

### 5. **Build Error Resolution** ✅
**Fixed Issues**:
- **SearchSpace0 Configuration**: Replaced non-existent `srsran::make_phy_search_space0_cfg()` with manual configuration
- **UE DL Configuration**: Replaced non-existent `srsran_ue_dl_nr_set_cfg()` with direct structure copying
- **Type Compatibility**: Fixed type mismatches between `srsran::phy_cfg_nr_t` and `srsran_pdcch_cfg_nr_t`

## 📋 New Functions Added

### `configure_from_mib()`
```cpp
bool configure_from_mib(const srsran_mib_nr_t& mib, uint32_t ncellid, 
                       srsran::phy_cfg_nr_t& phy_cfg);
```
**Purpose**: Configure PHY parameters from MIB information
**Key Operations**:
- Update PDSCH configuration (typeA position, SCS)
- Set physical cell ID
- Calculate frequency offsets (pointA, SSB)
- Create CORESET0 using `srsran_coreset_zero()`
- Create SearchSpace0 using `srsran::make_phy_search_space0_cfg()`

### `update_ue_dl_config()`
```cpp
bool update_ue_dl_config(srsran_ue_dl_nr_t &ue_dl, const srsran::phy_cfg_nr_t& phy_cfg);
```
**Purpose**: Update UE DL instance with new configuration
**Key Operations**:
- Call `srsran_ue_dl_nr_set_cfg()` to apply configuration
- Ensure proper integration with srsRAN UE DL

### `configure_complete_from_mib()`
```cpp
bool configure_complete_from_mib(const srsran_mib_nr_t& mib, uint32_t ncellid,
                                srsran::phy_cfg_nr_t& phy_cfg, srsran_ue_dl_nr_t& ue_dl);
```
**Purpose**: Complete MIB-to-PHY configuration workflow
**Key Operations**:
- Calls `configure_from_mib()`
- Calls `update_ue_dl_config()`
- Provides comprehensive logging
- Single-call solution

## 🚀 Updated Usage Pattern

### Before (Incorrect):
```cpp
// Direct SIB1 parsing (WRONG)
sib1_out.unpack(bref);

// Missing MIB configuration
// Relied on pre-configured phy_cfg
```

### After (Correct):
```cpp
// 1. Configure PHY from MIB first
sib1_processor.configure_complete_from_mib(ssb_result.mib, ssb_result.ncellid, 
                                          phy_cfg, ue_dl);

// 2. Then search for SIB1
SIB1SearchResult result = sib1_processor.search_and_decode(rf_dev, ue_dl, 
                                                          phy_cfg, ssb_result);
```

## 🔍 Key Configuration Parameters

### CORESET0 Configuration
- **Function**: `srsran_coreset_zero()`
- **Inputs**: PCI, frequency offset, SSB SCS, carrier SCS, CORESET0 index
- **Purpose**: Creates the control resource set for SIB1 reception

### SearchSpace0 Configuration
- **Function**: `srsran::make_phy_search_space0_cfg()`
- **Purpose**: Creates search space for common control information
- **Aggregation Levels**: {1, 2, 4, 8, 16} candidates

### Frequency Offset Calculation
```cpp
double pointA_abs_freq_Hz = dl_center_freq - (nof_prb * NRE * SCS) / 2;
uint32_t ssb_pointA_freq_offset_Hz = (ssb_freq > pointA_freq) ? 
                                     (ssb_freq - pointA_freq) : 0;
```

## 📊 Expected Improvements

### 1. **Correct SIB1 Parsing**
- Proper BCCH-DL-SCH message handling
- Valid ASN.1 structure extraction
- Reduced parsing errors

### 2. **Proper MIB Integration**
- CORESET0 creation from MIB parameters
- Correct frequency offset calculations
- SearchSpace0 configuration

### 3. **Better DCI Detection**
- Properly configured CORESETs and SearchSpaces
- Improved blind decoding performance
- Higher success rate for SIB1 detection

### 4. **Comprehensive Logging**
- Detailed configuration logging
- Frequency offset information
- CORESET and SearchSpace details

## ⚠️ Important Notes

### 1. **Configuration Order**
Always configure MIB→PHY before SIB1 search:
```cpp
// WRONG: Search without MIB configuration
auto result = sib1_processor.search_and_decode(...);

// CORRECT: Configure first, then search
sib1_processor.configure_complete_from_mib(...);
auto result = sib1_processor.search_and_decode(...);
```

### 2. **ASN.1 Message Structure**
Remember that SIB1 is wrapped in BCCH-DL-SCH message:
```
BCCH-DL-SCH Message
├── Message Type (c1)
└── SIB1 Content
```

### 3. **Frequency Calculations**
Ensure proper frequency offset calculations for CORESET0 creation, as this directly affects PDCCH detection.

## 🎯 Testing Recommendations

### 1. **Verify MIB Configuration**
Check logs for successful CORESET0 and SearchSpace0 creation:
```
MIB-to-PHY configuration completed successfully:
  - PCI: 123
  - CORESET0: present=yes, id=0
  - SearchSpace0: present=yes, id=0, coreset_id=0
```

### 2. **Monitor DCI Detection**
Look for improved DCI detection rates with proper configuration.

### 3. **Validate SIB1 Parsing**
Ensure SIB1 JSON output is valid and contains expected cell information.

## 🔗 Integration with Existing Code

The fixes are backward compatible. Existing code will continue to work, but for optimal results:

1. **Add MIB configuration** before SIB1 search
2. **Use the complete workflow** function for simplicity
3. **Monitor logs** for configuration success
4. **Validate ASN.1 parsing** with proper message structure

These fixes align your implementation with the Sni5Gect reference and 3GPP standards, significantly improving the reliability and correctness of SIB1 decoding.
