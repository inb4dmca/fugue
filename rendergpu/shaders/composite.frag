#version 440

layout(binding = 0, std140) uniform Params {
    ivec4 placement;       // source x,y,w,h in logical image pixels
    vec4 tint;             // R,G,B,alpha multipliers, in byte scale
    ivec4 options;         // blend mode, plane surface, has matrix, Operation (see executor.cpp)
    vec4 matrixRows[5];    // input R,G,B,A rows, then the constant row
    vec4 effect;
    vec4 extra;
    ivec4 controls;        // w: fold integer black flatten into the final Blend/HudBlit output
    // Only Blur/ShadowSilhouette may read this tail: other draws pack subsequent parameters here.
    // Keep executor.cpp::paramBytes in sync if another operation starts reading kernel.
    vec4 kernel[33];       // packed Gaussian weights or disk row extents, maximum radius 64
} u;
layout(binding = 1) uniform sampler2D sourceImage;
layout(binding = 2) uniform sampler2D destinationImage;
layout(location = 0) out vec4 fragColor;

vec4 quantize(vec4 v) {
    return floor(clamp(v, 0.0, 255.0) + 0.5);
}

vec4 over(vec4 s, vec4 d) {
    float sa = s.a / 255.0;
    float da = d.a / 255.0;
    float oa = sa + da * (1.0 - sa);
    if (oa <= 0.0) return vec4(0.0);
    return quantize(vec4((s.rgb * sa + d.rgb * da * (1.0 - sa)) / oa, oa * 255.0));
}

vec4 blend(vec4 s, vec4 d) {
    int mode = u.options.x;
    bool fast = mode == 0 || mode == 1;
    bool channelwise = mode >= 2 && mode <= 4;
    // These shortcuts, including RGB cleanup at zero destination alpha, are part of pixops.cpp's contract.
    if (s.a == 0.0 && (fast || channelwise)) {
        if (fast && d.a == 0.0) d.rgb = vec3(0.0);
        return d;
    }
    if (s.a == 255.0 && fast) return s;
    if (mode == 5) {
        vec4 masked = s;
        masked.a = floor(s.a * (d.a / 255.0) + 0.5);
        vec4 o = over(masked, d);
        o.a = d.a;
        return o;
    }
    if (!channelwise) return over(s, d);
    float sa = s.a / 255.0;
    float da = d.a / 255.0;
    if (mode == 3 || mode == 4) {
        if (d.a == 0.0) return vec4(0.0);
        if (mode == 3) return quantize(vec4(d.rgb - s.rgb * sa / da, d.a));
        // A finished plane's RGB is weighted by its alpha; an ordinary sprite's is not.
        vec3 product = s.rgb * d.rgb / 255.0;
        if (u.options.y != 0 && s.a != 255.0) product *= sa;
        return quantize(vec4(d.rgb * (1.0 - sa) + product, d.a));
    }
    float oa = sa + da * (1.0 - sa);
    if (oa <= 0.0) return vec4(0.0);
    float srcTerm = (1.0 - da) * sa;
    float dstTerm = (1.0 - sa) * da;
    float both = sa * da;
    return quantize(vec4((srcTerm * s.rgb + dstTerm * d.rgb + both * (d.rgb + s.rgb)) / oa, oa * 255.0));
}

// executor.cpp::blitOver: deliberately integer, including truncation before each addition/unpremultiply.
vec4 blit16(vec4 source, vec4 destination) {
    uvec4 s = uvec4(source), d = uvec4(destination);
    if (s.a == 255u) return source;
    uint sa = s.a * 257u, da = d.a * 257u;
    uvec3 sp = s.rgb * 257u * s.a / 255u;
    uvec3 dp = d.rgb * 257u * d.a / 255u;
    uint inverse = 65535u - sa;
    uvec3 rgb = (dp * inverse / 65535u + sp) & 65535u;
    uint alpha = (da * inverse / 65535u + sa) & 65535u;
    if (alpha != 0u && alpha != 65535u) rgb = rgb * 65535u / alpha;
    return vec4(vec3(rgb >> 8u), float(alpha >> 8u));
}

// executor.cpp::blitOver over opaque black: premultiply in 16-bit integer scale, then take the high byte.
// Inputs are byte-valued, including when called immediately after blend()/blit16(): do not flatten
// an unquantized intermediate or replace this with floating-point RGB * alpha.
vec4 black(vec4 bytes) {
    uvec4 b = uvec4(bytes);
    uvec3 rgb = ((b.rgb * 257u * b.a) / 255u) >> 8u;
    return vec4(vec3(rgb), 255.0);
}

