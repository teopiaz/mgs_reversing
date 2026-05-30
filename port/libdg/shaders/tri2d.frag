#version 330 core
in vec2 vUV;
in vec4 vCol;
flat in uint vTPage;
flat in uint vCLUT;
flat in uint vFlags;
flat in ivec4 vClip;            // PSX drawing-area clip (x0,y0,x1,y1)

uniform usampler2D uVRAM;
uniform sampler2D  uPrevFB;     // RGBA8 capture of the previous frame's FBO
uniform int uNoTextures;
uniform float uXScale;          // matches the vert shader (1.0 / 0.8)

out vec4 oColor;

uint fetchVRAM(int x, int y) { return texelFetch(uVRAM, ivec2(x, y), 0).r; }

vec3 decodePSX(uint p) {
    return vec3(
        float(p & 0x1Fu) / 31.0,
        float((p >> 5) & 0x1Fu) / 31.0,
        float((p >> 10) & 0x1Fu) / 31.0);
}

void main() {
    /* PSX drawing-area clip (E3/E4). Recover PSX coords from gl_FragCoord
       using the inverse of the vert-shader projection. The FBO size cancels
       out via textureSize(uPrevFB) (resized in lockstep with the FBO). The
       radar uses a 69x52 clip rect so distant enemy dots / vision cones
       outside that box get dropped here. */
    vec2 fboSize = vec2(textureSize(uPrevFB, 0));
    float psx_x = 160.0 * ((2.0 * gl_FragCoord.x / fboSize.x - 1.0) / uXScale + 1.0);
    float psx_y = 224.0 * (1.0 - gl_FragCoord.y / fboSize.y);
    if (psx_x < float(vClip.x) || psx_x > float(vClip.z) + 1.0 ||
        psx_y < float(vClip.y) || psx_y > float(vClip.w) + 1.0) {
        discard;
    }
    bool textured     = (vFlags & 1u)  != 0u && (uNoTextures == 0);
    bool fb_readback  = (vFlags & 32u) != 0u && (uNoTextures == 0);
    vec3 out_rgb;
    if (fb_readback) {
        // PSX framebuffer-readback effect (NewBlur, NewBlurPure). The
        // tpage's base coords point into the displayed framebuffer; sample
        // the previous-frame capture instead of doing a CLUT lookup.
        int bx = int(vTPage & 0xFu) * 64;
        int by = int((vTPage >> 4u) & 1u) * 256;
        if ((vTPage & 0x800u) != 0u) by += 512;
        int u = int(clamp(vUV.x, 0.0, 255.0));
        int v = int(clamp(vUV.y, 0.0, 255.0));
        // The PSX double-buffer pair lives at x=[0..319] and [320..639];
        // both hold the same display surface, just at different VRAM
        // offsets. mod 320 collapses to the buffer-local x. Y is shared.
        float fx = float((bx + u) % 320) / 320.0;
        float fy = float(by + v)         / 224.0;
        // FBO row 0 is GL-bottom (the 2D VS flips Y at projection time);
        // glCopyTexSubImage2D preserved that orientation. Flip back so
        // PSX-pixel-row 0 reads the GL-top texel.
        fy = 1.0 - fy;
        vec3 tex = texture(uPrevFB, vec2(fx, fy)).rgb;
        // PSX NewBlur/NewBlurPure: tex * (vCol/128) + 50% ABR blend gives
        // a heavy ghost trail. The math is identical to PSX but on a
        // sharp digital monitor (no CRT phosphor softening / no LCD
        // response-time smearing) the trail reads as overpowering.
        // Drop the tint multiplier from the standard 2.0 (= 256/128) to
        // 1.4 so each blur pass contributes ~33% prev-frame instead of
        // ~47%, which matches the perceptual strength on PSX hardware.
        out_rgb = clamp(tex * (vCol.rgb * 1.4), 0.0, 1.0);
    } else if (textured) {
        uint tp = (vTPage >> 7u) & 3u;
        int base_x = int(vTPage & 0xFu) * 64;
        int base_y = int((vTPage >> 4u) & 1u) * 256;
        if ((vTPage & 0x800u) != 0u) base_y += 512;
        int clut_x = int(vCLUT & 0x3Fu) * 16;
        int clut_y = int((vCLUT >> 6u) & 0x1FFu);
        int u = int(clamp(vUV.x, 0.0, 255.0));
        int v = int(clamp(vUV.y, 0.0, 255.0));
        uint texel;
        if (tp == 0u) {
            uint word = fetchVRAM(base_x + (u >> 2), base_y + v);
            int idx = int((word >> uint((u & 3) * 4)) & 0xFu);
            texel = fetchVRAM(clut_x + idx, clut_y);
        } else if (tp == 1u) {
            uint word = fetchVRAM(base_x + (u >> 1), base_y + v);
            int idx = ((u & 1) == 1) ? int((word >> 8u) & 0xFFu) : int(word & 0xFFu);
            texel = fetchVRAM(clut_x + idx, clut_y);
        } else {
            texel = fetchVRAM(base_x + u, base_y + v);
        }
        if (texel == 0u) discard;   // PSX transparent
        vec3 tex = decodePSX(texel);
        out_rgb = clamp(tex * (vCol.rgb * 2.0), 0.0, 1.0);
    } else {
        out_rgb = vCol.rgb;
    }
    oColor = vec4(out_rgb, 1.0);
}
