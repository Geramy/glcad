// Clip at the near plane before division to avoid unbounded triangles.
void stroke(vec3 start, vec3 end, float width, int corner,
            out vec4 position, out vec3 world) {
    vec4 a=u.viewProj*vec4(start,1), b=u.viewProj*vec4(end,1);
    if (a.z<0 && b.z<0) { position=vec4(0,0,-1,1); world=start; return; }
    if (a.z<0) { float t=-a.z/(b.z-a.z); a=mix(a,b,t); start=mix(start,end,t); }
    if (b.z<0) { float t=-b.z/(a.z-b.z); b=mix(b,a,t); end=mix(end,start,t); }
    if (a.w<=1e-8 || b.w<=1e-8) { position=vec4(0,0,-1,1); world=start; return; }
    vec2 delta=(b.xy/b.w-a.xy/a.w)*u.viewport.xy;
    float len=length(delta);
    vec2 tangent=len>1e-6 ? delta/len : vec2(1,0);
    vec2 normal=vec2(-tangent.y,tangent.x);
    const vec2 corners[6]=vec2[6](vec2(0,-1),vec2(1,-1),vec2(1,1),vec2(0,-1),vec2(1,1),vec2(0,1));
    vec2 c=corners[corner];
    position=mix(a,b,c.x); world=mix(start,end,c.x);
    vec2 offset=(normal*c.y+tangent*(c.x*2-1))*max(width,1.0)/u.viewport.xy;
    position.xy+=offset*position.w;
    position.z-=2e-4*position.w;
}
