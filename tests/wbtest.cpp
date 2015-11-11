#include <cstdio>
#include <algorithm>
#include <cstring>

#include "kms++.h"

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include <sys/ioctl.h>

#include <xf86drm.h>

using namespace std;
using namespace kms;

#define pexit(fmt, arg...) { \
		printf(fmt, ## arg); \
		exit(1); \
}

#define NUMBUF	3
#define NUMPLANES 2

static int debug = 0;
#define dprintf(fmt, arg...) if (debug) printf(fmt, ## arg)

struct omap_wb_plane {
	int fd;
	uint16_t pitch;
	uint32_t size;
};

struct omap_wb_buffer {
	struct omap_wb_plane plane[NUMPLANES];
};

struct image_params {
	uint32_t pipe; /* enum omap_plane */
	uint32_t fourcc;
	uint32_t field;
	uint16_t x_offset, y_offset;
	uint16_t x_pos, y_pos;
	uint16_t width, height;
	uint16_t out_width, out_height;
	uint32_t numbuf;
	enum v4l2_colorspace colorspace;
	struct omap_wb_buffer buf[NUMBUF];
	struct  v4l2_crop crop;
	uint8_t num_planes;
	uint32_t plane_size[NUMPLANES];
};

struct m2m {
	int fd;
	struct image_params src;
	struct image_params dst;
};

static char *fourcc_to_str(unsigned int fmt)
{
	static char code[5];

	code[0] = (unsigned char)(fmt & 0xff);
	code[1] = (unsigned char)((fmt >> 8) & 0xff);
	code[2] = (unsigned char)((fmt >> 16) & 0xff);
	code[3] = (unsigned char)((fmt >> 24) & 0xff);
	code[4] = '\0';

	return code;
}

/**
 *****************************************************************************
 * @brief:  open the device
 *
 * @return: m2m  struct m2m pointer
 *****************************************************************************
*/
struct m2m *m2m_open(char *devname)
{
	struct m2m *m2m;

	m2m = (struct m2m *)calloc(1, sizeof(*m2m));

	m2m->fd =  open(devname, O_RDWR);
	if(m2m->fd < 0)
		pexit("Cant open %s\n", devname);

	return m2m;
}

/**
 *****************************************************************************
 * @brief:  close the device and free memory
 *
 * @param:  m2m  struct m2m pointer
 *
 * @return: 0 on success
 *****************************************************************************
*/
int m2m_close(struct m2m *m2m)
{
	close(m2m->fd);
	free(m2m);

	return 0;
}

#if 0
/**
 *****************************************************************************
 * @brief:  sets crop parameters
 *
 * @param:  m2m  struct m2m pointer
 *
 * @return: 0 on success
 *****************************************************************************
*/
static int set_crop(struct m2m *m2m)
{
	int ret = 0;

	if ((m2m->crop.c.top == 0) && (m2m->crop.c.left == 0) &&
	    (m2m->crop.c.width == 0) && (m2m->crop.c.height == 0)) {
		dprintf("setting default crop params\n");
		m2m->crop.c.top = 0;
		m2m->crop.c.left = 0;
		m2m->crop.c.width = m2m->src.width;
		m2m->crop.c.height = m2m->src.height;
		m2m->crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	}

	ret = ioctl(m2m->fd, VIDIOC_S_CROP, &m2m->crop);
	if (ret < 0)
		pexit("error setting crop\n");

	return 0;
}
#endif


