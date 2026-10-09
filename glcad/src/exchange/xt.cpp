#include "exchange/xt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>

#include "geometry/nurbs.hpp"
#include "geometry/surface.hpp"
#include "geometry/triangulate.hpp"

namespace gxt {

using gcm::F64;
using gcm::u8;
using P3 = ggeo::d3::Point<F64>;
using V3 = ggeo::d3::Vec<F64>;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

// Type letters $d $p $f $c $u $l $n $v $i $b $h $w $t; count 0 scalar, 1 variable
struct FieldDef {
    std::string name;
    char        type{'d'};
    int         n_elts{0};
    int         ptr_class{0};
};

struct NodeDef {
    int                   type{0};
    std::string           name;
    std::vector<FieldDef> fields;
    bool variable() const {
        return !fields.empty() && fields.back().n_elts == 1;
    }
};

// "name:type" or "name:type[n]" or "name:type[]" (variable), space separated.
NodeDef def(int type, const char* name, const char* spec) {
    NodeDef d;
    d.type = type;
    d.name = name;
    std::istringstream in(spec);
    std::string tok;
    while (in >> tok) {
        FieldDef f;
        const std::size_t colon = tok.find(':');
        f.name = tok.substr(0, colon);
        std::string t = tok.substr(colon + 1);
        f.type = t[0];
        if (t.size() > 1 && t[1] == '[') {
            const std::string n = t.substr(2, t.size() - 3);
            f.n_elts = n.empty() ? 1 : std::atoi(n.c_str());
        }
        if (f.type == 'p') f.ptr_class = 1;
        d.fields.push_back(f);
    }
    return d;
}

const std::map<int, NodeDef>& base_schema() {
    static const std::map<int, NodeDef> s = [] {
        std::map<int, NodeDef> m;
        auto add = [&](NodeDef d) { m[d.type] = std::move(d); };
        const std::string cur = "node_id:d attributes_groups:p owner:p next:p previous:p geometric_owner:p sense:c ";
        const std::string surf = cur;
        add(def(10, "ASSEMBLY", "highest_node_id:d attributes_groups:p attribute_chains:p list:p surface:p curve:p point:p key:p res_size:f res_linear:f ref_instance:p next:p previous:p state:u owner:p type:u sub_instance:p"));
        add(def(11, "INSTANCE", "node_id:d attributes_groups:p type:u part:p transform:p assembly:p next_in_part:p prev_in_part:p next_of_part:p prev_of_part:p"));
        add(def(12, "BODY", "highest_node_id:d attributes_groups:p attribute_chains:p surface:p curve:p point:p key:p res_size:f res_linear:f ref_instance:p next:p previous:p state:u owner:p body_type:u nom_geom_state:u shell:p boundary_surface:p boundary_curve:p boundary_point:p region:p edge:p vertex:p"));
        add(def(13, "SHELL", "node_id:d attributes_groups:p body:p next:p face:p edge:p vertex:p region:p front_face:p"));
        add(def(14, "FACE", "node_id:d attributes_groups:p tolerance:f next:p previous:p loop:p shell:p surface:p sense:c next_on_surface:p previous_on_surface:p next_front:p previous_front:p front_shell:p"));
        add(def(15, "LOOP", "node_id:d attributes_groups:p fin:p face:p next:p"));
        add(def(16, "EDGE", "node_id:d attributes_groups:p tolerance:f fin:p previous:p next:p curve:p next_on_curve:p previous_on_curve:p owner:p"));
        add(def(17, "FIN", "attributes_groups:p loop:p forward:p backward:p vertex:p other:p edge:p curve:p next_at_vx:p sense:c"));
        add(def(18, "VERTEX", "node_id:d attributes_groups:p fin:p previous:p next:p point:p tolerance:f owner:p"));
        add(def(19, "REGION", "node_id:d attributes_groups:p body:p next:p previous:p shell:p type:c"));
        add(def(29, "POINT", "node_id:d attributes_groups:p owner:p next:p previous:p pvec:v"));
        add(def(30, "LINE", (cur + "pvec:v direction:v").c_str()));
        add(def(31, "CIRCLE", (cur + "centre:v normal:v x_axis:v radius:f").c_str()));
        add(def(32, "ELLIPSE", (cur + "centre:v normal:v x_axis:v major_radius:f minor_radius:f").c_str()));
        add(def(38, "INTERSECTION", (cur + "surface:p[2] chart:p start:p end:p").c_str()));
        add(def(40, "CHART", "base_parameter:f base_scale:f chart_count:d chordal_error:f angular_error:f parameter_error:f[2] hvec:h[]"));
        add(def(41, "LIMIT", "type:c hvec:h[]"));
        add(def(45, "BSPLINE_VERTICES", "vertices:f[]"));
        add(def(50, "PLANE", (surf + "pvec:v normal:v x_axis:v").c_str()));
        add(def(51, "CYLINDER", (surf + "pvec:v axis:v radius:f x_axis:v").c_str()));
        add(def(52, "CONE", (surf + "pvec:v axis:v radius:f sin_half_angle:f cos_half_angle:f x_axis:v").c_str()));
        add(def(53, "SPHERE", (surf + "centre:v radius:f axis:v x_axis:v").c_str()));
        add(def(54, "TORUS", (surf + "centre:v axis:v major_radius:f minor_radius:f x_axis:v").c_str()));
        add(def(56, "BLENDED_EDGE", (surf + "blend_type:c surface:p[2] spine:p range:f[2] thumb_weight:f[2] boundary:p[2] start:p end:p").c_str()));
        add(def(59, "BLEND_BOUND", (surf + "boundary:n blend:p").c_str()));
        add(def(60, "OFFSET_SURF", (surf + "check:c true_offset:l surface:p offset:f scale:f").c_str()));
        add(def(67, "SWEPT_SURF", (surf + "section:p sweep:v scale:f").c_str()));
        add(def(68, "SPUN_SURF", (surf + "profile:p base:v axis:v start:v end:v start_param:f end_param:f x_axis:v scale:f").c_str()));
        add(def(70, "LIST", "node_id:d owner:p next:p previous:p list_type:d list_length:d block_length:d size_of_entry:d list_block:p finger_block:p finger_index:d notransmit:l"));
        add(def(74, "POINTER_LIS_BLOCK", "n_entries:d next_block:p entries:p[]"));
        add(def(79, "ATT_DEF_ID", "string:c[]"));
        add(def(80, "ATTRIB_DEF", "next:p identifier:p type_id:d actions:u[8] field_names:p legal_owners:l[14] fields:u[]"));
        add(def(81, "ATTRIBUTE", "node_id:d definition:p owner:p next:p previous:p next_of_type:p previous_of_type:p fields:p[]"));
        add(def(82, "INT_VALUES", "values:d[]"));
        add(def(83, "REAL_VALUES", "values:f[]"));
        add(def(84, "CHAR_VALUES", "values:c[]"));
        add(def(85, "POINT_VALUES", "values:v[]"));
        add(def(86, "VECTOR_VALUES", "values:v[]"));
        add(def(87, "AXIS_VALUES", "values:v[]"));
        add(def(88, "TAG_VALUES", "values:d[]"));
        add(def(89, "DIRECTION_VALUES", "values:v[]"));
        add(def(90, "GROUP", "node_id:d attributes_groups:p owner:p next:p previous:p type:u"));
        add(def(91, "MEMBER_OF_GROUP", "dummy_node_id:d owning_group:p owner:p next:p previous:p next_member:p previous_member:p"));
        add(def(98, "UNICODE_VALUES", "values:w[]"));
        add(def(99, "FIELD_NAMES", "names:p[]"));
        add(def(100, "TRANSFORM", "node_id:d owner:p next:p previous:p rotation_matrix:f[9] translation_vector:v scale:f flag:d perspective_vector:v"));
        add(def(101, "WORLD", "assembly:p attribute:p body:p transform:p surface:p curve:p point:p alive:l attrib_def:p highest_id:d current_id:d"));
        add(def(102, "KEY", "string:c[]"));
        add(def(120, "PE_SURF", (surf + "type:c data:p tf:p internal_geom:p[]").c_str()));
        add(def(121, "INT_PE_DATA", "geom_type:d real_array:p int_array:p"));
        add(def(122, "EXT_PE_DATA", "key:p real_array:p int_array:p"));
        add(def(124, "B_SURFACE", (surf + "nurbs:p data:p").c_str()));
        add(def(125, "SURFACE_DATA", "original_uint:i original_vint:i extended_uint:i extended_vint:i self_int:u original_u_start:c original_u_end:c original_v_start:c original_v_end:c extended_u_start:c extended_u_end:c extended_v_start:c extended_v_end:c analytic_form_type:c swept_form_type:c spun_form_type:c blend_form_type:c analytic_form:p swept_form:p spun_form:p blend_form:p"));
        add(def(126, "NURBS_SURF", "u_periodic:l v_periodic:l u_degree:n v_degree:n n_u_vertices:d n_v_vertices:d u_knot_type:u v_knot_type:u n_u_knots:d n_v_knots:d rational:l u_closed:l v_closed:l surface_form:u vertex_dim:n bspline_vertices:p u_knot_mult:p v_knot_mult:p u_knots:p v_knots:p"));
        add(def(127, "KNOT_MULT", "mult:n[]"));
        add(def(128, "KNOT_SET", "knots:f[]"));
        add(def(130, "PE_CURVE", (cur + "type:c data:p tf:p internal_geom:p[]").c_str()));
        add(def(133, "TRIMMED_CURVE", (cur + "basis_curve:p point_1:v point_2:v parm_1:f parm_2:f external:p internal:p").c_str()));
        add(def(134, "B_CURVE", (cur + "nurbs:p data:p").c_str()));
        add(def(135, "CURVE_DATA", "self_int:u analytic_form:p"));
        add(def(136, "NURBS_CURVE", "degree:n n_vertices:d vertex_dim:n n_knots:d knot_type:u periodic:l closed:l rational:l curve_form:u bspline_vertices:p knot_mult:p knots:p"));
        add(def(137, "SP_CURVE", (cur + "surface:p b_curve:p original:p tolerance_to_original:f").c_str()));
        add(def(141, "GEOMETRIC_OWNER", "next:p previous:p shared_geometry:p owner:p"));
        add(def(163, "HELIX_CU_FORM", "axis_pt:v axis_dir:v point:v hand:c turns:i pitch:f tol:f"));
        add(def(184, "HELIX_SU_FORM", "axis_pt:v axis_dir:v hand:c turns:i pitch:f gap:f tol:f"));
        return m;
    }();
    return s;
}

// Records joined without newlines; a number is followed by one space, a char not.
class Stream {
public:
    explicit Stream(std::string s) : s_(std::move(s)) {}

