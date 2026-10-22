#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "fluluks/error.h"
#include "fluluks/format.h"

/* /dev/urandom existe sur Linux et Android ; une lecture courte ou en échec n'est jamais complétée par un
 * aléa plus faible : l'appelant reçoit FL_EIO. */
int fl_random(void *buf, size_t len) {
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return FL_EIO;
  unsigned char *p = buf;
  size_t done = 0;
  while (done < len) {
    ssize_t r = read(fd, p + done, len - done);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) {
      close(fd);
      return FL_EIO;
    }
    done += (size_t)r;
  }
  close(fd);
  return FL_OK;
}
