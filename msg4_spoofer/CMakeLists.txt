CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
set(SPOOFER_SRC_DIR src)
set(SPOOFER_HDR_DIR hdr)

file(GLOB_RECURSE SPOOFER_SOURCES CONFIGURE_DEPENDS "${SPOOFER_SRC_DIR}/*.cc")

add_executable(msg4_spoofer ${SPOOFER_SOURCES})

set_target_properties(msg4_spoofer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    LINKER_LANGUAGE CXX
)

target_include_directories(msg4_spoofer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/${SPOOFER_HDR_DIR}"
)

target_link_libraries(msg4_spoofer PRIVATE

    -Wl,--start-group
    srsran_phy
    srsran_phch
    srsran_common
    support
    srslog
    srsran_rf
    -Wl,--end-group

    pthread

)

install(TARGETS msg4_spoofer DESTINATION /usr/local/bin OPTIONAL)

