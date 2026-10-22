#include <string.h>

#include "fluluks/error.h"
#include "fluluks/format.h"
#include "fs/fs_internal.h"

#define MIB (1024ull * 1024ull)

int fl_mkfs_ext4(fl_bdev *part, const char *label, uint32_t root_uid, uint32_t root_gid) {
  if (!part) return FL_EINVAL;
  if (!label) label = "";
  if (strlen(label) > 16) return FL_EINVAL; /* s_volume_name : 16 octets */
  if (fl_bdev_size_bytes(part) < 8 * MIB) return FL_ENOSPC; /* journal de 4 Mio au moins compris */
  uint8_t uuid[16];
  int rc = fl_random(uuid, sizeof uuid);
  if (rc != FL_OK) return rc;
  uuid[6] = (uint8_t)((uuid[6] & 0x0F) | 0x40); /* UUID version 4 */
  uuid[8] = (uint8_t)((uuid[8] & 0x3F) | 0x80);
  return fl_ext4_mkfs(part, label, uuid, root_uid, root_gid);
}
