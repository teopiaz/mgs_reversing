#version 330 core
in vec2 vUV;
in vec4 vCol;
in vec3 vNormal;
flat in uint vTPage;
flat in uint vCLUT;
flat in uint vFlags;
flat in mat3 vLightDir;
flat in mat3 vLightColor;
flat in vec3 vAmbient;

uniform usampler2D uVRAM;
uniform sampler2D  uPrevFB;     // captured previous frame (Stealth)
uniform int uNoTextures;    // debug: 1 => skip texture sample
uniform int uFaceId;        // debug: 1 => color tri by gl_PrimitiveID
uniform int uShowNormals;   // debug: 1 => output normal.xyz*0.5+0.5

out vec4 oColor;

uint fetchVRAM(int x, int y) { return texelFetch(uVRAM, ivec2(x, y), 0).r; }

vec3 decodePSX(uint p) {
    return vec3(
        float(p & 0x1Fu) / 31.0,
        float((p >> 5) & 0x1Fu) / 31.0,
        float((p >> 10) & 0x1Fu) / 31.0);
}

// Cheap hash -> RGB. Used by the face-ID debug visualisation.
vec3 hashId(int i) {
    uint u = uint(i) * 2654435761u;
    return vec3(
        float((u      ) & 0xFFu) / 255.0,
        float((u >>  8) & 0xFFu) / 255.0,
        float((u >> 16) & 0xFFu) / 255.0);
}

void main() {
    // Debug overrides take precedence over the normal lit path.
    if (uShowNormals != 0) {
        vec3 n = normalize(vNormal);
        oColor = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }
    if (uFaceId != 0) {
        oColor = vec4(hashId(gl_PrimitiveID), 1.0);
        return;
    }
    vec3 tex = vec3(1.0);
    bool textured    = (vFlags & 1u)  != 0u && (uNoTextures == 0);
    bool fb_readback = (vFlags & 32u) != 0u && (uNoTextures == 0);
    bool per_pixel   = (vFlags & 16u) != 0u;
    if (fb_readback) {
        // Stealth / Optical Camo. The 3D run-batcher snapshots the FBO
        // into uPrevFB right before this run draws (after level geo,
        // before Snake) so we sample the scene-without-Snake -- no
        // accumulation. PSX kogaku2 uses a (3/4)*x + 160 horizontal
        // remap that compresses the sampled UV toward screen centre,
        // producing the iconic 'lensed' refraction. Replicated here
        // by squeezing uv.x by 25% around the centre.
        vec2 ts = vec2(textureSize(uPrevFB, 0));
        vec2 uv = gl_FragCoord.xy / ts;
        uv.x = 0.5 + (uv.x - 0.5) * 0.75;
        vec3 prev = texture(uPrevFB, uv).rgb;
        vec3 tint = vCol.rgb * 2.0;   // 128 = neutral, SNAKE_COLOR = 1.0,1.25,0.75
        oColor = vec4(clamp(prev * tint, 0.0, 1.0), 1.0);
        return;
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
        if (texel == 0u) discard;
        tex = decodePSX(texel);
    }

    // Pick the shading source. The Gouraud vertex color vCol is linear-
    // interpolated across the triangle; per-pixel recomputes the PSX NCS
    // formula at every fragment using the interpolated normal.
    //   PSX GTE:  IR  = LightDir * Normal       (clamp >= 0)
    //             RGB = LightColor * IR + Ambient
    // Both matrices and normals are 4.12 fixed-point (/4096 in our floats).
    // The Gouraud branch supplies the caller-computed shade pipeline result
    // in vCol, normalized so 0.5 (=128/255) is neutral-bright.
    vec3 shade;
    if (per_pixel) {
        vec3 n = normalize(vNormal);
        // PSX MATRIX is row-major (shorts[0..8] = m[0][0..2], m[1][0..2], ...)
        // while GLSL mat3 is column-major, so our uploaded mat3 holds the
        // transpose of the PSX matrix. To compute PSX_mat * v we use
        // `v * M` (left-multiply by row vector), which in GLSL equals
        // (M^T * v). Applied to our transposed M that recovers PSX_mat*v.
        vec3 ir = max(n * vLightDir, 0.0);
        // Guaranteed floor so back-facing fragments aren't pitch-black when
        // an object has no DG_FLAG_AMBIENT (typical for prop geometry).
        vec3 amb = max(vAmbient, vec3(0.15));
        shade = clamp(ir * vLightColor + amb, 0.0, 1.0);
    } else {
        // Gouraud: match the software path (tex * vCol * 2, neutral at 0.5)
        shade = vCol.rgb * 2.0;
    }
    bool sampled = textured || fb_readback;
    vec3 rgb = sampled ? tex * shade : (per_pixel ? shade : vCol.rgb);
    oColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
