float gridLine(vec2 w, vec2 fw, float spacing, float widthPx) {
    vec2 g = abs(fract(w / spacing - 0.5) - 0.5) * spacing / max(fw, vec2(1e-9));
    float d = min(g.x, g.y);
    return 1.0 - smoothstep(0.0, widthPx, d);
}
void gridAt(vec2 w, vec2 fw, out float mi, out float ma) {
    float decade = max(u.planeStyle.w, 1e-9);
    mi = gridLine(w, fw, decade, 1.0) * u.planeStyle.z;
    ma = gridLine(w, fw, decade * 10.0, 1.4) * u.planeStyle.z;
}

vec4 shadeMesh(vec3 vNormal, vec4 vColor, vec3 vView, vec3 vWorld, vec2 sheet, vec2 sheetWidth) {
    if (dot(vNormal,vNormal)<1e-12) return vColor;
    vec3 n = normalize(vNormal);
    vec3 v = normalize(vView);
    if (dot(n, v) < 0.0) n = -n;

    vec3 key  = normalize(vec3( 0.4,  0.5, 0.75));
    vec3 fill = normalize(vec3(-0.6,  0.1, 0.35));
    vec3 rim  = normalize(vec3( 0.0, -0.8, 0.20));

    float kd = max(dot(n, key), 0.0);
    float fd = max(dot(n, fill), 0.0) * 0.35;
    float rd = pow(max(dot(n, rim), 0.0), 2.0) * 0.25;

    float hemi = 0.5 + 0.5 * n.z;
    vec3 ambient = mix(vec3(0.18, 0.19, 0.22), vec3(0.34, 0.36, 0.40), hemi);

    vec3 lit = vColor.rgb * (ambient + kd * 0.75 + fd + rd);

    float fres = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    lit += vec3(0.10, 0.12, 0.15) * fres;

    if (u.planeStyle.x > 0.0 && vColor.a >= 0.99) {
        vec3 rel = vWorld - u.planeOrigin.xyz;
        float dist = dot(rel, u.planeN.xyz);
        float tol = max(1e-6, max(sheetWidth.x,sheetWidth.y)*1e-3);
        if (abs(dist) <= tol) {
            vec2 pu = sheet;
            float angleCos=cos(u.planeN.w),angleSin=sin(u.planeN.w);
            mat2 rotation=mat2(angleCos,-angleSin,angleSin,angleCos);
            vec2 lo = u.plane.xy, hi = u.plane.zw;
            if (pu.x >= lo.x && pu.x <= hi.x && pu.y >= lo.y && pu.y <= hi.y) {
                vec2 fw = max(sheetWidth,vec2(1e-9));
                float tilt = 1.0 - u.viewport.w;
                vec3 tint = mix(lit, u.gridMajor.rgb, 0.35 * u.planeStyle.x);
                float mi, ma;
                gridAt(rotation*pu,(abs(rotation[0])*fw.x+abs(rotation[1])*fw.y), mi, ma);
                tint = mix(tint, u.gridMinor.rgb, mi * 0.9);
                tint = mix(tint, u.gridMajor.rgb, ma * 0.9);
                float ax = 1.0 - smoothstep(0.0, 1.5, abs(pu.y) / max(fw.y, 1e-9));
                float ay = 1.0 - smoothstep(0.0, 1.5, abs(pu.x) / max(fw.x, 1e-9));
                tint = mix(tint, u.axisX.rgb, ax * 0.85);
                tint = mix(tint, u.axisY.rgb, ay * 0.85);
                float ex = min(pu.x - lo.x, hi.x - pu.x) / max(fw.x, 1e-9);
                float ey = min(pu.y - lo.y, hi.y - pu.y) / max(fw.y, 1e-9);
                float glow = 1.0 - smoothstep(0.0, u.planeStyle.y, min(ex, ey));
                glow *= glow;
                lit = mix(tint, u.gridMajor.rgb, 0.7 * glow * max(tilt, 0.35));
            }
        }
    }
    return vec4(lit, vColor.a);
}
