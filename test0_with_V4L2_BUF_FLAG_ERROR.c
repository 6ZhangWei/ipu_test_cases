#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>



#include <stdio.h>
#include <pthread.h>
#include <sched.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

enum io_method {
	IO_METHOD_READ,
	IO_METHOD_MMAP,
	IO_METHOD_USERPTR,
};

struct buffer {
	void   *start;
	size_t  length;
};

static char            *dev_name;
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
struct buffer          *buffers;
static unsigned int     n_buffers;
static int              out_buf;
static int              force_format;
static int              frame_count = 50000;
static int              CNT = 0;

static void errno_exit(const char *s)
{
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
	int r;

	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

static int g_index = 0;

//process_image(数据指针，大小)
static void process_image(const void *p, int size)
{
	
	fprintf(stderr, "Enter %s g_index = %d\n", __func__, g_index);
	char name[100];
	
	sprintf(name, "yuv_%d.yuv", g_index++);

	if (0) {
		FILE* fp = fopen(name, "w");
		fwrite(p, 1, 1280 * 720 * 2, fp);
		fclose(fp);
	}


	
	if (0 && out_buf)
		fwrite(p, size, 1, stdout); // should write some info.....

	fflush(stderr);
	fprintf(stderr, ".");
	fprintf(stderr, "  the image size is %d \n ", size);
	fflush(stdout);
}

static int read_frame(void)
{
	struct v4l2_buffer buf;
	unsigned int i;
	int ret;

	// fprintf(stderr, "Enter %s\n", __func__);
	switch (io) {
		case IO_METHOD_READ:
			if (-1 == read(fd, buffers[0].start, buffers[0].length)) {
				switch (errno) {
					case EAGAIN:
						return 0;

					case EIO:
						/* Could ignore EIO, see spec. */

						/* fall through */

					default:
						errno_exit("read");
				}
			}

			process_image(buffers[0].start, buffers[0].length);
			break;

		case IO_METHOD_MMAP:
			CLEAR(buf);

			// fprintf(stderr, "%s case IO_METHOD_MMAP\n", __func__);
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
		//	usleep(100000);
			// fprintf(stderr, "%s call VIDIOC_DQBUF\n", __func__);
			ret = xioctl(fd, VIDIOC_DQBUF, &buf);
			// fprintf(stderr, "%s VIDIOC_DQBUF ret %d\n", __func__, ret);

			if (-1 == ret){
				switch (errno) {
					case EAGAIN:
						fprintf(stderr, "%s VIDIOC_DQBUF return \n", __func__);
						return 0;

					case EIO:
						/* Could ignore EIO, see spec. */

						/* fall through */

					default:
						fprintf(stderr, "%s VIDIOC_DQBUF EXIT \n", __func__);
						errno_exit("VIDIOC_DQBUF");
				}
			}

			assert(buf.index < n_buffers);

			fprintf(stderr, "\n%s call VIDIOC_QBUF\n", __func__);
//			fprintf(stderr, "BUF sequence is %d \n", buf.sequence);
//			fprintf(stderr, "Time val:  second is  %ld ,  microseconds is %ld \n", buf.timestamp.tv_sec, buf.timestamp.tv_usec);


			process_image(buffers[buf.index].start, buf.bytesused);

			// fprintf(stderr, "%s call VIDIOC_QBUF\n", __func__);


			fprintf(stderr, "%s HACK: call VIDIOC_QBUF, force set flag V4L2_BUF_FLAG_ERROR\n", __func__);

			buf.flags |= V4L2_BUF_FLAG_ERROR;


			if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");
			break;

		case IO_METHOD_USERPTR:
			CLEAR(buf);

			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_USERPTR;

			if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
				switch (errno) {
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
				if (buf.m.userptr == (unsigned long)buffers[i].start
						&& buf.length == buffers[i].length)
					break;

			assert(i < n_buffers);

			process_image((void *)buf.m.userptr, buf.bytesused);

			if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");
			break;
	}

	return 1;
}

static void mainloop(void)
{
	unsigned int count;

	count = 100000;
	fprintf(stderr, "Enter %s\n", __func__);

	while (count-- > 0) {
		// sleep(2);
#if 1
		fd_set fds;
		struct timeval tv;
		int r;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		/* Timeout. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(fd + 1, &fds, NULL, NULL, &tv);

		if (-1 == r) {
			if (EINTR == errno)
				continue;
			errno_exit("select");
		}

		if (0 == r) {
			fprintf(stderr, "select timeout\n");
			exit(EXIT_FAILURE);
		}
#endif
		read_frame();
		/* EAGAIN - continue select loop. */

	}
}


//VIDIOC_STREAMOFF
static void stop_capturing(void)
{
	enum v4l2_buf_type type;
	fprintf(stderr, "Enter %s\n", __func__);

	switch (io) {
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
}


//VIDIOC_STREAMON
static void start_capturing(void)
{
	unsigned int i;
	enum v4l2_buf_type type;

	fprintf(stderr, "Enter %s\n", __func__);
	switch (io) {
		case IO_METHOD_READ:
			/* Nothing to do. */
			break;

		case IO_METHOD_MMAP:
			for (i = 0; i < n_buffers; ++i) {
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
			for (i = 0; i < n_buffers; ++i) {
				struct v4l2_buffer buf;

				CLEAR(buf);
				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_USERPTR;
				buf.index = i;
				buf.m.userptr = (unsigned long)buffers[i].start;
				buf.length = buffers[i].length;

				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
					errno_exit("VIDIOC_QBUF");
			}
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
				errno_exit("VIDIOC_STREAMON");
			break;
	}
	fprintf(stderr, "Exit %s\n", __func__);
}


//释放申请的内存
static void uninit_device(void)
{
	unsigned int i;
	fprintf(stderr, "Enter %s\n", __func__);

	switch (io) {
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
}

static void init_read(unsigned int buffer_size)
{
	buffers = calloc(1, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	buffers[0].length = buffer_size;
	buffers[0].start = malloc(buffer_size);

	if (!buffers[0].start) {
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

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support "
					"memory mappingn", dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n",
				dev_name);
		exit(EXIT_FAILURE);
	}

	buffers = calloc(req.count, sizeof(*buffers));

	fprintf(stderr, "%s req.count = %d\n", __func__, req.count);
	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = n_buffers;

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
			errno_exit("VIDIOC_QUERYBUF");

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start =
			mmap(NULL /* start anywhere */,
					buf.length,
					PROT_READ | PROT_WRITE /* required */,
					MAP_SHARED /* recommended */,
					fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start)
			errno_exit("mmap");
	}
}

static void init_userp(unsigned int buffer_size)
{
	struct v4l2_requestbuffers req;

	CLEAR(req);

	req.count  = 4;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support "
					"user pointer i/on", dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	buffers = calloc(4, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
		buffers[n_buffers].length = buffer_size;
		buffers[n_buffers].start = malloc(buffer_size);

		if (!buffers[n_buffers].start) {
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}
}

static void test_event(int fd){
	struct v4l2_event_subscription v4l2_event;
	v4l2_event.type = V4L2_EVENT_FRAME_SYNC;
	v4l2_event.id = 0;



	if (-1 == xioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &v4l2_event))
		errno_exit("VIDIOC_SUB_EVENT");
	
	printf("SUccess subscribe events.  \n");



	if (-1 == xioctl(fd, VIDIOC_UNSUBSCRIBE_EVENT, &v4l2_event))
		errno_exit("VIDIOC_SUB_EVENT");

	printf("Success unsubscribe events.  \n");


	return;


}

static void init_device(void)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	unsigned int min;

	fprintf(stderr, "Enter %s\n", __func__);
#if 1
	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s is no V4L2 device\n",
					dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s is no video capture device\n",
				dev_name);
		exit(EXIT_FAILURE);
	}
	switch (io) {
		case IO_METHOD_READ:
			if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
				fprintf(stderr, "%s does not support read i/o\n",
						dev_name);
				exit(EXIT_FAILURE);
			}
			break;

		case IO_METHOD_MMAP:
		case IO_METHOD_USERPTR:
			if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
				fprintf(stderr, "%s does not support streaming i/o\n",
						dev_name);
				exit(EXIT_FAILURE);
			}
			break;
	}


	/* Select video input, video standard and tune here. */

	CLEAR(cropcap);

	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect; /* reset to default */

		if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
			switch (errno) {
				case EINVAL:
					/* Cropping not supported. */
					break;
				default:
					/* Errors ignored. */
					break;
			}
		}
	} else {
		/* Errors ignored. */
	}

	CLEAR(fmt);
