# Dépendances tierces : compilées sans -Werror, avertissements masqués.
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# --- mbedTLS (cible mbedcrypto) ---
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
# Tables AES en ROM : sans cela mbedTLS les construit paresseusement au premier usage
# (écriture de variables statiques non protégée -> course de données entre threads).
# Le define est PUBLIC sur mbedcrypto : nos sources voient la même configuration.
set(MBEDTLS_USER_CONFIG_FILE ${CMAKE_CURRENT_LIST_DIR}/mbedtls_user_config.h CACHE FILEPATH "" FORCE)
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
# Le sous-module reste sur le commit amont. Ses src/ et include/ sont copiés dans le
# dossier de build, puis les correctifs de third_party/patches/lwext4/*.patch y sont
# appliqués dans l'ordre (voir third_party/PATCHES.md). La copie est refaite dès que
# l'empreinte (sources amont + correctifs) change ; un correctif qui ne s'applique pas
# fait échouer la configuration.
find_package(Git REQUIRED)
set(LWEXT4_UPSTREAM ${TP}/lwext4)
set(LWEXT4_DIR ${CMAKE_BINARY_DIR}/third_party/lwext4)
file(GLOB LWEXT4_PATCHES CONFIGURE_DEPENDS ${TP}/patches/lwext4/*.patch)
list(SORT LWEXT4_PATCHES)
file(GLOB LWEXT4_UPSTREAM_FILES CONFIGURE_DEPENDS
  ${LWEXT4_UPSTREAM}/src/*.c ${LWEXT4_UPSTREAM}/include/*.h ${LWEXT4_UPSTREAM}/include/misc/*.h)
list(SORT LWEXT4_UPSTREAM_FILES)
if(NOT LWEXT4_UPSTREAM_FILES)
  message(FATAL_ERROR "lwext4 absent : lancer « git submodule update --init --recursive »")
endif()
# Contenu des correctifs et des sources amont : toute modification relance la configuration.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LWEXT4_PATCHES} ${LWEXT4_UPSTREAM_FILES})
set(_lw_stamp "")
foreach(f IN LISTS LWEXT4_UPSTREAM_FILES LWEXT4_PATCHES)
  file(SHA256 ${f} _h)
  file(RELATIVE_PATH _r ${TP} ${f})
  string(APPEND _lw_stamp "${_h}  ${_r}\n")
endforeach()
set(_lw_old "")
if(EXISTS ${LWEXT4_DIR}/.fl_stamp)
  file(READ ${LWEXT4_DIR}/.fl_stamp _lw_old)
endif()
if(NOT _lw_old STREQUAL _lw_stamp)
  file(REMOVE_RECURSE ${LWEXT4_DIR})
  file(MAKE_DIRECTORY ${LWEXT4_DIR})
  file(COPY ${LWEXT4_UPSTREAM}/src ${LWEXT4_UPSTREAM}/include DESTINATION ${LWEXT4_DIR})
  foreach(p IN LISTS LWEXT4_PATCHES)
    # GIT_CEILING_DIRECTORIES : sans lui, git apply lancé sous un dépôt (le build est
    # souvent dans le dépôt) ignore en silence les chemins et réussit sans rien faire.
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}/third_party
              ${GIT_EXECUTABLE} apply --verbose -p1 ${p}
      WORKING_DIRECTORY ${LWEXT4_DIR}
      RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0 OR _err MATCHES "Skipped patch" OR NOT _err MATCHES "Applied patch")
      file(REMOVE_RECURSE ${LWEXT4_DIR})
      message(FATAL_ERROR "Correctif lwext4 non applicable : ${p}\n${_out}${_err}")
    endif()
    get_filename_component(_n ${p} NAME)
    message(STATUS "lwext4 : correctif appliqué ${_n}")
  endforeach()
  file(WRITE ${LWEXT4_DIR}/.fl_stamp "${_lw_stamp}")
endif()

file(GLOB LWEXT4_SRC ${LWEXT4_DIR}/src/*.c)
add_library(fl_lwext4 STATIC ${LWEXT4_SRC})
target_include_directories(fl_lwext4 SYSTEM PUBLIC ${LWEXT4_DIR}/include)
target_compile_definitions(fl_lwext4 PUBLIC
  CONFIG_USE_DEFAULT_CFG=1
  CONFIG_EXT4_BLOCKDEVS_COUNT=8
  CONFIG_EXT4_MOUNTPOINTS_COUNT=8
  CONFIG_DEBUG_PRINTF=0
  CONFIG_DEBUG_ASSERT=0)
target_compile_options(fl_lwext4 PRIVATE -w)
