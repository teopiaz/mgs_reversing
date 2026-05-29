#version 330 core
/* Samples the R16UI VRAM texture, decodes PSX pixel:
 *   bit 15 = semi-trans (ignored here -- pass through as opaque)
 *   bits 10-14 = B, 5-9 = G, 0-4 = R  (little-endian halfword)
 * In overlay mode (uDiscardZero != 0) pixel value 0x0000 is discarded so the
 * GL-rendered 3D scene shows through wherever the game has not drawn a 2D
 * primitive this frame.
 */
in vec2 vUV;
uniform usampler2D uVRAM;
uniform vec4       uRegion;       // (x, y, w, h) in VRAM texels
uniform int        uDiscardZero;  // 1 => PSX 0x0000 is transparent; 0 => opaque black
out vec4 oColor;
void main() {
    vec2 uv = vUV;
    if (uv.x > 1.0 || uv.y > 1.0) discard;
    uv.y = 1.0 - uv.y;   // VRAM row 0 is image top; GL screen origin is bottom
    ivec2 tc = ivec2(uRegion.xy + uv * uRegion.zw);
    uint px = texelFetch(uVRAM, tc, 0).r;
    if (uDiscardZero != 0 && px == 0u) discard;
    float r = float(px & 0x1Fu) / 31.0;
    float g = float((px >> 5) & 0x1Fu) / 31.0;
    float b = float((px >> 10) & 0x1Fu) / 31.0;
    oColor = vec4(r, g, b, 1.0);
}