    bool at_end() const noexcept { return i_ >= s_.size(); }
    std::size_t pos() const noexcept { return i_; }
    void skip_space() { if (i_ < s_.size() && s_[i_] == ' ') ++i_; }

    bool number(std::string& out) {
        skip_space();
        std::size_t j = i_;
        if (j < s_.size() && (s_[j] == '-' || s_[j] == '+')) ++j;
        std::size_t k = j;
        while (k < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[k])) || s_[k] == '.')) ++k;
        if (k == j) return false;
        if (k < s_.size() && (s_[k] == 'e' || s_[k] == 'E')) {
            std::size_t e = k + 1;
            if (e < s_.size() && (s_[e] == '-' || s_[e] == '+')) ++e;
            std::size_t d = e;
            while (d < s_.size() && std::isdigit(static_cast<unsigned char>(s_[d]))) ++d;
            if (d > e) k = d;
        }
        out.assign(s_.data() + i_, k - i_);
        i_ = k;
        skip_space();
        return true;
    }
    bool integer(long long& v) {
        std::string t;
        if (!number(t)) return false;
        v = std::strtoll(t.c_str(), nullptr, 10);
        return true;
    }
    // A double, or '?' for unset (stored as NaN).
    bool real(f64& v) {
        skip_space();
        if (i_ < s_.size() && s_[i_] == '?') { ++i_; v = std::nan(""); return true; }
        std::string t;
        if (!number(t)) return false;
        v = std::strtod(t.c_str(), nullptr);
        return true;
    }
    bool ch(char& c) {
        if (i_ >= s_.size()) return false;
        c = s_[i_++];
        return true;
    }
    // length, space, characters.
    bool short_string(std::string& out) {
        long long n;
        if (!integer(n) || n < 0 || n > 100000) return false;
        if (i_ + static_cast<std::size_t>(n) > s_.size()) return false;
        out.assign(s_.data() + i_, static_cast<std::size_t>(n));
        i_ += static_cast<std::size_t>(n);
        return true;
    }
    char peek() const noexcept { return i_ < s_.size() ? s_[i_] : '\0'; }
    std::string context() const {
        const std::size_t a = i_ > 40 ? i_ - 40 : 0;
        return s_.substr(a, std::min<std::size_t>(80, s_.size() - a));
    }

private:
    std::string s_;
    std::size_t i_{0};
};

struct Value {
    char             type{'d'};
    std::vector<f64> nums;   // every numeric type, arrays flattened
    std::string      text;   // c and l fields
};

struct Node {
    int   type{0};
    int   index{0};
    const NodeDef* def{nullptr};
    std::vector<Value> values;

    const Value* get(std::string_view name) const {
        if (!def) return nullptr;
        for (std::size_t i = 0; i < def->fields.size() && i < values.size(); ++i)
            if (def->fields[i].name == name) return &values[i];
        return nullptr;
    }
    int p(std::string_view name) const {
        const Value* v = get(name);
        return v && !v->nums.empty() ? static_cast<int>(v->nums[0]) : 0;
    }
    f64 f(std::string_view name) const {
        const Value* v = get(name);
        return v && !v->nums.empty() ? v->nums[0] : 0.0;
    }
    char c(std::string_view name) const {
        const Value* v = get(name);
        return v && !v->text.empty() ? v->text[0] : '\0';
    }
    bool vec(std::string_view name, f64& x, f64& y, f64& z) const {
        const Value* v = get(name);
        if (!v || v->nums.size() < 3) return false;
        x = v->nums[0]; y = v->nums[1]; z = v->nums[2];
        return true;
    }
};

struct Parsed {
    std::string modeller, schema;
    bool embedded{false};
    std::map<int, Node> nodes;
    std::map<int, NodeDef> effective;
    std::vector<NodeDef> owned;
    std::size_t unknown{0};
};

bool read_field(Stream& in, const FieldDef& f, int n_var, Value& out, std::string& err) {
    out.type = f.type;
    const int count = f.n_elts == 0 ? 1 : f.n_elts == 1 ? n_var : f.n_elts;
    for (int k = 0; k < count; ++k) {
        switch (f.type) {
            case 'd': case 'p': case 'n': case 'u': case 't': case 'w': {
                long long v;
                if (!in.integer(v)) { err = "expected a number for field " + f.name; return false; }
                out.nums.push_back(static_cast<f64>(v));
                break;
            }
            case 'f': {
                f64 v;
                if (!in.real(v)) { err = "expected a real for field " + f.name; return false; }
                out.nums.push_back(v);
                break;
            }
            case 'v': case 'h': {
                for (int i = 0; i < 3; ++i) {
                    f64 v;
                    if (!in.real(v)) { err = "expected a vector for field " + f.name; return false; }
                    out.nums.push_back(v);
                }
                break;
            }
            case 'i': {
                for (int i = 0; i < 2; ++i) {
                    f64 v;
                    if (!in.real(v)) { err = "expected an interval for field " + f.name; return false; }
                    out.nums.push_back(v);
                }
                break;
            }
            case 'b': {
                for (int i = 0; i < 6; ++i) {
                    f64 v;
                    if (!in.real(v)) { err = "expected a box for field " + f.name; return false; }
                    out.nums.push_back(v);
                }
                break;
            }
            case 'c': case 'l': {
                char ch;
                if (!in.ch(ch)) { err = "expected a character for field " + f.name; return false; }
                out.text.push_back(ch);
                break;
            }
            default:
                err = std::string("unknown field type '") + f.type + "' in " + f.name;
                return false;
        }
    }
    return true;
}

// Embedded field: name, ptr_class, n_elts, type when class is zero, transmit flag.
bool read_field_def(Stream& in, FieldDef& f, std::string& err) {
    if (!in.short_string(f.name)) { err = "bad field name in schema"; return false; }
    long long cls, n;
    if (!in.integer(cls)) { err = "bad ptr_class in schema for " + f.name; return false; }
    if (!in.integer(n)) { err = "bad n_elts in schema for " + f.name; return false; }
    f.ptr_class = static_cast<int>(cls);
    f.n_elts = static_cast<int>(n);
    if (cls == 0) {
        std::string t;
        if (!in.short_string(t) || t.empty()) { err = "bad type in schema for " + f.name; return false; }
        f.type = t[0];
    } else {
        f.type = 'p';
    }
    if (n == 1) {
        char x;
        if (!in.ch(x)) { err = "bad xmt_code in schema for " + f.name; return false; }
    }
    return true;
}

