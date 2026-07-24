add_executable(fbvector_sidecar
    src/hnsw.cpp
    src/sidecar_main.cpp
)

target_include_directories(fbvector_sidecar PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${FIREBIRD_INCLUDE_DIR}
)

target_link_libraries(fbvector_sidecar PRIVATE
    fbvector
    ${FIREBIRD_CLIENT_LIB}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fbvector_sidecar PRIVATE -Wall -Wextra)
endif()
