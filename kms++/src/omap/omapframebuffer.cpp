
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>
#include <drm_fourcc.h>
#include <drm.h>
#include <drm_mode.h>

#include <kms++/kms++.h>
#include <kms++/omap/omapkms++.h>

extern "C" {
#include <omap_drmif.h>
}

using namespace std;

namespace kms
{

OmapFramebuffer::OmapFramebuffer(OmapCard& card, uint32_t width, uint32_t height, PixelFormat format)
	: MappedFramebuffer(card, width, height), m_omap_card(card), m_format(format)
{
	Create();
}

OmapFramebuffer::~OmapFramebuffer()
{
	Destroy();
}

void OmapFramebuffer::Create()
{
	const PixelFormatInfo& format_info = get_pixel_format_info(m_format);

	m_num_planes = format_info.num_planes;

	for (int i = 0; i < format_info.num_planes; ++i) {
		const PixelFormatPlaneInfo& pi = format_info.planes[i];
		FramebufferPlane& plane = m_planes[i];

		uint32_t flags = OMAP_BO_SCANOUT | OMAP_BO_WC;

		uint32_t size = width() * height() * pi.bitspp / 8;

		struct omap_bo* bo =  omap_bo_new(m_omap_card.dev(), size, flags);
		if (!bo)
			throw invalid_argument(string("omap_bo_new failed: ") + strerror(errno));

		uint32_t stride = width() * pi.bitspp / 8;

		plane.omap_bo = bo;
		plane.handle = omap_bo_handle(bo);
		plane.stride = stride;
		plane.size = omap_bo_size(bo);
		plane.offset = 0;
		plane.map = 0;
		plane.prime_fd = -1;
	}

	/* create framebuffer object for the dumb-buffer */
	uint32_t bo_handles[4] = { m_planes[0].handle, m_planes[1].handle };
	uint32_t pitches[4] = { m_planes[0].stride, m_planes[1].stride };
	uint32_t offsets[4] = { m_planes[0].offset, m_planes[1].offset };
	uint32_t id;
	int r = drmModeAddFB2(card().fd(), width(), height(), (uint32_t)format(),
			  bo_handles, pitches, offsets, &id, 0);
	if (r)
		throw invalid_argument(string("drmModeAddFB2 failed: ") + strerror(errno));

	set_id(id);
}

void OmapFramebuffer::Destroy()
{
	drmModeRmFB(card().fd(), id());

	for (uint i = 0; i < m_num_planes; ++i) {
		FramebufferPlane& plane = m_planes[i];

		/* unmap buffer */
		if (plane.map)
			munmap(plane.map, plane.size);

		omap_bo_del(plane.omap_bo);

		if (plane.prime_fd >= 0)
			::close(plane.prime_fd);
	}
}

uint8_t* OmapFramebuffer::map(unsigned plane)
{
	FramebufferPlane& p = m_planes[plane];

	if (p.map)
		return p.map;

	p.map = (uint8_t*)omap_bo_map(p.omap_bo);
	if (p.map == MAP_FAILED)
		throw invalid_argument(string("mmap failed: ") + strerror(errno));

	return p.map;
}

int OmapFramebuffer::prime_fd(unsigned int plane)
{
	FramebufferPlane& p = m_planes[plane];

	if (p.prime_fd >= 0)
		return p.prime_fd;

	int fd = omap_bo_dmabuf(p.omap_bo);
	if (fd < 0)
		throw std::runtime_error("omap_bo_dmabuf failed\n");

	p.prime_fd = fd;

	return fd;
}

}