// 255 means as the base schema; otherwise a field count and edit sequence.
bool read_schema_info(Stream& in, Parsed& p, int type, std::string& err) {
    const auto& base = base_schema();
    const auto it = base.find(type);
    if (it == base.end()) {
        long long nf;
        if (!in.integer(nf)) { err = "bad field count for new node type " + std::to_string(type); return false; }
        NodeDef d;
        d.type = type;
        std::string desc;
        if (!in.short_string(d.name) || !in.short_string(desc)) { err = "bad name for new node type"; return false; }
        for (long long i = 0; i < nf; ++i) {
            FieldDef f;
            if (!read_field_def(in, f, err)) return false;
            d.fields.push_back(f);
        }
        p.effective[type] = d;
        return true;
    }
    long long flag;
    if (!in.integer(flag)) { err = "bad schema flag for type " + std::to_string(type); return false; }
    if (flag == 255) { p.effective[type] = it->second; return true; }

    const std::vector<FieldDef>& basef = it->second.fields;
    NodeDef d;
    d.type = type;
    d.name = it->second.name;
    std::size_t bi = 0;
    for (;;) {
        char op;
        if (!in.ch(op)) { err = "unterminated schema edit for " + d.name; return false; }
        if (op == 'C') {
            if (bi >= basef.size()) { err = "schema copies past the base fields of " + d.name; return false; }
            d.fields.push_back(basef[bi++]);
        } else if (op == 'D') {
            if (bi >= basef.size()) { err = "schema deletes past the base fields of " + d.name; return false; }
            ++bi;
        } else if (op == 'I' || op == 'A') {
            FieldDef f;
            if (!read_field_def(in, f, err)) return false;
            d.fields.push_back(f);
        } else if (op == 'Z') {
            break;
        } else {
            err = std::string("unknown schema edit '") + op + "' for " + d.name;
            return false;
        }
    }
    if (static_cast<long long>(d.fields.size()) != flag) {
        err = "schema for " + d.name + " declares " + std::to_string(flag) + " fields, edits give " +
              std::to_string(d.fields.size()) + " (base table for this type is wrong)";
        return false;
    }
    p.effective[type] = d;
    return true;
}

bool parse(std::string_view text, Parsed& p, std::string& err) {
    std::string joined;
    joined.reserve(text.size());
    std::size_t header_end = std::string::npos;
    {
        std::size_t pos = 0;
        bool past_header = false;
        while (pos < text.size()) {
            std::size_t nl = text.find('\n', pos);
            if (nl == std::string_view::npos) nl = text.size();
            std::string_view rec = text.substr(pos, nl - pos);
            while (!rec.empty() && rec.back() == '\r') rec.remove_suffix(1);
            if (!past_header) {
                if (rec.rfind("**END_OF_HEADER", 0) == 0) past_header = true;
            } else {
                joined.append(rec);
            }
            pos = nl + 1;
        }
        if (!past_header) { err = "no **END_OF_HEADER record: not a text XT file"; return false; }
        header_end = 0;
    }
    (void)header_end;
    Stream in(joined);

    char flag;
    if (!in.ch(flag) || flag != 'T') { err = "not a text transmit stream (flag is not T)"; return false; }
    if (!in.short_string(p.modeller)) { err = "bad modeller version string"; return false; }
    if (!in.short_string(p.schema)) { err = "bad schema string"; return false; }
    // SCH_m_n_base (two numbers) means embedded schema edits follow.
    p.embedded = std::count(p.schema.begin(), p.schema.end(), '_') >= 3;
    long long usfld = 0;
    if (p.embedded) {
        long long max_types;
        if (!in.integer(max_types)) { err = "bad node type count"; return false; }
    }
    if (!in.integer(usfld)) { err = "bad user field size"; return false; }
    if (usfld != 0) { err = "user fields are not supported (USFLD_SIZE=" + std::to_string(usfld) + ")"; return false; }
    if (!p.embedded) p.effective = base_schema();

    for (;;) {
        long long type;
        if (!in.integer(type)) {
            if (in.at_end()) { err = "stream ended without a terminator"; return false; }
            err = "expected a node type at offset " + std::to_string(in.pos());
            return false;
        }
        if (type == 1) {
            long long zero;
            in.integer(zero);
            break;
        }
        if (p.embedded && !p.effective.count(static_cast<int>(type))) {
            if (!read_schema_info(in, p, static_cast<int>(type), err)) return false;
        }
        const auto dit = p.effective.find(static_cast<int>(type));
        if (dit == p.effective.end()) {
            err = "node type " + std::to_string(type) + " is not in the base schema and the file does not describe it (near \"" +
                  in.context() + "\")";
            return false;
        }
        const NodeDef& d = dit->second;
        Node n;
        n.type = static_cast<int>(type);
        n.def = &d;
        long long n_var = 0;
        if (d.variable()) {
            if (!in.integer(n_var)) { err = "bad variable length for " + d.name; return false; }
        }
        long long index;
        if (!in.integer(index)) { err = "bad index for " + d.name; return false; }
        n.index = static_cast<int>(index);
        for (const FieldDef& f : d.fields) {
            Value v;
            if (!read_field(in, f, static_cast<int>(n_var), v, err)) {
                err += " (node " + d.name + " " + std::to_string(index) + ")";
                return false;
            }
            n.values.push_back(std::move(v));
        }
        p.nodes[n.index] = std::move(n);
    }
    return true;
}

struct Vec { f64 x, y, z; };
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec operator*(Vec a, f64 s) { return {a.x * s, a.y * s, a.z * s}; }
f64 dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
f64 len(Vec a) { return std::sqrt(dot(a, a)); }
Vec unit(Vec a) { const f64 l = len(a); return l > 1e-300 ? a * (1.0 / l) : Vec{0, 0, 1}; }
Vec vecf(const Node& n, const char* name) { Vec v{0, 0, 0}; n.vec(name, v.x, v.y, v.z); return v; }

using Nurbs3 = ggeo::NurbsCurve3<F64>;
using NurbsS = ggeo::NurbsSurface<F64>;

bool expand_knots(const Parsed& p, int knots_idx, int mult_idx, ggeo::KnotVector& out) {
    const auto k = p.nodes.find(knots_idx);
    const auto m = p.nodes.find(mult_idx);
    if (k == p.nodes.end() || m == p.nodes.end()) return false;
    const Value* kv = k->second.get("knots");
    const Value* mv = m->second.get("mult");
    if (!kv || !mv || kv->nums.size() != mv->nums.size()) return false;
    out.knots.clear();
    for (std::size_t i = 0; i < kv->nums.size(); ++i)
        for (int r = 0; r < static_cast<int>(mv->nums[i]); ++r) out.knots.push_back(kv->nums[i]);
    return !out.knots.empty();
}

std::shared_ptr<Nurbs3> nurbs_curve(const Parsed& p, int nurbs_idx, int dim_out) {
    const auto it = p.nodes.find(nurbs_idx);
    if (it == p.nodes.end() || it->second.type != 136) return nullptr;
    const Node& n = it->second;
    auto c = std::make_shared<Nurbs3>();
    c->degree = n.p("degree");
    const int nv = n.p("n_vertices"), dim = n.p("vertex_dim");
    const bool rational = n.c("rational") == 'T';
    const auto bv = p.nodes.find(n.p("bspline_vertices"));
    if (bv == p.nodes.end()) return nullptr;
    const Value* vals = bv->second.get("vertices");
    if (!vals || vals->nums.size() < static_cast<std::size_t>(nv * dim)) return nullptr;
    for (int i = 0; i < nv; ++i) {
        const f64* q = vals->nums.data() + static_cast<std::size_t>(i * dim);
        ggeo::ControlPoint3<F64> cp;
        // Rational vertices are homogeneous: (x w, y w, z w, w).
        const int spatial = rational ? dim - 1 : dim;
        const f64 w = rational ? q[dim - 1] : 1.0;
        const f64 x = spatial > 0 ? q[0] : 0.0, y = spatial > 1 ? q[1] : 0.0, z = spatial > 2 ? q[2] : 0.0;
        cp.p = P3::from(w != 0.0 ? x / w : x, w != 0.0 ? y / w : y, w != 0.0 ? z / w : z);
        cp.w = F64::from(w);
        c->control.push_back(cp);
    }
    (void)dim_out;
    if (!expand_knots(p, n.p("knots"), n.p("knot_mult"), c->knots)) return nullptr;
    return c->valid() ? c : nullptr;
}

std::shared_ptr<NurbsS> nurbs_surface(const Parsed& p, int nurbs_idx) {
    const auto it = p.nodes.find(nurbs_idx);
    if (it == p.nodes.end() || it->second.type != 126) return nullptr;
    const Node& n = it->second;
    auto s = std::make_shared<NurbsS>();
    s->degree_u = n.p("u_degree");
    s->degree_v = n.p("v_degree");
    s->count_u = static_cast<std::size_t>(n.p("n_u_vertices"));
    s->count_v = static_cast<std::size_t>(n.p("n_v_vertices"));
    const int dim = n.p("vertex_dim");
    const bool rational = n.c("rational") == 'T';
    const auto bv = p.nodes.find(n.p("bspline_vertices"));
    if (bv == p.nodes.end()) return nullptr;
    const Value* vals = bv->second.get("vertices");
    const std::size_t total = s->count_u * s->count_v;
    if (!vals || vals->nums.size() < total * static_cast<std::size_t>(dim)) return nullptr;
    // v varies fastest within a u row.
    for (std::size_t i = 0; i < total; ++i) {
        const f64* q = vals->nums.data() + i * static_cast<std::size_t>(dim);
        ggeo::ControlPoint3<F64> cp;
        const f64 w = rational ? q[dim - 1] : 1.0;
        cp.p = P3::from(w != 0.0 ? q[0] / w : q[0], w != 0.0 ? q[1] / w : q[1], w != 0.0 ? q[2] / w : q[2]);
        cp.w = F64::from(w);
        s->control.push_back(cp);
    }
    if (!expand_knots(p, n.p("u_knots"), n.p("u_knot_mult"), s->knots_u)) return nullptr;
    if (!expand_knots(p, n.p("v_knots"), n.p("v_knot_mult"), s->knots_v)) return nullptr;
    return s->valid() ? s : nullptr;
}