/**
 *****************************************************************************
 * @brief:  Intialize the m2m input by calling set_control, set_format,
 *	    set_crop, refbuf ioctls
 *
 * @param:  m2m  struct m2m pointer
 *
 * @return: 0 on success
 *****************************************************************************
*/
int m2m_input_init(struct m2m *m2m)
{
	int ret;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers rqbufs;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	ret = ioctl(m2m->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0)
		pexit( "m2m i/p: G_FMT_1 failed: %s\n", strerror(errno));

	fmt.fmt.pix_mp.width = m2m->src.width;
	fmt.fmt.pix_mp.height = m2m->src.height;
	fmt.fmt.pix_mp.pixelformat = m2m->src.fourcc;
	fmt.fmt.pix_mp.colorspace = m2m->src.colorspace;
	fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;

	ret = ioctl(m2m->fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		pexit( "m2m i/p: S_FMT failed: %s\n", strerror(errno));
	} else {
		m2m->src.plane_size[0] = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
		m2m->src.plane_size[1] = fmt.fmt.pix_mp.plane_fmt[1].sizeimage;

		if (fmt.fmt.pix_mp.width != m2m->src.width) {
			dprintf("m2m i/p: S_FMT: asked for width = %u got %u instead\n",
					m2m->src.width, fmt.fmt.pix_mp.width);
		}

	}

	ret = ioctl(m2m->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0)
		pexit( "m2m i/p: G_FMT_2 failed: %s\n", strerror(errno));

	dprintf("m2m i/p: G_FMT: width = %u, height = %u, 4cc = %s\n",
			fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
			fourcc_to_str(fmt.fmt.pix_mp.pixelformat));

//	set_crop(m2m);

	memset(&rqbufs, 0, sizeof(rqbufs));
	rqbufs.count = NUMBUF;
	rqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	rqbufs.memory = V4L2_MEMORY_DMABUF;

	ret = ioctl(m2m->fd, VIDIOC_REQBUFS, &rqbufs);
	if (ret < 0)
		pexit( "m2m i/p: REQBUFS failed: %s\n", strerror(errno));

	m2m->src.numbuf = rqbufs.count;
	dprintf("m2m i/p: allocated buffers = %d\n", rqbufs.count);

	return 0;
}

/**
 *****************************************************************************
 * @brief:  Initialize m2m output by calling set_format, reqbuf ioctls.
 *	    Also allocates buffer to display the m2m output.
 *
 * @param:  m2m  struct m2m pointer
 *
 * @return: 0 on success
 *****************************************************************************
*/
int m2m_output_init(struct m2m *m2m)
{
	int ret;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers rqbufs;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ret = ioctl(m2m->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0)
		pexit( "m2m o/p: G_FMT_1 failed: %s\n", strerror(errno));

	fmt.fmt.pix_mp.width = m2m->dst.width;
	fmt.fmt.pix_mp.height = m2m->dst.height;
	fmt.fmt.pix_mp.pixelformat = m2m->dst.fourcc;
	fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.colorspace = m2m->dst.colorspace;

	ret = ioctl(m2m->fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		pexit( "m2m o/p: S_FMT failed: %s\n", strerror(errno));
	} else {
		m2m->dst.plane_size[0] = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
		m2m->dst.plane_size[1] = fmt.fmt.pix_mp.plane_fmt[1].sizeimage;
		if (fmt.fmt.pix_mp.width != m2m->dst.width) {
			dprintf("m2m o/p: S_FMT: asked for width = %u got %u instead\n",
					m2m->dst.width, fmt.fmt.pix_mp.width);
		}
	}

	ret = ioctl(m2m->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0)
		pexit( "m2m o/p: G_FMT_2 failed: %s\n", strerror(errno));

	dprintf("m2m o/p: G_FMT: width = %u, height = %u, 4cc = %s\n",
			fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
			fourcc_to_str(fmt.fmt.pix_mp.pixelformat));

	memset(&rqbufs, 0, sizeof(rqbufs));
	rqbufs.count = NUMBUF;
	rqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	rqbufs.memory = V4L2_MEMORY_DMABUF;

	ret = ioctl(m2m->fd, VIDIOC_REQBUFS, &rqbufs);
	if (ret < 0)
		pexit( "m2m o/p: REQBUFS failed: %s\n", strerror(errno));

	m2m->dst.numbuf = rqbufs.count;
	dprintf("m2m o/p: allocated buffers = %d\n", rqbufs.count);

	return 0;
}

