#include "usbcam.h"

using namespace std;

static char *dev_name = NULL;
static io_method io = IO_METHOD_MMAP;
    /* can be V4L2_PIX_FMT_YUV422P or V4L2_PIX_FMT_MJPEG */
static int pixel_format = V4L2_PIX_FMT_MJPEG;
static int fd = -1;
struct UsbCameraBuffer *buffers = NULL;
static unsigned int n_buffers = 0;
static unsigned int width = 1280;
static unsigned int height = 720;
static unsigned int count = 200;
static void errno_exit(const char *s)
{
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
  exit(EXIT_FAILURE);
}

static int xioctl(int fd, int request, void *arg)
{
  int r;

  do
    r = ioctl(fd, request, arg);
  while (-1 == r && EINTR == errno);

  return r;
}

static void process_image(const void *p, ssize_t size, char* dir)
{
    static int no_image = 0;
    char filename[1024];
    int fd;
    ssize_t written = 0;
//    no_image = 0; //Save all Images to One
    snprintf(filename, sizeof(filename), "%s/%03d.%s",
             dir,no_image++, pixel_format == V4L2_PIX_FMT_YUV422P ? "yuv" : "jpeg");
//    snprintf(filename, sizeof(filename), "/tmp/ColorImage.%s",
//              pixel_format == V4L2_PIX_FMT_YUV422P ? "yuv" : "jpeg");
    printf("\nFilename: %s\n", filename);
    fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0)
    {
        fputc('*', stdout);
        fflush(stdout);
        return;
    }
    do
    {
        int ret;
        ret = write(fd, p + written, size - written);
        if (ret < 0)
        {
            fputc('+', stdout);
            fflush(stdout);
            return;
        }
        written += ret;
    }
    while (written < size);
    close(fd);

//    fputc('.', stdout);
    fflush(stdout);
}

static int read_frame(char * dir)
{
    struct v4l2_buffer buf;
    unsigned int i;
    ssize_t read_bytes;
    unsigned int total_read_bytes;

    switch (io)
    {
    case IO_METHOD_READ:
        total_read_bytes = 0;
        do
        {
            read_bytes = read(fd, buffers[0].start, buffers[0].length);
            if (read_bytes < 0)
            {
                switch (errno)
                {
                case EIO:
                case EAGAIN:
                    continue;
                default:
                    errno_exit("read");
                }
            }
            total_read_bytes += read_bytes;

        } while (total_read_bytes < buffers[0].length);
        process_image(buffers[0].start, buffers[0].length, dir);

        break;

    case IO_METHOD_MMAP:
        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
        {
            switch (errno)
            {
            case EAGAIN:
                return 0;

            case EIO:
                /* Could ignore EIO, see spec. */

                /* fall through */

            default:
                errno_exit("VIDIOC_DQBUF");
            }
        }

        assert(buf.index < n_buffers);

        process_image(buffers[buf.index].start, buf.length, dir);

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");

        break;

    case IO_METHOD_USERPTR:
        CLEAR(buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
        {
            switch (errno)
            {
            case EAGAIN:
                return 0;

            case EIO:
                /* Could ignore EIO, see spec. */

                /* fall through */

            default:
                errno_exit("VIDIOC_DQBUF");
            }
        }

        for (i = 0; i < n_buffers; ++i)
            if (buf.m.userptr == (unsigned long) buffers[i].start
                && buf.length == buffers[i].length)
                break;

        assert(i < n_buffers);

        process_image((void *) buf.m.userptr, buf.length, dir);

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");

        break;
    }

    return 1;
}

static void mainloop(char *dir)
{
    printf("Capturing");
//    count = 200;//Capture 7 picture every time
    while(count-- > 0)
    {
        for (;;)
        {
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            /* Timeout. */
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1, &fds, NULL, NULL, &tv);

            if (-1 == r)
            {
                if (EINTR == errno)
                    continue;

                errno_exit("select");
            }

            if (0 == r)
            {
                fprintf(stderr, "select timeout\n");
                exit(EXIT_FAILURE);
            }

            if (read_frame(dir))
                break;

            /* EAGAIN - continue select loop. */
        }
    }

}

static void stop_capturing(void)
{
    enum v4l2_buf_type type;

    switch (io)
    {
    case IO_METHOD_READ:
        /* Nothing to do. */
        break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
            errno_exit("VIDIOC_STREAMOFF");

        break;
    }
//    printf("\nStop Capture\n");
}

static void start_capturing(void)
{
    unsigned int i;
    enum v4l2_buf_type type;

    switch (io)
    {
    case IO_METHOD_READ:
        /* Nothing to do. */
        break;

    case IO_METHOD_MMAP:
        for (i = 0; i < n_buffers; ++i)
        {
            struct v4l2_buffer buf;

            CLEAR(buf);

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                errno_exit("VIDIOC_QBUF");
        }

        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
            errno_exit("VIDIOC_STREAMON");

        break;

    case IO_METHOD_USERPTR:
        for (i = 0; i < n_buffers; ++i)
        {
            struct v4l2_buffer buf;

            CLEAR(buf);

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_USERPTR;
            buf.m.userptr = (unsigned long) buffers[i].start;
            buf.length = buffers[i].length;

            if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                errno_exit("VIDIOC_QBUF");
        }


        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
            errno_exit("VIDIOC_STREAMON");

        break;
    }
//    printf("Start Capture\n");
}

