# Intégration Android : bibliothèque partagée fluluks_jni (chargée par NativeEngine.kt).
file(GLOB FL_JNI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/jni/*.c)
add_library(fluluks_jni SHARED ${FL_JNI_SOURCES})
target_link_libraries(fluluks_jni PRIVATE fluluks log)
target_compile_options(fluluks_jni PRIVATE -Wall -Wextra -Werror)
# Pas de dépendance de l'APK à la pile exécutable ; page 16 Kio (Android 15+).
target_link_options(fluluks_jni PRIVATE -Wl,-z,max-page-size=16384 -Wl,--build-id=none)

# -O2 sur toutes les cibles (fluluks, tierces, sous-modules comme mbedtls/framework),
# même en variante debug : Argon2 est 10 fois plus lent sans optimisation.
function(fl_force_o2 dir)
  get_property(_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(t IN LISTS _targets)
    get_target_property(_type ${t} TYPE)
    if(_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
      target_compile_options(${t} PRIVATE -O2)
    endif()
  endforeach()
  get_property(_subs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
  foreach(s IN LISTS _subs)
    fl_force_o2(${s})
  endforeach()
endfunction()
fl_force_o2(${CMAKE_CURRENT_SOURCE_DIR})