struct Surf {
    bool valid{false};
    gtopo::Surface surface;
    Vec origin{0, 0, 0}, axis{0, 0, 1}, xaxis{1, 0, 0}, yaxis{0, 1, 0};
    f64 radius{0}, minor{0}, sin_half{0}, cos_half{1};
    char sense{'+'};
    std::shared_ptr<NurbsS> nurbs;
    bool uv(Vec p, f64& u, f64& v) const {
        const Vec d = p - origin;
        switch (surface.kind) {
            case gtopo::SurfaceKind::Plane:
                u = dot(d, xaxis); v = dot(d, yaxis); return true;
            case gtopo::SurfaceKind::Cylinder: {
                v = dot(d, axis);
                const Vec r = d - axis * v;
                u = std::atan2(dot(r, yaxis), dot(r, xaxis));
                return true;
            }
            case gtopo::SurfaceKind::Cone: {
                // R(u,v) = P + (r + v sin) (X cos u + Y sin u) + v cos A.
                v = dot(d, axis) / (cos_half > 1e-12 ? cos_half : 1.0);
                const Vec r = d - axis * dot(d, axis);
                u = std::atan2(dot(r, yaxis), dot(r, xaxis));
                return true;
            }
            case gtopo::SurfaceKind::Sphere: {
                const f64 h = dot(d, axis);
                const Vec r = d - axis * h;
                u = std::atan2(dot(r, yaxis), dot(r, xaxis));
                v = std::atan2(h, len(r));
                return true;
            }
            case gtopo::SurfaceKind::Torus: {
                const f64 h = dot(d, axis);
                const Vec r = d - axis * h;
                u = std::atan2(dot(r, yaxis), dot(r, xaxis));
                v = std::atan2(h, len(r) - radius);
                return true;
            }
            case gtopo::SurfaceKind::BSpline: {
                if (!nurbs) return false;
                nurbs->invert(P3::from(p.x, p.y, p.z), u, v);
                return true;
            }
        }
        return false;
    }
    f64 period_u{0};
    bool periodic_u() const { return period_u > 0.0; }
    Vec normal_at(Vec p) const {
        Vec n{0, 0, 1};
        if (surface.kind == gtopo::SurfaceKind::BSpline && nurbs) {
            f64 u, v;
            nurbs->invert(P3::from(p.x, p.y, p.z), u, v);
            const auto nn = nurbs->normal(u, v);
            n = unit({nn.x.to_f64(), nn.y.to_f64(), nn.z.to_f64()});
        } else {
            f64 nx, ny, nz;
            if (surface.normal_at(p.x, p.y, p.z, nx, ny, nz)) n = {nx, ny, nz};
        }
        return sense == '-' ? n * -1.0 : n;
    }
};

Surf make_surface(const Parsed& p, int idx, std::string& why) {
    Surf s;
    const auto it = p.nodes.find(idx);
    if (it == p.nodes.end()) { why = "no surface"; return s; }
    const Node& n = it->second;
    s.sense = n.c("sense");
    switch (n.type) {
        case 50: {
            s.origin = vecf(n, "pvec"); s.axis = unit(vecf(n, "normal")); s.xaxis = unit(vecf(n, "x_axis"));
            s.yaxis = cross(s.axis, s.xaxis);
            s.surface = gtopo::Surface::plane(s.origin.x, s.origin.y, s.origin.z, s.axis.x, s.axis.y, s.axis.z);
            s.valid = true;
            break;
        }
        case 51: {
            s.origin = vecf(n, "pvec"); s.axis = unit(vecf(n, "axis")); s.xaxis = unit(vecf(n, "x_axis"));
            s.yaxis = cross(s.axis, s.xaxis);
            s.radius = n.f("radius");
            s.surface = gtopo::Surface::cylinder(s.origin.x, s.origin.y, s.origin.z, s.axis.x, s.axis.y, s.axis.z, s.radius);
            s.valid = s.radius > 0.0;
            break;
        }
        case 52: {
            s.origin = vecf(n, "pvec"); s.axis = unit(vecf(n, "axis")); s.xaxis = unit(vecf(n, "x_axis"));
            s.yaxis = cross(s.axis, s.xaxis);
            s.radius = n.f("radius");
            s.sin_half = n.f("sin_half_angle"); s.cos_half = n.f("cos_half_angle");
            // The kernel's cone is apex-based: the apex lies back along the
            // axis by radius / tan(half angle).
            const f64 tan_half = s.cos_half > 1e-12 ? s.sin_half / s.cos_half : 1.0;
            const Vec apex = s.origin - s.axis * (tan_half > 1e-12 ? s.radius / tan_half : 0.0);
            s.surface = gtopo::Surface::cone(apex.x, apex.y, apex.z, s.axis.x, s.axis.y, s.axis.z,
                                             std::atan2(s.sin_half, s.cos_half));
            s.valid = true;
            break;
        }
        case 53: {
            s.origin = vecf(n, "centre"); s.axis = unit(vecf(n, "axis")); s.xaxis = unit(vecf(n, "x_axis"));
            s.yaxis = cross(s.axis, s.xaxis);
            s.radius = n.f("radius");
            s.surface = gtopo::Surface::sphere(s.origin.x, s.origin.y, s.origin.z, s.radius);
            s.valid = s.radius > 0.0;
            break;
        }
        case 54: {
            s.origin = vecf(n, "centre"); s.axis = unit(vecf(n, "axis")); s.xaxis = unit(vecf(n, "x_axis"));
            s.yaxis = cross(s.axis, s.xaxis);
            s.radius = n.f("major_radius"); s.minor = n.f("minor_radius");
            s.surface = gtopo::Surface::torus(s.origin.x, s.origin.y, s.origin.z, s.axis.x, s.axis.y, s.axis.z, s.radius, s.minor);
            s.valid = s.radius > 0.0 && s.minor > 0.0;
            break;
        }
        case 124: {
            s.nurbs = nurbs_surface(p, n.p("nurbs"));
            if (!s.nurbs) { why = "B_SURFACE with an unreadable NURBS"; return s; }
            s.surface = gtopo::Surface::bspline(s.nurbs);
            s.valid = true;
            break;
        }
        default:
            why = std::string("surface type ") + (n.def ? n.def->name : std::to_string(n.type)) + " is not supported";
            return s;
    }
    switch (s.surface.kind) {
        case gtopo::SurfaceKind::Cylinder: case gtopo::SurfaceKind::Cone:
        case gtopo::SurfaceKind::Sphere: case gtopo::SurfaceKind::Torus:
            s.period_u = 2.0 * kPi;
            break;
        case gtopo::SurfaceKind::BSpline: {
            const f64 vm = 0.5 * (s.nurbs->v_first() + s.nurbs->v_last());
            const auto a = s.nurbs->evaluate(s.nurbs->u_first(), vm), b = s.nurbs->evaluate(s.nurbs->u_last(), vm);
            const Vec d{a.x.to_f64() - b.x.to_f64(), a.y.to_f64() - b.y.to_f64(), a.z.to_f64() - b.z.to_f64()};
            if (len(d) < 1e-9) s.period_u = s.nurbs->u_last() - s.nurbs->u_first();
            break;
        }
        default: break;
    }
    return s;
}

