/* Ajouts à la configuration mbedTLS (voir MBEDTLS_USER_CONFIG_FILE dans third_party.cmake). */
#ifndef FL_MBEDTLS_USER_CONFIG_H
#define FL_MBEDTLS_USER_CONFIG_H

/* Tables AES constantes (ROM) : pas d'initialisation paresseuse partagée, donc sûr entre threads. */
#define MBEDTLS_AES_ROM_TABLES

#endif