static void setup_input_buffer_info(struct image_params *img, DumbFramebuffer* fb,
		      uint32_t x, uint32_t y,
		      uint32_t out_w, uint32_t out_h)
{
	img->fourcc = (uint32_t)fb->format();
	img->colorspace = V4L2_COLORSPACE_SMPTE170M;
	img->x_pos = x;
	img->y_pos = y;
	img->width = fb->width();
	img->height = fb->height();
	img->out_width = out_w;
	img->out_height = out_h;

	img->num_planes = fb->num_planes();
}

static void setup_input_buffer(struct omap_wb_buffer *buf, DumbFramebuffer* fb)
{
	for (unsigned i = 0; i < fb->num_planes(); ++i) {
		buf->plane[i].fd = fb->prime_fd(i);
		buf->plane[i].pitch = fb->stride(i);
	}
}

static void setup_output_buffer_info(struct image_params *img, DumbFramebuffer* fb)
{
	img->fourcc = (uint32_t)fb->format();
	img->colorspace = V4L2_COLORSPACE_SMPTE170M;
	img->x_pos = 0;
	img->y_pos = 0;
	img->width = fb->width();
	img->height = fb->height();
	img->out_width = 0;
	img->out_height = 0;

	img->num_planes = fb->num_planes();
}

static void setup_output_buffer(struct omap_wb_buffer *buf, DumbFramebuffer* fb)
{
	for (unsigned i = 0; i < fb->num_planes(); ++i) {
		buf->plane[i].fd = fb->prime_fd(i);
		buf->plane[i].pitch = fb->stride(i);
	}
}

/**
 *****************************************************************************
 * @brief:  queue buffer to m2m input
 *
 * @param:  m2m  struct m2m pointer
 * @param:  index  buffer index to queue
 *
 * @return: 0 on success
 *****************************************************************************
*/
int m2m_input_qbuf(struct m2m *m2m, int index)
{
	int ret;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[2];

	if (m2m->src.field == V4L2_FIELD_TOP ||
	    m2m->src.field == V4L2_FIELD_BOTTOM) {
		dprintf("m2m i/p: QBUF(%d):%s field\n", index,
			m2m->src.field==V4L2_FIELD_TOP?"top":"bottom");
	} else {
		dprintf("m2m i/p: QBUF(%d)\n", index);
	}
	memset(&buf, 0, sizeof buf);
	memset(&planes, 0, sizeof planes);

	planes[0].length = planes[0].bytesused = m2m->src.plane_size[0];
	if(m2m->src.num_planes > 1)
		planes[1].length = planes[1].bytesused = m2m->src.plane_size[1];

	planes[0].data_offset = planes[1].data_offset = 0;

	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.index = index;
	buf.m.planes = &planes[0];
	buf.field = m2m->src.field;
	buf.length = m2m->src.num_planes;

	buf.m.planes[0].m.fd = m2m->src.buf[index].plane[0].fd;
	if(m2m->src.num_planes > 1)
		buf.m.planes[1].m.fd = m2m->src.buf[index].plane[1].fd;

	ret = ioctl(m2m->fd, VIDIOC_QBUF, &buf);
	if (ret < 0)
		pexit( "m2m i/p: QBUF failed: %s, index = %d\n",
			strerror(errno), index);

	return 0;
}

/**
 *****************************************************************************
 * @brief:  queue buffer to m2m output
 *
 * @param:  m2m  struct m2m pointer
 * @param:  index  buffer index to queue
 *
 * @return: 0 on success
 *****************************************************************************
*/
int m2m_output_qbuf(struct m2m *m2m, int index)
{
	int ret;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[2];

	dprintf("m2m o/p: QBUF(%d)\n", index);

	memset(&buf, 0, sizeof buf);
	memset(&planes, 0, sizeof planes);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.index = index;
	buf.m.planes = &planes[0];
	buf.length = m2m->dst.num_planes;
	buf.m.planes[0].m.fd = m2m->dst.buf[index].plane[0].fd;
	if(m2m->dst.num_planes > 1)
		buf.m.planes[1].m.fd = m2m->dst.buf[index].plane[1].fd;

	ret = ioctl(m2m->fd, VIDIOC_QBUF, &buf);
	if (ret < 0)
		pexit( "m2m o/p: QBUF failed: %s, index = %d\n",
			strerror(errno), index);

	return 0;
}

