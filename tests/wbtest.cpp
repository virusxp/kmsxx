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
#include <sys/types.h>
#include <dirent.h>

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
static bool display_on = false;
static bool enum_formats = false;

#define dprintf(fmt, arg...) if (debug) {printf(fmt, ## arg); fflush(stdout);}

struct omap_wb_plane {
	int fd;
	uint16_t pitch;
	uint32_t size;
};

struct omap_wb_buffer {
	struct omap_wb_plane plane[NUMPLANES];
};

struct image_params {
	enum v4l2_buf_type type;
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
};

struct m2m {
	int fd;
	struct v4l2_capability vcap;
	struct image_params src;
	struct image_params dst;
};

static string StringToUpper(string strToConvert)
{
    std::transform(strToConvert.begin(), strToConvert.end(), strToConvert.begin(), ::toupper);

    return strToConvert;
}

static std::string num2s(unsigned num)
{
	char buf[10];

	sprintf(buf, "%08x", num);
	return buf;
}

std::string buftype2s(int type)
{
	switch (type) {
	case 0:
		return "Invalid";
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return "Video Capture";
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return "Video Capture Multiplanar";
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return "Video Output";
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return "Video Output Multiplanar";
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return "Video Overlay";
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return "VBI Capture";
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return "VBI Output";
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		return "Sliced VBI Capture";
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return "Sliced VBI Output";
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return "Video Output Overlay";
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		return "SDR Capture";
	default:
		return "Unknown (" + num2s(type) + ")";
	}
}

std::string fcc2s(unsigned int val)
{
	std::string s;

	s += val & 0x7f;
	s += (val >> 8) & 0x7f;
	s += (val >> 16) & 0x7f;
	s += (val >> 24) & 0x7f;
	if (val & (1 << 31))
		s += "-BE";
	return s;
}

#define V4L2_CAP_IS_M2M(cap)                \
	(cap & V4L2_CAP_VIDEO_M2M_MPLANE || \
	 cap & V4L2_CAP_VIDEO_M2M)

/**
 *****************************************************************************
 * @brief:  open the device
 *
 * @return: m2m  struct m2m pointer
 *****************************************************************************
*/
struct m2m *m2m_open(char *devname)
{
	struct m2m *m2m = (struct m2m *)calloc(1, sizeof(*m2m));

	m2m->fd =  open(devname, O_RDWR);
	if(m2m->fd < 0)
		pexit("Cant open %s\n", devname);

	int ret = ioctl(m2m->fd, VIDIOC_QUERYCAP, &m2m->vcap);
	if (ret < 0)
		pexit("wbtest: QUERYCAP failed: %s\n", strerror(errno));

