# The verification probes: small programs that run the simulator's own code
# for the sweeps in verification/. Built only with
#   cmake -S . -B build-verify -G Ninja -DATLAS_BUILD_VERIFICATION=ON
# (scripts/run_verification.sh does this). Included from the root
# CMakeLists.txt, not added as a subdirectory, so the generated resource
# sources in APP_RESOURCES are in scope. The executables land in
# <build>/verification/probes/.
set(ATLAS_VERIFY_PROBE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/verification/probes)
set(ATLAS_VERIFY_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/verification/lib)

foreach(probe sweep cacheprobe asm)
    if(probe STREQUAL "sweep")
        set(source sweep_probe.cpp)
    elseif(probe STREQUAL "cacheprobe")
        set(source cache_probe.cpp)
    else()
        set(source asm_probe.cpp)
    endif()
    add_executable(atlas_verify_${probe} ${ATLAS_VERIFY_PROBE_DIR}/${source} ${APP_RESOURCES})
    target_include_directories(atlas_verify_${probe} PRIVATE ${ATLAS_VERIFY_LIB_DIR})
    target_link_libraries(atlas_verify_${probe} PRIVATE atlas_core)
    set_target_properties(atlas_verify_${probe} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/verification/probes)
endforeach()