/**
 *****************************************************************************
 * @brief:  start stream
 *
 * @param:  fd  device fd
 * @param:  type  buffer type (CAPTURE or OUTPUT)
 *
 * @return: 0 on success
 *****************************************************************************
*/
int stream_ON(int fd)
{
	int ret, type;

	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if (ret < 0)
		pexit("STREAMON failed,  %d: %s\n", type, strerror(errno));

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if (ret < 0)
		pexit("STREAMON failed,  %d: %s\n", type, strerror(errno));

	dprintf("stream ON: done! fd = %d\n", fd);

	return 0;
}

/**
 *****************************************************************************
 * @brief:  stop stream
 *
 * @param:  fd  device fd
 * @param:  type  buffer type (CAPTURE or OUTPUT)
 *
 * @return: 0 on success
 *****************************************************************************
*/
int stream_OFF(int fd)
{
	int ret, type;

	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0)
		pexit("STREAMOFF failed,  %d: %s\n", type, strerror(errno));

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0)
		pexit("STREAMOFF failed,  %d: %s\n", type, strerror(errno));

	dprintf("stream OFF: done! fd = %d\n", fd);

	return 0;
}

/**
 *****************************************************************************
 * @brief:  dequeue m2m input buffer
 *
 * @param:  m2m  struct m2m pointer
 *
 * @return: buf.index index of dequeued buffer
 *****************************************************************************
*/
int m2m_input_dqbuf(struct m2m *m2m)
{
	int ret;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[2];

//	dprintf("m2m input dequeue buffer\n");

	memset(&buf, 0, sizeof buf);
	memset(&planes, 0, sizeof planes);

	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.m.planes = &planes[0];
	buf.length = m2m->src.num_planes;
	ret = ioctl(m2m->fd, VIDIOC_DQBUF, &buf);
	if (ret < 0)
		pexit("m2m i/p: DQBUF failed: %s\n", strerror(errno));

	dprintf("m2m i/p: DQBUF index = %d\n", buf.index);

	return buf.index;
}

/**
 *****************************************************************************
 * @brief:  dequeue m2m output buffer
 *
 * @param:  m2m  struct m2m pointer
 *
 * @return: buf.index index of dequeued buffer
 *****************************************************************************
*/
int m2m_output_dqbuf(struct m2m *m2m)
{
	int ret;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[2];

//	dprintf("m2m output dequeue buffer\n");

	memset(&buf, 0, sizeof buf);
	memset(&planes, 0, sizeof planes);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.m.planes = &planes[0];
	buf.length = m2m->dst.num_planes;
	ret = ioctl(m2m->fd, VIDIOC_DQBUF, &buf);
	if (ret < 0)
		pexit("m2m o/p: DQBUF failed: %s\n", strerror(errno));

	dprintf("m2m o/p: DQBUF index = %d\n", buf.index);

	return buf.index;
}

/**
 *****************************************************************************
 * @brief:  read image from file & fill it in input buffer
 *
 * @param:  str  char pointer
 * @param:  fd   int
 * @param:  addr void pointer
 * @param:  size int
 *
 * @return: >0	 number of bytes read
 * @return: 0    end of file
 *****************************************************************************
*/
int do_read (int fd, DumbFramebuffer* fb) {
	int nbytes, size, ret = 0, val;
	void *addr;

	for (unsigned i = 0; i < fb->num_planes(); i++) {
		nbytes = fb->size(i);
		size = fb->size(i);
		addr = fb->map(i);
		ret = 0;

		do {
			nbytes = size - ret;
			addr = (void *)((unsigned int)addr + ret);
			if (nbytes == 0) {
				break;
			}
			ret = read(fd, addr, nbytes);
		} while(ret > 0);

		if (ret < 0) {
			val = errno;
			printf ("Read failed plane(%d): %d %s\n", i, ret, strerror(val));
			exit (1);
		} else {
			dprintf ("Total bytes read plane(%d) = %d\n", i, size);
		}
	}
	return ret;
}

