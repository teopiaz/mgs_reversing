#version 330 core
/* 3D pass. Takes eye-space vertices + per-triangle projection distance and
 * produces clip-space positions directly; the GPU does perspective divide and
 * perspective-correct interpolation of UV and vertex color. */
layout(location=0)  in vec3  aPos;
layout(location=1)  in float aDist;
layout(location=2)  in vec2  aUV;
layout(location=3)  in float aFaceZ;
layout(location=4)  in vec4  aCol;
layout(location=5)  in uvec4 aTex;         // tpage, clut, flags, _pad
layout(location=6)  in vec3  aNormal;      // per-vertex normal (4.12 / 4096)
layout(location=7)  in mat3  aLightDir;    // takes slots 7,8,9
layout(location=10) in mat3  aLightColor;  // slots 10,11,12
layout(location=13) in vec3  aAmbient;

out vec2 vUV;
out vec4 vCol;
out vec3 vNormal;                   // smooth-interpolated across tri
flat out uint vTPage;
flat out uint vCLUT;
flat out uint vFlags;
flat out mat3 vLightDir;            // constant per tri
flat out mat3 vLightColor;
flat out vec3 vAmbient;

uniform vec2 uHalfScreen;           // (160, 112)
uniform vec2 uNearFar;              // (near, far)
uniform int  uOrtho;                // 0 = perspective, 1 = orthographic
uniform vec4 uOrthoLRBT;            // (left, right, bottom, top) in eye-space units

void main() {
  if (uOrtho != 0) {
    /* Orthographic: aPos is already eye-space; map LRBT to NDC linearly.
       Y is flipped (PSX +Y down convention shared with the persp path). */
    float l = uOrthoLRBT.x, r = uOrthoLRBT.y;
    float b = uOrthoLRBT.z, t = uOrthoLRBT.w;
    float nx = (aPos.x - l) / (r - l) * 2.0 - 1.0;
    float ny = (aPos.y - b) / (t - b) * 2.0 - 1.0;
    /* Squash Z into [-1,1] using uNearFar. eye Z >= 0 means in front. */
    float n  = uNearFar.x, f = uNearFar.y;
    float nz = (aPos.z - n) / (f - n) * 2.0 - 1.0;
    gl_Position = vec4(nx, -ny, nz, 1.0);
  } else {
    /* Clip-space w = true eye-space z (NOT clamped). This lets the GPU's
       near-plane clipping discard/clip geometry at or behind the camera.
       The old 'if (cz < 4) cz = 4' floored w>=4, so behind-camera verts
       (eye z < 0) were pulled in front and splattered across the view in
       first-person. Depth still uses the face centroid (z_ndc) so painter
       ordering is unchanged; only verts with eye z < 4 (very close or
       behind) change behaviour -- far geometry (z >= 4) is identical. */
    float fz = aFaceZ;
    if (fz < 4.0) fz = 4.0;
    float n = uNearFar.x, f = uNearFar.y;
    float A = (f + n) / (f - n);
    float B = -2.0 * f * n / (f - n);
    float z_ndc = (A * fz + B) / fz;
    float w = aPos.z;
    gl_Position = vec4(
        aPos.x * aDist / uHalfScreen.x,
       -aPos.y * aDist / uHalfScreen.y,
        z_ndc * w,
        w);
  }
    vUV         = aUV;
    vCol        = aCol;
    vTPage      = aTex.x;
    vCLUT       = aTex.y;
    vFlags      = aTex.z;
    vNormal     = aNormal;
    vLightDir   = aLightDir;
    vLightColor = aLightColor;
    vAmbient    = aAmbient;
}
