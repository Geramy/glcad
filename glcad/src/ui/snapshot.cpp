#include "ui/snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "ui/shell.hpp"
#include "ui/frame_types.hpp"

namespace gcapp {

namespace {

struct Image {
    int width{0}, height{0};
    std::vector<unsigned char> px;
    std::vector<float>         depth;

    Image(int w, int h)
        : width(w), height(h), px(static_cast<std::size_t>(w) * h * 4, 0),
          depth(static_cast<std::size_t>(w) * h, 1.0f) {}

    void triangle(const float a[3], const float b[3], const float c[3], const float col[4]) {
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a[0], b[0], c[0]}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a[1], b[1], c[1]}))));
        const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max({a[0], b[0], c[0]}))));
        const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max({a[1], b[1], c[1]}))));
        const float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        if (std::fabs(area) < 1e-6f) return;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px_ = static_cast<float>(x) + 0.5f, py_ = static_cast<float>(y) + 0.5f;
                float w0 = ((b[0] - px_) * (c[1] - py_) - (b[1] - py_) * (c[0] - px_)) / area;
                float w1 = ((c[0] - px_) * (a[1] - py_) - (c[1] - py_) * (a[0] - px_)) / area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float z = a[2] + w1 * (b[2] - a[2]) + w2 * (c[2] - a[2]);
                const std::size_t i = static_cast<std::size_t>(y) * width + x;
                if (z > depth[i]) continue;
                depth[i] = z;
                blend(x, y, col[0], col[1], col[2], col[3]);
            }
        }
    }

    float clip_x0{-1e9f}, clip_y0{-1e9f}, clip_x1{1e9f}, clip_y1{1e9f};
    void blend(int x, int y, float r, float g, float b, float a) {
        if (static_cast<float>(x) + 0.5f < clip_x0 || static_cast<float>(x) + 0.5f > clip_x1 ||
            static_cast<float>(y) + 0.5f < clip_y0 || static_cast<float>(y) + 0.5f > clip_y1) return;
        if (x < 0 || y < 0 || x >= width || y >= height || a <= 0.0f) return;
        const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
        auto mix = [&](unsigned char dst, float src) {
            const float d = dst / 255.0f;
            return static_cast<unsigned char>(
                std::lround(std::clamp(src * a + d * (1.0f - a), 0.0f, 1.0f) * 255.0f));
        };
        px[i + 0] = mix(px[i + 0], r);
        px[i + 1] = mix(px[i + 1], g);
        px[i + 2] = mix(px[i + 2], b);
        px[i + 3] = 255;
    }

    void fill_rect(float x, float y, float w, float h, const float c[4], float radius) {
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = static_cast<int>(std::ceil(x + w));
        const int y1 = static_cast<int>(std::ceil(y + h));
        for (int py = y0; py < y1; ++py)
            for (int pxi = x0; pxi < x1; ++pxi) {
                if (radius > 0.5f) {
                    const float cx = std::clamp(static_cast<float>(pxi) + 0.5f,
                                                x + radius, x + w - radius);
                    const float cy = std::clamp(static_cast<float>(py) + 0.5f,
                                                y + radius, y + h - radius);
                    const float dx = static_cast<float>(pxi) + 0.5f - cx;
                    const float dy = static_cast<float>(py) + 0.5f - cy;
                    if (dx * dx + dy * dy > radius * radius) continue;
                }
                blend(pxi, py, c[0], c[1], c[2], c[3]);
            }
    }

    void stroke_rect(float x, float y, float w, float h, const float c[4],
                     float thickness, float radius) {
        const float r = std::min(radius, std::min(w, h) * 0.5f);
        const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        const int x1 = static_cast<int>(std::ceil(x + w)), y1 = static_cast<int>(std::ceil(y + h));
        const auto sd = [&](float px_, float py_) {
            const float hx = w * 0.5f - r, hy = h * 0.5f - r;
            const float qx = std::fabs(px_ - (x + w * 0.5f)) - hx;
            const float qy = std::fabs(py_ - (y + h * 0.5f)) - hy;
            const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
            return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
        };
        for (int py = y0; py < y1; ++py)
            for (int pxi = x0; pxi < x1; ++pxi) {
                const float d = sd(static_cast<float>(pxi) + 0.5f, static_cast<float>(py) + 0.5f);
                if (d > 0.5f || d < -thickness - 0.5f) continue;
                blend(pxi, py, c[0], c[1], c[2], c[3]);
            }
    }

    void line_depth(float x0, float y0, float z0, float x1, float y1, float z1,
                    const float c[4], float w) {
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, static_cast<int>(len * 2.0f));
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float x = x0 + dx * t, y = y0 + dy * t, z = z0 + (z1 - z0) * t;
            const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const int px_ = cx + ox, py_ = cy + oy;
                    if (px_ < 0 || py_ < 0 || px_ >= width || py_ >= height) continue;
                    const float d = std::hypot(static_cast<float>(px_) + 0.5f - x,
                                               static_cast<float>(py_) + 0.5f - y);
                    const float a = std::clamp(w * 0.5f + 0.5f - d, 0.0f, 1.0f);
                    if (a <= 0.0f) continue;
                    if (z > depth[static_cast<std::size_t>(py_) * width + px_]) continue;
                    blend(px_, py_, c[0], c[1], c[2], c[3] * a);
                }
        }
    }

    void line(float x0, float y0, float x1, float y1, const float c[4], float w) {
        const float dx = x1 - x0, dy = y1 - y0;
        const int steps = static_cast<int>(std::max(std::fabs(dx), std::fabs(dy))) + 1;
        for (int i = 0; i <= steps; ++i) {
            const float t = steps ? static_cast<float>(i) / steps : 0.0f;
            const float x = x0 + dx * t, y = y0 + dy * t;
            const int half = static_cast<int>(std::max(1.0f, w * 0.5f));
            for (int oy = -half; oy <= half; ++oy)
                for (int ox = -half; ox <= half; ++ox)
                    blend(static_cast<int>(x) + ox, static_cast<int>(y) + oy,
                          c[0], c[1], c[2], c[3]);
        }
    }

    std::vector<unsigned char> png_bytes() const;
    bool write_png(const char* path) const;
};