vec4 sceneOutput(vec4 bytes) {
    return (u.controls.w != 0 ? black(bytes) : bytes) / 255.0;
}

vec4 byteAt(sampler2D image, ivec2 pos) {
    if (any(lessThan(pos, ivec2(0))) || any(greaterThanEqual(pos, textureSize(image, 0)))) return vec4(0.0);
    return quantize(texelFetch(image, pos, 0) * 255.0);
}

vec4 premultiply(vec4 p) {
    float a = p.a / 255.0;
    return vec4(p.rgb * a, a);
}

vec4 linearMix(vec4 a, vec4 b, float t) { return a + (b - a) * t; }

vec4 bilinearPixel(vec2 p) {
    vec2 extent = vec2(textureSize(sourceImage, 0));
    // Check before float->int conversion: large inverse transforms can put coordinates far outside int32.
    if (any(lessThan(p, vec2(-1.0))) || any(greaterThanEqual(p, extent))) return vec4(0.0);
    ivec2 base = ivec2(floor(p));
    vec2 f = p - vec2(base);
    vec4 a = premultiply(byteAt(sourceImage, base));
    vec4 b = premultiply(byteAt(sourceImage, base + ivec2(1, 0)));
    vec4 c = premultiply(byteAt(sourceImage, base + ivec2(0, 1)));
    vec4 d = premultiply(byteAt(sourceImage, base + ivec2(1, 1)));
    vec4 pm = linearMix(linearMix(a, b, f.x), linearMix(c, d, f.x), f.y);
    if (pm.a <= 0.0) return vec4(0.0);
    vec4 result = quantize(vec4(pm.rgb / pm.a, pm.a * 255.0));
    // warpAboutCentre never writes the RGB of a sample whose quantized alpha is zero.
    return result.a == 0.0 ? vec4(0.0) : result;
}

float alphaAt(ivec2 pos) {
    if (any(lessThan(pos, ivec2(0))) || any(greaterThanEqual(pos, textureSize(sourceImage, 0)))) return 0.0;
    return texelFetch(sourceImage, pos, 0).r; // R32F, NOT a quantized RGBA8 alpha
}

float bilinearAlpha(vec2 p) {
    ivec2 base = ivec2(floor(p));
    vec2 f = p - vec2(base);
    return (1.0 - f.x) * (1.0 - f.y) * alphaAt(base)
         + f.x * (1.0 - f.y) * alphaAt(base + ivec2(1, 0))
         + (1.0 - f.x) * f.y * alphaAt(base + ivec2(0, 1))
         + f.x * f.y * alphaAt(base + ivec2(1, 1));
}