#endif
	/*
	   [1]: 'MJPG' (Motion-JPEG, compressed)   //V4L2_PIX_FMT_MJPEG
Size: Discrete 1920x1080                                                           │
Interval: Discrete 0.033s (30.000 fps)                                     │
Interval: Discrete 0.042s (24.000 fps)                                     │
Interval: Discrete 0.050s (20.000 fps)                                     │
Interval: Discrete 0.067s (15.000 fps)                                     │
Interval: Discrete 0.100s (10.000 fps)                                     │
Interval: Discrete 0.133s (7.500 fps)                                      │
Interval: Discrete 0.200s (5.000 fps) 

*/
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = 1280;
	fmt.fmt.pix.height      = 720;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	// fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
		errno_exit("VIDIOC_S_FMT");


	// test_event(fd);
	/* Buggy driver paranoia. */
	// min = fmt.fmt.pix.width * 2;
	// if (fmt.fmt.pix.bytesperline < min)
	//     fmt.fmt.pix.bytesperline = min;
	// min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	// if (fmt.fmt.pix.sizeimage < min)
	//     fmt.fmt.pix.sizeimage = min;

	printf("fmt.fmt.pix.width  is     %d  \n\r", fmt.fmt.pix.width);
	printf("fmt.fmt.pix.height  is     %d  \n\r", fmt.fmt.pix.height);
	printf("fmt.fmt.pix.sizeimage  is     %d  \n\r", fmt.fmt.pix.sizeimage);



	switch (io) {
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
}

