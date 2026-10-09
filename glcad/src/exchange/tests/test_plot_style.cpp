#include "test.hpp"
#include "exchange/plot_style.hpp"
#include "exchange/pdf.hpp"
#include <filesystem>
#include <fstream>
#include <cmath>

static std::vector<gcm::u8> fixture(const char* name) {
    std::ifstream in(std::filesystem::path(__FILE__).parent_path()/"fixtures"/name,std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)),{}};
}
TEST(plot_style_reads_existing_ctb_and_stb_files) {
    auto ctb=gplot::read_plot_style(fixture("ctbtest.ctb"));
    CHECK_MSG(ctb.ok,"%s",ctb.error.c_str()); CHECK(ctb.table.size()==255);
    const auto red=ctb.table.by_index(1); CHECK(red.screening==95); CHECK(red.grayscale);
    CHECK(!red.use_object_color); CHECK(std::fabs(red.width_mm-0.8)<1e-12);
    CHECK(!ctb.warnings.empty());
    auto stb=gplot::read_plot_style(fixture("stbtest.stb"));
    CHECK_MSG(stb.ok,"%s",stb.error.c_str()); CHECK(stb.table.mode()==gplot::StyleMode::Named);
    const auto* named=stb.table.find("mozman"); CHECK(named!=nullptr);
    if(named) { CHECK(std::fabs(named->width_mm-0.13)<1e-12); CHECK(named->screening==90); }
}
TEST(plot_style_rejects_corrupt_truncated_and_oversized_containers) {
    auto data=fixture("ctbtest.ctb");
    auto corrupt=data; corrupt.back()^=1; CHECK(!gplot::read_plot_style(corrupt).ok);
    data.resize(61); CHECK(!gplot::read_plot_style(data).ok);
    data=fixture("stbtest.stb"); data[55]=127; CHECK(!gplot::read_plot_style(data).ok);
    data=fixture("stbtest.stb"); data[0]='?'; CHECK(!gplot::read_plot_style(data).ok);
}
TEST(plot_style_applies_named_overrides_to_paths_and_notes) {
    auto parsed=gplot::read_plot_style(fixture("stbtest.stb")); CHECK(parsed.ok);
    gcm::WorkingVolume volume; volume.extent=1000; volume.resolution_bits=32;
    gdoc::LayerTable layers; layers.find("0")->plot_style="mozman";
    gplot::Sheet sheet; gplot::PlotJob job; job.sheet=&sheet; job.volume=volume; job.layers=&layers; job.styles=&parsed.table;
    gsnap::Shape shape; shape.kind=gsnap::ShapeKind::Segment; shape.a=gcm::to_lattice(volume,0,0); shape.b=gcm::to_lattice(volume,0.01,0.01);
    job.sheet_shapes={shape}; job.sheet_layers={"0"};
    gplot::PdfWriter pdf; CHECK(gplot::plot(job,pdf).ok); CHECK(pdf.paths_of_page(0).size()==1);
    const auto& path=pdf.paths_of_page(0)[0]; CHECK(std::fabs(path.width_mm-0.13)<1e-12);
    CHECK(path.endcap==gplot::LinecapStyle::Square);
    CHECK(path.color.r>0.99); CHECK(path.color.b>0.99); CHECK(std::fabs(path.color.g-0.1)<1e-6);
}

TEST(plot_style_authoring_roundtrips_real_tables_and_all_pattern_choices) {
    for(const auto* name:{"ctbtest.ctb","stbtest.stb"}) {
        auto original=gplot::read_plot_style(fixture(name)); CHECK(original.ok);
        auto& style=original.table.mode()==gplot::StyleMode::Named?original.table.by_name("Edited"):original.table.by_index(1);
        style.width_mm=.47; style.use_object_lineweight=false; style.pattern_id=3; style.use_object_linetype=false;
        style.fill_style=67; style.endcap=gplot::LinecapStyle::Diamond; style.join=gplot::LinejoinStyle::Diamond;
        std::string error; auto bytes=gplot::write_plot_style(original.table,&error); CHECK_MSG(!bytes.empty(),"%s",error.c_str());
        auto read=gplot::read_plot_style(bytes); CHECK(read.ok);
        const auto* pen=read.table.mode()==gplot::StyleMode::Named?read.table.find("Edited"):static_cast<const gplot::PlotStyleTable&>(read.table).by_index(1);
        CHECK(pen!=nullptr); if(!pen) continue;
        CHECK(std::abs(pen->width_mm-.47)<1e-12); CHECK(pen->pattern_id==3); CHECK(pen->fill_style==67);
        CHECK(pen->endcap==gplot::LinecapStyle::Diamond); CHECK(pen->join==gplot::LinejoinStyle::Diamond);
    }
    for(int i=1;i<=30;++i) { auto d=gplot::plot_style_dashes(i,.5,2); CHECK(!d.empty()); CHECK(d.size()%2==0); }
    for(int fill=65;fill<=72;++fill) {
        gplot::PdfWriter pdf; gplot::PdfPath p; p.points={{.01,.01},{.05,.03}}; p.width_mm=3; p.fill_style=fill;
        pdf.add(p); CHECK(pdf.path_count()>1);
        for(const auto& q:pdf.paths_of_page(0)) CHECK(q.fill_style==73);
    }
}
