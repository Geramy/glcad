#include "exchange/underlay_image.hpp"
#include "exchange/pdf.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

namespace gplot {
RasterImage load_underlay_dgn(const std::string&,int,std::string&,int,double,double);
#ifndef GLCAD_PORTABLE_UNDERLAYS
std::unique_ptr<PdfRasterSource> open_pdf_raster(const std::string&,std::string& error) {
    error="portable PDF rasterization is unavailable in this build";return {};
}
#endif
#ifdef GLCAD_XPS_UNDERLAYS
RasterImage load_underlay_xps(const std::string&, int, std::string&, int);
#endif
#ifdef GLCAD_PORTABLE_UNDERLAYS
RasterImage load_underlay_portable(const std::string&, int, std::string&, int);
#endif
#ifdef __APPLE__
namespace {
struct Url {
    CFURLRef value{};
    explicit Url(const std::string& path) { value=CFURLCreateFromFileSystemRepresentation(nullptr,reinterpret_cast<const UInt8*>(path.data()),path.size(),false); }
    ~Url(){if(value)CFRelease(value);}
};
}
#endif
#ifndef GLCAD_PORTABLE_UNDERLAYS
RasterImage rasterize_pdf(const std::string& bytes,int page,std::string& error,int limit) {
    error.clear();
    if(page<1||limit<1||limit>4096||bytes.empty()||bytes.size()>64*1024*1024) {
        error="invalid PDF data, page or raster size";return {};
    }
#ifdef __APPLE__
    auto data=CFDataCreate(nullptr,reinterpret_cast<const UInt8*>(bytes.data()),bytes.size());
    if(!data){error="cannot allocate PDF source";return {};}
    auto provider=CGDataProviderCreateWithCFData(data);CFRelease(data);
    auto pdf=provider?CGPDFDocumentCreateWithProvider(provider):nullptr;
    if(provider)CGDataProviderRelease(provider);
    if(!pdf){error="cannot decode PDF";return {};}
    RasterImage out;out.pages=int(CGPDFDocumentGetNumberOfPages(pdf));
    auto p=page<=out.pages&&(!CGPDFDocumentIsEncrypted(pdf)||CGPDFDocumentIsUnlocked(pdf))?CGPDFDocumentGetPage(pdf,page):nullptr;
    if(!p){CGPDFDocumentRelease(pdf);error="PDF page unavailable or password required";return {};}
    const auto box=CGPDFPageGetBoxRect(p,kCGPDFCropBox);double w=box.size.width,h=box.size.height;
    if(CGPDFPageGetRotationAngle(p)%180)std::swap(w,h);
    if(!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0){CGPDFDocumentRelease(pdf);error="invalid PDF page dimensions";return {};}
    const double ratio=double(limit)/std::max(w,h);
    out.width=std::clamp(int(std::ceil(w*ratio)),1,limit);out.height=std::clamp(int(std::ceil(h*ratio)),1,limit);
    out.metres_w=w*.0254/72;out.metres_h=h*.0254/72;
    out.rgba.assign(std::size_t(out.width)*out.height*4,255);
    auto colors=CGColorSpaceCreateDeviceRGB();
    auto ctx=CGBitmapContextCreate(out.rgba.data(),out.width,out.height,8,std::size_t(out.width)*4,colors,
        CGBitmapInfo(kCGImageAlphaPremultipliedLast)|CGBitmapInfo(kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(colors);
    if(ctx) {
        CGContextScaleCTM(ctx,double(out.width)/w,double(out.height)/h);
        CGContextConcatCTM(ctx,CGPDFPageGetDrawingTransform(p,kCGPDFCropBox,CGRectMake(0,0,w,h),0,true));
        CGContextDrawPDFPage(ctx,p);CGContextRelease(ctx);
    } else {out={};error="cannot allocate page bitmap";}
    CGPDFDocumentRelease(pdf);return out;
#else
    error="this build has no PDF decoder";return {};
#endif
}
#endif
RasterImage load_underlay(const std::string& path,int page,std::string& error,int max_pixels) {
    RasterImage out;error.clear();
    if(page<1 || max_pixels<1 || max_pixels>4096) {error="invalid page or raster size";return out;}
    if(gdoc::underlay_kind_of(path)==gdoc::UnderlayKind::Dgn)
        return load_underlay_dgn(path,page,error,max_pixels,0,0);
#ifdef GLCAD_XPS_UNDERLAYS
    if(gdoc::underlay_kind_of(path)==gdoc::UnderlayKind::Dwf)
        return load_underlay_xps(path,page,error,max_pixels);
#endif
#ifdef GLCAD_PORTABLE_UNDERLAYS
    return load_underlay_portable(path,page,error,max_pixels);
#elif defined(__APPLE__)
    Url url(path);if(!url.value){error="invalid path";return out;}
    auto ext=std::filesystem::path(path).extension().string();
    for(char& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));
    CGPDFDocumentRef pdf=nullptr;CGPDFPageRef pdfpage=nullptr;CGImageRef image=nullptr;
    double w=0,h=0;CGRect box{};
    if(ext==".pdf") {
        pdf=CGPDFDocumentCreateWithURL(url.value);
        if(!pdf){error="cannot decode PDF";return out;}
        out.pages=int(CGPDFDocumentGetNumberOfPages(pdf));
        if(page>out.pages || CGPDFDocumentIsEncrypted(pdf)&&!CGPDFDocumentIsUnlocked(pdf)) {error="PDF page unavailable or password required";CGPDFDocumentRelease(pdf);return out;}
        pdfpage=CGPDFDocumentGetPage(pdf,page);box=CGPDFPageGetBoxRect(pdfpage,kCGPDFCropBox);
        w=box.size.width;h=box.size.height;
        if(CGPDFPageGetRotationAngle(pdfpage)%180)std::swap(w,h);
        out.metres_w=w*.0254/72;out.metres_h=h*.0254/72;
    } else {
        auto source=CGImageSourceCreateWithURL(url.value,nullptr);
        if(!source){error="cannot decode image (PDF, PNG, JPEG and TIFF supported)";return out;}
        out.pages=int(CGImageSourceGetCount(source));
        int limit=max_pixels;CFNumberRef size=CFNumberCreate(nullptr,kCFNumberIntType,&limit);
        const void* keys[]={kCGImageSourceCreateThumbnailFromImageAlways,kCGImageSourceThumbnailMaxPixelSize,kCGImageSourceCreateThumbnailWithTransform};
        const void* values[]={kCFBooleanTrue,size,kCFBooleanTrue};
        auto options=CFDictionaryCreate(nullptr,keys,values,3,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        if(page<=out.pages)image=CGImageSourceCreateThumbnailAtIndex(source,page-1,options);
        // Scans without a page size are placed at 96 DPI.
        auto props=page<=out.pages?CGImageSourceCopyPropertiesAtIndex(source,page-1,nullptr):nullptr;
        double pw=0,ph=0,dx=96,dy=96;
        if(props) {
            auto number=[&](CFStringRef key,double& value){auto p=CFDictionaryGetValue(props,key);if(p&&CFGetTypeID(p)==CFNumberGetTypeID())CFNumberGetValue(static_cast<CFNumberRef>(p),kCFNumberDoubleType,&value);};
            number(kCGImagePropertyPixelWidth,pw);number(kCGImagePropertyPixelHeight,ph);number(kCGImagePropertyDPIWidth,dx);number(kCGImagePropertyDPIHeight,dy);CFRelease(props);
        }
        CFRelease(options);CFRelease(size);CFRelease(source);
        if(!image){error="image page unavailable or invalid";return out;}
        w=CGImageGetWidth(image);h=CGImageGetHeight(image);
        out.metres_w=(pw>0?pw:w)*.0254/(dx>0?dx:96);out.metres_h=(ph>0?ph:h)*.0254/(dy>0?dy:96);
    }
    if(!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0) {error="invalid page dimensions";if(pdf)CGPDFDocumentRelease(pdf);if(image)CGImageRelease(image);return {};}
    const double ratio=pdf?double(max_pixels)/std::max(w,h):std::min(1.0,double(max_pixels)/std::max(w,h));
    out.width=std::max(1,int(std::ceil(w*ratio)));out.height=std::max(1,int(std::ceil(h*ratio)));
    out.rgba.assign(std::size_t(out.width)*out.height*4,0);
    auto colors=CGColorSpaceCreateDeviceRGB();
    auto ctx=CGBitmapContextCreate(out.rgba.data(),out.width,out.height,8,std::size_t(out.width)*4,colors,CGBitmapInfo(kCGImageAlphaPremultipliedLast)|CGBitmapInfo(kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(colors);
    if(ctx) {
        if(pdfpage){CGContextSetRGBFillColor(ctx,1,1,1,1);CGContextFillRect(ctx,CGRectMake(0,0,out.width,out.height));}
        if(pdfpage) {CGContextScaleCTM(ctx,double(out.width)/w,double(out.height)/h);CGContextConcatCTM(ctx,CGPDFPageGetDrawingTransform(pdfpage,kCGPDFCropBox,CGRectMake(0,0,w,h),0,true));CGContextDrawPDFPage(ctx,pdfpage);}
        else CGContextDrawImage(ctx,CGRectMake(0,0,out.width,out.height),image);
        CGContextRelease(ctx);
        for(std::size_t i=0;i<out.rgba.size();i+=4)if(out.rgba[i+3]>0&&out.rgba[i+3]<255)for(int c=0;c<3;++c)out.rgba[i+c]=std::uint8_t(std::min(255,int(out.rgba[i+c])*255/int(out.rgba[i+3])));
    } else {out={};error="cannot allocate page bitmap";}
    if(pdf)CGPDFDocumentRelease(pdf);if(image)CGImageRelease(image);
    return out;
#else
    error="this build has no PDF/image decoder";return out;
#endif
}

namespace {
int underlay_page(const gdoc::Underlay& u,std::string& error) {
    if(u.sheet.empty()||(u.kind==gdoc::UnderlayKind::Dgn&&u.sheet=="Model"))return 1;
    try {
        std::size_t end=0;const int page=std::stoi(u.sheet,&end);
        if(end==u.sheet.size()&&page>0)return page;
    } catch(...) {}
    error="invalid underlay page or model";return 0;
}
RasterImage source_image(const gdoc::Underlay& u,std::string& error) {
    const int page=underlay_page(u,error);if(!page)return {};
    // Normalize into the source frame; resolve_underlay divides by this factor once.
    const double placement_unit=u.source_coordinates_pending?std::abs(u.scale):0;
    return u.kind==gdoc::UnderlayKind::Dgn?load_underlay_dgn(u.path,page,error,2048,u.source_unit_known?u.source_unit:0,placement_unit):load_underlay(u.path,page,error);
}
}
bool resolve_underlay(gdoc::Underlay& u,const gcm::WorkingVolume& volume,std::string& error,double source_unit,std::vector<std::string>* warnings) {
    error.clear();if(warnings)warnings->clear();auto next=u;
    if(source_unit!=0) {
        if(next.kind!=gdoc::UnderlayKind::Dgn||!std::isfinite(source_unit)||source_unit<=0){error="source units require a positive DGN metres-per-unit value";return false;}
        next.source_unit=source_unit;next.source_unit_known=true;
    } else if(next.source_coordinates_pending)next.source_unit_known=false;
    if(next.source_coordinates_pending&&next.kind!=gdoc::UnderlayKind::Dgn){error="underlay source unit/page mapping is not available";return false;}
    auto image=source_image(next,error);if(!image.valid())return false;
    const double x=image.metres_x,y=image.metres_y,w=image.metres_w,h=image.metres_h;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||
       std::max({std::abs(x),std::abs(y),std::abs(x+w),std::abs(y+h)})>volume.extent){error="underlay source bounds exceed the working volume";return false;}
    if(image.source_unit>0) {
        const double ratio=u.source_coordinates_pending?image.source_unit:
            (u.source_unit_known?image.source_unit/u.source_unit:1);
        const auto convert=[&](auto& points) {
            for(auto& p:points){double px,py;gcm::to_world(volume,p,px,py);px*=ratio;py*=ratio;
                if(!std::isfinite(px)||!std::isfinite(py)||std::max(std::abs(px),std::abs(py))>volume.extent)return false;
                p=gcm::to_lattice(volume,px,py);
            }return true;
        };
        if(!convert(next.clip)||!convert(next.clip_hole)){error="underlay clip exceeds the working volume";return false;}
        if(u.source_coordinates_pending)next.scale/=image.source_unit;
        if(!std::isfinite(next.scale)||next.scale==0){error="invalid resolved underlay scale";return false;}
        next.source_unit=image.source_unit;next.source_unit_known=true;next.source_coordinates_pending=false;
    }
    next.extent_lo=gcm::to_lattice(volume,x,y);next.extent_hi=gcm::to_lattice(volume,x+w,y+h);next.found=true;
    u=std::move(next);if(warnings)*warnings=std::move(image.warnings);return true;
}
RasterImage load_underlay(const gdoc::Underlay& u,const gcm::WorkingVolume& volume,std::string& error) {
    if(u.source_coordinates_pending){error="underlay source unit/page mapping is not available";return {};}
    auto image=source_image(u,error);if(!image.valid())return image;
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){
        auto* px=&image.rgba[(std::size_t(y)*image.width+x)*4];
        if(u.has_clip()&&!u.contains(gcm::to_lattice(volume,image.metres_x+(x+.5)*image.metres_w/image.width,image.metres_y+(image.height-y-.5)*image.metres_h/image.height))){px[3]=0;continue;}
        const double gray=.2126*px[0]+.7152*px[1]+.0722*px[2];
        for(int c=0;c<3;++c)px[c]=std::uint8_t(std::clamp(((u.monochrome?gray:px[c])-127.5)*(std::clamp(u.contrast,0,100)/(u.contrast_neutral>0?u.contrast_neutral:50.0))+127.5,0.0,255.0));
        px[3]=std::uint8_t(px[3]*(100-std::clamp(u.fade,0,100))/100);
    }
    return image;
}

std::vector<gsnap::Shape> underlay_snap_boundary(const gdoc::Underlay& u,
    const RasterImage& image, const gcm::WorkingVolume& volume,
    const std::array<double,6>& transform,const std::vector<gclip::Boundary>& clips) {
    if(!u.visible||u.source_coordinates_pending||u.fade>=100||!image.valid()||!std::isfinite(u.scale)||u.scale==0||!std::isfinite(u.y_factor)||u.y_factor==0||!std::isfinite(u.rotation))return {};
    bool placement_ok=true;
    const auto placed=[&](gcm::Lattice2 p) {
        double x,y;u.to_drawing(p,volume,x,y);
        const double wx=transform[0]*x+transform[1]*y+transform[2],wy=transform[3]*x+transform[4]*y+transform[5];
        if(!std::isfinite(wx)||!std::isfinite(wy)||std::max(std::abs(wx),std::abs(wy))>volume.extent){placement_ok=false;return gcm::Lattice2{};}
        return gcm::to_lattice(volume,wx,wy);
    };
    const double x=image.metres_x,y=image.metres_y,w=image.metres_w,h=image.metres_h;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||
       std::max({std::abs(x),std::abs(y),std::abs(x+w),std::abs(y+h)})>volume.extent)return {};
    std::vector<gcm::Lattice2> corners={placed(gcm::to_lattice(volume,x,y)),placed(gcm::to_lattice(volume,x+w,y)),
        placed(gcm::to_lattice(volume,x+w,y+h)),placed(gcm::to_lattice(volume,x,y+h))};
    if(!placement_ok||!gclip::valid_boundary(corners))return {};
    std::vector<gsnap::Shape> boundary;
    for(gcm::u32 i=0;i<4;++i) {
        gsnap::Shape shape;shape.a=corners[i];shape.b=corners[(i+1)%4];
        shape.poly=1;shape.poly_index=i;shape.region=true;boundary.push_back(shape);
    }
    std::vector<gclip::Boundary> masks;
    if(u.has_clip()) {
        gclip::Boundary mask;mask.inverted=u.clip_inverted;
        for(auto p:u.clip)mask.points.push_back(placed(p));
        if(!placement_ok)return {};
        masks.push_back(std::move(mask));
        if(u.clip_hole.size()>=3) {
            gclip::Boundary hole;hole.inverted=true;
            for(auto p:u.clip_hole)hole.points.push_back(placed(p));
            if(!placement_ok)return {};
            masks.push_back(std::move(hole));
        }
    }
    masks.insert(masks.end(),clips.begin(),clips.end());
    for(const auto& mask:masks) {
        auto result=gclip::shapes(boundary,mask.points,mask.inverted);
        if(!result.ok)return {};
        boundary=std::move(result.shapes);
    }
    for(auto& shape:boundary){shape.region=false;shape.poly=0;shape.poly_index=0;shape.region_loop=0;shape.region_reversed=false;}
    return boundary;
}

#ifdef __APPLE__
namespace {
struct PathReader {
    PdfImport result;const gcm::WorkingVolume& volume;
    CGAffineTransform matrix{CGAffineTransformIdentity};
    std::vector<PaperClip> clips;
    struct State {CGAffineTransform matrix;std::vector<PaperClip> clips;};
    std::vector<State> stack;
    std::vector<std::vector<CGPoint>> paths;
    bool text{},images{},pending_clip{},fills{};std::size_t vertices{};int depth{};
    CGPDFOperatorTableRef table{};
    explicit PathReader(const gcm::WorkingVolume& v):volume(v){}
    CGPoint transform(double x,double y){return CGPointApplyAffineTransform(CGPointMake(x,y),matrix);}
    bool nums(CGPDFScannerRef s,double* n,int count){for(int i=count-1;i>=0;--i){CGPDFReal v;if(!CGPDFScannerPopNumber(s,&v)||!std::isfinite(v))return false;n[i]=v;}return true;}
    void push(CGPoint p){if(paths.empty())return;if(++vertices>200000){result.error="PDF exceeds the 200,000 vertex import limit";return;}paths.back().push_back(p);}
    void curve(CGPoint b,CGPoint c,CGPoint e){if(paths.empty()||paths.back().empty())return;auto a=paths.back().back();for(int i=1;i<=32;++i){double t=double(i)/32,u=1-t;push(CGPointMake(u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*e.x,u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*e.y));}}
    void finish_clip(){
        if(!pending_clip)return;pending_clip=false;
        if(paths.size()!=1||paths.front().size()<3){result.error="compound PDF clipping requires IMAGE mode";return;}
        PaperClip clip;clip.lo={1e300,1e300};clip.hi={-1e300,-1e300};
        for(auto p:paths.front()){clip.polygon.push_back({p.x,p.y});clip.lo.x=std::min(clip.lo.x,p.x);clip.lo.y=std::min(clip.lo.y,p.y);clip.hi.x=std::max(clip.hi.x,p.x);clip.hi.y=std::max(clip.hi.y,p.y);}
        if(clip.polygon.size()>1&&clip.polygon.front().x==clip.polygon.back().x&&clip.polygon.front().y==clip.polygon.back().y)clip.polygon.pop_back();
        if(!clip.valid()){result.error="invalid PDF clip";return;}clips.push_back(std::move(clip));
    }
    void emit(bool close){
        for(auto p:paths){if(close&&p.size()>1)p.push_back(p.front());for(std::size_t i=1;i<p.size();++i){
            std::vector<std::array<PaperPoint,2>> spans{{PaperPoint{p[i-1].x,p[i-1].y},PaperPoint{p[i].x,p[i].y}}};
            for(const auto& clip:clips){std::vector<std::array<PaperPoint,2>> next;for(auto span:spans){auto pieces=clip.segments(span[0],span[1]);next.insert(next.end(),pieces.begin(),pieces.end());}spans=std::move(next);}
            for(auto span:spans){auto a=span[0],b=span[1];
                if(result.shapes.size()>=200000){result.error="PDF exceeds the 200,000 segment import limit";return;}
                if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(b.x)||!std::isfinite(b.y)||std::max({std::abs(a.x),std::abs(a.y),std::abs(b.x),std::abs(b.y)})*.0254/72>volume.extent){result.error="PDF coordinates exceed the working volume";return;}
                gsnap::Shape sh;sh.kind=gsnap::ShapeKind::Segment;
                sh.a=gcm::to_lattice(volume,a.x*.0254/72,a.y*.0254/72);sh.b=gcm::to_lattice(volume,b.x*.0254/72,b.y*.0254/72);
                if(sh.a.x!=sh.b.x||sh.a.y!=sh.b.y)result.shapes.push_back(sh);
            }
        }}finish_clip();paths.clear();
    }
};
}
#endif
PdfImport import_pdf_paths(const std::string& path,int page,const gcm::WorkingVolume& volume) {
#ifdef __APPLE__
    PathReader r(volume);Url url(path);auto pdf=url.value?CGPDFDocumentCreateWithURL(url.value):nullptr;
    if(!pdf){r.result.error="cannot decode PDF";return r.result;}
    auto p=page>0&&page<=CGPDFDocumentGetNumberOfPages(pdf)?CGPDFDocumentGetPage(pdf,page):nullptr;
    if(!p){CGPDFDocumentRelease(pdf);r.result.error="PDF page unavailable";return r.result;}
    auto box=CGPDFPageGetBoxRect(p,kCGPDFCropBox);auto target=CGRectMake(0,0,box.size.width,box.size.height);
    if(CGPDFPageGetRotationAngle(p)%180)std::swap(target.size.width,target.size.height);
    r.matrix=CGPDFPageGetDrawingTransform(p,kCGPDFCropBox,target,0,true);
    auto table=CGPDFOperatorTableCreate();r.table=table;
    CGPDFOperatorTableSetCallback(table,"q",[](CGPDFScannerRef,void* d){auto& r=*static_cast<PathReader*>(d);r.stack.push_back({r.matrix,r.clips});});
    CGPDFOperatorTableSetCallback(table,"Q",[](CGPDFScannerRef,void* d){auto& r=*static_cast<PathReader*>(d);if(!r.stack.empty()){r.matrix=r.stack.back().matrix;r.clips=std::move(r.stack.back().clips);r.stack.pop_back();}});
    CGPDFOperatorTableSetCallback(table,"cm",[](CGPDFScannerRef s,void* d){auto& r=*static_cast<PathReader*>(d);double n[6];if(r.nums(s,n,6))r.matrix=CGAffineTransformConcat(CGAffineTransformMake(n[0],n[1],n[2],n[3],n[4],n[5]),r.matrix);});
    CGPDFOperatorTableSetCallback(table,"m",[](CGPDFScannerRef s,void* d){auto& r=*static_cast<PathReader*>(d);double n[2];if(r.nums(s,n,2))r.paths.push_back({r.transform(n[0],n[1])});});
    CGPDFOperatorTableSetCallback(table,"l",[](CGPDFScannerRef s,void* d){auto& r=*static_cast<PathReader*>(d);double n[2];if(r.nums(s,n,2)&&!r.paths.empty())r.push(r.transform(n[0],n[1]));});
    CGPDFOperatorTableSetCallback(table,"re",[](CGPDFScannerRef s,void* d){auto& r=*static_cast<PathReader*>(d);double n[4];if(r.nums(s,n,4))r.paths.push_back({r.transform(n[0],n[1]),r.transform(n[0]+n[2],n[1]),r.transform(n[0]+n[2],n[1]+n[3]),r.transform(n[0],n[1]+n[3]),r.transform(n[0],n[1])});});
    CGPDFOperatorTableSetCallback(table,"c",[](CGPDFScannerRef s,void* d){auto& r=*static_cast<PathReader*>(d);double n[6];if(r.nums(s,n,6))r.curve(r.transform(n[0],n[1]),r.transform(n[2],n[3]),r.transform(n[4],n[5]));});
    CGPDFOperatorTableSetCallback(table,"v",[](CGPDFScannerRef s,void* d){auto& r=*static_cast<PathReader*>(d);double n[4];if(r.nums(s,n,4)&&!r.paths.empty()&&!r.paths.back().empty())r.curve(r.paths.back().back(),r.transform(n[0],n[1]),r.transform(n[2],n[3]));});
    CGPDFOperatorTableSetCallback(table,"y",[](CGPDFScannerRef s,void* d){auto& r=*static_cast<PathReader*>(d);double n[4];if(r.nums(s,n,4))r.curve(r.transform(n[0],n[1]),r.transform(n[2],n[3]),r.transform(n[2],n[3]));});
    CGPDFOperatorTableSetCallback(table,"h",[](CGPDFScannerRef,void* d){auto& r=*static_cast<PathReader*>(d);if(!r.paths.empty()&&!r.paths.back().empty())r.paths.back().push_back(r.paths.back().front());});
    for(const char* op:{"S","B","B*"})CGPDFOperatorTableSetCallback(table,op,[](CGPDFScannerRef,void* d){static_cast<PathReader*>(d)->emit(false);});
    for(const char* op:{"s","b","b*"})CGPDFOperatorTableSetCallback(table,op,[](CGPDFScannerRef,void* d){static_cast<PathReader*>(d)->emit(true);});
    for(const char* op:{"f","F","f*"})CGPDFOperatorTableSetCallback(table,op,[](CGPDFScannerRef,void* d){auto& r=*static_cast<PathReader*>(d);r.fills=true;r.emit(true);});
    CGPDFOperatorTableSetCallback(table,"n",[](CGPDFScannerRef,void* d){auto& r=*static_cast<PathReader*>(d);r.finish_clip();r.paths.clear();});
    CGPDFOperatorTableSetCallback(table,"BT",[](CGPDFScannerRef,void* d){static_cast<PathReader*>(d)->text=true;});
    CGPDFOperatorTableSetCallback(table,"Do",[](CGPDFScannerRef scanner,void* data){
        auto& r=*static_cast<PathReader*>(data);const char* name=nullptr;if(!CGPDFScannerPopName(scanner,&name))return;
        auto parent=CGPDFScannerGetContentStream(scanner);auto obj=CGPDFContentStreamGetResource(parent,"XObject",name);CGPDFStreamRef stream=nullptr;
        if(!obj||!CGPDFObjectGetValue(obj,kCGPDFObjectTypeStream,&stream)){r.result.error="invalid PDF form reference";return;}
        auto dict=CGPDFStreamGetDictionary(stream);const char* kind=nullptr;CGPDFDictionaryGetName(dict,"Subtype",&kind);
        if(!kind||std::strcmp(kind,"Form")){r.images=true;return;}
        if(++r.depth>16){r.result.error="PDF form nesting exceeds 16 levels";--r.depth;return;}
        const auto saved_matrix=r.matrix;const auto saved_clips=r.clips;const auto saved_stack=r.stack;auto saved_paths=std::move(r.paths);const bool saved_pending=r.pending_clip;r.pending_clip=false;
        CGPDFArrayRef matrix=nullptr;if(CGPDFDictionaryGetArray(dict,"Matrix",&matrix)&&CGPDFArrayGetCount(matrix)==6){double n[6]{};bool ok=true;for(int i=0;i<6;++i){CGPDFReal x;if(!CGPDFArrayGetNumber(matrix,i,&x)||!std::isfinite(x)){ok=false;break;}n[i]=x;}if(ok)r.matrix=CGAffineTransformConcat(CGAffineTransformMake(n[0],n[1],n[2],n[3],n[4],n[5]),r.matrix);}
        CGPDFArrayRef box=nullptr;if(CGPDFDictionaryGetArray(dict,"BBox",&box)&&CGPDFArrayGetCount(box)==4){CGPDFReal n[4]{};bool ok=true;for(int i=0;i<4;++i)ok=CGPDFArrayGetNumber(box,i,&n[i])&&ok;if(ok){r.paths={{r.transform(n[0],n[1]),r.transform(n[2],n[1]),r.transform(n[2],n[3]),r.transform(n[0],n[3])}};r.pending_clip=true;r.finish_clip();r.paths.clear();}}
        CGPDFDictionaryRef resources=nullptr;CGPDFDictionaryGetDictionary(dict,"Resources",&resources);
        auto contents=CGPDFContentStreamCreateWithStream(stream,resources,parent);auto nested=CGPDFScannerCreate(contents,r.table,&r);
        if(!CGPDFScannerScan(nested))r.result.error="invalid PDF form content";
        CGPDFScannerRelease(nested);CGPDFContentStreamRelease(contents);
        r.matrix=saved_matrix;r.clips=saved_clips;r.stack=saved_stack;r.paths=std::move(saved_paths);r.pending_clip=saved_pending;--r.depth;
    });
    for(const char* op:{"W","W*"})CGPDFOperatorTableSetCallback(table,op,[](CGPDFScannerRef,void* d){static_cast<PathReader*>(d)->pending_clip=true;});
    auto stream=CGPDFContentStreamCreateWithPage(p);auto scanner=CGPDFScannerCreate(stream,table,&r);
    if(!CGPDFScannerScan(scanner))r.result.error="invalid PDF content stream";
    CGPDFScannerRelease(scanner);CGPDFContentStreamRelease(stream);CGPDFOperatorTableRelease(table);CGPDFDocumentRelease(pdf);
    if(r.text)r.result.warnings.push_back("PDF text is retained only in IMAGE mode; it is not converted into text entities");
    if(r.images)r.result.warnings.push_back("PDF raster images require IMAGE mode");
    if(r.fills)r.result.warnings.push_back("PDF filled regions were imported as editable boundaries");
    if(!r.result.error.empty())r.result.shapes.clear();return r.result;
#else
    PdfImport r;r.error="this build has no PDF path decoder";return r;
#endif
}
}
