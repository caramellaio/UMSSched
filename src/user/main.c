#include "ums_api.h"
#include "../shared/ums_request.h"
#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
  int err = 0;
  int fd = open("/dev/usermodscheddev", 0);

  err = ioctl(fd, UMS_REQUEST_ENTER_UMS_SCHEDULING, 11);
}