// Excludes both ends; `forward` follows the curve's own parameter.
bool sample_curve(const Parsed& p, int curve_idx, Vec a, Vec b, bool forward, bool ring,
                  std::vector<Vec>& out, std::string& why) {
    out.clear();
    const auto it = p.nodes.find(curve_idx);
    if (it == p.nodes.end()) return true;
    const Node& n = it->second;
    switch (n.type) {
        case 30: return true;
        case 31: case 32: {
            const Vec c = vecf(n, "centre"), nrm = unit(vecf(n, "normal")), x = unit(vecf(n, "x_axis"));
            const Vec y = cross(nrm, x);
            const f64 ra = n.type == 31 ? n.f("radius") : n.f("major_radius");
            const f64 rb = n.type == 31 ? n.f("radius") : n.f("minor_radius");
            const auto angle = [&](Vec q) {
                const Vec d = q - c;
                return std::atan2(dot(d, y) / rb, dot(d, x) / ra);
            };
            f64 t0 = ring ? 0.0 : angle(a), t1 = ring ? 2.0 * kPi : angle(b);
            if (ring) {
                if (!forward) { t0 = 2.0 * kPi; t1 = 0.0; }
            } else if (forward) {
                while (t1 <= t0 + 1e-12) t1 += 2.0 * kPi;
            } else {
                while (t1 >= t0 - 1e-12) t1 -= 2.0 * kPi;
            }
            const int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(t1 - t0) / (2.0 * kPi) * 48.0)));
            for (int i = ring ? 0 : 1; i < steps; ++i) {
                const f64 t = t0 + (t1 - t0) * i / steps;
                out.push_back(c + x * (ra * std::cos(t)) + y * (rb * std::sin(t)));
            }
            return true;
        }
        case 134: {
            auto crv = nurbs_curve(p, n.p("nurbs"), 3);
            if (!crv) { why = "B_CURVE with an unreadable NURBS"; return false; }
            const f64 ka = crv->knots.knots[static_cast<std::size_t>(crv->degree)];
            const f64 kb = crv->knots.knots[crv->control.size()];
            f64 t0 = ka, t1 = kb;
            if (!ring) {
                crv->invert(P3::from(a.x, a.y, a.z), t0);
                crv->invert(P3::from(b.x, b.y, b.z), t1);
                const bool closed = n.c("sense") != '\0' && [&] {
                    const auto s = crv->evaluate(ka), e = crv->evaluate(kb);
                    return len({s.x.to_f64() - e.x.to_f64(), s.y.to_f64() - e.y.to_f64(), s.z.to_f64() - e.z.to_f64()}) < 1e-9;
                }();
                if (forward && t1 <= t0 + 1e-12 && closed) t1 += (kb - ka);
                if (!forward && t1 >= t0 - 1e-12 && closed) t1 -= (kb - ka);
            } else if (!forward) {
                std::swap(t0, t1);
            }
            const int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(t1 - t0) / (kb - ka) * 32.0)));
            for (int i = ring ? 0 : 1; i < steps; ++i) {
                f64 t = t0 + (t1 - t0) * i / steps;
                while (t > kb) t -= (kb - ka);
                while (t < ka) t += (kb - ka);
                const auto q = crv->evaluate(t);
                out.push_back({q.x.to_f64(), q.y.to_f64(), q.z.to_f64()});
            }
            return true;
        }
        case 133: {
            return sample_curve(p, n.p("basis_curve"), vecf(n, "point_1"), vecf(n, "point_2"), forward, false, out, why);
        }
        case 38: {
            const auto ch = p.nodes.find(n.p("chart"));
            if (ch == p.nodes.end()) return true;
            const Value* hv = ch->second.get("hvec");
            if (!hv) return true;
            std::vector<Vec> pts;
            for (std::size_t i = 0; i + 2 < hv->nums.size(); i += 3) pts.push_back({hv->nums[i], hv->nums[i + 1], hv->nums[i + 2]});
            if (!forward) std::reverse(pts.begin(), pts.end());
            for (const Vec& q : pts) {
                if (len(q - a) < 1e-9 || len(q - b) < 1e-9) continue;
                out.push_back(q);
            }
            return true;
        }
        case 137: {
            std::string w;
            Surf s = make_surface(p, n.p("surface"), w);
            const auto bc = p.nodes.find(n.p("b_curve"));
            if (!s.valid || bc == p.nodes.end()) { why = "SP_CURVE without a usable surface"; return false; }
            auto crv = nurbs_curve(p, bc->second.p("nurbs"), 2);
            if (!crv) { why = "SP_CURVE with an unreadable NURBS"; return false; }
            const f64 ka = crv->knots.knots[static_cast<std::size_t>(crv->degree)];
            const f64 kb = crv->knots.knots[crv->control.size()];
            const int steps = 32;
            std::vector<Vec> pts;
            for (int i = 0; i <= steps; ++i) {
                const f64 t = ka + (kb - ka) * i / steps;
                const auto q = crv->evaluate(t);
                f64 x, y, z;
                if (!s.surface.evaluate(q.x.to_f64(), q.y.to_f64(), x, y, z)) continue;
                pts.push_back({x, y, z});
            }
            if (!forward) std::reverse(pts.begin(), pts.end());
            for (std::size_t i = 1; i + 1 < pts.size(); ++i) out.push_back(pts[i]);
            return true;
        }
        default:
            why = std::string("curve type ") + (n.def ? n.def->name : std::to_string(n.type)) + " is not supported";
            return false;
    }
}

struct FaceLoops {
    std::vector<std::vector<Vec>> loops;
};

struct BodyBuild {
    gtopo::Builder builder;
    std::vector<gtopo::Surface> surfaces;
    std::unordered_map<std::string, gtopo::VertexId> vertex_of;
    const gcm::WorkingVolume* vol{nullptr};

    gtopo::VertexId vid(Vec p) {
        const gcm::Lattice3 l = gcm::to_lattice(*vol, p.x, p.y, p.z);
        char key[80];
        std::snprintf(key, sizeof(key), "%lld,%lld,%lld", static_cast<long long>(l.x), static_cast<long long>(l.y), static_cast<long long>(l.z));
        const auto it = vertex_of.find(key);
        if (it != vertex_of.end()) return it->second;
        const gtopo::VertexId id = builder.add_vertex(l);
        vertex_of.emplace(key, id);
        return id;
    }
    void face(const std::vector<std::vector<gtopo::VertexId>>& loops, const gtopo::Surface& s) {
        builder.begin_face();
        for (const auto& l : loops) builder.add_loop(l);
        builder.end_face();
        surfaces.push_back(s);
    }
};

f64 area2(const std::vector<std::pair<f64, f64>>& poly) {
    f64 a = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const auto& p = poly[i];
        const auto& q = poly[(i + 1) % poly.size()];
        a += p.first * q.second - q.first * p.second;
    }
    return a * 0.5;
}

void dedupe(std::vector<Vec>& loop) {
    std::vector<Vec> out;
    for (const Vec& q : loop)
        if (out.empty() || len(q - out.back()) > 1e-12) out.push_back(q);
    while (out.size() > 1 && len(out.front() - out.back()) <= 1e-12) out.pop_back();
    loop = std::move(out);
}

bool oriented(const Surf& s, char face_sense, Vec a, Vec b, Vec c) {
    const Vec n = cross(b - a, c - a);
    Vec want = s.normal_at((a + b + c) * (1.0 / 3.0));
    if (face_sense == '-') want = want * -1.0;
    return dot(n, want) >= 0.0;
}

void emit_tri(BodyBuild& bb, const Surf& s, char face_sense, Vec a, Vec b, Vec c) {
    if (len(cross(b - a, c - a)) < 1e-18) return;
    if (!oriented(s, face_sense, a, b, c)) std::swap(b, c);
    const gtopo::VertexId ia = bb.vid(a), ib = bb.vid(b), ic = bb.vid(c);
    if (ia == ib || ib == ic || ia == ic) return;
    bb.face({{ia, ib, ic}}, s.surface);
}

void build_planar(BodyBuild& bb, const Surf& s, char face_sense, FaceLoops& fl) {
    std::vector<std::vector<gtopo::VertexId>> loops;
    std::vector<std::pair<f64, std::size_t>> order;
    for (std::size_t i = 0; i < fl.loops.size(); ++i) {
        std::vector<std::pair<f64, f64>> poly;
        for (const Vec& q : fl.loops[i]) { f64 u, v; s.uv(q, u, v); poly.push_back({u, v}); }
        order.push_back({-std::fabs(area2(poly)), i});
    }
    std::sort(order.begin(), order.end());
    Vec want = s.normal_at(fl.loops[order[0].second][0]);
    if (face_sense == '-') want = want * -1.0;
    for (const auto& [neg, i] : order) {
        std::vector<Vec> loop = fl.loops[i];
        // Outer loop counter-clockwise about the normal; holes the other way.
        Vec nn{0, 0, 0};
        for (std::size_t k = 0; k < loop.size(); ++k) {
            const Vec& a = loop[k];
            const Vec& b = loop[(k + 1) % loop.size()];
            nn.x += (a.y - b.y) * (a.z + b.z);
            nn.y += (a.z - b.z) * (a.x + b.x);
            nn.z += (a.x - b.x) * (a.y + b.y);
        }
        const bool ccw = dot(nn, want) > 0.0;
        const bool outer = (&order[0] == &order[std::find_if(order.begin(), order.end(), [&](auto& o) { return o.second == i; }) - order.begin()]);
        if (ccw != outer) std::reverse(loop.begin(), loop.end());
        std::vector<gtopo::VertexId> ids;
        for (const Vec& q : loop) {
            const gtopo::VertexId id = bb.vid(q);
            if (ids.empty() || ids.back() != id) ids.push_back(id);
        }
        while (ids.size() > 1 && ids.front() == ids.back()) ids.pop_back();
        if (ids.size() >= 3) loops.push_back(std::move(ids));
    }
    if (!loops.empty()) bb.face(loops, s.surface);
}

