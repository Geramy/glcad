#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <utility>
#include <vector>
#include "geometry/snap.hpp"
#include "geometry/clip.hpp"
#include <array>
#include "engine/underlay.hpp"

namespace gplot {
struct RasterImage {
    int width{}, height{}, pages{1};
    double metres_w{}, metres_h{};
    std::vector<std::uint8_t> rgba; // top row first, straight alpha
    double metres_x{}, metres_y{};
    double source_unit{}; // metres per source unit, zero when not supplied by decoder
    std::vector<std::string> warnings;
    bool valid() const { return width>0 && height>0 && rgba.size()==std::size_t(width)*height*4; }
};
// Pixel bounds are in the oriented page's top-left coordinate system.
class PdfRasterSource {
public:
    virtual ~PdfRasterSource() = default;
    virtual int page_count() const = 0;
    virtual std::pair<double,double> page_size(int page,std::string& error) = 0;
    virtual RasterImage tile(int page,double dpi_x,double dpi_y,int x,int y,int width,int height,std::string& error) = 0;
};
std::unique_ptr<PdfRasterSource> open_pdf_raster(const std::string& path,std::string& error);
RasterImage rasterize_pdf(const std::string& bytes, int page, std::string& error, int max_pixels=2048);
// Page numbers are one-based.
RasterImage load_underlay(const std::string& path, int page, std::string& error, int max_pixels=2048);
RasterImage load_underlay(const gdoc::Underlay& underlay, const gcm::WorkingVolume& volume, std::string& error);
// An explicit unit is metres per DGN master unit; zero uses the decoded mapping.
bool resolve_underlay(gdoc::Underlay& underlay, const gcm::WorkingVolume& volume,
    std::string& error, double source_unit=0, std::vector<std::string>* warnings=nullptr);
std::vector<gsnap::Shape> underlay_snap_boundary(const gdoc::Underlay& underlay,
    const RasterImage& image, const gcm::WorkingVolume& volume,
    const std::array<double,6>& transform = {1,0,0,0,1,0},
    const std::vector<gclip::Boundary>& clips = {});
struct PdfImport {
    std::vector<gsnap::Shape> shapes;
    std::string error;
    std::vector<std::string> warnings;
};
PdfImport import_pdf_paths(const std::string& path, int page, const gcm::WorkingVolume& volume);
}