// Filter pass outputs are RGBA8 except OutlineSeed/OutlineStep, which keep the alpha field in R32F.
vec4 effectPixel(ivec2 pos) {
    int op = u.options.w;
    if (op == 4) { // Warp: inverse DM transform about the viewport centre, +DM y points up
        if (u.controls.x != 0) return vec4(0.0); // singular matrix
        float dx = float(pos.x) + 0.5 - u.extra.z - u.extra.x;
        float dy = -(float(pos.y) + 0.5 - u.extra.w - u.extra.y);
        float x = u.extra.z + (u.effect.x * dx + u.effect.y * dy) - 0.5;
        float y = u.extra.w - (u.effect.z * dx + u.effect.w * dy) - 0.5;
        return bilinearPixel(vec2(x, y)) / 255.0;
    }
    if (op == 5) { // Blur: edge-clamped, premultiplied convolution; quantize after EACH axis
        ivec2 extent = textureSize(sourceImage, 0);
        int radius = u.controls.x;
        vec4 total = vec4(0.0);
        for (int t = -radius; t <= radius; ++t) {
            ivec2 tap = clamp(pos + (u.controls.y != 0 ? ivec2(t, 0) : ivec2(0, t)), ivec2(0), extent - 1);
            int i = t + radius;
            total += premultiply(byteAt(sourceImage, tap)) * u.kernel[i / 4][i % 4];
        }
        if (total.a <= 0.0) return vec4(0.0);
        return quantize(vec4(total.rgb / total.a, total.a * 255.0)) / 255.0;
    }
    if (op == 6) { // OutlineSeed
        return vec4(byteAt(sourceImage, pos).a / 255.0, 0.0, 0.0, 1.0);
    }
    if (op == 7) { // OutlineStep: nine bilinear taps with transparent borders
        vec2 p = vec2(pos);
        float t = u.effect.x, d = u.effect.y;
        float c = alphaAt(pos);
        float s = c;
        s += bilinearAlpha(p + vec2(-t, 0)) + bilinearAlpha(p + vec2(t, 0));
        s += bilinearAlpha(p + vec2(0, -t)) + bilinearAlpha(p + vec2(0, t));
        s += bilinearAlpha(p + vec2(-d, -d)) + bilinearAlpha(p + vec2(d, -d));
        s += bilinearAlpha(p + vec2(-d, d)) + bilinearAlpha(p + vec2(d, d));
        s = u.controls.x != 0 ? (s >= 0.75 ? 1.0 : 0.0) : min(s, 1.0);
        return vec4(c + (1.0 - c) * u.effect.z * s, 0.0, 0.0, 1.0);
    }
    if (op == 8) { // OutlineFinish: colour only the grown ring, preserving the original silhouette
        vec4 original = byteAt(sourceImage, pos);
        float sa = original.a / 255.0;
        float af = texelFetch(destinationImage, pos, 0).r;
        if (af <= sa) return original / 255.0;
        vec3 pm = original.rgb / 255.0 * sa + u.tint.rgb / 255.0 * (af - sa);
        return quantize(vec4(pm / af * 255.0, af * 255.0)) / 255.0;
    }
    if (op == 9) { // ShadowSilhouette: circular max-alpha dilation before opacity and blur
        int radius = u.controls.x;
        float best = 0.0;
        for (int y = -radius; y <= radius; ++y) {
            int i = y + radius;
            int extent = int(u.kernel[i / 4][i % 4]);
            for (int x = -extent; x <= extent; ++x)
                best = max(best, byteAt(sourceImage, pos + ivec2(x, y)).a);
        }
        return vec4(u.tint.rgb, floor(best * (u.tint.a / 255.0) + 0.5)) / 255.0;
    }
    // ShadowFinish: blit the shadow over transparent, then overPx(original, shadow) WITHOUT source-alpha shortcuts.
    vec4 shadow = vec4(0.0);
    if (u.controls.x == 0) shadow = byteAt(destinationImage, pos - u.placement.xy);
    if (shadow.a == 0.0) shadow = vec4(0.0);
    return over(byteAt(sourceImage, pos), shadow) / 255.0;
}

vec4 colour(vec4 s) {
    s = quantize(s * (u.tint / 255.0));
    if (u.options.z != 0) {
        // Keep tint and matrix quantization separate, and apply the matrix even to transparent pixels.
        vec4 m = s.r * u.matrixRows[0] + s.g * u.matrixRows[1];
        m = m + s.b * u.matrixRows[2];
        m = m + s.a * u.matrixRows[3];
        m = m + 255.0 * u.matrixRows[4];
        s = quantize(m);
    }
    return s;
}

void main() {
    ivec2 pos = ivec2(gl_FragCoord.xy);
    if (u.options.w == 11) { // SpriteCopy: scissored binary EFFECTIVE alpha, destination is NOT sampled
        vec4 s = colour(quantize(texelFetch(sourceImage, pos - u.placement.xy, 0) * 255.0));
        if (s.a == 0.0) discard; // after the matrix: constants may make transparent source texels opaque
        fragColor = s / 255.0;
        return;
    }
    if (u.options.w >= 4) {
        fragColor = effectPixel(pos);
        return;
    }
    vec4 d = quantize(texelFetch(destinationImage, pos, 0) * 255.0);
    if (u.options.w == 1) {
        fragColor = black(d) / 255.0;
        return;
    }
    ivec2 sp = pos - u.placement.xy;
    if (any(lessThan(sp, ivec2(0))) || any(greaterThanEqual(sp, u.placement.zw))) {
        fragColor = sceneOutput(d);
        return;
    }
    vec4 s = quantize(texelFetch(sourceImage, sp, 0) * 255.0);
    if (u.options.w == 3) {
        fragColor = sceneOutput(blit16(s, d));
        return;
    }
    s = colour(s);
    fragColor = sceneOutput(u.options.w == 2 ? s : blend(s, d));
}
