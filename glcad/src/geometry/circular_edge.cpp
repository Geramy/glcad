#include "geometry/solid_edit.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "geometry/boolean.hpp"

namespace gtopo {
namespace {
using V = ggeo::d3::Vec<gcm::F64>;
V point(const Body& b, VertexId id, const Volume& vol) {
    double x,y,z; gcm::to_world(vol,b.vertex_point[id],x,y,z);
    return V::from(x,y,z);
}
double dot(V a,V b) { return (a.x*b.x+a.y*b.y+a.z*b.z).to_f64(); }
V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
V scale(V a,double t) { return a*gcm::F64::from(t); }
double length(V a) { return std::sqrt(dot(a,a)); }
struct Rim {
    FaceId face{kInvalid};
    u32 loop{kInvalid};
    std::vector<HalfedgeId> edges;
    std::vector<VertexId> vertices;
    V centre,normal;
    double radius{},radial_sign{},axial_sign{},depth{};
    std::vector<FaceId> walls;
    std::vector<VertexId> far_ring;
};

bool subdivided_rim(const Body& b,HalfedgeId edge,const Volume& vol,Rim& rim) {
    for (const auto h:{edge,b.he_twin[edge]}) {
        const auto li=b.he_loop[h],f=b.loop_face[li];
        const auto begin=b.loop_begin(li),end=b.loop_end(li);
        if (end-begin<12) continue;
        const auto plane=face_plane(b,f,vol);
        const V n=V::from(plane.nx,plane.ny,plane.nz);
        const double resolution=vol.inv_scale();
        std::vector<VertexId> vertices,corners;
        std::vector<HalfedgeId> edges;
        for (auto e=begin;e<end;++e) {
            const auto id=b.he_vertex[e];vertices.push_back(id);edges.push_back(e);
            const auto prev=e==begin?end-1:e-1;
            const V u=point(b,id,vol)-point(b,b.he_vertex[prev],vol);
            const V v=point(b,b.he_vertex[b.he_next[e]],vol)-point(b,id,vol);
            const double a=length(u),c=length(v);
            if (a>resolution && c>resolution && length(cross(u,v))/(a*c)>std::max(1e-5,4*resolution*(1/a+1/c))) corners.push_back(id);
        }
        if (corners.size()<12) continue;
        const V a=point(b,corners[0],vol),u=point(b,corners[corners.size()/3],vol)-a,
                v=point(b,corners[2*corners.size()/3],vol)-a,uv=cross(u,v);
        const double den=2*dot(uv,uv);if(den<1e-24)continue;
        const V centre=a+scale(cross(v,uv),dot(u,u)/den)+scale(cross(uv,u),dot(v,v)/den);
        const double radius=length(a-centre),tol=std::max(resolution*4,radius*1e-7);
        bool valid=radius>tol;double chord_error=0;
        for (std::size_t i=0;i<corners.size();++i) {
            const V p=point(b,corners[i],vol),q=point(b,corners[(i+1)%corners.size()],vol);
            if(std::fabs(length(p-centre)-radius)>tol || std::fabs(dot(p-centre,n))>tol)valid=false;
            chord_error=std::max(chord_error,radius-length(scale(p+q,.5)-centre));
        }
        if (!valid)continue;
        const double band=chord_error+tol;
        for (auto id:vertices) {
            const V d=point(b,id,vol)-centre;
            if(std::fabs(dot(d,n))>tol || std::fabs(length(d)-radius)>band)valid=false;
        }
        if(!valid)continue;
        std::vector<bool> wall(b.face_count(),false),seen(b.face_count(),false);
        std::vector<FaceId> pending,walls;
        for(auto e:edges)if(b.he_twin[e]!=kInvalid)pending.push_back(b.loop_face[b.he_loop[b.he_twin[e]]]);
        while(!pending.empty()) {
            const auto face=pending.back();pending.pop_back();if(seen[face])continue;seen[face]=true;
            bool on=true;double low=INFINITY,high=-INFINITY;
            for(auto l=b.face_loop_begin(face);l<b.face_loop_end(face);++l)
                for(auto e=b.loop_begin(l);e<b.loop_end(l);++e) {
                    V d=point(b,b.he_vertex[e],vol)-centre;
                    const double height=dot(d,n);low=std::min(low,height);high=std::max(high,height);
                    d=d-scale(n,height);
                    if(std::fabs(length(d)-radius)>band)on=false;
                }
            // Lattice rounding tilts thin fragments: test the envelope, not normals.
            if(!on || high-low<=tol)continue;
            wall[face]=true;walls.push_back(face);
            for(auto l=b.face_loop_begin(face);l<b.face_loop_end(face);++l)
                for(auto e=b.loop_begin(l);e<b.loop_end(l);++e)
                    if(b.he_twin[e]!=kInvalid)pending.push_back(b.loop_face[b.he_loop[b.he_twin[e]]]);
        }
        std::unordered_map<VertexId,VertexId> far_next;
        std::size_t near_count=0;
        for(auto face:walls) for(auto l=b.face_loop_begin(face);l<b.face_loop_end(face);++l)
            for(auto e=b.loop_begin(l);e<b.loop_end(l);++e) {
                const auto twin=b.he_twin[e];
                if(twin==kInvalid){valid=false;continue;}
                if(wall[b.loop_face[b.he_loop[twin]]])continue;
                if(b.he_loop[twin]==li){++near_count;continue;}
                if(!far_next.emplace(b.he_vertex[e],b.he_vertex[b.he_next[e]]).second)valid=false;
            }
        if(!valid || near_count!=vertices.size() || far_next.size()<12)continue;
        std::vector<VertexId> far;
        auto id=far_next.begin()->first;const auto first=id;
        do {
            far.push_back(id);const auto it=far_next.find(id);if(it==far_next.end()){valid=false;break;}
            id=it->second;
        }while(id!=first && far.size()<=far_next.size());
        if(!valid || far.size()!=far_next.size())continue;
        const double depth=dot(point(b,far[0],vol)-centre,n);
        if(std::fabs(depth)<=tol)continue;
        for(auto vtx:far)if(std::fabs(dot(point(b,vtx,vol)-centre,n)-depth)>tol)valid=false;
        for(auto face:walls)for(auto l=b.face_loop_begin(face);l<b.face_loop_end(face);++l)
            for(auto e=b.loop_begin(l);e<b.loop_end(l);++e) {
                const double t=dot(point(b,b.he_vertex[e],vol)-centre,n)/depth;
                if(t<-tol/std::fabs(depth) || t>1+tol/std::fabs(depth))valid=false;
            }
        if(!valid)continue;
        rim={f,li,std::move(edges),std::move(vertices),centre,n,radius,
             li==b.face_loop_begin(f)?-1.0:1.0,depth>0?1.0:-1.0,std::fabs(depth),std::move(walls),std::move(far)};
        return true;
    }
    return false;
}

bool find_rim(const Body& b,HalfedgeId edge,const Volume& vol,Rim& rim) {
    if(edge>=b.halfedge_count() || b.he_twin[edge]==kInvalid) return false;
    for(const auto h:{edge,b.he_twin[edge]}) {
        const u32 li=b.he_loop[h],f=b.loop_face[li];
        const u32 begin=b.loop_begin(li),end=b.loop_end(li),count=end-begin;
        if(count<12) continue;
        const auto plane=face_plane(b,f,vol);
        if(!plane.valid()) continue;
        const V n=V::from(plane.nx,plane.ny,plane.nz);
        const V a=point(b,b.he_vertex[begin],vol);
        const V u=point(b,b.he_vertex[begin+count/3],vol)-a;
        const V v=point(b,b.he_vertex[begin+2*count/3],vol)-a;
        const V uv=cross(u,v); const double den=2*dot(uv,uv);
        if(den<1e-24) continue;
        const V centre=a+scale(cross(v,uv),dot(u,u)/den)+scale(cross(uv,u),dot(v,v)/den);
        const double radius=length(a-centre),tol=std::max(vol.inv_scale()*4,radius*1e-7);
        bool valid=radius>tol;
        std::vector<VertexId> vertices;
        std::vector<HalfedgeId> edges;
        for(u32 e=begin;e<end && valid;++e) {
            const V p=point(b,b.he_vertex[e],vol);
            valid=std::fabs(length(p-centre)-radius)<=tol && std::fabs(dot(p-centre,n))<=tol;
            vertices.push_back(b.he_vertex[e]);edges.push_back(e);
        }
        if(!valid) continue;
        double axial=0,depth=INFINITY;
        for(u32 e:edges) {
            const u32 twin=b.he_twin[e];
            if(twin==kInvalid) {valid=false;break;}
            const u32 wall=b.loop_face[b.he_loop[twin]];
            const auto wall_plane=face_plane(b,wall,vol);
            if(std::fabs(wall_plane.nx*n.x.to_f64()+wall_plane.ny*n.y.to_f64()+wall_plane.nz*n.z.to_f64())>1e-5) {valid=false;break;}
            for(VertexId id:{b.he_vertex[e],b.he_vertex[b.he_next[e]]}) {
                bool found=false; const V p=point(b,id,vol);
                for(u32 l=b.face_loop_begin(wall);l<b.face_loop_end(wall);++l)
                    for(u32 k=b.loop_begin(l);k<b.loop_end(l);++k) {
                        const auto qid=b.he_vertex[k];
                        if(std::find(vertices.begin(),vertices.end(),qid)!=vertices.end()) continue;
                        const V delta=point(b,qid,vol)-p;const double d=dot(delta,n);
                        if(std::fabs(d)<=tol || length(delta-scale(n,d))>tol) continue;
                        if(axial!=0 && axial*d<0) {valid=false;break;}
                        axial=d;depth=std::min(depth,std::fabs(d));found=true;
                    }
                if(!found) valid=false;
            }
        }
        if(!valid || axial==0) continue;
        rim={f,li,std::move(edges),std::move(vertices),centre,n,radius,
             li==b.face_loop_begin(f)?-1.0:1.0,axial>0?1.0:-1.0,depth};
        return true;
    }
    return subdivided_rim(b,edge,vol,rim);
}
}

std::vector<FaceId> circular_wall_faces(const Body& b,HalfedgeId edge,const Volume& vol) {
    Rim rim;
    if (!find_rim(b,edge,vol,rim)) return {};
    if (rim.walls.empty()) for (const auto h:rim.edges)
        rim.walls.push_back(b.loop_face[b.he_loop[b.he_twin[h]]]);
    std::sort(rim.walls.begin(),rim.walls.end());
    rim.walls.erase(std::unique(rim.walls.begin(),rim.walls.end()),rim.walls.end());
    return rim.walls;
}

std::vector<HalfedgeId> circular_edge_ring(const Body& b,HalfedgeId edge,const Volume& vol) {
    Rim rim;
    return find_rim(b,edge,vol,rim)?rim.edges:std::vector<HalfedgeId>{};
}

EditResult blend_circular_edge(const Body& b,HalfedgeId edge,double radial,
                               double axial,int segments,bool round,const Volume& vol) {
    EditResult result;
    Rim rim;
    if(!find_rim(b,edge,vol,rim)) {result.reason="That edge is not a circular rim";return result;}
    if(!std::isfinite(radial) || !std::isfinite(axial) || radial<=vol.inv_scale()*4 || axial<=vol.inv_scale()*4) {
        result.reason="Enter a positive size larger than the modelling resolution";return result;
    }
    if(axial>=rim.depth-vol.inv_scale()*4 || (rim.radial_sign<0 && radial>=rim.radius-vol.inv_scale()*4)) {
        result.reason="The round or bevel is larger than the adjoining face";return result;
    }
    {
        for(u32 l=b.face_loop_begin(rim.face);l<b.face_loop_end(rim.face);++l) {
            if(l==rim.loop) continue;
            for(u32 h=b.loop_begin(l);h<b.loop_end(l);++h) {
                const V a=point(b,b.he_vertex[h],vol)-rim.centre;
                const V d=point(b,b.he_vertex[b.he_next[h]],vol)-rim.centre-a;
                const double t=std::clamp(-dot(a,d)/std::max(dot(d,d),1e-30),0.0,1.0);
                const bool reaches=rim.radial_sign>0
                    ? length(a+scale(d,t))<=rim.radius+radial+vol.inv_scale()*4
                    : std::max(length(a),length(a+d))>=rim.radius-radial-vol.inv_scale()*4;
                if(reaches) {
                    result.reason="The round or bevel reaches another boundary";return result;
                }
            }
        }
    }
    segments=round?std::clamp(segments,2,64):1;
    const std::size_t count=rim.vertices.size();
    Builder builder;
    std::vector<gcm::Lattice3> points=b.vertex_point;
    std::vector<std::vector<VertexId>> rings(segments+1);
    for(int k=0;k<=segments;++k) {
        const double t=double(k)/segments,angle=t*1.5707963267948966;
        const double r=radial*(round?1-std::sin(angle):1-t);
        const double h=axial*(round?1-std::cos(angle):t);
        for(VertexId id:rim.vertices) {
            const V old=point(b,id,vol),direction=scale(old-rim.centre,1/length(old-rim.centre));
            const V base=rim.walls.empty()?old:rim.centre+scale(direction,rim.radius);
            const V p=base+scale(direction,rim.radial_sign*r)+scale(rim.normal,rim.axial_sign*h);
            rings[k].push_back(VertexId(points.size()));
            points.push_back(gcm::to_lattice(vol,p.x.to_f64(),p.y.to_f64(),p.z.to_f64()));
        }
    }
    std::vector<std::vector<std::vector<VertexId>>> faces;
    std::vector<Surface> surfaces;
    std::unordered_map<VertexId,std::size_t> index;
    std::vector<bool> replaced_wall(b.face_count(),false);
    for(auto f:rim.walls)replaced_wall[f]=true;
    for(std::size_t i=0;i<count;++i) index[rim.vertices[i]]=i;
    for(u32 f=0;f<b.face_count();++f) {
        if(replaced_wall[f])continue;
        std::vector<std::vector<VertexId>> loops;
        for(u32 l=b.face_loop_begin(f);l<b.face_loop_end(f);++l) {
            std::vector<VertexId> loop;
            for(u32 h=b.loop_begin(l);h<b.loop_end(l);++h) {
                const auto id=b.he_vertex[h]; const auto it=index.find(id);
                loop.push_back(it==index.end()?id:rings[f==rim.face?0:segments][it->second]);
            }
            loops.push_back(std::move(loop));
        }
        faces.push_back(std::move(loops));
        surfaces.push_back(b.has_surfaces()?b.face_surface[f]:Surface{});
    }
    const V centre=rim.centre+scale(rim.normal,rim.axial_sign*axial);
    const Surface torus=Surface::torus(centre.x.to_f64(),centre.y.to_f64(),centre.z.to_f64(),
        rim.normal.x.to_f64(),rim.normal.y.to_f64(),rim.normal.z.to_f64(),rim.radius+rim.radial_sign*radial,radial);
    const double apex_offset=rim.axial_sign*axial*(rim.radius+rim.radial_sign*radial)/(rim.radial_sign*radial);
    const V apex=rim.centre+scale(rim.normal,apex_offset);
    const V axis=scale(rim.normal,apex_offset>0?-1.0:1.0);
    const Surface cone=Surface::cone(apex.x.to_f64(),apex.y.to_f64(),apex.z.to_f64(),
        axis.x.to_f64(),axis.y.to_f64(),axis.z.to_f64(),std::atan2(radial,axial));
    for(int k=0;k<segments;++k) for(std::size_t i=0;i<count;++i) {
        const auto j=(i+1)%count;
        faces.push_back({{rings[k][j],rings[k][i],rings[k+1][i]}});
        faces.push_back({{rings[k][j],rings[k+1][i],rings[k+1][j]}});
        surfaces.push_back(round?torus:cone);surfaces.push_back(round?torus:cone);
    }
    if(!rim.walls.empty()) {
        const V x=point(b,rim.vertices[0],vol)-rim.centre;
        const V y=cross(rim.normal,x);
        const double sign=rim.radial_sign<0?1.0:-1.0;
        const auto angle=[&](VertexId id) {
            const V p=point(b,id,vol)-rim.centre;
            return sign*std::atan2(dot(p,y),dot(p,x));
        };
        auto far=rim.far_ring;
        auto first=std::min_element(far.begin(),far.end(),[&](auto a,auto b) {return std::fabs(angle(a))<std::fabs(angle(b));});
        std::rotate(far.begin(),first,far.end());
        const auto parameters=[&](const std::vector<VertexId>& loop) {
            std::vector<double> values{angle(loop[0])};
            for(std::size_t i=1;i<=loop.size();++i) {
                double step=angle(loop[i%loop.size()])-angle(loop[i-1]);
                while(step<=0)step+=6.283185307179586;
                values.push_back(values.back()+step);
            }
            return values;
        };
        const auto a=parameters(rim.vertices),bparam=parameters(far);
        if(std::fabs(a.back()-a.front()-6.283185307179586)>1e-5 ||
           std::fabs(bparam.back()-bparam.front()-6.283185307179586)>1e-5) {
            result.reason="The circular wall has an intersected boundary";return result;
        }
        const auto cylinder=Surface::cylinder(rim.centre.x.to_f64(),rim.centre.y.to_f64(),rim.centre.z.to_f64(),
            rim.normal.x.to_f64(),rim.normal.y.to_f64(),rim.normal.z.to_f64(),rim.radius);
        std::size_t i=0,j=0;
        while(i<count || j<far.size()) {
            if(i<count && (j==far.size() || a[i+1]<=bparam[j+1])) {
                faces.push_back({{rings.back()[(i+1)%count],rings.back()[i%count],far[j%far.size()]}});++i;
            } else {
                faces.push_back({{rings.back()[i%count],far[j%far.size()],far[(j+1)%far.size()]}});++j;
            }
            surfaces.push_back(cylinder);
        }
    }
    std::vector<VertexId> remap(points.size(),kInvalid);
    for(const auto& face:faces) for(const auto& loop:face) for(auto id:loop)
        if(remap[id]==kInvalid) remap[id]=builder.add_vertex(points[id]);
    for(const auto& face:faces) {
        builder.begin_face();
        for(const auto& loop:face) {std::vector<VertexId> mapped;for(auto id:loop)mapped.push_back(remap[id]);builder.add_loop(mapped);}
        builder.end_face();
    }
    auto built=builder.build();
    const auto topology=built.analyse();
    if(!topology.closed || !topology.manifold || !all_faces_planar(built)) {
        result.reason="Could not form a closed round or bevel at this size";return result;
    }
    result.volume_before=volume(b,vol);result.volume_after=volume(built,vol);
    if(result.volume_after<=0 || (result.volume_after-result.volume_before)*rim.axial_sign<=0) {
        result.reason="The round or bevel does not fit this edge";return result;
    }
    for(u32 f=0;f<built.face_count();++f) if(surfaces[f].kind==SurfaceKind::Plane) {
        const auto plane=face_plane(built,f,vol);
        const V origin=point(built,built.he_vertex[built.loop_begin(built.face_loop_begin(f))],vol);
        surfaces[f]=Surface::plane(origin.x.to_f64(),origin.y.to_f64(),origin.z.to_f64(),plane.nx,plane.ny,plane.nz);
    }
    built.face_surface=std::move(surfaces);
    result.body=std::move(built);result.ok=true;return result;
}
}
