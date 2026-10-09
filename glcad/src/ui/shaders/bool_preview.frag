#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "view_ray.glsl"
#include "mesh_shading.glsl"
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

struct Triangle { vec4 a, edge1, edge2, normal, color; };
struct Node { vec4 lo, hi; uvec4 range; };
layout(set = 1, binding = 0, std430) readonly buffer Triangles { Triangle values[]; } triangles;
layout(set = 1, binding = 1, std430) readonly buffer Nodes { Node values[]; } nodes;
layout(push_constant) uniform Params { uvec4 counts; vec4 originEpsilon, resultLo, resultHi; } params;

bool occupied(bvec2 inside) {
    if (params.counts.z == 1u) return inside.x && !inside.y;
    if (params.counts.z == 2u) return inside.x && inside.y;
    return inside.x || inside.y;
}

bool boxHit(vec3 o, vec3 inverseDirection, bvec3 parallel, vec3 lo, vec3 hi, float after, float before) {
    // Explicit parallel slabs avoid 0 * infinity and NaNs on axis views.
    for (int k = 0; k < 3; ++k) {
        if (parallel[k]) {
            if (o[k] < lo[k] || o[k] > hi[k]) return false;
        } else {
            float a = (lo[k]-o[k])*inverseDirection[k], b = (hi[k]-o[k])*inverseDirection[k];
            after = max(after, min(a,b)); before = min(before, max(a,b));
            if (after > before) return false;
        }
    }
    return true;
}

float hitTriangle(vec3 o, vec3 d, Triangle tri) {
    vec3 e1 = tri.edge1.xyz, e2 = tri.edge2.xyz;
    vec3 h = cross(d, e2);
    float determinant = dot(e1,h);
    if (abs(determinant) <= tri.a.w) return 1e30;
    vec3 s = o-tri.a.xyz;
    float u = dot(s,h)/determinant;
    vec3 q = cross(s,e1);
    float v = dot(d,q)/determinant;
    if (u < -1e-6 || v < -1e-6 || u+v > 1.000001) return 1e30;
    return dot(tri.a.xyz-o,tri.normal.xyz)/dot(d,tri.normal.xyz);
}

void main() {
    // Derivatives must precede the divergent ray search and its discards.
    vec2 sheet = sketchCoordinates(vNdc);
    vec2 sheetWidth = fwidth(sheet);
    vec3 o,d;
    viewRay(vNdc,o,d);
    o -= params.originEpsilon.xyz;
    float eps = params.originEpsilon.w;
    bvec3 parallel = lessThan(abs(d),vec3(1e-20));
    vec3 inverseDirection = 1.0/mix(d,vec3(1),parallel);
    if (!boxHit(o,inverseDirection,parallel,params.resultLo.xyz,params.resultHi.xyz,-1e30,1e30)) discard;
    Node root = nodes.values[0];
    float after = dot((root.lo.xyz+root.hi.xyz)*0.5-o,d) - length(root.hi.xyz-root.lo.xyz) - eps*4;
    bvec2 inside = bvec2(false);
    for (uint step = 0u; step < params.counts.y; ++step) {
        vec2 nearest = vec2(1e30);
        uvec2 selected = uvec2(0);
        float eventEpsilon = eps;
        for (uint index = 0u; index < params.counts.x;) {
            Node node = nodes.values[index];
            // Slabs must include the same positional roundoff as crossings.
            if (!boxHit(o,inverseDirection,parallel,node.lo.xyz-vec3(eps),node.hi.xyz+vec3(eps),after,min(nearest.x,nearest.y)+eventEpsilon)) { index = node.range.z; continue; }
            if (node.range.y == 0u) { ++index; continue; }
            for (uint i = node.range.x; i < node.range.x+node.range.y; ++i) {
                Triangle tri = triangles.values[i];
                uint operand = uint(tri.normal.w);
                float t = hitTriangle(o,d,tri);
                if (t > after && t < nearest[operand]) {
                    nearest[operand] = t; selected[operand] = i;
                    // At grazing angles, scale positional roundoff into ray distance.
                    uint closest = nearest.x <= nearest.y ? 0u : 1u;
                    vec3 normal = triangles.values[selected[closest]].normal.xyz;
                    eventEpsilon = (eps*4)/max(abs(dot(normal,d)),1e-4);
                }
            }
            index = node.range.z;
        }
        float t = min(nearest.x,nearest.y);
        if (t >= 1e29) discard;
        bool before = occupied(inside);
        uint source = nearest.x <= nearest.y ? 0u : 1u;
        for (uint operand = 0u; operand < 2u; ++operand)
            if (nearest[operand] <= t+eventEpsilon)
                inside[operand] = dot(triangles.values[selected[operand]].normal.xyz,d) < 0.0;
        after = t+eventEpsilon;
        if (before == occupied(inside) || (perspectiveView() && t < 0.0)) continue;
        vec3 world = o+d*t+params.originEpsilon.xyz;
        if (dot(u.section.xyz,u.section.xyz) > 1e-12 && dot(u.section.xyz,world) > u.section.w) continue;
        vec4 projected = u.viewProj * vec4(world,1);
        float depth = projected.z/projected.w;
        if (depth < 0.0 || depth > 1.0) continue;
        Triangle surface = triangles.values[selected[source]];
        vec3 normal = surface.normal.xyz;
        if (source == 1u && params.counts.z == 1u) normal = -normal;
        gl_FragDepth = depth;
        outColor = shadeMesh(normal,surface.color,u.eye.xyz-world,world,sheet,sheetWidth);
        return;
    }
    discard;
}
