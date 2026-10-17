#include "render/jpeg.h"

#include <csetjmp>
#include <cstdio>
#include <vector>

#include <jpeglib.h>

namespace fugue::render {

namespace {
// libjpeg reports a fatal error by calling error_exit, which must not return: jump back out of decodeJpeg instead of the
// default exit(). The message is kept for `why`.
struct ErrMgr {
    jpeg_error_mgr pub;
    std::jmp_buf jump;
    char msg[JMSG_LENGTH_MAX];
};
void onError(j_common_ptr c) {
    auto* e = reinterpret_cast<ErrMgr*>(c->err);
    (*c->err->format_message)(c, e->msg);
    std::longjmp(e->jump, 1);
}
void onMessage(j_common_ptr) {}   // warnings (e.g. a corrupt but recoverable stream) are not printed
}  // namespace

bool decodeJpeg(const uint8_t* d, size_t n, RgbaImage& out, std::string& why, uint64_t maxPixels) {
    jpeg_decompress_struct c;
    ErrMgr err;
    c.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = onError;
    err.pub.output_message = onMessage;
    err.msg[0] = 0;
    // Everything the longjmp can skip is declared above it and is trivially destructible or reset below.
    std::vector<uint8_t> row;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&c);
        why = std::string("jpeg: ") + err.msg;
        return false;
    }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, const_cast<unsigned char*>(d), static_cast<unsigned long>(n));
    if (jpeg_read_header(&c, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&c);
        why = "jpeg: no image";
        return false;
    }
    if (c.jpeg_color_space == JCS_CMYK || c.jpeg_color_space == JCS_YCCK) {
        jpeg_destroy_decompress(&c);
        why = "jpeg: CMYK is not supported";
        return false;
    }
    if (c.image_width == 0 || c.image_height == 0 || uint64_t(c.image_width) * c.image_height > maxPixels) {
        jpeg_destroy_decompress(&c);
        why = "jpeg: image size";
        return false;
    }
    c.out_color_space = JCS_RGB;  // grayscale expands; otherwise libjpeg's defaults (JDCT_ISLOW, as stock DreamDaemon uses)
    jpeg_start_decompress(&c);
    const int w = int(c.output_width), h = int(c.output_height);
    out.w = w;
    out.h = h;
    out.px.assign(size_t(w) * size_t(h) * 4, 255);
    row.resize(size_t(w) * 3);
    while (c.output_scanline < c.output_height) {
        const size_t y = c.output_scanline;
        JSAMPROW rp = row.data();
        jpeg_read_scanlines(&c, &rp, 1);
        uint8_t* dst = &out.px[y * size_t(w) * 4];
        for (int x = 0; x < w; x++) {
            dst[x * 4 + 0] = row[size_t(x) * 3 + 0];
            dst[x * 4 + 1] = row[size_t(x) * 3 + 1];
            dst[x * 4 + 2] = row[size_t(x) * 3 + 2];
        }
    }
    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    return true;
}

}  // namespace fugue::render
