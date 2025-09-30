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

add_subdirectory(agc)
add_subdirectory(ch_estimation)
add_subdirectory(common)
add_subdirectory(fec)
add_subdirectory(mimo)
add_subdirectory(phch)
add_subdirectory(rf)
add_subdirectory(sync)
add_subdirectory(utils)
add_subdirectory(channel)
add_subdirectory(dft)
add_subdirectory(io)
add_subdirectory(modem)
add_subdirectory(resampling)
add_subdirectory(scrambling)
add_subdirectory(ue)
add_subdirectory(enb)
add_subdirectory(gnb)
add_subdirectory(cfr)

# Aggregate object files from subdirectories
set(srsran_srcs     $<TARGET_OBJECTS:srsran_agc>
        $<TARGET_OBJECTS:srsran_ch_estimation>
        $<TARGET_OBJECTS:srsran_phy_common>
        $<TARGET_OBJECTS:srsran_fec>
        $<TARGET_OBJECTS:srsran_mimo>
        $<TARGET_OBJECTS:srsran_phch>
        $<TARGET_OBJECTS:srsran_sync>
        $<TARGET_OBJECTS:srsran_utils>
        $<TARGET_OBJECTS:srsran_channel>
        $<TARGET_OBJECTS:srsran_dft>
        $<TARGET_OBJECTS:srsran_io>
        $<TARGET_OBJECTS:srsran_modem>
        $<TARGET_OBJECTS:srsran_resampling>
        $<TARGET_OBJECTS:srsran_scrambling>
        $<TARGET_OBJECTS:srsran_ue>
        $<TARGET_OBJECTS:srsran_enb>
        $<TARGET_OBJECTS:srsran_gnb>
        $<TARGET_OBJECTS:srsran_cfr>
        )

# Create the static library from object files - Explicitly STATIC
add_library(srsran_phy STATIC ${srsran_srcs})

# --- Define ALL dependencies with PRIVATE scope ---
# These are needed to *build* srsran_phy, but targets linking srsran_phy
# generally don't need to call these functions directly.
target_link_libraries(srsran_phy PRIVATE
    # Core srsRAN dependencies used by phy object files
    srsran_common   # Must link common if phy uses its functions/types
    support         # Must link support if phy uses its functions/types
    srslog          # Must link srslog if phy uses its functions/types

    # External libraries used by phy object files
    Threads::Threads # Use imported target for pthreads
    ${FFT_LIBRARIES} # FFTW library variable/target
    ${SEC_LIBRARIES} # MbedTLS library variable/target
    m                # Math library
    # ${ATOMIC_LIBS} # Uncomment only if CheckAtomic found it was needed and set the variable
)
# --- End target_link_libraries ---


# Install the static library
install(TARGETS srsran_phy DESTINATION ${LIBRARY_DIR} OPTIONAL)

# Optional: Define and install a shared library version if needed by other parts
# add_library(srsran_phy_shared SHARED ${srsran_srcs})
# target_link_libraries(srsran_phy_shared PRIVATE ...) # Repeat dependencies from above
# set_target_properties(srsran_phy_shared PROPERTIES OUTPUT_NAME srsran_phy)
# install(TARGETS srsran_phy_shared DESTINATION ${LIBRARY_DIR} OPTIONAL)