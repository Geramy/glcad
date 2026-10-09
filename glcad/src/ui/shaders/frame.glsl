// Offsets match gcapp::FrameUniforms.
layout(set = 0, binding = 1, std140) uniform Frame {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 eye;
    vec4 viewport;      // w, h, text px range, plan
    vec4 background;
    vec4 gridMinor;
    vec4 gridMajor;
    vec4 axisX;
    vec4 axisY;
    vec4 plane;         // x0, y0, x1, y1 on the ground
    vec4 planeStyle;    // tint alpha, inner highlight width px, grid on, -
    vec4 planeOrigin;
    vec4 planeU;
    vec4 planeV;
    vec4 planeN;
    vec4 section;       // nx, ny, nz, d -- cut away n.p > d; zero normal = none
} u;
