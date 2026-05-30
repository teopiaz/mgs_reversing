#version 330 core
/* Unified 2D shader -- pixel-space triangles (and lines), optional texture
 * with CLUT decode (same logic as the 3D FS). Per-vertex color modulates
 * the texel when textured, or supplies the color directly when flat. */
layout(location=0) in vec2 aPos;    // 0..320 x 0..224
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
layout(location=3) in uvec4 aTex;   // tpage, clut, flags, depth_flag
layout(location=4) in ivec4 aClip;  // PSX drawing-area clip (x0,y0,x1,y1)

uniform float uXScale;              // 1.0 in 4:3, 320/render_w in widescreen
uniform vec2 uNearFar;              // (near, far) -- matches the 3D pass

out vec2 vUV;
out vec4 vCol;
flat out uint vTPage;
flat out uint vCLUT;
flat out uint vFlags;
flat out ivec4 vClip;               // forwarded to FS for fragment-level scissor

void main() {
    /* NDC depth from the PSX OT slot encoded in aTex.w (depth_flag):
         0 / 1 -> front (z=-1), always on top (HUD/menu/skybox-bg pass);
         >=2   -> world VFX at OT slot (aTex.w-2); eye_z = slot<<8, mapped
                  through the SAME near/far curve as the 3D pass so the
                  fragment depth-tests against 3D geometry and gets
                  occluded by closer surfaces. */
    float z2d;
    if (aTex.w <= 1u) {
        z2d = -1.0;
    } else {
        float n = uNearFar.x, f = uNearFar.y;
        float Z = float((aTex.w - 2u) << 8u);
        if (Z < n) Z = n;
        float A = (f + n) / (f - n);
        float B = -2.0 * f * n / (f - n);
        z2d = (A * Z + B) / Z;
    }
    gl_Position = vec4((aPos.x / 160.0 - 1.0) * uXScale,
                       1.0 - aPos.y / 112.0,
                       z2d, 1.0);
    vUV = aUV;
    vCol = aCol;
    vTPage = aTex.x;
    vCLUT  = aTex.y;
    vFlags = aTex.z;
    vClip  = aClip;
}
