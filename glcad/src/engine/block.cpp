#include "engine/block.hpp"
#include "engine/layer.hpp"
#include "geometry/region.hpp"
#include "geometry/clip.hpp"

#include <algorithm>
#include <cmath>

namespace gdoc {

const AttributeDef* BlockDef::attribute(std::string_view tag) const {
    for (const auto& a : attributes)
        if (a.tag == tag) return &a;
    return nullptr;
}

std::optional<std::string> BlockRef::attribute(std::string_view tag) const {
    for (const auto& a : attributes)
        if (a.tag == tag) return a.value;
    return std::nullopt;
}

void BlockRef::set_attribute(std::string_view tag, std::string_view value) {
    for (auto& a : attributes)
        if (a.tag == tag) { a.value = std::string(value); return; }
    attributes.push_back({std::string(tag), std::string(value)});
}

BlockDef& BlockTable::define(BlockDef d) {
    ++revision_;
    for (auto& e : defs_)
        if (e.name == d.name) { e = std::move(d); return e; }
    defs_.push_back(std::move(d));
    return defs_.back();
}

const BlockDef* BlockTable::find(std::string_view name) const {
    for (const auto& d : defs_)
        if (d.name == name) return &d;
    return nullptr;
}

bool BlockTable::remove(std::string_view name) {
    ++revision_;
    if (!references_to(name).empty()) return false;
    for (const auto& d : defs_)
        for (const auto& n : nested_insertions(d))
            if (n.name == name) return false;

    const auto it = std::find_if(defs_.begin(), defs_.end(),
                                 [&](const BlockDef& d) { return d.name == name; });
    if (it == defs_.end()) return false;
    defs_.erase(it);
    return true;
}

bool BlockTable::rename(std::string_view from, std::string_view to) {
    ++revision_;
    if (find(to)) return false;
    BlockDef* d = nullptr;
    for (auto& e : defs_)
        if (e.name == from) d = &e;
    if (!d) return false;

    const std::string old_name(from),new_name(to);
    from=old_name;to=new_name;
    d->name = new_name;
    for(auto& x:xrefs_)if(x.name==from)x.name=new_name;
    for(auto& dynamic:dynamics_)if(dynamic.name==from)dynamic.name=new_name;
    for(auto& def:defs_)if(def.source_owner==from)def.source_owner=new_name;
    for (auto& r : refs_)
        if (r.name == from) r.name = std::string(to);
    for (auto& e : defs_) {
        for (auto& n : e.nested)if (n == from) n = std::string(to);
        for (auto& n : e.nested_refs)if(n.name==from)n.name=std::string(to);
    }
    return true;
}

bool BlockTable::would_recurse(std::string_view name,
                               const std::vector<std::string>& nested) const {
    std::vector<std::string> stack(nested.begin(), nested.end());
    std::vector<std::string> seen;

    while (!stack.empty()) {
        const std::string cur = stack.back();
        stack.pop_back();
        if (cur == name) return true;
        if (std::find(seen.begin(), seen.end(), cur) != seen.end()) continue;
        seen.push_back(cur);
        if (const BlockDef* d = find(cur))
            for (const auto& n : nested_insertions(*d)) stack.push_back(n.name);
    }
    return false;
}

std::vector<BlockRef> BlockTable::nested_insertions(const BlockDef& def) const {
    auto out=def.nested_refs;
    for(const auto& name:def.nested) {
        BlockRef ref;ref.name=name;ref.insertion=def.base;
        ref.attrs.color=Color::by_block();ref.attrs.linetype="ByBlock";
        ref.attrs.lineweight=Lineweight::ByBlock;
        out.push_back(std::move(ref));
    }
    return out;
}

gcm::u32 BlockTable::insert(BlockRef r) {
    ++revision_;
    populate_attributes(r);
    refs_.push_back(std::move(r));
    return static_cast<gcm::u32>(refs_.size() - 1);
}

std::vector<gcm::u32> BlockTable::references_to(std::string_view name) const {
    std::vector<gcm::u32> out;
    for (gcm::u32 i = 0; i < refs_.size(); ++i)
        if (refs_[i].name == name) out.push_back(i);
    return out;
}

BlockRef* BlockTable::reference(gcm::u32 i) {
    ++revision_;
    return i < refs_.size() ? &refs_[i] : nullptr;
}

void BlockTable::populate_attributes(BlockRef& r) const {
    const BlockDef* d = find(r.name);
    if (!d) return;
    for (const auto& a : d->attributes) {
        if (r.attribute(a.tag).has_value()) continue;
        r.attributes.push_back({a.tag, a.default_value});
    }
}

gsnap::Shape place_in_block(const gsnap::Shape& s, const BlockRef& r, Point2 base,
                            const gcm::WorkingVolume& v) {
    const f64 c = std::cos(r.rotation), sn = std::sin(r.rotation);

    auto xf = [&](Point2 p) {
        f64 px, py, bx, by, ix, iy;
        gcm::to_world(v, p, px, py);
        gcm::to_world(v, base, bx, by);
        gcm::to_world(v, r.insertion, ix, iy);

        const f64 lx = (px - bx) * r.scale_x;
        const f64 ly = (py - by) * r.scale_y;
        return gcm::to_lattice(v, ix + lx * c - ly * sn, iy + lx * sn + ly * c);
    };

    gsnap::Shape o = s;
    o.a = xf(s.a);
    if (s.is_linear()) {
        o.b = xf(s.b);
    } else if(s.is_elliptical() || (s.is_curve()&&std::abs(std::abs(r.scale_x)-std::abs(r.scale_y))>1e-12)) {
        // Transform both axes to preserve shear across nested insertions.
        double ux=double(s.radius),uy=0,vx=0,vy=double(s.radius);
        if(s.is_elliptical()) {
            ux=double(s.b.x)-s.a.x;uy=double(s.b.y)-s.a.y;
            const double length=std::hypot(ux,uy);if(length==0)return o;
            vx=-uy*s.radius/length;vy=ux*s.radius/length;
        }
        const auto vector=[&](double& x,double& y){const auto a=x*r.scale_x,b=y*r.scale_y;x=c*a-sn*b;y=sn*a+c*b;};
        vector(ux,uy);vector(vx,vy);
        const double xx=ux*ux+vx*vx,xy=ux*uy+vx*vy,yy=uy*uy+vy*vy;
        const double direction=.5*std::atan2(2*xy,xx-yy),ex=std::cos(direction),ey=std::sin(direction);
        const double major=std::sqrt(std::max(0.,.5*(xx+yy+std::hypot(xx-yy,2*xy))));
        const double determinant=ux*vy-uy*vx,minor=major>0?std::abs(determinant)/major:0;
        if(major==0||minor==0)return o;
        o.kind=gsnap::ShapeKind::Ellipse;o.b={o.a.x+i64(std::llround(major*ex)),o.a.y+i64(std::llround(major*ey))};o.radius=i64(std::llround(minor));
        const auto parameter=[&](double t){const double x=ux*std::cos(t)+vx*std::sin(t),y=uy*std::cos(t)+vy*std::sin(t);return std::atan2((-ey*x+ex*y)/minor,(ex*x+ey*y)/major);};
        if(s.kind==gsnap::ShapeKind::Circle || (s.is_elliptical()&&s.ellipse_sweep()>=6.283185307179585)) {o.start_angle=0;o.end_angle=6.28318530717958647692;}
        else {o.start_angle=parameter(s.start_angle);o.end_angle=parameter(s.end_angle);if(determinant<0)std::swap(o.start_angle,o.end_angle);}
        if(determinant<0&&o.region)o.region_reversed=!o.region_reversed;
    } else if (s.is_curve()) {
        const f64 k = (std::fabs(r.scale_x) + std::fabs(r.scale_y)) * 0.5;
        o.radius = static_cast<gcm::i64>(std::llround(f64(s.radius) * k));
        // Mirroring reflects arc angles and reverses sweep; never negate the radius.
        f64 a0 = s.start_angle, a1 = s.end_angle;
        const bool mx = r.scale_x < 0.0, my = r.scale_y < 0.0;
        if (mx != my) {
            if (o.region && o.kind==gsnap::ShapeKind::Arc) o.region_reversed=!o.region_reversed;
            const f64 pi = 3.14159265358979323846;
            if (mx) { const f64 t0 = pi - a1, t1 = pi - a0; a0 = t0; a1 = t1; }
            else    { const f64 t0 = -a1, t1 = -a0; a0 = t0; a1 = t1; }
        } else if (mx && my) {
            a0 += 3.14159265358979323846;
            a1 += 3.14159265358979323846;
        }
        o.start_angle = a0 + r.rotation;
        o.end_angle = a1 + r.rotation;
    }
    return o;
}

namespace {
void inherit_appearance(Attributes& attrs, std::string& member_layer,
                        const BlockRef& r, const LayerTable* layers) {
    const auto* layer=layers?layers->find(r.layer):nullptr;
    if (member_layer.empty() || member_layer == "0") member_layer = r.layer;
    if (attrs.color.kind == Color::Kind::ByBlock) {
        attrs.color=r.attrs.color;
        if(attrs.color.kind==Color::Kind::ByLayer || attrs.color.kind==Color::Kind::ByBlock) {
            attrs.color=layer?layer->color:Color::by_layer();
            if(!layer)member_layer=r.layer;
        }
    }
    if(attrs.linetype=="ByBlock") {
        attrs.linetype=r.attrs.linetype;
        if((attrs.linetype=="ByLayer"||attrs.linetype=="ByBlock")&&layer)attrs.linetype=layer->linetype;
    }
    if(attrs.lineweight==Lineweight::ByBlock) {
        attrs.lineweight=r.attrs.lineweight;
        if((attrs.lineweight==Lineweight::ByLayer||attrs.lineweight==Lineweight::ByBlock)&&layer)attrs.lineweight=layer->lineweight;
    }
    if(attrs.transparency==-2)attrs.transparency=transparency_percent(r.attrs,layer?layer->transparency:0);
    attrs.visible=attrs.visible&&r.attrs.visible&&(!layer||layer->visible());
}

inline gsnap::Shape place(const gsnap::Shape& s, const BlockRef& r, Point2 base,
                          const gcm::WorkingVolume& v) {
    return place_in_block(s, r, base, v);
}
}

std::vector<gsnap::Shape> BlockTable::expand(const BlockRef& r,
                                             const gcm::WorkingVolume& v,
                                             int depth,const LayerTable* layers) const {
    std::vector<gsnap::Shape> out;
    if (depth <= 0) return out;

    const BlockDef* d = find(r.name);
    if (!d) return out;
    const auto* reference=xref(r.name);
    if(reference&&(reference->state==XrefState::Unloaded||reference->state==XrefState::NotFound))return out;

    const std::vector<gsnap::Shape>* geometry = &d->geometry;
    DynamicResult evaluated;
    if (const auto* dyn = dynamic(r.name)) {
        evaluated = evaluate(*d, *dyn, v);
        if (evaluated.ok) geometry = &evaluated.geometry;
    }
    for (const auto& s : *geometry) {
        auto placed = place(s, r, d->base, v);
        inherit_appearance(placed.attrs, placed.layer, r, layers);
        out.push_back(std::move(placed));
    }

    // Expand in definition coordinates, then apply the parent, to keep shear.
    const auto children=nested_insertions(*d);
    for (std::size_t child=0;child<children.size();++child) {
        auto inner=children[child];
        if(d->is_xref)if(const auto* nested=xref(inner.name);nested&&nested->type==XrefType::Overlay)continue;
        if(child>=d->nested_refs.size())inner.attrs=r.attrs;
        inherit_appearance(inner.attrs, inner.layer, r, layers);
        gcm::u32 last=0;
        for(const auto& shape:out) last=std::max(last,shape.poly);
        std::unordered_map<gcm::u32,gcm::u32> compounds;
        for (auto& s : expand(inner, v, depth - 1,layers)) {
            s=place(s,r,d->base,v);
            if(s.poly) {
                auto [it,inserted]=compounds.try_emplace(s.poly,0);
                if(inserted) it->second=++last;
                s.poly=it->second;
            }
            out.push_back(std::move(s));
        }
    }
    gregion::reconcile_joints(out);
    if(reference&&reference->clip_enabled&&!reference->clip.empty()) {
        auto boundary=reference->clip;
        if(boundary.size()==2){const auto a=boundary[0],b=boundary[1];boundary={a,{b.x,a.y},b,{a.x,b.y}};}
        for(auto& point:boundary){gsnap::Shape marker;marker.a=point;point=place(marker,r,d->base,v).a;}
        auto clipped=gclip::shapes(out,boundary,reference->clip_inverted);
        if(!clipped.ok)return {};
        return std::move(clipped.shapes);
    }
    return out;
}

namespace {
template<class Instance, class Inherit, class Fetch>
std::vector<Instance> placed_members(const BlockTable& table, const BlockRef& ref,
    const gcm::WorkingVolume& volume, int depth, const LayerTable* layers,
    Fetch fetch, Inherit inherit) {
    if (depth <= 0) return {};
    const auto* def=table.find(ref.name);
    if (!def) return {};
    const auto* external=table.xref(ref.name);
    if (external && (external->state==XrefState::Unloaded || external->state==XrefState::NotFound)) return {};
    double bx,by,ix,iy;
    gcm::to_world(volume,def->base,bx,by);
    gcm::to_world(volume,ref.insertion,ix,iy);
    const auto c=std::cos(ref.rotation),s=std::sin(ref.rotation);
    const std::array<double,6> outer{c*ref.scale_x,-s*ref.scale_y,
        ix-c*ref.scale_x*bx+s*ref.scale_y*by,
        s*ref.scale_x,c*ref.scale_y,iy-s*ref.scale_x*bx-c*ref.scale_y*by};
    const auto transform_clip=[&](gclip::Boundary& clip) {
        for(auto& point:clip.points) {
            double x,y;gcm::to_world(volume,point,x,y);
            point=gcm::to_lattice(volume,outer[0]*x+outer[1]*y+outer[2],outer[3]*x+outer[4]*y+outer[5]);
        }
    };
    std::vector<gclip::Boundary> clips;
    if(external&&external->clip_enabled&&!external->clip.empty()) {
        gclip::Boundary clip{external->clip,external->clip_inverted};
        if(clip.points.size()==2){const auto a=clip.points[0],b=clip.points[1];clip.points={a,{b.x,a.y},b,{a.x,b.y}};}
        transform_clip(clip);clips.push_back(std::move(clip));
    }
    std::vector<Instance> result;
    for (auto member:fetch(*def,ref)) {
        inherit(member,ref,layers);
        result.push_back({std::move(member),outer,clips});
    }
    const auto children=table.nested_insertions(*def);
    for (std::size_t i=0;i<children.size();++i) {
        auto child=children[i];
        if(def->is_xref) if(const auto* x=table.xref(child.name);x&&x->type==XrefType::Overlay)continue;
        if(i>=def->nested_refs.size())child.attrs=ref.attrs;
        inherit_appearance(child.attrs,child.layer,ref,layers);
        for (auto item:placed_members<Instance>(table,child,volume,depth-1,layers,fetch,inherit)) {
            const auto inner=item.transform;
            item.transform={outer[0]*inner[0]+outer[1]*inner[3],
                outer[0]*inner[1]+outer[1]*inner[4],
                outer[0]*inner[2]+outer[1]*inner[5]+outer[2],
                outer[3]*inner[0]+outer[4]*inner[3],
                outer[3]*inner[1]+outer[4]*inner[4],
                outer[3]*inner[2]+outer[4]*inner[5]+outer[5]};
            for(auto& clip:item.clips)transform_clip(clip);
            item.clips.insert(item.clips.end(),clips.begin(),clips.end());
            result.push_back(std::move(item));
        }
    }
    return result;
}
}

std::vector<BlockTable::NoteInstance> BlockTable::note_instances(
    const BlockRef& ref,const gcm::WorkingVolume& volume,int depth,const LayerTable* layers) const {
    return placed_members<NoteInstance>(*this,ref,volume,depth,layers,
        [](const BlockDef& def,const BlockRef& r) {
            std::vector<BlockNote> notes=def.notes;
            notes.insert(notes.end(),r.notes.begin(),r.notes.end());
            return notes;
        },
        [](auto& note,const auto& insertion,const auto* table) {
            inherit_appearance(note.attrs,note.layer,insertion,table);
        });
}

std::vector<BlockTable::UnderlayInstance> BlockTable::underlay_instances(
    const BlockRef& ref,const gcm::WorkingVolume& volume,int depth,const LayerTable* layers) const {
    return placed_members<UnderlayInstance>(*this,ref,volume,depth,layers,
        [](const BlockDef& def,const BlockRef&) { return def.underlays; },
        [](auto& underlay,const auto& insertion,const auto* table) {
            const auto* layer=table?table->find(insertion.layer):nullptr;
            if(underlay.layer.empty()||underlay.layer=="0")underlay.layer=insertion.layer;
            const auto* own_layer=table?table->find(underlay.layer):nullptr;
            underlay.visible=underlay.visible&&insertion.attrs.visible&&(!layer||layer->visible())&&(!own_layer||own_layer->visible());
            underlay.plottable=underlay.plottable&&(!own_layer||own_layer->plottable)&&(!layer||layer->plottable);
            const auto opacity=1.0-transparency_percent(insertion.attrs,layer?layer->transparency:0)/100.0;
            underlay.fade=int(std::lround(100-(100-underlay.fade)*opacity));
        });
}

std::optional<std::vector<gsnap::Shape>> BlockTable::explode(
    gcm::u32 index, const gcm::WorkingVolume& v) const {
    if (index >= refs_.size()) return std::nullopt;
    const BlockRef& r = refs_[index];
    const BlockDef* d = find(r.name);
    if (!d || !d->allow_exploding || d->is_xref || xref(r.name) ||
        (!note_instances(r,v).empty() || !underlay_instances(r,v).empty())) return std::nullopt;
    return expand(r, v);
}

void BlockTable::replace_xref_contents(Xref reference, std::vector<BlockDef> definitions,
    std::vector<Xref> nested, std::vector<DynamicBlock> dynamics) {
    std::vector<std::string> previous;
    for(const auto& def:defs_)if(def.source_owner==reference.name)previous.push_back(def.name);
    const auto owned=[&](const std::string& name){return std::find(previous.begin(),previous.end(),name)!=previous.end();};
    std::erase_if(defs_,[&](const auto& def){return def.name==reference.name||owned(def.name);});
    std::erase_if(xrefs_,[&](const auto& x){return owned(x.name);});
    std::erase_if(dynamics_,[&](const auto& d){return owned(d.name);});
    for(auto& def:definitions)define(std::move(def));
    for(auto& x:nested)attach(std::move(x));
    for(auto& dynamic:dynamics)dynamics_.push_back(std::move(dynamic));
    attach(std::move(reference));
}

Xref& BlockTable::attach(Xref x) {
    ++revision_;
    for (auto& e : xrefs_)
        if (e.name == x.name) { e = std::move(x); return e; }
    xrefs_.push_back(std::move(x));
    return xrefs_.back();
}

Xref* BlockTable::xref(std::string_view n) {
    ++revision_;
    for (auto& x : xrefs_)
        if (x.name == n) return &x;
    return nullptr;
}
const Xref* BlockTable::xref(std::string_view n) const {
    for (const auto& x : xrefs_)
        if (x.name == n) return &x;
    return nullptr;
}

bool BlockTable::detach(std::string_view n) {
    ++revision_;
    const auto it = std::find_if(xrefs_.begin(), xrefs_.end(),
                                 [&](const Xref& x) { return x.name == n; });
    if (it == xrefs_.end()) return false;
    const std::string name(n);
    std::vector<std::string> names{name};
    for(const auto& def:defs_)if(def.source_owner==name)names.push_back(def.name);
    const auto owned=[&](const std::string& value){return std::find(names.begin(),names.end(),value)!=names.end();};
    std::erase_if(xrefs_,[&](const auto& x){return owned(x.name);});
    std::erase_if(refs_,[&](const auto& ref){return owned(ref.name);});
    for(auto& def:defs_) {
        std::erase_if(def.nested,owned);
        std::erase_if(def.nested_refs,[&](const auto& ref){return owned(ref.name);});
    }
    std::erase_if(defs_,[&](const auto& def){return owned(def.name)&&(def.is_xref||!def.source_owner.empty());});
    std::erase_if(dynamics_,[&](const auto& d){return owned(d.name);});
    return true;
}

void BlockTable::reload(std::string_view n, bool found) {
    ++revision_;
    if (Xref* x = xref(n)) {
        x->state = found ? XrefState::Loaded : XrefState::NotFound;
        if (found) x->resolved_path = x->path;
    }
}

void BlockTable::unload(std::string_view n) {
    ++revision_;
    if (Xref* x = xref(n)) x->state = XrefState::Unloaded;
}

std::size_t BlockTable::repair_paths(std::string_view old_prefix,
                                     std::string_view new_prefix) {
    ++revision_;
    std::size_t fixed = 0;
    for (auto& x : xrefs_) {
        if (x.path.rfind(old_prefix, 0) != 0) continue;
        x.path = std::string(new_prefix) + x.path.substr(old_prefix.size());
        x.state = XrefState::Unresolved;
        ++fixed;
    }
    return fixed;
}


bool BlockTable::erase_reference(gcm::u32 index) {
    ++revision_;
    if (index >= refs_.size()) return false;
    refs_.erase(refs_.begin() + static_cast<long>(index));
    return true;
}

const char* parameter_kind_name(ParameterKind k) noexcept {
    switch (k) {
        case ParameterKind::Linear:     return "linear";
        case ParameterKind::Point:      return "point";
        case ParameterKind::Polar:      return "polar";
        case ParameterKind::XY:         return "xy";
        case ParameterKind::Rotation:   return "rotation";
        case ParameterKind::Flip:       return "flip";
        case ParameterKind::Visibility: return "visibility";
        case ParameterKind::Lookup:     return "lookup";
        case ParameterKind::Alignment:  return "alignment";
    }
    return "?";
}

const char* action_kind_name(ActionKind k) noexcept {
    switch (k) {
        case ActionKind::Move:    return "move";
        case ActionKind::Scale:   return "scale";
        case ActionKind::Stretch: return "stretch";
        case ActionKind::Polar:   return "polar";
        case ActionKind::Rotate:  return "rotate";
        case ActionKind::Flip:    return "flip";
        case ActionKind::Array:   return "array";
        case ActionKind::Lookup:  return "lookup";
    }
    return "?";
}

bool BlockParameter::valid_value(f64 v) const noexcept {
    if (minimum != maximum && (v < minimum || v > maximum)) return false;
    if (increment > 0.0) {
        const f64 steps = (v - minimum) / increment;
        if (std::fabs(steps - std::round(steps)) > 1e-9) return false;
    }
    return true;
}

f64 BlockParameter::clamp_value(f64 v) const noexcept {
    if (minimum != maximum) v = std::clamp(v, minimum, maximum);
    if (increment > 0.0) v = minimum + std::round((v - minimum) / increment) *
                                           increment;
    return v;
}

BlockParameter* DynamicBlock::parameter(std::string_view name) {
    for (auto& p : parameters)
        if (p.name == name) return &p;
    return nullptr;
}

const BlockParameter* DynamicBlock::parameter(std::string_view name) const {
    for (const auto& p : parameters)
        if (p.name == name) return &p;
    return nullptr;
}

bool DynamicBlock::set_parameter(std::string_view name, f64 value) {
    BlockParameter* p = parameter(name);
    if (!p) return false;
    p->value = p->clamp_value(value);
    return true;
}

bool DynamicBlock::set_choice(std::string_view name, std::string_view choice) {
    BlockParameter* p = parameter(name);
    if (!p) return false;
    for (std::size_t i = 0; i < p->choices.size(); ++i)
        if (p->choices[i] == choice) {
            p->choice = i;
            return true;
        }
    return false;
}

bool DynamicBlock::set_visibility(std::string_view set_name) {
    for (std::size_t i = 0; i < visibility.size(); ++i)
        if (visibility[i].name == set_name) {
            visible_set = i;
            return true;
        }
    return false;
}

DynamicResult evaluate(const BlockDef& def, const DynamicBlock& dyn,
                       const gcm::WorkingVolume& v) {
    DynamicResult out;
    out.geometry = def.geometry;

    for (const BlockAction& a : dyn.actions) {
        const BlockParameter* p = dyn.parameter(a.parameter);
        if (!p) continue;

        // The parameter value is a delta from its defined value.
        const f64 defined = std::hypot(f64(a.window_hi.x - a.window_lo.x),
                                       f64(a.window_hi.y - a.window_lo.y));
        (void)defined;

        for (u32 index : a.geometry) {
            if (index >= out.geometry.size()) continue;
            gsnap::Shape& s = out.geometry[index];

            switch (a.kind) {
                case ActionKind::Move: {
                    const i64 dx = static_cast<i64>(
                        std::llround(a.dx * p->value * v.scale()));
                    const i64 dy = static_cast<i64>(
                        std::llround(a.dy * p->value * v.scale()));
                    s.a.x += dx;
                    s.a.y += dy;
                    s.b.x += dx;
                    s.b.y += dy;
                    ++out.moved;
                    break;
                }

                case ActionKind::Stretch: {
                    const i64 dx = static_cast<i64>(
                        std::llround(a.dx * p->value * v.scale()));
                    const i64 dy = static_cast<i64>(
                        std::llround(a.dy * p->value * v.scale()));
                    auto inside = [&](const Point2& q) {
                        return q.x >= a.window_lo.x && q.x <= a.window_hi.x &&
                               q.y >= a.window_lo.y && q.y <= a.window_hi.y;
                    };
                    bool touched = false;
                    if (inside(s.a)) {
                        s.a.x += dx;
                        s.a.y += dy;
                        touched = true;
                    }
                    if (inside(s.b)) {
                        s.b.x += dx;
                        s.b.y += dy;
                        touched = true;
                    }
                    if (touched) ++out.moved;
                    break;
                }

                case ActionKind::Scale: {
                    const f64 k = p->value != 0.0 ? p->value : 1.0;
                    auto scale_point = [&](Point2& q) {
                        q.x = a.origin.x +
                              static_cast<i64>(std::llround(f64(q.x - a.origin.x) * k));
                        q.y = a.origin.y +
                              static_cast<i64>(std::llround(f64(q.y - a.origin.y) * k));
                    };
                    scale_point(s.a);
                    scale_point(s.b);
                    s.radius = static_cast<i64>(std::llround(f64(s.radius) * k));
                    ++out.moved;
                    break;
                }

                case ActionKind::Rotate:
                case ActionKind::Polar: {
                    const f64 c = std::cos(p->value), sn = std::sin(p->value);
                    auto turn = [&](Point2& q) {
                        const f64 x = f64(q.x - a.origin.x);
                        const f64 y = f64(q.y - a.origin.y);
                        q.x = a.origin.x + static_cast<i64>(std::llround(x * c - y * sn));
                        q.y = a.origin.y + static_cast<i64>(std::llround(x * sn + y * c));
                    };
                    turn(s.a);
                    turn(s.b);
                    if (s.is_curve()) {
                        s.start_angle += p->value;
                        s.end_angle += p->value;
                    }
                    ++out.moved;
                    break;
                }

                case ActionKind::Flip: {
                    // Any non-zero value means flipped.
                    if (p->value == 0.0) break;
                    auto mirror = [&](Point2& q) {
                        const f64 ax = f64(p->base.x), ay = f64(p->base.y);
                        const f64 bx = f64(p->end.x), by = f64(p->end.y);
                        const f64 dx = bx - ax, dy = by - ay;
                        const f64 len2 = dx * dx + dy * dy;
                        if (len2 < 1e-9) return;
                        const f64 px = f64(q.x) - ax, py = f64(q.y) - ay;
                        const f64 t = (px * dx + py * dy) / len2;
                        const f64 rx = ax + dx * t, ry = ay + dy * t;
                        q.x = static_cast<i64>(std::llround(2.0 * rx - f64(q.x)));
                        q.y = static_cast<i64>(std::llround(2.0 * ry - f64(q.y)));
                    };
                    mirror(s.a);
                    mirror(s.b);
                    ++out.moved;
                    break;
                }

                default:
                    break;
            }
        }
    }

    if (!dyn.visibility.empty() && dyn.visible_set < dyn.visibility.size()) {
        const auto& shown = dyn.visibility[dyn.visible_set].geometry;
        std::vector<gsnap::Shape> kept;
        for (u32 i = 0; i < out.geometry.size(); ++i) {
            const bool visible =
                std::find(shown.begin(), shown.end(), i) != shown.end();
            if (visible) kept.push_back(out.geometry[i]);
            else ++out.hidden;
        }
        out.geometry = std::move(kept);
    }

    out.ok = true;
    return out;
}

DynamicBlock& BlockTable::make_dynamic(std::string_view name) {
    ++revision_;
    for (auto& d : dynamics_)
        if (d.name == name) return d;
    DynamicBlock d;
    d.name = std::string(name);
    dynamics_.push_back(std::move(d));
    return dynamics_.back();
}

DynamicBlock* BlockTable::dynamic(std::string_view name) {
    ++revision_;
    for (auto& d : dynamics_)
        if (d.name == name) return &d;
    return nullptr;
}

const DynamicBlock* BlockTable::dynamic(std::string_view name) const {
    for (const auto& d : dynamics_)
        if (d.name == name) return &d;
    return nullptr;
}

bool BlockTable::is_dynamic(std::string_view name) const {
    return dynamic(name) != nullptr;
}

}
