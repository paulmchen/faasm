include_directories(
        ${CMAKE_CURRENT_LIST_DIR}
        ${FAASM_FAABRIC_ROOT_DIR}/tests/utils
)

set(LIB_FILES
        matrix_utils.cpp
        system_utils.cpp
        worker_utils.cpp
        utils.h
        )

faasm_private_lib(test_utils "${LIB_FILES}")

target_link_libraries(test_utils
        catch
        faabric_test_utils
        faasm
        upload_lib
        faaslet_lib
        emulator
        )