// Ring faces on periodic surfaces are zipped between loops, not triangulated.
void build_curved(BodyBuild& bb, const Surf& s, char face_sense, FaceLoops& fl, std::string& why) {
    struct UV { f64 u, v; Vec p; };
    std::vector<std::vector<UV>> loops;
    for (const auto& L : fl.loops) {
        std::vector<UV> uv;
        f64 prev_u = 0.0;
        for (std::size_t i = 0; i < L.size(); ++i) {
            f64 u, v;
            if (!s.uv(L[i], u, v)) { why = "could not parameterise a loop"; return; }
            if (s.periodic_u() && i > 0) {
                // Unwrap: stay within half a period of the previous point.
                while (u - prev_u > 0.5 * s.period_u) u -= s.period_u;
                while (u - prev_u < -0.5 * s.period_u) u += s.period_u;
            }
            prev_u = u;
            uv.push_back({u, v, L[i]});
        }
        loops.push_back(std::move(uv));
    }

    const auto span_u = [](const std::vector<UV>& l) {
        f64 lo = 1e300, hi = -1e300;
        for (const auto& q : l) { lo = std::min(lo, q.u); hi = std::max(hi, q.u); }
        return hi - lo;
    };
    bool ring = s.periodic_u() && loops.size() == 2;
    if (ring)
        for (const auto& l : loops) if (span_u(l) < 0.75 * s.period_u) ring = false;
    if (ring) {
        const f64 period = s.period_u;
        auto normalise = [period](std::vector<UV>& l) {
            for (auto& q : l) { while (q.u < 0) q.u += period; while (q.u >= period) q.u -= period; }
            std::sort(l.begin(), l.end(), [](const UV& a, const UV& b) { return a.u < b.u; });
        };
        std::vector<UV> A = loops[0], B = loops[1];
        normalise(A); normalise(B);
        std::size_t i = 0, j = 0;
        const std::size_t na = A.size(), nb = B.size();
        while (i < na || j < nb) {
            const f64 ua = i < na ? A[i].u : 1e300, ub = j < nb ? B[j].u : 1e300;
            const Vec a0 = A[i % na].p, a1 = A[(i + 1) % na].p, b0 = B[j % nb].p, b1 = B[(j + 1) % nb].p;
            if (i < na && (j >= nb || ua <= ub)) { emit_tri(bb, s, face_sense, a0, a1, b0); ++i; }
            else { emit_tri(bb, s, face_sense, b0, a0, b1); ++j; }
        }
        return;
    }

    f64 lo_u = 1e300, hi_u = -1e300, lo_v = 1e300, hi_v = -1e300;
    for (const auto& l : loops) for (const auto& q : l) {
        lo_u = std::min(lo_u, q.u); hi_u = std::max(hi_u, q.u);
        lo_v = std::min(lo_v, q.v); hi_v = std::max(hi_v, q.v);
    }
    const f64 range = std::max({hi_u - lo_u, hi_v - lo_v, 1e-9});
    const f64 scale = bb.vol->extent * 0.4 / range;
    std::vector<gcm::Lattice2> pts;
    std::vector<std::vector<gcm::u32>> idx;
    std::vector<Vec> world;
    std::vector<std::pair<f64, std::size_t>> order;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        std::vector<std::pair<f64, f64>> poly;
        for (const auto& q : loops[i]) poly.push_back({q.u, q.v});
        order.push_back({-std::fabs(area2(poly)), i});
    }
    std::sort(order.begin(), order.end());
    for (const auto& [neg, li] : order) {
        std::vector<gcm::u32> ids;
        for (const auto& q : loops[li]) {
            ids.push_back(static_cast<gcm::u32>(pts.size()));
            pts.push_back(gcm::to_lattice(*bb.vol, (q.u - lo_u - range * 0.5) * scale, (q.v - lo_v - range * 0.5) * scale));
            world.push_back(q.p);
        }
        std::vector<std::pair<f64, f64>> poly;
        for (const auto& q : loops[li]) poly.push_back({q.u, q.v});
        const bool ccw = area2(poly) > 0.0;
        const bool outer = (li == order[0].second);
        if (ccw != outer) std::reverse(ids.begin(), ids.end());
        idx.push_back(std::move(ids));
    }
    const std::vector<gcm::u32> tri = gtopo::triangulate_face(idx, pts);
    if (tri.empty()) { why = "a curved face did not triangulate"; return; }
    for (std::size_t i = 0; i + 2 < tri.size(); i += 3)
        emit_tri(bb, s, face_sense, world[tri[i]], world[tri[i + 1]], world[tri[i + 2]]);
}

void build_bodies(const Parsed& p, const gcm::WorkingVolume& vol, ReadResult& out) {
    // face.shell -> shell.region -> region.body, or via the front shell.
    std::map<int, std::vector<int>> faces_of_body;
    for (const auto& [idx, n] : p.nodes) {
        if (n.type != 14) continue;
        int shell = n.p("shell");
        if (!shell) shell = n.p("front_shell");
        const auto sh = p.nodes.find(shell);
        if (sh == p.nodes.end()) continue;
        const auto rg = p.nodes.find(sh->second.p("region"));
        const int body = rg != p.nodes.end() ? rg->second.p("body") : sh->second.p("body");
        faces_of_body[body].push_back(idx);
    }

    for (const auto& [body_idx, faces] : faces_of_body) {
        BodyBuild bb;
        bb.vol = &vol;
        for (int fi : faces) {
            const Node& face = p.nodes.at(fi);
            std::string why;
            Surf s = make_surface(p, face.p("surface"), why);
            if (!s.valid) { out.skipped.push_back("face " + std::to_string(face.p("node_id")) + ": " + why); continue; }
            const char face_sense = face.c("sense");

            FaceLoops fl;
            bool bad = false;
            for (int li = face.p("loop"); li; ) {
                const auto lit = p.nodes.find(li);
                if (lit == p.nodes.end()) break;
                const Node& loop = lit->second;
                std::vector<const Node*> fins;
                const int head = loop.p("fin");
                for (int f = head; f; ) {
                    const auto ft = p.nodes.find(f);
                    if (ft == p.nodes.end()) break;
                    fins.push_back(&ft->second);
                    f = ft->second.p("forward");
                    if (f == head || fins.size() > 100000) break;
                }
                std::vector<Vec> poly;
                for (std::size_t k = 0; k < fins.size(); ++k) {
                    const Node& fin = *fins[k];
                    const Node& prev = *fins[(k + fins.size() - 1) % fins.size()];
                    const auto vertex_point = [&](int vidx, Vec& q) {
                        const auto vt = p.nodes.find(vidx);
                        if (vt == p.nodes.end()) return false;
                        const auto pt = p.nodes.find(vt->second.p("point"));
                        if (pt == p.nodes.end()) return false;
                        q = vecf(pt->second, "pvec");
                        return true;
                    };
                    Vec a{0, 0, 0}, b{0, 0, 0};
                    const bool ring = fin.p("vertex") == 0;
                    if (!ring) {
                        if (!vertex_point(prev.p("vertex"), a) || !vertex_point(fin.p("vertex"), b)) { bad = true; break; }
                    }
                    const auto et = p.nodes.find(fin.p("edge"));
                    int curve = fin.p("curve");
                    char curve_sense = '+';
                    if (et != p.nodes.end()) {
                        if (!curve) curve = et->second.p("curve");
                    }
                    if (const auto ct = p.nodes.find(curve); ct != p.nodes.end()) curve_sense = ct->second.c("sense");
                    const bool forward = (fin.c("sense") != '-') == (curve_sense != '-');
                    std::vector<Vec> mids;
                    if (!sample_curve(p, curve, a, b, forward, ring, mids, why)) {
                        out.skipped.push_back("edge of face " + std::to_string(face.p("node_id")) + ": " + why);
                        mids.clear();
                    }
                    if (!ring) poly.push_back(a);
                    for (const Vec& m : mids) poly.push_back(m);
                }
                if (bad) break;
                dedupe(poly);
                if (poly.size() >= 3) fl.loops.push_back(std::move(poly));
                li = loop.p("next");
            }
            if (bad || fl.loops.empty()) { out.skipped.push_back("face " + std::to_string(face.p("node_id")) + ": loops could not be followed"); continue; }

            if (s.surface.kind == gtopo::SurfaceKind::Plane) build_planar(bb, s, face_sense, fl);
            else {
                build_curved(bb, s, face_sense, fl, why);
                if (!why.empty()) out.skipped.push_back("face " + std::to_string(face.p("node_id")) + ": " + why);
            }
        }
        gtopo::Body b = bb.builder.build();
        if (b.face_count() == 0) continue;
        if (bb.surfaces.size() == b.face_count()) b.face_surface = std::move(bb.surfaces);
        out.bodies.push_back(std::move(b));
    }
}

}

ReadResult read(std::string_view text, const gcm::WorkingVolume& v) {
    ReadResult r;
    Parsed p;
    std::string err;
    if (!parse(text, p, err)) { r.error = err; return r; }
    r.modeller = p.modeller;
    r.schema = p.schema;
    r.nodes = p.nodes.size();
    r.unknown_nodes = p.unknown;
    build_bodies(p, v, r);
    r.ok = true;
    return r;
}

