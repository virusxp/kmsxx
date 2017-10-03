#include <linux/videodev2.h>
#include <cstdio>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <glob.h>
#include "jpeglib.h"

#include <kms++/kms++.h>
#include <kms++util/kms++util.h>

#include "/home/tomba/work/ludev/include/ti/cmem.h"
#include "/home/tomba/work/linux/include/uapi/linux/dma-buf.h"

#define CAMERA_BUF_QUEUE_SIZE	3
#define MAX_CAMERA		9

using namespace std;
using namespace kms;


#define CMEM_BLOCKID CMEM_CMABLOCKID

static CMEM_AllocParams cmem_alloc_params = {
	CMEM_HEAP,	/* type */
	CMEM_CACHED,	/* flags */
	4		/* alignment */
};

int dmabuf_do_cache_operation(int dma_buf_fd, uint32_t cache_operation)
{
	struct dma_buf_sync sync { };
	sync.flags = cache_operation;
	return ioctl(dma_buf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

int dmabuf_cpu_start_read(int fd)
{
	return dmabuf_do_cache_operation(fd, (DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ));
}

int dmabuf_cpu_end_read(int fd)
{
	return dmabuf_do_cache_operation(fd, (DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ));
}

//CMEM_free(cmem_buffer, &cmem_alloc_params);

enum class BufferProvider {
	DRM,
	V4L2,
	CMEM,
};

class CameraPipeline
{
public:
	CameraPipeline(int cam_fd, Card& card, Crtc* crtc, Plane* plane, uint32_t x, uint32_t y,
		       uint32_t iw, uint32_t ih, PixelFormat pixfmt,
		       BufferProvider buffer_provider);
	~CameraPipeline();

	CameraPipeline(const CameraPipeline& other) = delete;
	CameraPipeline& operator=(const CameraPipeline& other) = delete;

	void show_next_frame(AtomicReq &req);
	int fd() const { return m_fd; }
	void start_streaming();
private:
	ExtFramebuffer* GetExtFrameBuffer(Card& card, uint32_t i, PixelFormat pixfmt);
	ExtFramebuffer* GetExtCMEMFrameBuffer(Card& card, PixelFormat pixfmt);
	int m_fd;	/* camera file descriptor */
	Crtc* m_crtc;
	Plane* m_plane;
	BufferProvider m_buffer_provider;
	vector<Framebuffer*> m_fb;
	int m_prev_fb_index;
	uint32_t m_in_width, m_in_height; /* camera capture resolution */
	/* image properties for display */
	uint32_t m_out_width, m_out_height;
	uint32_t m_out_x, m_out_y;
};

static int buffer_export(int v4lfd, enum v4l2_buf_type bt, uint32_t index, int *dmafd)
{
	struct v4l2_exportbuffer expbuf;

	memset(&expbuf, 0, sizeof(expbuf));
	expbuf.type = bt;
	expbuf.index = index;
	if (ioctl(v4lfd, VIDIOC_EXPBUF, &expbuf) == -1) {
		perror("VIDIOC_EXPBUF");
		return -1;
	}

	*dmafd = expbuf.fd;

	return 0;
}

ExtFramebuffer* CameraPipeline::GetExtFrameBuffer(Card& card, uint32_t i, PixelFormat pixfmt)
{
	int r, dmafd;

	r = buffer_export(m_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, i, &dmafd);
	ASSERT(r == 0);

	const PixelFormatInfo& format_info = get_pixel_format_info(pixfmt);
	ASSERT(format_info.num_planes == 1);

	vector<int> fds { dmafd };
	vector<uint32_t> pitches { m_in_width * (format_info.planes[0].bitspp / 8) };
	vector<uint32_t> offsets { 0 };

	return new ExtFramebuffer(card, m_in_width, m_in_height, pixfmt,
				  fds, pitches, offsets);
}

ExtFramebuffer* CameraPipeline::GetExtCMEMFrameBuffer(Card& card, PixelFormat pixfmt)
{
	// XXX we never free cmem buffers

	const PixelFormatInfo& format_info = get_pixel_format_info(pixfmt);
	ASSERT(format_info.num_planes == 1);

	uint32_t size = m_in_width * (format_info.planes[0].bitspp / 8) * m_in_height;

	void* cmem_buf = CMEM_alloc2(CMEM_BLOCKID, size, &cmem_alloc_params);

	ASSERT(cmem_buf);

	int dmafd = CMEM_export_dmabuf(cmem_buf);

	ASSERT(dmafd >= 0);

	vector<int> fds { dmafd };
	vector<uint32_t> pitches { m_in_width * (format_info.planes[0].bitspp / 8) };
	vector<uint32_t> offsets { 0 };

	return new ExtFramebuffer(card, m_in_width, m_in_height, pixfmt,
				  fds, pitches, offsets);
}

bool inline better_size(struct v4l2_frmsize_discrete* v4ldisc,
			uint32_t iw, uint32_t ih,
			uint32_t best_w, uint32_t best_h)
{
	if (v4ldisc->width <= iw && v4ldisc->height <= ih &&
	    (v4ldisc->width >= best_w || v4ldisc->height >= best_h))
		return true;

	return false;
}

CameraPipeline::CameraPipeline(int cam_fd, Card& card, Crtc *crtc, Plane* plane, uint32_t x, uint32_t y,
			       uint32_t iw, uint32_t ih, PixelFormat pixfmt,
			       BufferProvider buffer_provider)
	: m_fd(cam_fd), m_crtc(crtc), m_buffer_provider(buffer_provider), m_prev_fb_index(-1)
{

	int r;
	uint32_t best_w = 320;
	uint32_t best_h = 240;

	struct v4l2_frmsizeenum v4lfrms = { };
	v4lfrms.pixel_format = (uint32_t)pixfmt;
	while (ioctl(m_fd, VIDIOC_ENUM_FRAMESIZES, &v4lfrms) == 0) {
		if (v4lfrms.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
			v4lfrms.index++;
			continue;
		}

		if (v4lfrms.discrete.width > iw || v4lfrms.discrete.height > ih) {
			//skip
		} else if (v4lfrms.discrete.width == iw && v4lfrms.discrete.height == ih) {
			// Exact match
			best_w = v4lfrms.discrete.width;
			best_h = v4lfrms.discrete.height;
			break;
		} else if (v4lfrms.discrete.width >= best_w || v4lfrms.discrete.height >= ih) {
			best_w = v4lfrms.discrete.width;
			best_h = v4lfrms.discrete.height;
		}

		v4lfrms.index++;
	};

	m_out_width = m_in_width = best_w;
	m_out_height = m_in_height = best_h;
	/* Move it to the middle of the requested area */
	m_out_x = x + iw / 2 - m_out_width / 2;
	m_out_y = y + ih / 2 - m_out_height / 2;

	printf("Capture: %ux%u\n", best_w, best_h);

	struct v4l2_format v4lfmt = { };
	v4lfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	r = ioctl(m_fd, VIDIOC_G_FMT, &v4lfmt);
	ASSERT(r == 0);

	v4lfmt.fmt.pix.pixelformat = (uint32_t)pixfmt;
	v4lfmt.fmt.pix.width = m_in_width;
	v4lfmt.fmt.pix.height = m_in_height;

	r = ioctl(m_fd, VIDIOC_S_FMT, &v4lfmt);
	ASSERT(r == 0);

	uint32_t v4l_mem;

	if (m_buffer_provider == BufferProvider::V4L2)
		v4l_mem = V4L2_MEMORY_MMAP;
	else
		v4l_mem = V4L2_MEMORY_DMABUF;

	struct v4l2_requestbuffers v4lreqbuf = { };
	v4lreqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4lreqbuf.memory = v4l_mem;
	v4lreqbuf.count = CAMERA_BUF_QUEUE_SIZE;
	r = ioctl(m_fd, VIDIOC_REQBUFS, &v4lreqbuf);
	ASSERT(r == 0);
	ASSERT(v4lreqbuf.count == CAMERA_BUF_QUEUE_SIZE);

	struct v4l2_buffer v4lbuf = { };
	v4lbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4lbuf.memory = v4l_mem;

	for (unsigned i = 0; i < CAMERA_BUF_QUEUE_SIZE; i++) {
		Framebuffer *fb;

		switch (m_buffer_provider) {
		case BufferProvider::V4L2:
			fb = GetExtFrameBuffer(card, i, pixfmt);
			break;
		case BufferProvider::DRM:
			fb = new DumbFramebuffer(card, m_in_width, m_in_height, pixfmt);
			break;
		case BufferProvider::CMEM:
			fb = GetExtCMEMFrameBuffer(card, pixfmt);
			break;
		default:
			ASSERT(0);
		}

		v4lbuf.index = i;
		if (m_buffer_provider != BufferProvider::V4L2)
			v4lbuf.m.fd = fb->prime_fd(0);
		r = ioctl(m_fd, VIDIOC_QBUF, &v4lbuf);
		ASSERT(r == 0);

		m_fb.push_back(fb);
	}

	m_plane = plane;

	// Do initial plane setup with first fb, so that we only need to
	// set the FB when page flipping
	AtomicReq req(card);

	Framebuffer *fb = m_fb[0];

	req.add(m_plane, "CRTC_ID", m_crtc->id());
	req.add(m_plane, "FB_ID", fb->id());

	req.add(m_plane, "CRTC_X", m_out_x);
	req.add(m_plane, "CRTC_Y", m_out_y);
	req.add(m_plane, "CRTC_W", m_out_width);
	req.add(m_plane, "CRTC_H", m_out_height);

	req.add(m_plane, "SRC_X", 0);
	req.add(m_plane, "SRC_Y", 0);
	req.add(m_plane, "SRC_W", m_in_width << 16);
	req.add(m_plane, "SRC_H", m_in_height << 16);

	r = req.commit_sync();
	FAIL_IF(r, "initial plane setup failed");
}

CameraPipeline::~CameraPipeline()
{
	for (unsigned i = 0; i < m_fb.size(); i++)
		delete m_fb[i];

	::close(m_fd);
}

void CameraPipeline::start_streaming()
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	int r = ioctl(m_fd, VIDIOC_STREAMON, &type);
	FAIL_IF(r, "Failed to enable camera stream: %d", r);
}

void save_jpeg(Framebuffer *fb)
{
	char filename[128];
	const int quality = 90;
	static int framen = 0;
	static uint8_t* tmp = 0;

	if (!tmp)
		tmp = (uint8_t*)malloc(fb->width() * 3);

	framen++;

	if (framen != 20 && framen != 40)
		return;


	Stopwatch sw;
	sw.start();

	dmabuf_cpu_start_read(fb->prime_fd(0));

	uint8_t* p = fb->map(0);

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE * outfile;		/* target file */
	JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */

	/* Step 1: allocate and initialize JPEG compression object */

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	/* Step 2: specify data destination (eg, a file) */

	sprintf(filename, "captest-%d.jpg", framen);

	if ((outfile = fopen(filename, "wb")) == NULL) {
		fprintf(stderr, "can't open %s\n", filename);
		exit(1);
	}
	jpeg_stdio_dest(&cinfo, outfile);

	/* Step 3: set parameters for compression */
	cinfo.image_width = fb->width(); 	/* image width and height, in pixels */
	cinfo.image_height = fb->height();
	cinfo.input_components = 3;		/* # of color components per pixel */
	cinfo.in_color_space = JCS_YCbCr; 	/* colorspace of input image */

	jpeg_set_defaults(&cinfo);

	jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

	/* Step 4: Start compressor */

	jpeg_start_compress(&cinfo, TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */

	/* Here we use the library's state variable cinfo.next_scanline as the
	  * loop counter, so that we don't have to keep track ourselves.
	  * To keep things simple, we pass one scanline per call; you can pass
	  * more if you wish, though.
	  */

	while (cinfo.next_scanline < cinfo.image_height) {
		unsigned offset = cinfo.next_scanline * fb->stride(0);

		for (unsigned i = 0, j = 0; i < cinfo.image_width * 2; i += 4, j += 6) {
		    tmp[j + 0] = p[offset + i + 0]; // Y
		    tmp[j + 1] = p[offset + i + 1]; // U
		    tmp[j + 2] = p[offset + i + 3]; // V
		    tmp[j + 3] = p[offset + i + 2]; // Y
		    tmp[j + 4] = p[offset + i + 1]; // U
		    tmp[j + 5] = p[offset + i + 3]; // V
		}

		//row_pointer[0] = p + cinfo.next_scanline * row_stride;
		row_pointer[0] = tmp;

		(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	/* Step 6: Finish compression */

	jpeg_finish_compress(&cinfo);

	fclose(outfile);

	/* Step 7: release JPEG compression object */

	jpeg_destroy_compress(&cinfo);

	dmabuf_cpu_end_read(fb->prime_fd(0));

	double us = sw.elapsed_us();
	printf("Wrote %s in %u ms\n", filename, (unsigned)(us/1000));
}

void CameraPipeline::show_next_frame(AtomicReq& req)
{
	int r;
	uint32_t v4l_mem;

	if (m_buffer_provider == BufferProvider::V4L2)
		v4l_mem = V4L2_MEMORY_MMAP;
	else
		v4l_mem = V4L2_MEMORY_DMABUF;

	struct v4l2_buffer v4l2buf = { };
	v4l2buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2buf.memory = v4l_mem;
	r = ioctl(m_fd, VIDIOC_DQBUF, &v4l2buf);
	if (r != 0) {
		printf("VIDIOC_DQBUF ioctl failed with %d\n", errno);
		return;
	}

	unsigned fb_index = v4l2buf.index;

	Framebuffer *fb = m_fb[fb_index];

	save_jpeg(fb);

	req.add(m_plane, "FB_ID", fb->id());

	if (m_prev_fb_index >= 0) {
		memset(&v4l2buf, 0, sizeof(v4l2buf));
		v4l2buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2buf.memory = v4l_mem;
		v4l2buf.index = m_prev_fb_index;
		if (m_buffer_provider != BufferProvider::V4L2)
			v4l2buf.m.fd = m_fb[m_prev_fb_index]->prime_fd(0);
		r = ioctl(m_fd, VIDIOC_QBUF, &v4l2buf);
		ASSERT(r == 0);

	}

	m_prev_fb_index = fb_index;
}

static bool is_capture_dev(int fd)
{
	struct v4l2_capability cap = { };
	int r = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	ASSERT(r == 0);
	return cap.capabilities & V4L2_CAP_VIDEO_CAPTURE;
}

std::vector<std::string> glob(const std::string& pat)
{
	glob_t glob_result;
	glob(pat.c_str(), 0, NULL, &glob_result);
	vector<string> ret;
	for(unsigned i = 0; i < glob_result.gl_pathc; ++i)
		ret.push_back(string(glob_result.gl_pathv[i]));
	globfree(&glob_result);
	return ret;
}

static const char* usage_str =
		"Usage: kmscapture [OPTIONS]\n\n"
		"Options:\n"
		"  -s, --single                Single camera mode. Open only /dev/video0\n"
		"      --buffer-type=<drm|v4l> Use DRM or V4L provided buffers. Default: DRM\n"
		"  -h, --help                  Print this help\n"
		;


int main(int argc, char** argv)
{
	//test();
	//return 0;

	BufferProvider buffer_provider = BufferProvider::CMEM;
	bool single_cam = false;

	OptionSet optionset = {
		Option("s|single", [&]()
		{
			single_cam = true;
		}),
		Option("|buffer-type=", [&](string s)
		{
			if (s == "v4l")
			buffer_provider = BufferProvider::V4L2;
			else if (s == "drm")
			buffer_provider = BufferProvider::DRM;
			else if (s == "cmem")
			buffer_provider = BufferProvider::CMEM;
			else
			FAIL("Invalid buffer provider: %s", s.c_str());
		}),
		Option("h|help", [&]()
		{
			puts(usage_str);
			exit(-1);
		}),
	};

	optionset.parse(argc, argv);

	if (optionset.params().size() > 0) {
		puts(usage_str);
		exit(-1);
	}

	if (buffer_provider == BufferProvider::CMEM)
		CMEM_init();

	auto pixfmt = PixelFormat::YUYV;

	Card card;

	auto conn = card.get_first_connected_connector();
	auto crtc = conn->get_current_crtc();
	printf("Display: %dx%d\n", crtc->width(), crtc->height());
	printf("Buffer provider: %s\n", buffer_provider == BufferProvider::V4L2? "V4L" : (buffer_provider == BufferProvider::DRM ? "DRM" : "CMEM"));

	vector<int> camera_fds;

	for (string vidpath : glob("/dev/video*")) {
		int fd = ::open(vidpath.c_str(), O_RDWR | O_NONBLOCK);

		if (fd < 0)
			continue;

		if (!is_capture_dev(fd)) {
			close(fd);
			continue;
		}

		camera_fds.push_back(fd);
		printf("Using %s\n", vidpath.c_str());

		if (single_cam)
			break;
	}

	FAIL_IF(camera_fds.size() == 0, "No cameras found");

	vector<Plane*> available_planes;
	for (Plane* p : crtc->get_possible_planes()) {
		if (p->plane_type() != PlaneType::Overlay)
			continue;

		if (!p->supports_format(pixfmt))
			continue;

		available_planes.push_back(p);
	}

	FAIL_IF(available_planes.size() < camera_fds.size(), "Not enough video planes for cameras");

	uint32_t plane_w = crtc->width() / camera_fds.size();
	vector<CameraPipeline*> cameras;

	for (unsigned i = 0; i < camera_fds.size(); ++i) {
		int cam_fd = camera_fds[i];
		Plane* plane = available_planes[i];

		auto cam = new CameraPipeline(cam_fd, card, crtc, plane, i * plane_w, 0,
					      plane_w, crtc->height(), pixfmt, buffer_provider);
		cameras.push_back(cam);
	}

	unsigned nr_cameras = cameras.size();

	vector<pollfd> fds(nr_cameras + 1);

	for (unsigned i = 0; i < nr_cameras; i++) {
		fds[i].fd = cameras[i]->fd();
		fds[i].events =  POLLIN;
	}
	fds[nr_cameras].fd = 0;
	fds[nr_cameras].events =  POLLIN;

	for (auto cam : cameras)
		cam->start_streaming();

	while (true) {
		int r = poll(fds.data(), nr_cameras + 1, -1);
		ASSERT(r > 0);

		if (fds[nr_cameras].revents != 0)
			break;

		AtomicReq req(card);

		for (unsigned i = 0; i < nr_cameras; i++) {
			if (!fds[i].revents)
				continue;
			cameras[i]->show_next_frame(req);
			fds[i].revents = 0;
		}

		r = req.test();
		FAIL_IF(r, "Atomic commit failed: %d", r);

		req.commit_sync();
	}

	for (auto cam : cameras)
		delete cam;
}
