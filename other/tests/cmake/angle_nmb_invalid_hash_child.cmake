set(ANGLE_NMB_TESTING ON)
include("${ANGLE_NMB_MODULE}")
if(WIN32)
    set(source_url "file:///${ANGLE_NMB_TEST_SOURCE}")
else()
    set(source_url "file://${ANGLE_NMB_TEST_SOURCE}")
endif()
_angle_nmb_download_archive(
    "${source_url}"
    "${ANGLE_NMB_TEST_ROOT}/archive.zip"
    "0000000000000000000000000000000000000000000000000000000000000000"
)