	if (!V4L2_CAP_IS_M2M(m2m->vcap.capabilities))
		pexit("wbtest: %s is not a mem2mem device\n", devname);

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
 * @brief:  Intialize the m2m input or output pipe by calling set_control,
 *	     set_format, set_crop, refbuf ioctls
 *
 * @param:  m2m  struct m2m pointer
 *
 * @return: 0 on success
 *****************************************************************************
*/
int m2m_pipe_init(struct m2m *m2m, struct image_params *img)
{
	struct v4l2_format fmt = {};
	fmt.type = img->type;

	int ret = ioctl(m2m->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0)
		pexit("m2m %c/p: G_FMT_1 failed: %s\n",
		      V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
		      strerror(errno));

	fmt.fmt.pix_mp.width = img->width;
	fmt.fmt.pix_mp.height = img->height;
	fmt.fmt.pix_mp.pixelformat = img->fourcc;
	fmt.fmt.pix_mp.colorspace = img->colorspace;
	fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;

	ret = ioctl(m2m->fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		pexit("m2m %c/p: S_FMT failed: %s\n",
		      V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
		      strerror(errno));
	} else {
		/* These should match. complain otherwise */
		if (img->buf[0].plane[0].size != fmt.fmt.pix_mp.plane_fmt[0].sizeimage)
			dprintf("m2m %c/p: S_FMT: plane[0] size mismatch has %u got %u instead\n",
				V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
				img->buf[0].plane[0].size, fmt.fmt.pix_mp.plane_fmt[0].sizeimage);

		if (img->num_planes > 1)
			if (img->buf[0].plane[1].size != fmt.fmt.pix_mp.plane_fmt[1].sizeimage)
				dprintf("m2m %c/p: S_FMT: plane[1] size mismatch has %u got %u instead\n",
					V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
					img->buf[0].plane[1].size, fmt.fmt.pix_mp.plane_fmt[1].sizeimage);

		if (fmt.fmt.pix_mp.width != img->width) {
			dprintf("m2m %c/p: S_FMT: asked for width = %u got %u instead\n",
				V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
				img->width, fmt.fmt.pix_mp.width);
		}

	}

	ret = ioctl(m2m->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0)
		pexit("m2m %c/p: G_FMT_2 failed: %s\n",
		      V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
		      strerror(errno));

	dprintf("m2m %c/p: G_FMT: width = %u, height = %u, 4cc = %s\n",
		V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
		fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
		fcc2s(fmt.fmt.pix_mp.pixelformat).c_str());

//	if (V4L2_TYPE_IS_OUTPUT(img->type))
//		set_crop(m2m);

	struct v4l2_requestbuffers rqbufs = {};
	rqbufs.count = NUMBUF;
	rqbufs.type = img->type;
	rqbufs.memory = V4L2_MEMORY_DMABUF;

	ret = ioctl(m2m->fd, VIDIOC_REQBUFS, &rqbufs);
	if (ret < 0)
		pexit("m2m %c/p: REQBUFS failed: %s\n",
		      V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
		      strerror(errno));

	img->numbuf = rqbufs.count;
	dprintf("m2m %c/p: allocated buffers = %d\n",
		V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
		rqbufs.count);

	return 0;
}

static void set_buffer_info(struct image_params *img, enum v4l2_buf_type type,
			    enum v4l2_colorspace colorspace,
			    DumbFramebuffer* fb,
			    uint32_t x, uint32_t y,
			    uint32_t out_w, uint32_t out_h)
{
	img->type = type;
	img->fourcc = (uint32_t)fb->format();
	img->colorspace = colorspace;
	img->x_pos = x;
	img->y_pos = y;
	img->width = fb->width();
	img->height = fb->height();
	img->out_width = out_w;
	img->out_height = out_h;

	img->num_planes = fb->num_planes();
}

static void setup_buffer(struct omap_wb_buffer *buf, DumbFramebuffer* fb)
{
	for (unsigned i = 0; i < fb->num_planes(); ++i) {
		buf->plane[i].fd = fb->prime_fd(i);
		buf->plane[i].pitch = fb->stride(i);
		buf->plane[i].size = fb->size(i);
	}
}

/**
 *****************************************************************************
 * @brief:  queue buffer to m2m
 *
 * @param:  m2m  struct m2m pointer
 * @param:  index  buffer index to queue
 *
 * @return: 0 on success
 *****************************************************************************
*/
static int m2m_qbuf(struct m2m *m2m, struct image_params *img, int index)
{
	struct v4l2_buffer buf = {};
	struct v4l2_plane planes[2] = {};

	planes[0].length = planes[0].bytesused = img->buf[index].plane[0].size;
	if(img->num_planes > 1)
		planes[1].length = planes[1].bytesused = img->buf[index].plane[1].size;

	planes[0].data_offset = planes[1].data_offset = 0;

	buf.type = img->type;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.index = index;
	buf.m.planes = &planes[0];
	buf.field = img->field;
	buf.length = img->num_planes;

	buf.m.planes[0].m.fd = img->buf[index].plane[0].fd;
	if(img->num_planes > 1)
		buf.m.planes[1].m.fd = img->buf[index].plane[1].fd;

	int ret = ioctl(m2m->fd, VIDIOC_QBUF, &buf);
	if (ret < 0)
		pexit("m2m %c/p: QBUF failed: %s, index = %d\n",
		      V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
		      strerror(errno), index);

	dprintf("m2m %c/p: QBUF index = %d\n",
		V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
	 	buf.index);

	return 0;
}

/**
 *****************************************************************************
 * @brief:  start stream
 *
 * @param:  fd  device fd
 *
 * @return: 0 on success
 *****************************************************************************
*/
static int stream_ON(int fd)
{
	int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	int ret = ioctl(fd, VIDIOC_STREAMON, &type);
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
 *
 * @return: 0 on success
 *****************************************************************************
*/
static int stream_OFF(int fd)
{
	int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	int ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
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
static int m2m_dqbuf(struct m2m *m2m, struct image_params *img)
{
	struct v4l2_buffer buf = {};
	struct v4l2_plane planes[2] = {};

	buf.type = img->type;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.m.planes = &planes[0];
	buf.length = img->num_planes;
	int ret = ioctl(m2m->fd, VIDIOC_DQBUF, &buf);
	if (ret < 0)
		pexit("m2m %c/p: DQBUF failed: %s\n",
		      V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
		      strerror(errno));

	dprintf("m2m %c/p: DQBUF index = %d\n",
		V4L2_TYPE_IS_OUTPUT(img->type) ? 'i' : 'o',
	 	buf.index);

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
static int do_read (int fd, DumbFramebuffer* fb) {
	int ret = 0;

	for (unsigned i = 0; i < fb->num_planes(); i++) {
		int nbytes = fb->size(i);
		int size = fb->size(i);
		void *addr = fb->map(i);
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
			pexit("Read failed plane(%d): %d %s\n", i, ret, strerror(errno));
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
static int do_write (int fd, DumbFramebuffer* fb) {
	int ret = 0;

	for (unsigned i = 0; i < fb->num_planes(); i++) {
		int nbytes = fb->size(i);
		int size = fb->size(i);
		void *addr = fb->map(i);
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
			pexit("Writing failed plane(%d): %d %s\n", i, ret, strerror(errno));
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
static int display_buffer(Crtc *crtc, unsigned disp_w, unsigned disp_h, DumbFramebuffer* fb)
{

	Plane* plane = 0;

	for (Plane* p : crtc->get_possible_planes()) {
		if (p->plane_type() == PlaneType::Overlay) {
			plane = p;
			break;
		}
	}

	int ret = crtc->set_plane(plane, *fb,
			    0, 0, disp_w, disp_h,
			    0, 0, fb->width(), fb->height());
	ASSERT(ret == 0);

	return ret;
}

static void video_enum_formats(struct m2m *m2m, enum v4l2_buf_type type)
{
	for (unsigned i = 0; ; ++i) {
		struct v4l2_fmtdesc fmt = {};
		fmt.index = i;
		fmt.type = type;
		int ret = ioctl(m2m->fd, VIDIOC_ENUM_FMT, &fmt);
		if (ret < 0)
			break;

		if (i != fmt.index)
			printf("Warning: driver returned wrong format index "
				"%u.\n", fmt.index);
		if (type != fmt.type)
			printf("Warning: driver returned wrong format type "
				"%u.\n", fmt.type);

		printf("  Format %u: %s (%08x)", i,
			fcc2s(fmt.pixelformat).c_str(), fmt.pixelformat);
		printf("  Type: %s (%u)\n", buftype2s(fmt.type).c_str(),
			(unsigned int)fmt.type);
	}
}

enum {
	OPT_DISPLAY = 1,
	OPT_INFO,
	OPT_LISTDEV,
	OPT_LISTFMT,
	OPT_ENUMFMT,
};

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
	{"display",		no_argument, 0, OPT_DISPLAY},
	{"enum-formats",	no_argument, 0, OPT_ENUMFMT},
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
//	"  [-c, --crop=<top,left,width,height>]  : Crop target\n"
	"  -o, --output-file=<Output>            : Output file name\n"
	"  -p, --output-size=<WxH>               : Output frame size\n"
	"  -q, --output-format=<Pixel Format>    : Output frame format\n"
	"  -n, --num-frames=<num frames>         : Number of frames to convert\n"
	"  -h, --help                            : Display this help message\n"
	"  -v, --verbose                         : Verbose output\n"
	"  --enum-formats                        : Enumerate formats\n"
	"  --display                             : Display converted output\n"
	"\n"
	"Frame size and format can also be derived from the filename if formatted appropriately.\n"
	"For example \"video-file-720-480-nv12.yuv\" would be parsed as\n"
	"width = 720\nheight = 480\nfourcc = NV12\n";

	printf("%s\n", localusage);
}

int main(int argc, char **argv)
{
	int	srcHeight  = 0, dstHeight = 0;
	int	srcWidth   = 0, dstWidth  = 0;
	int	fin = -1, fout = -1;
	unsigned int	num_frames_convert = -1UL;
	DumbFramebuffer *srcfb[NUMBUF], *dstfb[NUMBUF];

	Card card;

	auto conn = card.get_first_connected_connector();
	auto crtc = conn->get_current_crtc();

	unsigned src_w = 800;
	unsigned src_h = 600;
	string src_fourcc = "YUYV";

	unsigned dst_w = 1280;
	unsigned dst_h = 800;
	string dst_fourcc = "XR24";

	unsigned disp_w = 800;
	unsigned disp_h = 600;

	/* let's setup default values before parsing arguments */
	string devname = "/dev/video10";
	string srcfile = "";
	string dstfile = "";
	unsigned int sf_w = 0, sf_h = 0, df_w = 0, df_h = 0;
	char sf_f[30], df_f[30];

	struct v4l2_selection selection = {};
	selection.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	selection.target = V4L2_SEL_TGT_CROP_ACTIVE;

	// These parameters can be implicetely found by parsing the file name.
	// But they can also be overriden if actually provided as parameters
	// so we need to know which vales to use
	bool src_fmt_provided = false;
	bool src_size_provided = false;
	bool dst_fmt_provided = false;
	bool dst_size_provided = false;
	int option_char;
	char shortoptions[] = "d:i:j:k:o:p:q:c:n:vh";

	do {
		int option_index;
		char *endptr;
		option_char = getopt_long(argc, argv, shortoptions, long_options, &option_index);

		switch (option_char) {
		case 0:
		case EOF:
			break;
		case OPT_DISPLAY:
			display_on = true;
			break;
		case OPT_ENUMFMT:
			enum_formats = true;
			break;
		case 'd':
		case 'D':
			char tmp_str[30];
			if (isdigit(optarg[0]) && strlen(optarg) <= 3) {
				sprintf(tmp_str, "/dev/video%s", optarg);
				devname = tmp_str;
			} else if (!strncmp(optarg, "/dev/video", 10)) {
				devname = optarg;
			} else {
				printf("ERROR: Device name not recognized: %s\n\n",
				       optarg);
				usage();
				exit(1);
			}
			printf("device_name: %s\n", devname.c_str());
			break;
		case 'i':
		case 'I':
			srcfile = optarg;
			printf("srcfile: %s\n", srcfile.c_str());
			fin = open(srcfile.c_str(), O_RDONLY);

			/* File name parsing */
			sscanf(srcfile.c_str(),
			       "%*[^0-9]%d%*[-_x]%d%*[-_]%[a-zA-Z0-9].",
			       &sf_w, &sf_h, sf_f);
			dprintf("Parsed i/f: %dx%d %s\n", sf_w, sf_h, sf_f);
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
			src_size_provided = true;

			/* default crop values at first */
			if (selection.r.height == 0) {
				selection.r.top = selection.r.left = 0;
				selection.r.width = srcWidth;
				selection.r.height = srcHeight;
			}
			break;
		case 'k':
		case 'K':
			src_fourcc = StringToUpper(optarg);
			src_fmt_provided = true;
			break;
		case 'o':
		case 'O':
			dstfile = optarg;
			printf("dstfile: %s\n", dstfile.c_str());
			fout = open(dstfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);

			/* File name parsing */
			sscanf(dstfile.c_str(),
			       "%*[^0-9]%d%*[-_x]%d%*[-_]%[a-zA-Z0-9].",
			       &df_w, &df_h, df_f);
			dprintf("Parsed o/f: %dx%d %s\n", df_w, df_h, df_f);
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
			dst_size_provided = true;
			break;
		case 'q':
		case 'Q':
			dst_fourcc = StringToUpper(optarg);
			dst_fmt_provided = true;
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
	} while (option_char != EOF);

	if (fin > 0) {
		if (!src_size_provided) {
			src_w = sf_w;
			src_h = sf_h;
		}
		if (!src_fmt_provided) {
			src_fourcc = StringToUpper(sf_f);
		}
	}
	if (fout > 0) {
		if (!dst_size_provided) {
			dst_w = df_w;
			dst_h = df_h;
		}
		if (!dst_fmt_provided) {
			dst_fourcc = StringToUpper(df_f);
		}
	}

	dprintf("in  %dx%d '%s'\n", src_w, src_h, src_fourcc.c_str());
	dprintf("out %dx%d '%s'\n", dst_w, dst_h, dst_fourcc.c_str());
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

	struct m2m *m2m = m2m_open((char *)devname.c_str());

	if (enum_formats) {
		printf("- Available formats:\n");
		video_enum_formats(m2m, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		video_enum_formats(m2m, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		video_enum_formats(m2m, V4L2_BUF_TYPE_VIDEO_OUTPUT);
		video_enum_formats(m2m, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		video_enum_formats(m2m, V4L2_BUF_TYPE_VIDEO_OVERLAY);
	}

	set_buffer_info(&m2m->src,
			V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
			V4L2_COLORSPACE_SMPTE170M,
			srcfb[0], 0, 0, srcfb[0]->width(), srcfb[0]->height());
	set_buffer_info(&m2m->dst,
			V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
			V4L2_COLORSPACE_SMPTE170M,
			dstfb[0], 0, 0, 0, 0);

	for (unsigned i = 0; i < NUMBUF; ++i) {
		setup_buffer(&m2m->src.buf[i], srcfb[i]);
		setup_buffer(&m2m->dst.buf[i], dstfb[i]);
	}

	m2m_pipe_init(m2m, &m2m->src);
	m2m_pipe_init(m2m, &m2m->dst);

	for (unsigned i = 0; i < NUMBUF; ++i) {
		m2m_qbuf(m2m, &m2m->dst, i);
	}

	unsigned num_frames_sent = 0;
	unsigned num_frames_received = 0;
	for (unsigned i = 0; i < m2m->src.numbuf && i < num_frames_convert; ++i) {
		/* Read from file or something */
		if (fin >= 0) {
			if (do_read(fin, srcfb[i]) <= 0) {
				num_frames_convert = num_frames_sent;
				break;
			}
		}
		m2m_qbuf(m2m, &m2m->src, i);
		num_frames_sent++;
	}

	stream_ON(m2m->fd);

	while(1) {

		if (num_frames_sent < num_frames_convert) {
			int index = m2m_dqbuf(m2m, &m2m->src);
			/* Read from file if available */
			if (fin >= 0) {
				if (do_read(fin, srcfb[index]) <= 0)
					num_frames_convert = num_frames_sent;
			}
			m2m_qbuf(m2m, &m2m->src, index);
			num_frames_sent++;
		}

		if (num_frames_received >= num_frames_sent)
			break;

		int index = m2m_dqbuf(m2m, &m2m->dst);
		num_frames_received++;
		/* Save result to file if available */
		if (fout >= 0) {
			if (do_write(fout, dstfb[index]) <= 0)
				break;
		}

		if (display_on)
			display_buffer(crtc, disp_w, disp_h, dstfb[index]);

		m2m_qbuf(m2m, &m2m->dst, index);
	}

	printf("%d frame(s) processed.\n", num_frames_received);

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
