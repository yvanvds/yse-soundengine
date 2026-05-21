# dump_patcher_meta — emits a JSON description of every registered
# patcher object's documentation metadata for the auto-generated docs
# page. See documentation/source/_data/patcher_objects.json and the
# Sphinx pre-build hook in conf.py.
#
# Linked directly against yse_objects (same trick the test binary uses)
# so internal pRegistry / pObject getters are reachable without going
# through the DLL export boundary.

add_executable(dump_patcher_meta main.cpp)

target_link_libraries(dump_patcher_meta PRIVATE yse_objects)
target_compile_features(dump_patcher_meta PRIVATE cxx_std_17)

# main.cpp pulls in patcher/pObject.h which transitively includes
# utils/json.hpp; mirror the patcher-TU warning suppressions so the
# vendored json.hpp does not flood the build with the same diagnostics
# already suppressed engine-wide. Engine code itself is unaffected.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(dump_patcher_meta PRIVATE
    -Wno-deprecated-literal-operator
    -Wno-tautological-overlap-compare
  )
endif()

set_target_properties(dump_patcher_meta PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