unsigned long crc_table[256];
bool crc_ready = false;

unsigned long png_crc(const unsigned char* buf, std::size_t len) {
    if (!crc_ready) {
        for (unsigned long n = 0; n < 256; ++n) {
            unsigned long c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            crc_table[n] = c;
        }
        crc_ready = true;
    }
    unsigned long c = 0xFFFFFFFFUL;
    for (std::size_t i = 0; i < len; ++i)
        c = crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFUL;
}

void put32(std::vector<unsigned char>& v, unsigned long x) {
    v.push_back(static_cast<unsigned char>((x >> 24) & 0xFF));
    v.push_back(static_cast<unsigned char>((x >> 16) & 0xFF));
    v.push_back(static_cast<unsigned char>((x >> 8) & 0xFF));
    v.push_back(static_cast<unsigned char>(x & 0xFF));
}

void chunk(std::vector<unsigned char>& out, const char* type,
           const std::vector<unsigned char>& data) {
    put32(out, static_cast<unsigned long>(data.size()));
    std::vector<unsigned char> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, png_crc(body.data(), body.size()));
}

std::vector<unsigned char> Image::png_bytes() const {
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>(height) * (width * 4 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::size_t row = static_cast<std::size_t>(y) * width * 4;
        raw.insert(raw.end(), px.begin() + static_cast<long>(row),
                   px.begin() + static_cast<long>(row + static_cast<std::size_t>(width) * 4));
    }

    std::vector<unsigned char> z;
    z.push_back(0x78);
    z.push_back(0x01);
    std::size_t at = 0;
    while (at < raw.size()) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        z.push_back(at + n >= raw.size() ? 1 : 0);
        z.push_back(static_cast<unsigned char>(n & 0xFF));
        z.push_back(static_cast<unsigned char>((n >> 8) & 0xFF));
        z.push_back(static_cast<unsigned char>(~n & 0xFF));
        z.push_back(static_cast<unsigned char>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<long>(at),
                 raw.begin() + static_cast<long>(at + n));
        at += n;
    }
    unsigned long a = 1, b = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);

    std::vector<unsigned char> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<unsigned char> ihdr;
    put32(ihdr, static_cast<unsigned long>(width));
    put32(ihdr, static_cast<unsigned long>(height));
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    return out;
}