ReadResult read_file(const std::string& path, const gcm::WorkingVolume& v) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { ReadResult r; r.error = "could not open " + path; return r; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return read(ss.str(), v);
}

std::string dump(std::string_view text, std::string* error) {
    Parsed p;
    std::string err;
    if (!parse(text, p, err)) { if (error) *error = err; return {}; }
    std::string out;
    char buf[64];
    for (const auto& [idx, n] : p.nodes) {
        std::snprintf(buf, sizeof(buf), "%d %d ", n.type, n.index);
        out += buf;
        out += n.def ? n.def->name : "?";
        for (std::size_t i = 0; i < n.values.size() && n.def && i < n.def->fields.size(); ++i) {
            out += ' ';
            out += n.def->fields[i].name;
            out += '=';
            const Value& v = n.values[i];
            if (!v.text.empty()) out += v.text;
            for (std::size_t k = 0; k < v.nums.size(); ++k) {
                if (k) out += ',';
                std::snprintf(buf, sizeof(buf), "%.17g", v.nums[k]);
                out += buf;
            }
        }
        out += '\n';
    }
    return out;
}

namespace {

// Parasolid style: shortest round-trip form, no leading zero, trailing space.
std::string num(f64 v) {
    if (std::isnan(v)) return "?";
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    std::string s(buf);
    for (int prec = 1; prec < 17; ++prec) {
        char b2[40];
        std::snprintf(b2, sizeof(b2), "%.*g", prec, v);
        if (std::strtod(b2, nullptr) == v) { s = b2; break; }
    }
    if (s.rfind("0.", 0) == 0) s.erase(0, 1);
    else if (s.rfind("-0.", 0) == 0) s.erase(1, 1);
    return s + ' ';
}
std::string vec(Vec v) { return num(v.x) + num(v.y) + num(v.z); }
std::string integer(long long v) { return std::to_string(v) + ' '; }

struct Writer {
    std::string body_text;
    int next_index{1};
    int next_node_id{1};
    int alloc() { return next_index++; }
    int node_id() { return next_node_id++; }
    void line(const std::string& s) {
        body_text += s;
        body_text += '\n';
    }
};

}

