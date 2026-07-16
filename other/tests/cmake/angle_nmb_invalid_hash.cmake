file(REMOVE_RECURSE "${ANGLE_NMB_TEST_ROOT}")
file(MAKE_DIRECTORY "${ANGLE_NMB_TEST_ROOT}")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "-DANGLE_NMB_MODULE=${ANGLE_NMB_MODULE}"
        "-DANGLE_NMB_TEST_SOURCE=${ANGLE_NMB_TEST_SOURCE}"
        "-DANGLE_NMB_TEST_ROOT=${ANGLE_NMB_TEST_ROOT}"
        -P
        "${ANGLE_NMB_TEST_CHILD}"
    RESULT_VARIABLE result
)

if(result EQUAL 0)
    message(FATAL_ERROR "ANGLE invalid-hash test unexpectedly succeeded.")
endif()
if(EXISTS "${ANGLE_NMB_TEST_ROOT}/archive.zip")
    message(FATAL_ERROR "ANGLE invalid-hash test retained a failed archive.")
endif()
if(EXISTS "${ANGLE_NMB_TEST_ROOT}/package/.angle-nmb-ready")
    message(FATAL_ERROR "ANGLE invalid-hash test marked a package as valid.")
endif()
