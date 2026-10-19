/* Configuration de libntfs-3g 2022.10.3 pour FluLuks (phase 5), à la place du config.h produit par
 * ./configure (voir docs/superpowers/plans/2026-10-19-phase5-ntfs.md, Global Constraints). Linux (PC) et
 * Android (bionic) : en-têtes standard présents. Pas d'opérations de périphérique par défaut (unix_io.c
 * n'est pas compilé : ntfs_dev.c fournit les nôtres), ni ACL POSIX, ni xattr, ni EFS, ni syslog, ni
 * recherche des montages (mntent : ntfs_check_if_mounted rend « non monté »). */
#ifndef FL_NTFS3G_CONFIG_H
#define FL_NTFS3G_CONFIG_H

#define PACKAGE_NAME "ntfs-3g"
#define PACKAGE_VERSION "2022.10.3"
#define VERSION "2022.10.3"
#define STDC_HEADERS 1

#define HAVE_BYTESWAP_H 1
#define HAVE_CTYPE_H 1
#define HAVE_ENDIAN_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LINUX_FS_H 1
#define HAVE_LINUX_HDREG_H 1
#define HAVE_LOCALE_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_SYSMACROS_H 1
#define MAJOR_IN_SYSMACROS 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_WCHAR_H 1

#define HAVE_CLOCK_GETTIME 1
#define HAVE_DAEMON 1
#define HAVE_FFS 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_MBSINIT 1
#define HAVE_REALPATH 1
#define HAVE_STRSEP 1

#define NO_NTFS_DEVICE_DEFAULT_IO_OPS 1

#endif