bool Image::write_png(const char* path) const {
    const std::vector<unsigned char> out = png_bytes();
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

}

std::vector<gcm::u8> snapshot_png(Shell& shell, int width, int height) {
    if (width <= 0 || height <= 0) return {};
    shell.build_frame();
    Image image(width, height);

    const FrameUniforms uni = shell.frame_uniforms();
    {
        const auto& cam = shell.camera();
        const auto lerp3 = [](const float* a, const float* b, float k, float* o) {
            for (int i = 0; i < 3; ++i) o[i] = a[i] + (b[i] - a[i]) * k;
        };
        const auto grid_line = [](double wx, double wy, double fwx, double fwy,
                                  double spacing, double w) {
            const auto frac = [](double v) { return v - std::floor(v); };
            const double gx = std::fabs(frac(wx / spacing - 0.5) - 0.5) * spacing / std::max(fwx, 1e-9);
            const double gy = std::fabs(frac(wy / spacing - 0.5) - 0.5) * spacing / std::max(fwy, 1e-9);
            const double d = std::min(gx, gy);
            if (d >= w) return 0.0;
            const double u = std::clamp(d / w, 0.0, 1.0);
            return 1.0 - (u * u * (3.0 - 2.0 * u));
        };
        for (int py = 0; py < height; ++py) {
            for (int pxi = 0; pxi < width; ++pxi) {
                const double sx = pxi + 0.5, sy = py + 0.5;
                double wx, wy, wx1, wy1, wx2, wy2;
                {
                    double hx, hy;
                    if (cam.plane_hit(sx, sy, 0.0, hx, hy)) {
                        const gview::Mat4 vp = cam.view_projection();
                        const double z = vp.m[2] * hx + vp.m[6] * hy + vp.m[14];
                        const double w = vp.m[3] * hx + vp.m[7] * hy + vp.m[15];
                        image.depth[static_cast<std::size_t>(py) * width + pxi] =
                            static_cast<float>(w > 1e-12 ? z / w : 1.0) + 4e-7f;
                    }
                }
                if (!cam.plane_hit(sx, sy, 0.0, wx, wy) ||
                    !cam.plane_hit(sx + 1.0, sy, 0.0, wx1, wy1) ||
                    !cam.plane_hit(sx, sy + 1.0, 0.0, wx2, wy2)) {
                    image.blend(pxi, py, uni.background[0], uni.background[1],
                                uni.background[2], 1.0f);
                    continue;
                }
                const double fwx = std::max(std::fabs(wx1 - wx), std::fabs(wx2 - wx));
                const double fwy = std::max(std::fabs(wy1 - wy), std::fabs(wy2 - wy));
                const double c=std::cos(uni.plane_n[3]),sn=std::sin(uni.plane_n[3]);
                const double gx=c*wx+sn*wy,gy=-sn*wx+c*wy;
                const double gfwx=std::max(std::abs(c*(wx1-wx)+sn*(wy1-wy)),std::abs(c*(wx2-wx)+sn*(wy2-wy)));
                const double gfwy=std::max(std::abs(-sn*(wx1-wx)+c*(wy1-wy)),std::abs(-sn*(wx2-wx)+c*(wy2-wy)));
                const double decade=std::max(double(uni.plane_style[3]),1e-9);
                const double mi=grid_line(gx,gy,gfwx,gfwy,decade,1.0)*uni.plane_style[2];
                const double ma=grid_line(gx,gy,gfwx,gfwy,decade*10,1.4)*uni.plane_style[2];
                const auto axis = [](double dist, double fw) {
                    const double u = std::clamp(std::fabs(dist) / std::max(fw, 1e-9) / 1.5, 0.0, 1.0);
                    return 1.0 - (u * u * (3.0 - 2.0 * u));
                };
                float c1[3], c2[3], c3[3], c4[3];
                lerp3(uni.background, uni.grid_minor, float(mi * 0.9), c1);
                lerp3(c1, uni.grid_major, float(ma * 0.9), c2);
                lerp3(c2, uni.axis_x, float(axis(wy, fwy) * 0.85 * uni.plane_u[3]), c3);
                lerp3(c3, uni.axis_y, float(axis(wx, fwx) * 0.85 * uni.plane_u[3]), c4);
                const float tilt = 1.0f - uni.viewport[3];
                if (tilt > 0.0f && wx >= uni.plane[0] && wx <= uni.plane[2] &&
                    wy >= uni.plane[1] && wy <= uni.plane[3]) {
                    const double ex = std::min(wx - uni.plane[0], uni.plane[2] - wx) / std::max(fwx, 1e-9);
                    const double ey = std::min(wy - uni.plane[1], uni.plane[3] - wy) / std::max(fwy, 1e-9);
                    const double edge = std::min(ex, ey);
                    double g = 1.0 - std::clamp(edge / double(uni.plane_style[1]), 0.0, 1.0);
                    g = g * g * (3.0 - 2.0 * g); g = 1.0 - g; g = 1.0 - g;
                    const double glow = g * g;
                    float tint[3], lit[3];
                    lerp3(c4, uni.grid_major, uni.plane_style[0] * tilt, tint);
                    lerp3(tint, uni.grid_major, float(0.7 * glow * tilt), lit);
                    c4[0] = lit[0]; c4[1] = lit[1]; c4[2] = lit[2];
                }
                image.blend(pxi, py, c4[0], c4[1], c4[2], 1.0f);
            }
        }
    }

    for(const auto& draw:shell.images()) {
        if(!draw.image||!draw.image->valid())continue;
        const auto& raster=*draw.image;
        for(std::size_t k=0;k+5<draw.vertices.size();k+=6) {
            float x0=float(width),y0=float(height),x1=0,y1=0;
            for(std::size_t i=k;i<k+6;++i){const auto& v=draw.vertices[i];x0=std::min(x0,v.pos[0]);x1=std::max(x1,v.pos[0]);y0=std::min(y0,v.pos[1]);y1=std::max(y1,v.pos[1]);}
            for(int y=std::max(0,int(std::floor(y0)));y<std::min(height,int(std::ceil(y1)));++y)
            for(int x=std::max(0,int(std::floor(x0)));x<std::min(width,int(std::ceil(x1)));++x) {
                const float sx=x+.5f,sy=y+.5f;const auto& clip=draw.vertices[k].clip;
                if(sx<clip[0]||sy<clip[1]||sx>clip[0]+clip[2]||sy>clip[1]+clip[3])continue;
                for(int tri=0;tri<2;++tri){const auto& a=draw.vertices[k+tri*3];const auto& b=draw.vertices[k+tri*3+1];const auto& c=draw.vertices[k+tri*3+2];
                    const double area=(b.pos[0]-a.pos[0])*(c.pos[1]-a.pos[1])-(b.pos[1]-a.pos[1])*(c.pos[0]-a.pos[0]);if(std::abs(area)<1e-8)continue;
                    const double wa=((b.pos[0]-sx)*(c.pos[1]-sy)-(b.pos[1]-sy)*(c.pos[0]-sx))/area;
                    const double wb=((c.pos[0]-sx)*(a.pos[1]-sy)-(c.pos[1]-sy)*(a.pos[0]-sx))/area,wc=1-wa-wb;
                    if(wa<0||wb<0||wc<0)continue;
                    const double u=wa*a.uv[0]+wb*b.uv[0]+wc*c.uv[0],v=wa*a.uv[1]+wb*b.uv[1]+wc*c.uv[1];
                    const double tx=std::clamp(u*raster.width-.5,0.0,double(raster.width-1)),ty=std::clamp(v*raster.height-.5,0.0,double(raster.height-1));
                    const int ix=int(tx),iy=int(ty),jx=std::min(ix+1,raster.width-1),jy=std::min(iy+1,raster.height-1);const double fx=tx-ix,fy=ty-iy;
                    float color[4]{};for(int ch=0;ch<4;++ch){auto sample=[&](int px,int py){return raster.rgba[(std::size_t(py)*raster.width+px)*4+ch]/255.0;};color[ch]=float((sample(ix,iy)*(1-fx)+sample(jx,iy)*fx)*(1-fy)+(sample(ix,jy)*(1-fx)+sample(jx,jy)*fx)*fy);}
                    image.blend(x,y,color[0],color[1],color[2],color[3]);break;
                }
            }
        }
    }

    {
        const auto& cam = shell.camera();
        const auto& mesh = shell.mesh();
        const gview::Mat4 vp = cam.view_projection();
        const auto clip_z = [&](const float* p) {
            const double z = vp.m[2] * p[0] + vp.m[6] * p[1] + vp.m[10] * p[2] + vp.m[14];
            const double w = vp.m[3] * p[0] + vp.m[7] * p[1] + vp.m[11] * p[2] + vp.m[15];
            return static_cast<float>(w > 1e-12 ? z / w : 1.0);
        };
        const float light[3] = {0.35f, 0.25f, 0.9f};
        for (std::size_t i = 0; i + 2 < mesh.size(); i += 3) {
            float s[3][3];
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                double px, py;
                const float* p = mesh[i + k].pos;
                ok = cam.project(gview::Point3::from(p[0], p[1], p[2]), px, py);
                s[k][0] = float(px); s[k][1] = float(py); s[k][2] = clip_z(p);
            }
            if (!ok) continue;
            const float* n = mesh[i].normal;
            const float lambert = (n[0]*n[0]+n[1]*n[1]+n[2]*n[2]<1e-12f) ? 1.f : 0.45f + 0.55f * std::max(0.0f, n[0] * light[0] + n[1] * light[1] + n[2] * light[2]);
            const float c[4] = {mesh[i].color[0] * lambert, mesh[i].color[1] * lambert,
                                mesh[i].color[2] * lambert, mesh[i].color[3]};
            image.triangle(s[0], s[1], s[2], c);
        }
    }

    const auto& cam = shell.camera();
    auto to_screen = [&](const float* p, float& sx, float& sy) {
        double px, py;
        if (!cam.project(gview::Point3::from(p[0], p[1], p[2]), px, py)) return false;
        sx = float(px); sy = float(py);
        return true;
    };
    std::vector<LineVertex> lines = shell.static_lines();
    lines.insert(lines.end(), shell.canvas_lines().begin(), shell.canvas_lines().end());
    const gview::Mat4 vpm = cam.view_projection();
    const auto depth_of = [&](const float* p) {
        const double z = vpm.m[2] * p[0] + vpm.m[6] * p[1] + vpm.m[10] * p[2] + vpm.m[14];
        const double w = vpm.m[3] * p[0] + vpm.m[7] * p[1] + vpm.m[11] * p[2] + vpm.m[15];
        return static_cast<float>(w > 1e-12 ? z / w - 2e-4 : 1.0);
    };
    const std::size_t n_sketch = std::min(shell.static_lines().size() + shell.sketch_line_count(), lines.size());
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        float ax, ay, bx, by;
        if (!to_screen(lines[i].pos, ax, ay) || !to_screen(lines[i + 1].pos, bx, by))
            continue;
        if (i < n_sketch)
            image.line_depth(ax, ay, depth_of(lines[i].pos), bx, by,
                             depth_of(lines[i + 1].pos), lines[i].color, 1.5f);
        else
            image.line(ax, ay, bx, by, lines[i].color, 1.5f);
    }
    const auto draw_arcs = [&](const std::vector<ArcInstance>& arcs, bool depth) {
        for (const auto& a : arcs) {
            float prev[3] = {0, 0, 0};
            for (int i = 0; i <= 96; ++i) {
                const float t = a.centre[3] + (a.major[3] - a.centre[3]) * (float(i) / 96.0f);
                float p3[3];
                for (int k = 0; k < 3; ++k) p3[k] = a.centre[k] + a.major[k] * std::cos(t) + a.minor[k] * std::sin(t);
                float sx0, sy0, sx1, sy1;
                if (i && to_screen(prev, sx0, sy0) && to_screen(p3, sx1, sy1)) {
                    if (depth) image.line_depth(sx0, sy0, depth_of(prev), sx1, sy1, depth_of(p3), a.color, 1.5f);
                    else image.line(sx0, sy0, sx1, sy1, a.color, 1.5f);
                }
                prev[0] = p3[0]; prev[1] = p3[1]; prev[2] = p3[2];
            }
        }
    };
    draw_arcs(shell.static_arcs(), true);
    draw_arcs(shell.canvas_arcs(), false);
    for (const auto& p : shell.canvas_points()) {
        float sx, sy;
        if (!to_screen(p.pos, sx, sy)) continue;
        image.fill_rect(sx - 2.0f, sy - 2.0f, 4.0f, 4.0f, p.color, 0.0f);
    }

    const auto draw_ui_slice = [&](std::size_t lo, std::size_t hi) {
        const auto& ui = shell.ui();
        hi = std::min(hi, ui.size());
        for (std::size_t i = lo; i < hi; ++i) {
            const UiInstance& u = ui[i];
            const float kind = u.params[2];
            if (kind == 2.0f) continue;
            image.clip_x0 = u.clip[0]; image.clip_y0 = u.clip[1];
            image.clip_x1 = u.clip[0] + u.clip[2]; image.clip_y1 = u.clip[1] + u.clip[3];
            if (kind == 1.0f)
                image.stroke_rect(u.rect[0], u.rect[1], u.rect[2], u.rect[3], u.color,
                                  std::max(1.0f, u.params[1]), u.params[0]);
            else
                image.fill_rect(u.rect[0], u.rect[1], u.rect[2], u.rect[3], u.color,
                                u.params[0]);
        }
        image.clip_x0 = image.clip_y0 = -1e9f;
        image.clip_x1 = image.clip_y1 = 1e9f;
    };

    const auto draw_tris_slice = [&](std::size_t lo, std::size_t hi) {
        const auto& tv = shell.tris();
        hi = std::min(hi, tv.size());
        for (std::size_t i = lo; i + 2 < hi; i += 3) {
            const float a[3] = {tv[i].pos[0], tv[i].pos[1], -1.0e9f};
            const float b[3] = {tv[i + 1].pos[0], tv[i + 1].pos[1], -1.0e9f};
            const float c[3] = {tv[i + 2].pos[0], tv[i + 2].pos[1], -1.0e9f};
            image.triangle(a, b, c, tv[i].color);
        }
    };

    const float px_range = gtext::Font::sdf_distance_range;
    const float edge = gtext::Font::sdf_on_edge / 255.0f;
    const auto&      atlas = shell.font().atlas();
    const int        aw = shell.font().atlas_width();
    const int        ah = shell.font().atlas_height();
    const auto& text = shell.text();
    const bool text_ok = !atlas.empty() && aw > 0 && ah > 0;
    const auto sample = [&](float u, float v) {
        const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(aw) - 0.5f;
        const float fy = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(ah) - 0.5f;
        const int   x0 = static_cast<int>(std::floor(fx));
        const int   y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(x0);
        const float ty = fy - static_cast<float>(y0);
        const auto  at = [&](int x, int y) {
            x = std::clamp(x, 0, aw - 1);
            y = std::clamp(y, 0, ah - 1);
            return atlas[static_cast<std::size_t>(y) * aw + x] / 255.0f;
        };
        const float a = at(x0, y0), b = at(x0 + 1, y0);
        const float c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
        return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
    };

    const auto draw_text_slice = [&](std::size_t lo, std::size_t hi) {
        if (!text_ok) return;
        hi = std::min(hi, text.size());
        for (std::size_t i = lo; i + 5 < hi; i += 6) {
            float x0 = text[i].pos[0], y0 = text[i].pos[1];
            float x1 = x0, y1 = y0;
            float u0 = text[i].uv[0], v0 = text[i].uv[1];
            float u1 = u0, v1 = v0;
            for (std::size_t k = 0; k < 6; ++k) {
                x0 = std::min(x0, text[i + k].pos[0]);
                y0 = std::min(y0, text[i + k].pos[1]);
                x1 = std::max(x1, text[i + k].pos[0]);
                y1 = std::max(y1, text[i + k].pos[1]);
                u0 = std::min(u0, text[i + k].uv[0]);
                v0 = std::min(v0, text[i + k].uv[1]);
                u1 = std::max(u1, text[i + k].uv[0]);
                v1 = std::max(v1, text[i + k].uv[1]);
            }
            if (x1 - x0 < 0.01f || y1 - y0 < 0.01f) continue;
            const float texels_per_pixel = std::max(
                (u1 - u0) * aw / (x1 - x0), (v1 - v0) * ah / (y1 - y0));
            const float* col  = text[i].color;
            const float* clip = text[i].clip;

            const int px0 = static_cast<int>(std::floor(x0));
            const int py0 = static_cast<int>(std::floor(y0));
            const int px1 = static_cast<int>(std::ceil(x1));
            const int py1 = static_cast<int>(std::ceil(y1));
            for (int py = py0; py < py1; ++py) {
                for (int pxi = px0; pxi < px1; ++pxi) {
                    const float sx = static_cast<float>(pxi) + 0.5f;
                    const float sy = static_cast<float>(py) + 0.5f;
                    if (sx < clip[0] || sx > clip[0] + clip[2] ||
                        sy < clip[1] || sy > clip[1] + clip[3])
                        continue;
                    const float u = u0 + (u1 - u0) * (sx - x0) / (x1 - x0);
                    const float v = v0 + (v1 - v0) * (sy - y0) / (y1 - y0);
                    const float sd = sample(u, v);
                    const float d  = (sd - edge) * px_range / std::max(texels_per_pixel, 1e-4f);
                    const float a  = std::clamp(d + 0.5f, 0.0f, 1.0f);
                    if (a <= 0.002f) continue;
                    image.blend(pxi, py, col[0], col[1], col[2], col[3] * a);
                }
            }
        }
    };

    const UiLayers& L = shell.ui_layers();
    if (!L.runs.empty()) {
        for (const auto& run:L.runs) {
            if (run.kind==UiLayers::Kind::Surface) draw_ui_slice(run.first,run.first+run.count);
            else if (run.kind==UiLayers::Kind::Triangle) draw_tris_slice(run.first,run.first+run.count);
            else draw_text_slice(run.first,run.first+run.count);
        }
    } else for (int k = 0; k < UiLayers::kCount; ++k) {
        draw_ui_slice(L.ui[k], L.ui[k + 1]);
        draw_tris_slice(L.tris[k], L.tris[k + 1]);
        draw_text_slice(L.text[k], L.text[k + 1]);
    }

    return image.png_bytes();
}

}
