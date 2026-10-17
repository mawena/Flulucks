cmake_minimum_required(VERSION 3.20)
project(fluluks C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(FL_TESTS "Compiler les tests" OFF)
option(FL_SANITIZE "ASan + UBSan (global : s'applique aussi aux dépendances tierces, pour éviter les faux positifs de code non instrumenté)" OFF)

if(FL_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

include(cmake/third_party.cmake)

# Les tâches suivantes ajoutent leurs sources ici.
set(FL_SOURCES
  src/error.c
  src/secure.c
  src/blockdev/blockdev.c
  src/blockdev/file_bdev.c
  src/blockdev/slice_bdev.c
  src/part/crc32.c
  src/part/gpt.c
  src/part/mbr.c
  src/part/partition.c
  src/probe/probe.c
  src/crypto/fl_crypto.c
  src/crypto/sector_cipher.c
  src/luks/luks.c
  src/luks/luks1.c
  src/luks/luks2.c
  src/luks/luks2_json.c
  src/luks/b64.c
  src/luks/af.c
  src/luks/crypt_bdev.c
)

add_library(fluluks STATIC ${FL_SOURCES})
target_include_directories(fluluks PUBLIC include PRIVATE src)
target_compile_options(fluluks PRIVATE -Wall -Wextra -Werror)
target_link_libraries(fluluks PUBLIC fl_mbedcrypto fl_argon2 fl_cjson fl_lwext4)

if(FL_TESTS)
  enable_testing()

  # Génère les images de test (sans root) ; idempotent grâce au fichier .stamp.
  add_test(NAME generate_fixtures
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../tools/fixtures/make_fixtures.sh ${CMAKE_BINARY_DIR}/fixtures)
  set_tests_properties(generate_fixtures PROPERTIES FIXTURES_SETUP fx)

  # fl_add_test(nom [LABELS l...]) : compile tests/<nom>.c, lie à fluluks, enregistre dans CTest.
  function(fl_add_test name)
    cmake_parse_arguments(T "" "" "LABELS" ${ARGN})
    add_executable(${name} tests/${name}.c)
    target_link_libraries(${name} PRIVATE fluluks)
    target_include_directories(${name} PRIVATE tests/support src)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
      ENVIRONMENT "FL_FIXTURES=${CMAKE_BINARY_DIR}/fixtures"
      FIXTURES_REQUIRED fx)
    if(T_LABELS)
      set_tests_properties(${name} PROPERTIES LABELS "${T_LABELS}")
    endif()
  endfunction()

  fl_add_test(test_error)
  fl_add_test(test_blockdev)
  fl_add_test(test_partition)
  fl_add_test(test_probe)
  fl_add_test(test_crypto)
  fl_add_test(test_luks1)
  fl_add_test(test_luks2)
  fl_add_test(test_luks2_lent LABELS lent)
endif()
