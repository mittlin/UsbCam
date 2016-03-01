#ifndef USBCAM_H
#define USBCAM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>		/* getopt_long() */
#include <fcntl.h>		/* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>		/* for videodev2.h */
#include <linux/videodev2.h>

#define CLEAR(x) memset (&(x), 0, sizeof (x))

typedef enum
{
  IO_METHOD_READ,
  IO_METHOD_MMAP,
  IO_METHOD_USERPTR,
} io_method;

struct UsbCameraBuffer
{
  void *start;
  size_t length;
};



static void open_device();
static void close_device(void);
static void init_device(void);
static void init_userp(unsigned int buffer_size);
static void init_mmap(void);
static void init_read(unsigned int buffer_size);
static void uninit_device(void);
static void start_capturing(void);
static void stop_capturing(void);
static void mainloop(char *dir);
static int read_frame(char *dir);



#endif // USBCAM_H
