#
# Copyright 2013-2023 Software Radio Systems Limited
#
# This file is part of srsRAN
#
# srsRAN is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of
# the License, or (at your option) any later version.
#
# srsRAN is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Affero General Public License for more details.
#
# A copy of the GNU Affero General Public License can be found in
# the LICENSE file in the top-level directory of this distribution
# and at http://www.gnu.org/licenses/.
#

set(SOURCES arch_select.cc
            enb_events.cc
            backtrace.c
            byte_buffer.cc
            band_helper.cc
            bearer_manager.cc
            buffer_pool.cc
            crash_handler.cc
            gen_mch_tables.c
            liblte_security.cc
            mac_pcap.cc
            mac_pcap_base.cc
            nas_pcap.cc
            network_utils.cc
            mac_pcap_net.cc
            pcap.c
            phy_cfg_nr.cc
            phy_cfg_nr_default.cc
            rrc_common.cc
            rlc_pcap.cc
            s1ap_pcap.cc
            ngap_pcap.cc
            security.cc
            standard_streams.cc
            thread_pool.cc
            threads.c
            tti_sync_cv.cc
            time_prof.cc
            version.c
            zuc.cc
            s3g.cc)

# Avoid warnings caused by libmbedtls about deprecated functions
set_source_files_properties(security.cc PROPERTIES COMPILE_FLAGS -Wno-deprecated-declarations)

# Define the static library target
add_library(srsran_common STATIC ${SOURCES})

# Custom target for build info
add_custom_target(gen_build_info COMMAND cmake -P ${CMAKE_BINARY_DIR}/SRSRANbuildinfo.cmake)
add_dependencies(srsran_common gen_build_info) # Common depends on build info

# Executable using common (assumed uses srsran_common)
add_executable(arch_select arch_select.cc)
# Explicitly link arch_select executable to the library it uses
target_link_libraries(arch_select PRIVATE srsran_common)


# --- Corrected Include Directories ---
# Use INTERFACE if headers in include/srsran/common need these paths.
# Use PRIVATE if only the *.cc files above need these paths.
target_include_directories(srsran_common INTERFACE
    ${SEC_INCLUDE_DIRS}     # Likely needed by headers using crypto types defined in common headers
    ${BACKWARD_INCLUDE_DIRS} # If common headers use backward-cpp types
)
# If common/hdr/*.h files *don't* include mbedtls or backward headers, change INTERFACE to PRIVATE above.
# It's unlikely CMAKE_SOURCE_DIR needs to be exposed via common library's interface.


# --- Corrected Link Libraries ---
# Link dependencies PRIVATELY, as they are implementation details of srsran_common.
# Remove srsran_phy dependency. Add missing system libraries.
target_link_libraries(srsran_common PRIVATE
    # srsran_phy         # <<< REMOVED! Common should NOT depend on PHY.
    support              # Common likely uses support utils
    srslog               # Common likely uses logging
    ${SEC_LIBRARIES}     # For security.cc, zuc.cc etc.
    ${BACKWARD_LIBRARIES} # For backtrace.c / crash_handler.cc
    ${SCTP_LIBRARIES}    # If network_utils.cc uses SCTP (ensure this variable is set if needed)
    Threads::Threads     # If threads.c or thread_pool.cc use pthreads
    m                    # If math functions are used
    # ${ATOMIC_LIBS}     # Uncomment if CheckAtomic determined it was needed
)
# --- End Corrected Link Libraries ---

# Link compile definitions PRIVATELY
target_compile_definitions(srsran_common PRIVATE ${BACKWARD_DEFINITIONS})

# Install the static library
install(TARGETS srsran_common DESTINATION ${LIBRARY_DIR} OPTIONAL)