std::string write(const std::vector<gtopo::Body>& bodies, const gcm::WorkingVolume& vol,
                  const WriteOptions& o) {
    Writer w;
    for (const gtopo::Body& b : bodies) {
        const int body_i = w.alloc();
        const int list_i = w.alloc();
        const int block_i = w.alloc();
        const int void_region = w.alloc(), solid_region = w.alloc();
        const int void_shell = w.alloc(), solid_shell = w.alloc();

        std::vector<int> face_i(b.face_count()), loop_i(b.loop_count()), fin_i(b.halfedge_count());
        std::vector<int> edge_i(b.halfedge_count(), 0), curve_i(b.halfedge_count(), 0);
        std::vector<int> vertex_i(b.vertex_count()), point_i(b.vertex_count());
        std::vector<int> surf_i(b.face_count());
        for (auto& x : face_i) x = w.alloc();
        for (auto& x : loop_i) x = w.alloc();
        for (auto& x : fin_i) x = w.alloc();
        for (gcm::u32 h = 0; h < b.halfedge_count(); ++h) {
            const gcm::u32 tw = b.he_twin[h];
            if (tw != gtopo::kInvalid && tw < h) { edge_i[h] = edge_i[tw]; curve_i[h] = curve_i[tw]; continue; }
            edge_i[h] = w.alloc();
            curve_i[h] = w.alloc();
        }
        for (auto& x : vertex_i) x = w.alloc();
        for (auto& x : point_i) x = w.alloc();
        std::map<const void*, int> nurbs_nodes;
        std::vector<int> nurbs_i(b.face_count(), 0);
        for (gcm::u32 f = 0; f < b.face_count(); ++f) {
            surf_i[f] = w.alloc();
            if (o.write_exact_surfaces && b.face_surface.size() == b.face_count() &&
                b.face_surface[f].kind == gtopo::SurfaceKind::BSpline && b.face_surface[f].nurbs) {
                const void* key = b.face_surface[f].nurbs.get();
                if (!nurbs_nodes.count(key)) {
                    const int base = w.alloc();
                    w.alloc(); w.alloc(); w.alloc(); w.alloc(); w.alloc();
                    nurbs_nodes[key] = base;
                }
                nurbs_i[f] = nurbs_nodes[key];
            }
        }

        std::vector<int> fin_of_vertex(b.vertex_count(), 0);
        for (gcm::u32 h = 0; h < b.halfedge_count(); ++h) fin_of_vertex[b.he_vertex[b.he_next[h]]] = fin_i[h];
        std::vector<gcm::u32> face_of_loop(b.loop_count());
        for (gcm::u32 f = 0; f < b.face_count(); ++f)
            for (gcm::u32 l = b.face_loop_begin(f); l < b.face_loop_end(f); ++l) face_of_loop[l] = f;

        std::vector<Vec> face_normal(b.face_count(), Vec{0, 0, 1}), face_first(b.face_count(), Vec{0, 0, 0});
        std::vector<char> face_sense(b.face_count(), '+');
        for (gcm::u32 f = 0; f < b.face_count(); ++f) {
            const gcm::u32 l = b.face_loop_begin(f);
            Vec nn{0, 0, 0}, centroid{0, 0, 0};
            std::vector<Vec> pts;
            for (gcm::u32 h = b.loop_begin(l); h < b.loop_end(l); ++h) {
                f64 x, y, z;
                gcm::to_world(vol, b.vertex_point[b.he_vertex[h]], x, y, z);
                pts.push_back({x, y, z});
                centroid = centroid + Vec{x, y, z};
            }
            for (std::size_t k = 0; k < pts.size(); ++k) {
                const Vec& a = pts[k];
                const Vec& c = pts[(k + 1) % pts.size()];
                nn.x += (a.y - c.y) * (a.z + c.z);
                nn.y += (a.z - c.z) * (a.x + c.x);
                nn.z += (a.x - c.x) * (a.y + c.y);
            }
            face_normal[f] = unit(nn);
            if (!pts.empty()) face_first[f] = pts[0];
            if (nurbs_i[f]) {
                const NurbsS& ns = *b.face_surface[f].nurbs;
                centroid = centroid * (pts.empty() ? 1.0 : 1.0 / pts.size());
                f64 u, v;
                ns.invert(P3::from(centroid.x, centroid.y, centroid.z), u, v);
                const auto sn = ns.normal(u, v);
                if (dot(face_normal[f], {sn.x.to_f64(), sn.y.to_f64(), sn.z.to_f64()}) < 0.0) face_sense[f] = '-';
            }
        }

        {
            std::string s;
            s += integer(12) + integer(body_i);
            s += integer(w.next_node_id + 100000);   // highest_node_id: safely above any used
            s += integer(0) + integer(list_i);        // attributes_groups, attribute_chains
            s += integer(0) + integer(0) + integer(0) + integer(0);   // surface, curve, point, key
            s += num(1e3) + num(1e-8);                // res_size, res_linear
            s += integer(0) + integer(0) + integer(0);   // ref_instance, next, previous
            s += integer(1) + integer(0);             // state, owner
            s += integer(1) + integer(1);             // body_type: solid; nominal geometry state
            s += integer(solid_shell) + integer(0) + integer(0) + integer(0);   // shell, boundaries
            s += integer(void_region);                // region chain
            s += integer(b.halfedge_count() ? edge_i[0] : 0);
            s += integer(b.vertex_count() ? vertex_i[0] : 0);
            w.line(s);
        }
        w.line(integer(70) + integer(list_i) + integer(0) + integer(body_i) + integer(0) + integer(0) +
               integer(4) + integer(0) + integer(20) + integer(8) + integer(block_i) + integer(block_i) + integer(1) + "T");
        {
            std::string s = integer(74) + integer(20) + integer(block_i) + integer(0) + integer(0);
            for (int k = 0; k < 20; ++k) s += integer(0);
            w.line(s);
        }
        w.line(integer(19) + integer(void_region) + integer(w.node_id()) + integer(0) + integer(body_i) +
               integer(solid_region) + integer(0) + integer(void_shell) + "V");
        w.line(integer(19) + integer(solid_region) + integer(w.node_id()) + integer(0) + integer(body_i) +
               integer(0) + integer(void_region) + integer(solid_shell) + "S");
        // The solid's shell lists faces as back faces; the void's as front.
        w.line(integer(13) + integer(void_shell) + integer(w.node_id()) + integer(0) + integer(body_i) + integer(0) +
               integer(0) + integer(0) + integer(0) + integer(void_region) + integer(b.face_count() ? face_i[0] : 0));
        w.line(integer(13) + integer(solid_shell) + integer(w.node_id()) + integer(0) + integer(body_i) + integer(0) +
               integer(b.face_count() ? face_i[0] : 0) + integer(0) + integer(0) + integer(solid_region) + integer(0));

        for (gcm::u32 f = 0; f < b.face_count(); ++f) {
            const int next = f + 1 < b.face_count() ? face_i[f + 1] : 0;
            const int prev = f > 0 ? face_i[f - 1] : 0;
            std::string s = integer(14) + integer(face_i[f]) + integer(w.node_id()) + integer(0) + "?";
            s += integer(next) + integer(prev) + integer(loop_i[b.face_loop_begin(f)]) + integer(solid_shell);
            s += integer(surf_i[f]);
            s += face_sense[f];
            s += integer(0) + integer(0);             // next/previous on surface
            s += integer(next) + integer(prev) + integer(void_shell);
            w.line(s);
            for (gcm::u32 l = b.face_loop_begin(f); l < b.face_loop_end(f); ++l) {
                const int lnext = l + 1 < b.face_loop_end(f) ? loop_i[l + 1] : 0;
                w.line(integer(15) + integer(loop_i[l]) + integer(w.node_id()) + integer(0) +
                       integer(fin_i[b.loop_begin(l)]) + integer(face_i[f]) + integer(lnext));
            }
        }
        // One FIN per halfedge; the forward vertex is the destination.
        for (gcm::u32 l = 0; l < b.loop_count(); ++l) {
            for (gcm::u32 h = b.loop_begin(l); h < b.loop_end(l); ++h) {
                const gcm::u32 tw = b.he_twin[h];
                const gcm::u32 dest = b.he_vertex[b.he_next[h]];
                // The edge runs with its lower-index halfedge.
                const bool along = tw == gtopo::kInvalid || h < tw;
                std::string s = integer(17) + integer(fin_i[h]) + integer(0) + integer(loop_i[l]);
                s += integer(fin_i[b.he_next[h]]) + integer(fin_i[b.he_prev[h]]);
                s += integer(vertex_i[dest]);
                s += integer(tw == gtopo::kInvalid ? 0 : fin_i[tw]);
                s += integer(edge_i[h]) + integer(0) + integer(0);   // curve (on the edge), next_at_vx
                s += along ? "+" : "-";
                w.line(s);
            }
        }
        int prev_edge = 0;
        std::vector<int> edges_in_order;
        for (gcm::u32 h = 0; h < b.halfedge_count(); ++h) {
            const gcm::u32 tw = b.he_twin[h];
            if (tw != gtopo::kInvalid && tw < h) continue;
            edges_in_order.push_back(static_cast<int>(h));
        }
        for (std::size_t k = 0; k < edges_in_order.size(); ++k) {
            const gcm::u32 h = static_cast<gcm::u32>(edges_in_order[k]);
            const int next = k + 1 < edges_in_order.size() ? edge_i[edges_in_order[k + 1]] : 0;
            std::string s = integer(16) + integer(edge_i[h]) + integer(w.node_id()) + integer(0) + "?";
            s += integer(fin_i[h]) + integer(prev_edge) + integer(next) + integer(curve_i[h]);
            s += integer(0) + integer(0) + integer(solid_shell);
            w.line(s);
            prev_edge = edge_i[h];
            f64 ax, ay, az, bx, by, bz;
            gcm::to_world(vol, b.vertex_point[b.he_vertex[h]], ax, ay, az);
            gcm::to_world(vol, b.vertex_point[b.he_vertex[b.he_next[h]]], bx, by, bz);
            const Vec d = unit({bx - ax, by - ay, bz - az});
            w.line(integer(30) + integer(curve_i[h]) + integer(w.node_id()) + integer(0) + integer(edge_i[h]) +
                   integer(0) + integer(0) + integer(0) + "+" + vec({ax, ay, az}) + vec(d));
        }
        for (gcm::u32 vtx = 0; vtx < b.vertex_count(); ++vtx) {
            const int next = vtx + 1 < b.vertex_count() ? vertex_i[vtx + 1] : 0;
            const int prev = vtx > 0 ? vertex_i[vtx - 1] : 0;
            w.line(integer(18) + integer(vertex_i[vtx]) + integer(w.node_id()) + integer(0) + integer(fin_of_vertex[vtx]) +
                   integer(prev) + integer(next) + integer(point_i[vtx]) + "?" + integer(solid_shell));
            f64 x, y, z;
            gcm::to_world(vol, b.vertex_point[vtx], x, y, z);
            w.line(integer(29) + integer(point_i[vtx]) + integer(w.node_id()) + integer(0) + integer(vertex_i[vtx]) +
                   integer(0) + integer(0) + vec({x, y, z}));
        }
        for (gcm::u32 f = 0; f < b.face_count(); ++f) {
            if (nurbs_i[f]) {
                w.line(integer(124) + integer(surf_i[f]) + integer(w.node_id()) + integer(0) + integer(face_i[f]) +
                       integer(0) + integer(0) + integer(0) + "+" + integer(nurbs_i[f]) + integer(0));
                continue;
            }
            const Vec nn = face_normal[f];
            const Vec first = face_first[f];
            const Vec seed = std::fabs(nn.z) < 0.9 ? Vec{0, 0, 1} : Vec{1, 0, 0};
            const Vec xa = unit(cross(seed, nn));
            w.line(integer(50) + integer(surf_i[f]) + integer(w.node_id()) + integer(0) + integer(face_i[f]) +
                   integer(0) + integer(0) + integer(0) + "+" + vec(first) + vec(nn) + vec(xa));
        }
        for (const auto& [key, base] : nurbs_nodes) {
            const NurbsS* ns = static_cast<const NurbsS*>(key);
            const int verts = base + 1, umult = base + 2, vmult = base + 3, uknots = base + 4, vknots = base + 5;
            const bool rational = ns->control.size() && [&] {
                for (const auto& c : ns->control) if (std::fabs(c.w.to_f64() - 1.0) > 1e-12) return true;
                return false;
            }();
            const int dim = rational ? 4 : 3;
            auto distinct = [](const ggeo::KnotVector& k, std::vector<f64>& vals, std::vector<int>& mult) {
                for (f64 x : k.knots) {
                    if (!vals.empty() && std::fabs(vals.back() - x) < 1e-12) ++mult.back();
                    else { vals.push_back(x); mult.push_back(1); }
                }
            };
            std::vector<f64> ku, kv; std::vector<int> mu, mv;
            distinct(ns->knots_u, ku, mu);
            distinct(ns->knots_v, kv, mv);
            std::string s = integer(126) + integer(base) + "FF";
            s += integer(ns->degree_u) + integer(ns->degree_v);
            s += integer(static_cast<long long>(ns->count_u)) + integer(static_cast<long long>(ns->count_v));
            s += integer(2) + integer(2);                       // knot types: non-uniform
            s += integer(static_cast<long long>(ku.size())) + integer(static_cast<long long>(kv.size()));
            s += rational ? "T" : "F";
            s += "FF";                                          // closed u, v
            s += integer(2);                                    // surface form: arbitrary
            s += integer(dim);
            s += integer(verts) + integer(umult) + integer(vmult) + integer(uknots) + integer(vknots);
            w.line(s);
            std::string vs = integer(45) + integer(static_cast<long long>(ns->control.size() * dim)) + integer(verts);
            for (const auto& c : ns->control) {
                const f64 wgt = c.w.to_f64();
                if (rational) vs += num(c.p.x.to_f64() * wgt) + num(c.p.y.to_f64() * wgt) + num(c.p.z.to_f64() * wgt) + num(wgt);
                else vs += num(c.p.x.to_f64()) + num(c.p.y.to_f64()) + num(c.p.z.to_f64());
            }
            w.line(vs);
            std::string ms = integer(127) + integer(static_cast<long long>(mu.size())) + integer(umult);
            for (int m : mu) ms += integer(m);
            w.line(ms);
            ms = integer(127) + integer(static_cast<long long>(mv.size())) + integer(vmult);
            for (int m : mv) ms += integer(m);
            w.line(ms);
            std::string ks = integer(128) + integer(static_cast<long long>(ku.size())) + integer(uknots);
            for (f64 x : ku) ks += num(x);
            w.line(ks);
            ks = integer(128) + integer(static_cast<long long>(kv.size())) + integer(vknots);
            for (f64 x : kv) ks += num(x);
            w.line(ks);
        }
    }
    w.line("1 0 ");

    std::string out;
    out += "**ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz**************************\n";
    out += "**PARASOLID !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~0123456789**************************\n";
    out += "**PART1;MC=unknown;MC_MODEL=unknown;MC_ID=unknown;OS=unknown;OS_RELEASE=unknown;\n";
    out += "FRU=glcad;APPL=" + o.application + ";SITE=unknown;USER=unknown;FORMAT=text;GUISE=transmit;\n";
    out += "KEY=glcad;FILE=glcad.x_t;DATE=unknown;\n";
    out += "**PART2;SCH=SCH_1200000_12006;USFLD_SIZE=0;\n";
    out += "**PART3;\n";
    out += "**END_OF_HEADER*****************************************************************\n";
    const std::string version = ": TRANSMIT FILE created by modeller version 1200000";
    out += "T" + std::to_string(version.size()) + " " + version + "17 SCH_1200000_120060 \n";
    out += w.body_text;
    return out;
}

}
