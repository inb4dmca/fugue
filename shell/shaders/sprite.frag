#version 440
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 result;
layout(std140, binding = 0) uniform Appearance {
    mat4 matrix;
    vec4 offset;
    vec4 options;
    vec4 effect; // operation, size, padding, flags
    vec4 effectColor;
    vec4 texel;
    vec4 blurKernel[5]; // two (offset, weight) pairs per vector
    vec4 storageTexel; // physical half-texel dimensions for pooled inputs
    vec4 waves[16]; // wave/ripple: (count, bounded, -, -), then six floats per record
};
layout(binding = 1) uniform sampler2D icon;
vec4 iconAt(vec2 p) {
    if (options.x > 0.5 && options.x < 1.5 && any(lessThan(options.yz,vec2(1.0)))) {
        // A logical image occupies the origin of a possibly larger pooled
        // texture. Clamp to its own texel centers, never stale/padded storage.
        p = clamp(p*options.yz,storageTexel.xy,options.yz-storageTexel.xy);
    }
    return texture(icon,p);
}
vec4 borderAt(ivec2 p) {
    ivec2 dimensions = ivec2(round(vec2(1.0) / texel.xy));
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, dimensions))) return vec4(0.0);
    return iconAt((vec2(p) + vec2(0.5)) * texel.xy);
}
float outlineAlpha(float a) {
    // Stock byte-step alpha stored in RGBA16F must be recovered before sampling:
    // half-float approximations of n/255 otherwise accumulate across the ladder.
    return floor(a*255.0+0.5)/255.0;
}
float alphaAt(ivec2 p) { return outlineAlpha(borderAt(p).a); }
vec4 borderLinear(vec2 p) {
    ivec2 base = ivec2(floor(p)); vec2 f = fract(p);
    return mix(mix(borderAt(base), borderAt(base + ivec2(1,0)), f.x),
               mix(borderAt(base + ivec2(0,1)), borderAt(base + ivec2(1,1)), f.x), f.y);
}
float alphaLinear(vec2 pixel, vec2 delta) {
    // D3D9 readback profile: normalized-coordinate grid, then texel weights.
    // Use LOGICAL dimensions, not pooled storage or division by 1/size.
    vec2 coord = floor((pixel+delta*texel.xy)*1048576.0+0.5)/1048576.0;
    vec2 p = coord*round(vec2(1.0)/texel.xy)-vec2(0.5);
    ivec2 b = ivec2(floor(p)); vec2 f = floor(fract(p)*256.0+0.5)/256.0;
    float xy = floor(f.x*f.y*256.0+0.5)/256.0;
    // Quantize the cross weight, deriving the others to preserve unit sum.
    vec4 w = vec4(1.0-f.x-f.y+xy,f.x-xy,f.y-xy,xy);
    vec4 a = vec4(alphaAt(b),alphaAt(b+ivec2(1,0)),alphaAt(b+ivec2(0,1)),alphaAt(b+ivec2(1,1)));
    return floor(dot(w,a)*65535.0+0.5)/65535.0;
}
vec4 blurLinear(vec2 pixel) {
    // Linear sampler with transparent (not replicated) edge texels. Recover
    // coverage explicitly because QRhi's portable sampler is clamp-to-edge.
    vec2 dimensions = round(vec2(1.0)/texel.xy);
    vec2 coverage = clamp(min(pixel+vec2(1.0),dimensions-pixel),0.0,1.0);
    return iconAt((pixel+vec2(0.5))*texel.xy)*coverage.x*coverage.y;
}
float waveParam(int k) { return waves[k/4][k%4]; }
vec4 byteAt(ivec2 p) { return floor(borderAt(p)*255.0+0.5)/255.0; }
// D3D9 bilinear sample of stock's own surface (effectColor: its rect in input pixels) at normalized t, with
// 8-bit weights; outside the surface is transparent (blurSampler border) or the edge texel (WAVE_BOUNDED clamp).
vec4 stockSurfaceAt(vec2 t, bool clampEdge) {
    vec2 p = t*effectColor.zw-vec2(0.5);
    ivec2 b = ivec2(floor(p)); vec2 f = floor(fract(p)*256.0+0.5)/256.0;
    ivec2 size = ivec2(effectColor.zw), origin = ivec2(effectColor.xy);
    vec4 c[4];
    for (int i = 0; i < 4; ++i) {
        ivec2 q = b+ivec2(i%2,i/2);
        if (clampEdge) q = clamp(q,ivec2(0),size-ivec2(1));
        c[i] = any(lessThan(q,ivec2(0))) || any(greaterThanEqual(q,size)) ? vec4(0.0) : byteAt(origin+q);
    }
    return mix(mix(c[0],c[1],f.x),mix(c[2],c[3],f.x),f.y);
}
// Spreadout taps: the outline's D3D9 normalized-coordinate grid, on every channel.
vec4 rgbaLinear(vec2 pixel, vec2 delta) {
    vec2 coord = floor((pixel+delta*texel.xy)*1048576.0+0.5)/1048576.0;
    vec2 p = coord*round(vec2(1.0)/texel.xy)-vec2(0.5);
    ivec2 b = ivec2(floor(p)); vec2 f = floor(fract(p)*256.0+0.5)/256.0;
    float xy = floor(f.x*f.y*256.0+0.5)/256.0;
    vec4 w = vec4(1.0-f.x-f.y+xy,f.x-xy,f.y-xy,xy);
    return w.x*byteAt(b)+w.y*byteAt(b+ivec2(1,0))+w.z*byteAt(b+ivec2(0,1))+w.w*byteAt(b+ivec2(1,1));
}
// Measured stock effect attachment profile, not general material rounding.
vec4 stockBytes(vec4 v) {
    // D3D9 A8R8G8B8 readback also distinguishes the exact half tie (down).
    return ceil(floor(clamp(v,0.0,1.0)*4096.0)*(255.0/4096.0)-0.5)/255.0;
}
vec4 blurByte(ivec2 p, ivec2 axis, float x) {
    int b = int(floor(x)); float f = floor(fract(x)*256.0+0.5)/256.0;
    return mix(floor(borderAt(p+axis*b)*255.0+0.5),
               floor(borderAt(p+axis*(b+1))*255.0+0.5),f)/255.0;
}
void main() {
    if (effect.x > 2.5) {
        vec4 src = iconAt(uv);
        if (effect.x == 11.0 || effect.x == 12.0) {
            // Stock displaced copy / alpha-to-color seed. Allocation rounds
            // outward, but sampling retains the original fractional offset.
            vec2 dimensions = round(vec2(1.0)/texel.xy);
            vec2 p = floor(uv*texel.zw)+effect.zw;
            if (any(lessThan(p,vec2(-0.5))) || any(greaterThanEqual(p,dimensions-vec2(0.5)))) {
                result = vec4(0.0); return;
            }
            p = clamp(p,vec2(0.0),dimensions-vec2(1.0));
            ivec2 b = ivec2(floor(p));
            vec2 f = floor(fract(p)*256.0+0.5)/256.0;
            float xy = floor(f.x*f.y*256.0+0.5)/256.0;
            vec4 weights = vec4(1.0-f.x-f.y+xy,f.x-xy,f.y-xy,xy);
            vec4 sampled = vec4(0.0);
            for (int i=0;i<4;++i) {
                ivec2 at = min(b+ivec2(i%2,i/2),ivec2(dimensions)-ivec2(1));
                sampled += weights[i]*floor(borderAt(at)*255.0+0.5)/255.0;
            }
            sampled = floor(sampled*65535.0+0.5)/65535.0;
            if (effect.x == 11.0) {
                float a = sampled.a*effectColor.a;
                // Stock supplies premultiplied coefficients to its PM matrix
                // shader, which multiplies RGB by output alpha once more.
                sampled = vec4(effectColor.rgb*a*a,a);
            }
            result = stockBytes(sampled); return;
        } else if (effect.x == 15.0 || effect.x == 16.0) {
            // Stock 516.1688 wave / ripple, one pass for up to ten records. t is the
            // normalized coordinate on stock's own surface; output pixels map into the input by effect.zw.
            vec2 t = (floor(uv*texel.zw)+vec2(0.5)+effect.zw-effectColor.xy)/effectColor.zw;
            int n = int(waves[0].x);
            if (effect.x == 15.0) {
                vec2 r = t-vec2(0.5);
                for (int i = 0; i < n; ++i) {
                    int k = 4+6*i;
                    float a = (dot(r,vec2(waveParam(k),waveParam(k+1)))+waveParam(k+4))*6.28318531;
                    t += vec2(waveParam(k+2),waveParam(k+3))*sin(a);
                }
            } else {
                vec2 pix = t*effectColor.zw, o = vec2(0.0);
                for (int i = 0; i < n; ++i) {
                    int k = 4+6*i;
                    vec2 rel = pix-vec2(waveParam(k),waveParam(k+1));
                    float len = length(rel);
                    if (len == 0.0) continue;
                    float r = max(waveParam(k+4)-len/waveParam(k+2),0.0);
                    o += sin(6.28318531*r)*(rel/len)*waveParam(k+5)/(waveParam(k+3)*r*r+1.0);
                }
                t += o/effectColor.zw;
            }
            result = floor(clamp(stockSurfaceAt(t,waves[0].y > 0.5),0.0,1.0)*255.0+0.5)/255.0; return;
        } else if (effect.x == 17.0) {
            // Stock bloom threshold: keep a pixel when any straight channel exceeds the threshold.
            src = floor(src*255.0+0.5)/255.0;
            float ia = src.a > 0.0 ? 1.0/src.a : 1.0;
            vec4 t = clamp(src*vec4(ia,ia,ia,1.0)-vec4(effectColor.rgb,1.0),0.0,1.0);
            result = any(greaterThan(t,vec4(0.0))) ? src : vec4(0.0); return;
        } else if (effect.x == 18.0) {
            // Stock bloom spread on the outline's geometry: nine summed taps, renormalized by alpha.
            vec2 dimensions = round(vec2(1.0)/texel.xy);
            vec2 p = (uv*(dimensions*texel.xy)-vec2(0.5))*(vec2(1.0)+2.0*effect.z*texel.xy)+vec2(0.5);
            float t = effect.y, d = t*0.70710677;
            vec4 c = rgbaLinear(p,vec2(0)) + rgbaLinear(p,vec2(-d,-d)) + rgbaLinear(p,vec2(0,-t)) +
                rgbaLinear(p,vec2(d,-d)) + rgbaLinear(p,vec2(-t,0)) + rgbaLinear(p,vec2(t,0)) +
                rgbaLinear(p,vec2(-d,d)) + rgbaLinear(p,vec2(0,t)) + rgbaLinear(p,vec2(d,d));
            c.a += 0.00001;
            c *= clamp(c.a,0.0,1.0)/c.a;
            result = floor(clamp(c,0.0,1.0)*255.0+0.5)/255.0; return;
        } else if (effect.x == 20.0) {
            // Stock modulates the bloom glow by its alpha byte (diffuse colour aaaa) before adding it.
            result = floor(src*255.0+0.5)/255.0*effectColor; return;
        } else if (effect.x == 14.0) {
            // Recover source attachment bytes before blending repeated shadows.
            result = floor(src*255.0+0.5)/255.0; return;
        } else if (effect.x == 13.0) {
            // Blended capture uses the attachment boundary, not the shader's
            // measured float-to-UNORM profile used by the seed/blur operations.
            result = floor(clamp(src,0.0,1.0)*255.0+0.5)/255.0; return;
        } else if (effect.x > 8.5) {
            // Standalone stock cascade: at most nineteen bilinear samples per
            // axis. Output domains grow/shrink independently of the input.
            vec2 pixel = floor(uv*effectColor.xy)+effect.zw;
            vec2 axis = effect.x < 9.5 ? vec2(1,0) : vec2(0,1);
            result = blurLinear(pixel)*blurKernel[0].y;
            for (int i = 1; i < 10; ++i) {
                vec4 pair = blurKernel[i/2];
                vec2 tap = (i%2 == 0) ? pair.xy : pair.zw;
                if (tap.y == 0.0) break;
                vec2 delta = axis*tap.x;
                result += (blurLinear(pixel-delta)+blurLinear(pixel+delta))*tap.y;
            }
        } else if (effect.x < 4.5) {
            // Paired stock samples use 8-bit fractions and bytes on BOTH axes.
            ivec2 p = ivec2(floor(uv*(round(vec2(1.0)/texel.xy)+2.0*effect.z))-effect.z);
            ivec2 axis = effect.x < 3.5 ? ivec2(1,0) : ivec2(0,1);
            result = blurByte(p,axis,0.0)*blurKernel[0].y;
            for (int i = 1; i < 10; ++i) {
                vec4 pair = blurKernel[i/2];
                vec2 tap = i%2 == 0 ? pair.xy : pair.zw;
                if (tap.y == 0.0) break;
                result += (blurByte(p,axis,-tap.x)+blurByte(p,axis,tap.x))*tap.y;
            }
            result = stockBytes(result); return;
        } else if (effect.x < 5.5) {
            // D3DXSprite uses float(width * reciprocalWidth) for its UV extent;
            // the outline then expands around 0.5. Neither product cancels exactly.
            vec2 dimensions = round(vec2(1.0)/texel.xy);
            vec2 stockUv = (uv*(dimensions*texel.xy)-vec2(0.5))*
                (vec2(1.0)+2.0*effect.z*texel.xy)+vec2(0.5);
            src = blurLinear(stockUv/texel.xy-vec2(0.5));
            if (src.a > 0.0) {
                // Recover every A8R8G8B8 channel from the half-float surface.
                // Alpha-only rescaling accumulates RGB error in translucent ladders.
                src = floor(src*255.0+0.5)/255.0;
            }
            // Stock samples normalized coordinates.
            vec2 p = stockUv;
            float t = effect.y, d = mod(floor(effect.w/2.0),2.0) != 0.0 ? t : t*0.70710677;
            float a = src.a + alphaLinear(p,vec2(-t,0)) + alphaLinear(p,vec2(t,0)) +
                alphaLinear(p,vec2(0,-t)) + alphaLinear(p,vec2(0,t)) +
                alphaLinear(p,vec2(-d,-d)) + alphaLinear(p,vec2(d,-d)) +
                alphaLinear(p,vec2(-d,d)) + alphaLinear(p,vec2(d,d));
            a = mod(effect.w,2.0) != 0.0 ? (a >= 0.75 ? 1.0 : 0.0) : min(1.0,a);
            float ring = (1.0-src.a)*effectColor.a*a;
            result = src + vec4(effectColor.rgb*ring,ring);
        } else if (effect.x > 7.5) result = borderLinear(uv/texel.xy-vec2(0.5));
        else result = src;
        // The outline rounds each step; it recovers those byte values before its
        // next bilinear sample. Other passes round straight channels.
        if (effect.x == 5.0) {
            result = stockBytes(result);
        } else if (texel.z > 1.5) {
            // Plane capture boundary: byte PREMULTIPLIED channels, after blend.
            result = floor(clamp(result,0.0,1.0)*255.0+0.5)/255.0;
        } else if (texel.z > 0.5) {
            vec3 rgb = result.a > 0.0 ? result.rgb/result.a : vec3(0.0);
            vec4 bytes = clamp(vec4(rgb,result.a),0.0,1.0)*255.0;
            // Adding 0.5 can turn a just-below-half byte into an exact integer.
            // Compare the fractional part instead, preserving the stock threshold.
            vec4 rounded = (texel.w > 0.5 ? floor(bytes)+step(vec4(0.5),fract(bytes)) : floor(bytes+0.5))/255.0;
            result = vec4(rounded.rgb*rounded.a,rounded.a);
        }
        if (effect.x > 8.5 && texel.z < 0.5) {
            // Explicit nearest half conversion avoids a repeated render-target
            // truncation bias through the large-blur cascade.
            vec4 unit = exp2(max(floor(log2(max(result,vec4(0.00006103515625))))-vec4(10.0),vec4(-24.0)));
            result = floor(result/unit+vec4(0.5))*unit;
        }
        return;
    }
    // Solid coverage uses geometry, not a per-frame mask texture.
    vec4 pixel = options.x > 1.5 ? vec4(0.0, 0.0, 0.0, 1.0) : iconAt(uv);
    if (effect.x == 1.0) {
        // Stock scalar-master tint: nearest-byte premultiplied coefficient.
        result = pixel * vec4(floor(color.rgb*color.a*255.0+0.5)/255.0,color.a);
        return;
    }
    if (options.x > 0.5 && options.x < 1.5 && options.w >= 0.0)
        pixel.rgb = pixel.a > 0.0 ? pixel.rgb / pixel.a : vec3(0.0);
    // Stock discards hidden source RGB at zero alpha before applying a
    // matrix (which may itself generate opaque output). Keep partial-alpha
    // source channels straight; lighting basis textures depend on them.
    if (pixel.a == 0.0) pixel.rgb = vec3(0.0);
    vec4 tinted = clamp(matrix * pixel + offset, 0.0, 1.0) * color;
    // Contributor materials write straight RGBA bytes for the next pass.
    // Decoded master matrices write premultiplied output.
    // Attachment conversion (including ties) is not an extra shader rounding rule.
    result = options.w > 0.5 ? tinted : vec4(tinted.rgb * tinted.a, tinted.a);
}
