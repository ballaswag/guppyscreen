#include <sys/ioctl.h>
#include <sys/kd.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(void) {
  struct stat buffer;

  // disable blinking cursor
  if (stat("/dev/tty0", &buffer) == 0) {
    int console_fd = open("/dev/tty0", O_RDWR);
    if (console_fd) {
      ioctl(console_fd, KDSETMODE, KD_GRAPHICS);
    }
  }
  return 0;
}
