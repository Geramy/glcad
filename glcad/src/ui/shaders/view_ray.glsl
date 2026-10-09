// Keep ortho ray origins near the eye; z=0 unprojection loses float32 precision.
bool perspectiveView() {
    return any(notEqual(vec3(u.viewProj[0].w,u.viewProj[1].w,u.viewProj[2].w),vec3(0)));
}
void viewRay(vec2 ndc, out vec3 origin, out vec3 direction) {
    if (!perspectiveView()) {
        vec4 eyeClip = u.viewProj * vec4(u.eye.xyz,1);
        vec2 offset = ndc-eyeClip.xy/eyeClip.w;
        origin = u.eye.xyz + u.invViewProj[0].xyz*offset.x + u.invViewProj[1].xyz*offset.y;
        direction = normalize(u.invViewProj[2].xyz);
    } else {
        vec4 nearH = u.invViewProj * vec4(ndc,0,1);
        origin = u.eye.xyz;
        direction = normalize(nearH.xyz-origin*nearH.w);
    }
}
vec2 sketchCoordinates(vec2 ndc) {
    vec3 origin,direction;
    viewRay(ndc,origin,direction);
    float denominator = dot(direction,u.planeN.xyz);
    if (abs(denominator)<1e-8) return vec2(0);
    vec3 rel = origin-u.planeOrigin.xyz;
    rel -= direction*(dot(rel,u.planeN.xyz)/denominator);
    return vec2(dot(rel,u.planeU.xyz),dot(rel,u.planeV.xyz));
}