static void uninit_device(void)
{
  unsigned int i;

  switch (io)
    {
    case IO_METHOD_READ:
      free(buffers[0].start);
      break;

    case IO_METHOD_MMAP:
      for (i = 0; i < n_buffers; ++i)
    if (-1 == munmap(buffers[i].start, buffers[i].length))
      errno_exit("munmap");
      break;

    case IO_METHOD_USERPTR:
      for (i = 0; i < n_buffers; ++i)
    free(buffers[i].start);
      break;
    }

  free(buffers);
//  printf("Uninit  %s \t[OK]\n",dev_name);
}

static void init_read(unsigned int buffer_size)
{
  buffers = (UsbCameraBuffer *)calloc(1, sizeof(*buffers));

  if (!buffers)
    {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }

  buffers[0].length = buffer_size;
  buffers[0].start = malloc(buffer_size);

  if (!buffers[0].start)
    {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }
}

static void init_mmap(void)
{
  struct v4l2_requestbuffers req;

  CLEAR(req);

  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
    {
      if (EINVAL == errno)
    {
      fprintf(stderr, "%s does not support "
          "memory mapping\n", dev_name);
      exit(EXIT_FAILURE);
    }
      else
    {
      errno_exit("VIDIOC_REQBUFS");
    }
    }

  if (req.count < 2)
    {
      fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
      exit(EXIT_FAILURE);
    }

  buffers = (UsbCameraBuffer *)calloc(req.count, sizeof(*buffers));

  if (!buffers)
    {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }

  for (n_buffers = 0; n_buffers < req.count; ++n_buffers)
    {
      struct v4l2_buffer buf;

      CLEAR(buf);

      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = n_buffers;

      if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
    errno_exit("VIDIOC_QUERYBUF");

      buffers[n_buffers].length = buf.length;
      buffers[n_buffers].start = mmap(NULL /* start anywhere */ ,
                      buf.length,
                      PROT_READ | PROT_WRITE /* required */ ,
                      MAP_SHARED /* recommended */ ,
                      fd, buf.m.offset);

      if (MAP_FAILED == buffers[n_buffers].start)
    errno_exit("mmap");
    }
}

static void init_userp(unsigned int buffer_size)
{
  struct v4l2_requestbuffers req;

  CLEAR(req);

  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;

  if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
    {
      if (EINVAL == errno)
    {
      fprintf(stderr, "%s does not support "
          "user pointer i/o\n", dev_name);
      exit(EXIT_FAILURE);
    }
      else
    {
      errno_exit("VIDIOC_REQBUFS");
    }
    }

  buffers = (UsbCameraBuffer *)calloc(4, sizeof(*buffers));

  if (!buffers)
    {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }

  for (n_buffers = 0; n_buffers < 4; ++n_buffers)
    {
      buffers[n_buffers].length = buffer_size;
      buffers[n_buffers].start = malloc(buffer_size);

      if (!buffers[n_buffers].start)
    {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }
    }
}

static void init_device(void)
{
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_streamparm setfps;
    struct v4l2_control ctrl;
    unsigned int min;

    //query cap
    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap))
    {
        if (EINVAL == errno)
        {
            fprintf(stderr, "%s is no V4L2 device\n", dev_name);
            exit(EXIT_FAILURE);
        }else{
            errno_exit("VIDIOC_QUERYCAP");
        }
    }else{
//        printf("driver:\t\t%s\n",cap.driver);
//        printf("card:\t\t%s\n",cap.card);
//        printf("bus_info:\t%s\n",cap.bus_info);
//        printf ("version:\t%d\n",cap.version);
//        printf("capabilities:\t%x\n",cap.capabilities);
        /*
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
        {
            fprintf(stderr, "%s is no video capture device\n", dev_name);
            exit(EXIT_FAILURE);
            }*/
        if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == V4L2_CAP_VIDEO_CAPTURE)
        {
//            printf("Device %s: supports capture.\n",dev_name);
        }

        if ((cap.capabilities & V4L2_CAP_STREAMING) == V4L2_CAP_STREAMING)
        {
//            printf("Device %s: supports streaming.\n",dev_name);
        }
    }

    //emu all support fmt
    fmtdesc.index=0;
    fmtdesc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
