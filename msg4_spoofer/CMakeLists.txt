CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
set(SPOOFER_SRC_DIR src)
set(SPOOFER_HDR_DIR hdr)

# Common source files
set(COMMON_SOURCES
    ${SPOOFER_SRC_DIR}/rf.cc
    ${SPOOFER_SRC_DIR}/rf_base.cc
    ${SPOOFER_SRC_DIR}/rf_file.cc
    ${SPOOFER_SRC_DIR}/data_source.cc
    ${SPOOFER_SRC_DIR}/logging.cc
)

# msg4_spoofer executable (RAR decoder)
add_executable(msg4_spoofer 
    ${COMMON_SOURCES} 
    ${SPOOFER_SRC_DIR}/main.cc
    ${SPOOFER_SRC_DIR}/rar_decoder.cc
    ${SPOOFER_SRC_DIR}/ssb_decoder.cc
    ${SPOOFER_SRC_DIR}/sib1_processor.cc
)

set_target_properties(msg4_spoofer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    LINKER_LANGUAGE CXX
)

target_include_directories(msg4_spoofer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/${SPOOFER_HDR_DIR}"
)

target_link_libraries(msg4_spoofer PRIVATE
    srsran_ue
    srsran_phy
    srsran_common
    srsran_mac
    support
    srslog
    srsran_rf
    rrc_nr_asn1
    srsran_asn1
    Threads::Threads
    ${FFT_LIBRARIES}
    ${SEC_LIBRARIES}
    m
    pthread
)

install(TARGETS msg4_spoofer DESTINATION /usr/local/bin OPTIONAL)