/**
 *****************************************************************************
 * @brief:  write image to file
 *
 * @param:  str  char pointer
 * @param:  fd   int
 * @param:  addr void pointer
 * @param:  size int
 *
 *****************************************************************************
*/
int do_write (int fd, DumbFramebuffer* fb) {
	int nbytes, size, ret = 0, val;
	void *addr;

	for (unsigned i = 0; i < fb->num_planes(); i++) {
		nbytes = fb->size(i);
		size = fb->size(i);
		addr = fb->map(i);
		ret = 0;

		do {
			nbytes = size - ret;
			addr = (void *)((unsigned int)addr + ret);
			if (nbytes == 0) {
				break;
			}
			ret = write(fd, addr, nbytes);
		} while(ret > 0);

		if (ret < 0) {
			val = errno;
			printf ("Writing failed plane(%d): %d %s\n", i, ret, strerror(val));
			exit (1);
		} else {
			dprintf ("Total bytes written plane(%d) = %d\n", i, size);
		}
	}
	return ret;
}

/**
 *****************************************************************************
 * @brief:  buffer retried by index and displays the contents
 *
 * @param:  wb  struct wb pointer
 * @param: index index of dequeued output buffer
 *
 * @return: 0 on success
 *****************************************************************************
*/
int display_buffer(Crtc *crtc, unsigned disp_w, unsigned disp_h, DumbFramebuffer* fb)
{
	int ret;

	Plane* plane = 0;

	for (Plane* p : crtc->get_possible_planes()) {
		if (p->plane_type() == PlaneType::Overlay) {
			plane = p;
			break;
		}
	}

	ret = crtc->set_plane(plane, *fb,
			    0, 0, disp_w, disp_h,
			    0, 0, fb->width(), fb->height());
	ASSERT(ret == 0);

	return ret;
}

static struct option long_options[] = {
	{"device",		required_argument, 0, 'd'},
	{"input-file",		required_argument, 0, 'i'},
	{"input-size",		required_argument, 0, 'j'},
	{"input-format",	required_argument, 0, 'k'},
	{"crop",		required_argument, 0, 'c'},
	{"output-file",		required_argument, 0, 'o'},
	{"output-size",		required_argument, 0, 'p'},
	{"output-format",	required_argument, 0, 'q'},
	{"num-frames",		required_argument, 0, 'n'},
	{"help",		no_argument, 0, 'h'},
	{"verbose",		no_argument, 0, 'v'},
	{"display",		no_argument, 0, 1},
	{0, 0, 0, 0}
};

static void usage(void)
{
	char localusage[] =
	"Usage: wbtest [-d <Device file|number>] -i <Input> -j <WxH> -k <Pixel Format>\n"
	"       -o <Output> -p <WxH> -q <Pixel Format>\n"
	"       [-c <top,left,width,height>] [-v] [-n <num frames>]\n"
	"Convert input video file into desired output format\n"
	"  [-d, --device=<Device file>]          : /dev/video10 (default)\n"
	"  -i, --input-file=<Input>              : Input file name\n"
	"  -j, --input-size=<WxH>                : Input frame size\n"
	"  -k, --input-format=<Pixel Format>     : Input frame format\n"
	"  [-c, --crop=<top,left,width,height>]  : Crop target\n"
	"  -o, --output-file=<Output>            : Output file name\n"
	"  -p, --output-size=<WxH>               : Output frame size\n"
	"  -q, --output-format=<Pixel Format>    : Output frame format\n"
	"  -n, --num-frames=<num frames>         : Number of frames to convert\n"
	"  -h, --help                            : Display this help message\n"
	"  -v, --verbose                         : Verbose output\n"
	"  --display                             : Display converted output\n";

	printf("%s\n", localusage);
}