//    printf("Support format:\n");
//    while(ioctl(fd,VIDIOC_ENUM_FMT,&fmtdesc)!=-1)
//    {
//        printf("\t%d.%s\n",fmtdesc.index+1,fmtdesc.description);
//        fmtdesc.index++;
//    }


    switch (io){

    case IO_METHOD_READ:
        if (!(cap.capabilities & V4L2_CAP_READWRITE))
        {
            fprintf(stderr, "%s does not support read i/o\n", dev_name);
            exit(EXIT_FAILURE);
        }

        break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
        if (!(cap.capabilities & V4L2_CAP_STREAMING))
        {
            fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
            exit(EXIT_FAILURE);
        }

        break;
    }

    /* Select video input, video standard and tune here. */

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl(fd, VIDIOC_CROPCAP, &cropcap))
    {
        /* Errors ignored. */
    }

    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect;	/* reset to default */

    if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop))
    {
        switch (errno)
        {
        case EINVAL:
            /* Cropping not supported. */
            break;
        default:
            /* Errors ignored. */
            break;
        }
    }

    CLEAR(fmt);

    //set fmt
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixel_format;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
        errno_exit("VIDIOC_S_FMT");

    /* Note VIDIOC_S_FMT may change width and height. */
    width = fmt.fmt.pix.width;
    height = fmt.fmt.pix.height;

    switch (io)
    {
    case IO_METHOD_READ:
        init_read(fmt.fmt.pix.sizeimage);
        break;

    case IO_METHOD_MMAP:
        init_mmap();
        break;

    case IO_METHOD_USERPTR:
        init_userp(fmt.fmt.pix.sizeimage);
        break;
    }
//    printf("fmt.type:\t\t%d\n",fmt.type);
//    printf("pix.pixelformat:\t%c%c%c%c\n",fmt.fmt.pix.pixelformat & 0xFF, (fmt.fmt.pix.pixelformat >> 8) & 0xFF,(fmt.fmt.pix.pixelformat >> 16) & 0xFF, (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
//    printf("pix.height:\t\t%d\n",fmt.fmt.pix.height);
//    printf("pix.width:\t\t%d\n",fmt.fmt.pix.width);
//    printf("pix.field:\t\t%d\n",fmt.fmt.pix.field);

    //set fps
    CLEAR(setfps);
    setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    setfps.parm.capture.timeperframe.numerator = 1; //分子
    setfps.parm.capture.timeperframe.denominator = 2;  //分母

    if (-1 == xioctl(fd, VIDIOC_S_PARM, &setfps))
        errno_exit("VIDIOC_S_PARM");
    if(xioctl(fd, VIDIOC_G_PARM, &setfps) == 0){
//        printf("\n  Frame rate:   %u/%u\n",
//               setfps.parm.capture.timeperframe.denominator,
//               setfps.parm.capture.timeperframe.numerator);
    }
    else{
//        perror("Unable to read out current frame rate");
    }

    //设定
    CLEAR(ctrl);
    ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
    ctrl.value = 1;
    if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
//        printf("Couldn't set auto white balance!\n");
    }

    CLEAR(ctrl);
    ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = 1;
    if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
//        printf("Couldn't set auto exposure!\n");
    }

    CLEAR(ctrl);
    ctrl.id = V4L2_CID_GAIN;
    /*    if(-1 == xioctl(fd, VIDIOC_G_CTRL, &ctrl))
        errno_exit("VIDIOC_G_CTRL");
    else
        printf("GAIN value: %d\n",ctrl.value);*/
    ctrl.value= 1;
    if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
//        printf("Couldn't set GAIN!\n");
     }

    CLEAR(ctrl);
    ctrl.id = V4L2_CID_FOCUS_AUTO;
    ctrl.value = 1;
    if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
//        printf("Couldn't set auto foucs!\n");
    }

    CLEAR(ctrl);
    ctrl.id = V4L2_CID_HFLIP; //水平镜像
    ctrl.value = 1;
    if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
//        printf("Couldn't set h flip!\n");
    }


    CLEAR(ctrl);
    ctrl.id = V4L2_CID_VFLIP;
    ctrl.value = 1;
    if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
//        printf("Couldn't set v flip!\n");
    }

//    printf("init %s \t[OK]\n",dev_name);

}

static void close_device(void)
{
  if (-1 == close(fd))
    errno_exit("close");

  fd = -1;
//  printf("Close  %s \t[OK]\n",dev_name);
}

static void open_device()
{
  struct stat st;

  if (-1 == stat(dev_name, &st))
    {
      fprintf(stderr, "Cannot identify '%s': %d, %s\n",
          dev_name, errno, strerror(errno));
      exit(EXIT_FAILURE);
    }

  if (!S_ISCHR(st.st_mode))
    {
      fprintf(stderr, "%s is no device\n", dev_name);
      exit(EXIT_FAILURE);
    }

  fd = open(dev_name, O_RDWR /* required */  | O_NONBLOCK, 0);

  if (-1 == fd)
    {
      fprintf(stderr, "Cannot open '%s': %d, %s\n",
          dev_name, errno, strerror(errno));
      exit(EXIT_FAILURE);
    }

}


int main(int argc, char* argv[])
{
  dev_name = "/dev/video0";

  char *dir = argv[1];
  open_device();

  init_device();

  start_capturing();

  mainloop(dir);

  stop_capturing();

  uninit_device();

  close_device();

//  exit(EXIT_SUCCESS);

  return 0;
}
