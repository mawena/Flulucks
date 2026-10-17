# Dépendances tierces : compilées sans -Werror, avertissements masqués.
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# --- mbedTLS (cible mbedcrypto) ---
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/mbedtls ${CMAKE_CURRENT_BINARY_DIR}/mbedtls EXCLUDE_FROM_ALL)
add_library(fl_mbedcrypto INTERFACE)
target_link_libraries(fl_mbedcrypto INTERFACE mbedcrypto)
target_include_directories(fl_mbedcrypto SYSTEM INTERFACE ${TP}/mbedtls/include)

# --- Argon2 (ref.c partout : opt.c exige SSE) ---
add_library(fl_argon2 STATIC
  ${TP}/argon2/src/argon2.c ${TP}/argon2/src/core.c ${TP}/argon2/src/encoding.c
  ${TP}/argon2/src/thread.c ${TP}/argon2/src/ref.c ${TP}/argon2/src/blake2/blake2b.c)
target_include_directories(fl_argon2 SYSTEM PUBLIC ${TP}/argon2/include)
target_include_directories(fl_argon2 PRIVATE ${TP}/argon2/src)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(fl_argon2 PUBLIC Threads::Threads)
target_compile_options(fl_argon2 PRIVATE -w)

# --- cJSON ---
add_library(fl_cjson STATIC ${TP}/cjson/cJSON.c)
target_include_directories(fl_cjson SYSTEM PUBLIC ${TP}/cjson)
target_compile_options(fl_cjson PRIVATE -w)

# --- lwext4 ---
file(GLOB LWEXT4_SRC ${TP}/lwext4/src/*.c)
add_library(fl_lwext4 STATIC ${LWEXT4_SRC})
target_include_directories(fl_lwext4 SYSTEM PUBLIC ${TP}/lwext4/include)
target_compile_definitions(fl_lwext4 PUBLIC
  CONFIG_USE_DEFAULT_CFG=1
  CONFIG_EXT4_BLOCKDEVS_COUNT=8
  CONFIG_EXT4_MOUNTPOINTS_COUNT=8
  CONFIG_DEBUG_PRINTF=0
  CONFIG_DEBUG_ASSERT=0)
target_compile_options(fl_lwext4 PRIVATE -w)