int main(int argc, char **argv)
{
	int	index;
	char	devname[30];
	char	srcfile[256];
	char	dstfile[256];
	int	srcHeight  = 0, dstHeight = 0;
	int	srcWidth   = 0, dstWidth  = 0;
	int	fin = -1, fout = -1;
	char	srcFmt[30], dstFmt[30];
	struct	v4l2_selection selection;
	unsigned int	num_frames_convert = -1UL, num_frames;
	bool display_on = false;
	DumbFramebuffer *srcfb[NUMBUF], *dstfb[NUMBUF];

	Card card;

	auto conn = card.get_first_connected_connector();
	auto crtc = conn->get_current_crtc();

	int option_char, option_index;
	char *endptr;
	char shortoptions[] = "d:i:j:k:o:p:q:c:n:vh";
	unsigned src_w = 800;
	unsigned src_h = 600;
	string src_fourcc = "YUYV";

	unsigned dst_w = 1280;
	unsigned dst_h = 800;
	string dst_fourcc = "XR24";

	struct m2m *m2m;
	unsigned disp_w = 800;
	unsigned disp_h = 600;

	/* let's setup default values before parsing arguments */
	strcpy(devname, "/dev/video10");
	srcfile[0] = '\0';
	dstfile[0] = '\0';

	selection.r.top = selection.r.left = 0;
	selection.r.width = 0;
	selection.r.height = 0;
	selection.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	selection.target = V4L2_SEL_TGT_CROP_ACTIVE;

	while ((option_char = getopt_long(argc, argv, shortoptions, long_options, &option_index)) != EOF)
		switch (option_char) {
		case 0:
			break;
		case 1:
			display_on = true;
			break;
		case 'd':
		case 'D':
			if (isdigit(optarg[0]) && strlen(optarg) <= 3) {
				sprintf(devname, "/dev/video%s", optarg);
			} else if (!strncmp(optarg, "/dev/video", 10)) {
				strcpy(devname, optarg);
			} else {
				printf("ERROR: Device name not recognized: %s\n\n",
				       optarg);
				usage();
				exit(1);
			}
			printf("device_name: %s\n", devname);
			break;
		case 'i':
		case 'I':
			strcpy(srcfile, optarg);
			printf("srcfile: %s\n", srcfile);
			fin = open(srcfile, O_RDONLY);
			break;
		case 'j':
		case 'J':
			srcWidth = strtol(optarg, &endptr, 10);
			if (*endptr != 'x' || endptr == optarg) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			srcHeight = strtol(endptr + 1, &endptr, 10);
			if (*endptr != 0) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			src_w = srcWidth;
			src_h = srcHeight;
			/* default crop values at first */
			if (selection.r.height == 0) {
				selection.r.top = selection.r.left = 0;
				selection.r.width = srcWidth;
				selection.r.height = srcHeight;
			}
			break;
		case 'k':
		case 'K':
			strcpy(srcFmt, optarg);
			src_fourcc = srcFmt;
			break;
		case 'o':
		case 'O':
			strcpy(dstfile, optarg);
			printf("dstfile: %s\n", dstfile);
			fout = open(dstfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			break;
		case 'p':
		case 'P':
			dstWidth = strtol(optarg, &endptr, 10);
			if (*endptr != 'x' || endptr == optarg) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			dstHeight = strtol(endptr + 1, &endptr, 10);
			if (*endptr != 0) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			dst_w = dstWidth;
			dst_h = dstHeight;
			break;
		case 'q':
		case 'Q':
			strcpy(dstFmt, optarg);
			dst_fourcc = dstFmt;
			break;
		case 'c':
		case 'C':
			selection.r.top = strtol(optarg, &endptr, 10);
			if (*endptr != ',' || endptr == optarg) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			selection.r.left = strtol(endptr + 1, &endptr, 10);
			if (*endptr != ',' || endptr == optarg) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			selection.r.width = strtol(endptr + 1, &endptr, 10);
			if (*endptr != ',' || endptr == optarg) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			selection.r.height = strtol(endptr + 1, &endptr, 10);
			if (*endptr != 0) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			break;
		case 'n':
		case 'N':
			num_frames_convert = atoi(optarg);
			if (num_frames_convert == 0)
				num_frames_convert = 5;
			break;
		case 'v':
		case 'V':
			debug = 1;
			break;
		case 'h':
		case 'H':
		default:
			usage();
			exit(1);
		}

	for (unsigned i = 0; i < NUMBUF; ++i) {
		srcfb[i] = new DumbFramebuffer(card, src_w, src_h, FourCCToPixelFormat(src_fourcc));
		draw_test_pattern(*srcfb[i]);

		dstfb[i] = new DumbFramebuffer(card, dst_w, dst_h, FourCCToPixelFormat(dst_fourcc));
	}

	if (crtc->width() < dst_w)
		disp_w = crtc->width();
	else
		disp_w = dst_w;

	if (crtc->height() < dst_h)
		disp_h = crtc->width();
	else
		disp_h = dst_h;

	/*
	 * If there is no input file and the number of wanted
	 * frames was not explictly specified then we probably
	 * only want one frame.
	 */
	if (fin == -1 && num_frames_convert == -1UL)
		num_frames_convert = 1;

	m2m = m2m_open(devname);

	setup_input_buffer_info(&m2m->src, srcfb[0], 0, 0, srcfb[0]->width(), srcfb[0]->height());
	setup_output_buffer_info(&m2m->dst, dstfb[0]);

	for (unsigned i = 0; i < NUMBUF; ++i) {
		setup_input_buffer(&m2m->src.buf[i], srcfb[i]);
		setup_output_buffer(&m2m->dst.buf[i], dstfb[i]);
	}

	m2m_input_init(m2m);
	m2m_output_init(m2m);

	for (unsigned i = 0; i < NUMBUF; ++i) {
		m2m_output_qbuf(m2m, i);
	}

	for (unsigned i = 0; i < m2m->src.numbuf && i < num_frames_convert; ++i) {
		/* Read from file or something */
		if (fin >= 0) {
			if (do_read(fin, srcfb[i]) <= 0)
				break;
		}
		m2m_input_qbuf(m2m, i);
	}

	stream_ON(m2m->fd);

	num_frames = 0;
	while(num_frames_convert) {

		index = m2m_input_dqbuf(m2m);
		/* Read from file if available */
		if (fin >= 0) {
			if (do_read(fin, srcfb[index]) <= 0)
				break;
		}
		m2m_input_qbuf(m2m, index);

		index = m2m_output_dqbuf(m2m);
		/* Save result to file if available */
		if (fout >= 0) {
			if (do_write(fout, dstfb[index]) <= 0)
				break;
		}

		if (display_on)
			display_buffer(crtc, disp_w, disp_h, dstfb[index]);

		m2m_output_qbuf(m2m, index);

		num_frames++;
		num_frames_convert--;
	}

	/*
	 * We have run out of source frames,
	 * Let's empty the destination queue
	 */
	for (unsigned i = 0; i < m2m->dst.numbuf && num_frames < num_frames_convert; ++i) {
		index = m2m_output_dqbuf(m2m);
		/* Save result to file if available */
		if (fout >= 0) {
			if (do_write(fout, dstfb[index]) <= 0)
				break;
		}
		if (display_on)
			display_buffer(crtc, disp_w, disp_h, dstfb[index]);

		num_frames++;
	}

	printf("%d frames processed.\n", num_frames);

	if (display_on) {
		printf("press enter to exit\n");
		getchar();
	}

	stream_OFF(m2m->fd);
	m2m_close(m2m);
	if (fin >= 0)
		close(fin);
	if (fout >= 0)
		close(fout);
}
