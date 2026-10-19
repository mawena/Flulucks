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

# --- FatFs R0.16 (ChaN) ---
# Copie d'amont non modifiée dans third_party/fatfs (voir third_party/PATCHES.md). Comme pour lwext4, ses
# sources sont copiées dans le dossier de build puis les correctifs de third_party/patches/fatfs/*.patch y
# sont appliqués dans l'ordre ; empreinte et échec de configuration identiques. Notre configuration est
# cmake/fatfs/ffconf.h (ff.h l'inclut par "ffconf.h", absent du dossier d'amont : il est trouvé ici).
set(FATFS_DIR ${CMAKE_BINARY_DIR}/third_party/fatfs)
file(GLOB FATFS_PATCHES CONFIGURE_DEPENDS ${TP}/patches/fatfs/*.patch)
list(SORT FATFS_PATCHES)
file(GLOB FATFS_UPSTREAM_FILES CONFIGURE_DEPENDS ${TP}/fatfs/*.c ${TP}/fatfs/*.h)
list(SORT FATFS_UPSTREAM_FILES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FATFS_PATCHES} ${FATFS_UPSTREAM_FILES})
set(_ff_stamp "")
foreach(f IN LISTS FATFS_UPSTREAM_FILES FATFS_PATCHES)
  file(SHA256 ${f} _h)
  file(RELATIVE_PATH _r ${TP} ${f})
  string(APPEND _ff_stamp "${_h}  ${_r}\n")
endforeach()
set(_ff_old "")
if(EXISTS ${FATFS_DIR}/.fl_stamp)
  file(READ ${FATFS_DIR}/.fl_stamp _ff_old)
endif()
if(NOT _ff_old STREQUAL _ff_stamp)
  file(REMOVE_RECURSE ${FATFS_DIR})
  file(MAKE_DIRECTORY ${FATFS_DIR})
  file(COPY ${FATFS_UPSTREAM_FILES} DESTINATION ${FATFS_DIR})
  foreach(p IN LISTS FATFS_PATCHES)
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}/third_party
              ${GIT_EXECUTABLE} apply --verbose -p1 ${p}
      WORKING_DIRECTORY ${FATFS_DIR}
      RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0 OR _err MATCHES "Skipped patch" OR NOT _err MATCHES "Applied patch")
      file(REMOVE_RECURSE ${FATFS_DIR})
      message(FATAL_ERROR "Correctif FatFs non applicable : ${p}\n${_out}${_err}")
    endif()
    get_filename_component(_n ${p} NAME)
    message(STATUS "FatFs : correctif appliqué ${_n}")
  endforeach()
  file(WRITE ${FATFS_DIR}/.fl_stamp "${_ff_stamp}")
endif()
add_library(fl_fatfs STATIC ${FATFS_DIR}/ff.c ${FATFS_DIR}/ffunicode.c ${FATFS_DIR}/ffsystem.c)
target_include_directories(fl_fatfs SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR}/fatfs ${FATFS_DIR})
target_compile_options(fl_fatfs PRIVATE -w)

# --- libntfs-3g 2022.10.3 (phase 5) --------------------------------------------------------------
# Copie d'amont non modifiée dans third_party/ntfs-3g (voir third_party/PATCHES.md), copiée dans le dossier
# de build puis corrigée par third_party/patches/ntfs-3g/*.patch, comme FatFs. Notre configuration est
# cmake/ntfs-3g/config.h ; unix_io.c et win32_io.c ne sont pas copiés (ntfs_dev.c fournit les opérations de
# périphérique). Les en-têtes s'incluent par « ntfs-3g/xxx.h » depuis notre code, par « xxx.h » entre eux.
set(NTFS3G_DIR ${CMAKE_BINARY_DIR}/third_party/ntfs-3g)
file(GLOB NTFS3G_PATCHES CONFIGURE_DEPENDS ${TP}/patches/ntfs-3g/*.patch)
list(SORT NTFS3G_PATCHES)
file(GLOB NTFS3G_UPSTREAM_FILES CONFIGURE_DEPENDS ${TP}/ntfs-3g/libntfs-3g/*.c ${TP}/ntfs-3g/include/ntfs-3g/*.h)
list(SORT NTFS3G_UPSTREAM_FILES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${NTFS3G_PATCHES} ${NTFS3G_UPSTREAM_FILES})
set(_n3_stamp "")
foreach(f IN LISTS NTFS3G_UPSTREAM_FILES NTFS3G_PATCHES)
  file(SHA256 ${f} _h)
  file(RELATIVE_PATH _r ${TP} ${f})
  string(APPEND _n3_stamp "${_h}  ${_r}\n")
endforeach()
set(_n3_old "")
if(EXISTS ${NTFS3G_DIR}/.fl_stamp)
  file(READ ${NTFS3G_DIR}/.fl_stamp _n3_old)
endif()
if(NOT _n3_old STREQUAL _n3_stamp)
  file(REMOVE_RECURSE ${NTFS3G_DIR})
  file(MAKE_DIRECTORY ${NTFS3G_DIR})
  file(COPY ${TP}/ntfs-3g/libntfs-3g ${TP}/ntfs-3g/include DESTINATION ${NTFS3G_DIR})
  foreach(p IN LISTS NTFS3G_PATCHES)
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}/third_party
              ${GIT_EXECUTABLE} apply --verbose -p1 ${p}
      WORKING_DIRECTORY ${NTFS3G_DIR}
      RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0 OR _err MATCHES "Skipped patch" OR NOT _err MATCHES "Applied patch")
      file(REMOVE_RECURSE ${NTFS3G_DIR})
      message(FATAL_ERROR "Correctif libntfs-3g non applicable : ${p}\n${_out}${_err}")
    endif()
    get_filename_component(_n ${p} NAME)
    message(STATUS "libntfs-3g : correctif appliqué ${_n}")
  endforeach()
  file(WRITE ${NTFS3G_DIR}/.fl_stamp "${_n3_stamp}")
endif()
set(NTFS3G_SOURCES)
foreach(f IN LISTS NTFS3G_UPSTREAM_FILES)
  if(f MATCHES "\\.c$")
    file(RELATIVE_PATH _r ${TP}/ntfs-3g ${f})
    list(APPEND NTFS3G_SOURCES ${NTFS3G_DIR}/${_r})
  endif()
endforeach()
add_library(fl_ntfs3g STATIC ${NTFS3G_SOURCES})
target_include_directories(fl_ntfs3g SYSTEM PUBLIC ${NTFS3G_DIR}/include ${CMAKE_CURRENT_LIST_DIR}/ntfs-3g)
target_include_directories(fl_ntfs3g PRIVATE ${NTFS3G_DIR}/include/ntfs-3g)
target_compile_definitions(fl_ntfs3g PUBLIC HAVE_CONFIG_H)
target_compile_definitions(fl_ntfs3g PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
set_target_properties(fl_ntfs3g PROPERTIES C_EXTENSIONS ON)
target_compile_options(fl_ntfs3g PRIVATE -w)