static void close_device(void)
{
	fprintf(stderr, "Enter %s\n", __func__);
	if (-1 == close(fd))
		errno_exit("close");

	fd = -1;
}

static void open_device(void)
{
	struct stat st;

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n",
				dev_name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no devicen", dev_name);
		exit(EXIT_FAILURE);
	}

	fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n",
				dev_name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	} else {
		fprintf(stderr, "success open %s\n", dev_name);
	}

}



static void usage(FILE *fp, int argc, char **argv)
{
	fprintf(fp,
			"Usage: %s [options]\n\n"
			"Version 1.3\n"
			"Options:\n"
			"-d | --device name   Video device name [%s]\n"
			"-h | --help          Print this messagen\n"
			"-m | --mmap          Use memory mapped buffers [default]\n"
			"-r | --read          Use read() calls\n"
			"-u | --userp         Use application allocated buffers\n"
			"-o | --output        Outputs stream to stdout\n"
			"-f | --format        Force format to 640x480 YUYV\n"
			"-c | --count         Number of frames to grab [%i]\n"
			"",
			argv[0], dev_name, frame_count);
}

static const char short_options[] = "d:hmruofc:";

static const struct option
long_options[] = {
	{ "device", required_argument, NULL, 'd' },
	{ "help",   no_argument,       NULL, 'h' },
	{ "mmap",   no_argument,       NULL, 'm' },
	{ "read",   no_argument,       NULL, 'r' },
	{ "userp",  no_argument,       NULL, 'u' },
	{ "output", no_argument,       NULL, 'o' },
	{ "format", no_argument,       NULL, 'f' },
	{ "count",  required_argument, NULL, 'c' },
	{ 0, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	dev_name = "/dev/video0";
	printf("pid= %d \n", getpid()); 
	for (;;) {
		int idx;
		int c;

		c = getopt_long(argc, argv,
				short_options, long_options, &idx);

		if (-1 == c)
			break;

		switch (c) {
			case 0: /* getopt_long() flag */
				break;

			case 'd':
				dev_name = optarg;
				break;

			case 'h':
				usage(stdout, argc, argv);
				exit(EXIT_SUCCESS);

			case 'm':
				io = IO_METHOD_MMAP;
				break;

			case 'r':
				io = IO_METHOD_READ;
				break;

			case 'u':
				io = IO_METHOD_USERPTR;
				break;

			case 'o':
				out_buf++;
				break;

			case 'f':
				force_format++;
				break;

			case 'c':
				errno = 0;
				frame_count = strtol(optarg, NULL, 0);
				if (errno)
					errno_exit(optarg);
				break;

			default:
				usage(stderr, argc, argv);
				exit(EXIT_FAILURE);
		}
	}

	open_device();
	init_device();
	// sleep(60);
	printf("start........  \n\r");

	pid_t pid = getpid();
	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_RR);   // 也可用SCHED_RR
	sched_setscheduler(pid, SCHED_RR, &param);                   // 设置当前进程
	pthread_setschedparam(pthread_self(), SCHED_RR, &param);   // 设置当前线程

	start_capturing();
	mainloop();

	stop_capturing();
	uninit_device();
	close_device();
	fprintf(stderr, "\n");
	return 0;
}
