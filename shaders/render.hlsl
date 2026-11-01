// PSMid  : in-between frame with a per-pixel confidence fallback.
// PSAA   : edge-directed anti-aliasing at source resolution.
// PSUp   : edge-adaptive Lanczos upscale in linear light (4x4 taps, anisotropic kernel, de-ringing) -> FP16 target.
// PSFinal: contrast-adaptive sharpen + vibrance + OLED emulation + dither -> back buffer.
Texture2D<float4> tA : register(t0);   // PSMid: previous frame   | PSFinal: frame to display
Texture2D<float4> tB : register(t1);   // PSMid: current frame
Texture2D<float4> tFlow : register(t2);
StructuredBuffer<uint> tStats : register(t3);
Texture2D<float4> tReverseFlow : register(t4);
SamplerState sLin : register(s0);

cbuffer CB : register(b0)
{
    float2 srcSize;
    float2 outSize;
    float2 offset;
    float scale;
    float frame;
    float sharp;
    float oledLevel;
    float oledIntensity;
    float vibrance;
    float contrast;
    float saturation;
    float brightness;
    float gamma;
    float splitPreview;
    float phase;
    float2 cbPadding;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VS(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uv;
    return o;
}

static const float3 kLuma = float3(0.299, 0.587, 0.114);

float4 PSMid(VSOut i) : SV_Target
{
    // GPU-only scene-cut / near-static fallback. Never discard localized changes.
    uint total = tStats[1];
    if (total == 0 || float(tStats[0]) / float(total) > 0.55 ||
        float(tStats[2]) / float(total) >= 0.995)
        return float4(tB.SampleLevel(sLin, i.pos.xy / srcSize, 0).rgb, 1);
    float2 pc = i.pos.xy;
    int2 ip = int2(pc);
    int2 blk = ip / 16;
    int2 maxB = (int2(srcSize) + 15) / 16 - 1;

    float4 f0 = tFlow.Load(int3(clamp(blk, int2(0, 0), maxB), 0));
    float2 v = f0.xy;

    // Local flow disagreement: large near object borders where block matching is unreliable.
    float varMax = 0;
    [unroll] for (int y = -1; y <= 1; y++)
    {
        [unroll] for (int x = -1; x <= 1; x++)
        {
            float4 fn = tFlow.Load(int3(clamp(blk + int2(x, y), int2(0, 0), maxB), 0));
            varMax = max(varMax, length(fn.xy - v));
        }
    }

    float2 uv = pc / srcSize;
    float3 a0 = tA.SampleLevel(sLin, uv, 0).rgb;
    float3 b0 = tB.SampleLevel(sLin, uv, 0).rgb;
    float t = saturate(phase);
    float3 xfade = lerp(a0, b0, t);

    // Object at current position C was at C+v in the previous frame; midpoint M = C + v/2.
    float3 pw = tA.SampleLevel(sLin, (pc + t * v) / srcSize, 0).rgb;
    float3 cw = tB.SampleLevel(sLin, (pc - (1.0 - t) * v) / srcSize, 0).rgb;
    float3 warped = lerp(pw, cw, t);
    // Sample the reverse field at the matching previous-frame location.
    int2 reverseBlock = clamp(int2((pc + v) / 16.0), int2(0,0), maxB);
    float4 reverse = tReverseFlow.Load(int3(reverseBlock, 0));
    float fbError = length(v + reverse.xy);
    float consistency = saturate(1.0 - fbError / 2.5);
    bool inBounds = all(pc + t*v >= 0) && all(pc + t*v < srcSize) &&
                    all(pc - (1.0-t)*v >= 0) && all(pc - (1.0-t)*v < srcSize);

    float d = dot(abs(pw - cw), float3(0.3333, 0.3333, 0.3333));
    float cColor = saturate(1.0 - d / 0.08);
    float cVar = saturate(1.0 - varMax / 5.0);
    float cSad = saturate(1.0 - f0.z / 0.06);
    float cFast = saturate(1.0 - (length(v) - 40.0) / 20.0);
    float conf = cColor * cVar * cSad * cFast * consistency * (inBounds ? 1.0 : 0.0);
    conf = smoothstep(0.35, 0.8, conf);

    // Low confidence -> plain cross-fade, never a warped smear.
    return float4(lerp(xfade, warped, conf), 1);
}


float Lum(float3 c)
{
    return dot(c, kLuma);
}

float3 LdSrc(int2 p)
{
    return tA.Load(int3(clamp(p, int2(0, 0), int2(srcSize) - 1), 0)).rgb;
}

// ---------------------------------------------------------------- anti-aliasing
float4 PSAA(VSOut i) : SV_Target
{
    int2 ip = int2(i.pos.xy);
    float3 c = LdSrc(ip);
    float3 n = LdSrc(ip + int2(0, -1));
    float3 s = LdSrc(ip + int2(0, 1));
    float3 e = LdSrc(ip + int2(1, 0));
    float3 w = LdSrc(ip + int2(-1, 0));
    float3 nw = LdSrc(ip + int2(-1, -1));
    float3 ne = LdSrc(ip + int2(1, -1));
    float3 sw = LdSrc(ip + int2(-1, 1));
    float3 se = LdSrc(ip + int2(1, 1));

    float lc = Lum(c), ln = Lum(n), ls = Lum(s), le = Lum(e), lw = Lum(w);
    float lnw = Lum(nw), lne = Lum(ne), lsw = Lum(sw), lse = Lum(se);
    float lmin = min(lc, min(min(ln, ls), min(le, lw)));
    float lmax = max(lc, max(max(ln, ls), max(le, lw)));
    float rng = lmax - lmin;
    if (rng < max(0.0312, lmax * 0.125)) return float4(c, 1);

    float3 box3 = (c + n + s + e + w + nw + ne + sw + se) * (1.0 / 9.0);
    float subpx = saturate(abs(Lum(box3) - lc) / rng);
    subpx = smoothstep(0.0, 1.0, subpx);
    subpx = subpx * subpx * 0.75;

    float edgeH = abs(lnw + lne - 2.0 * ln) + 2.0 * abs(lw + le - 2.0 * lc) + abs(lsw + lse - 2.0 * ls);
    float edgeV = abs(lne + lse - 2.0 * le) + 2.0 * abs(ln + ls - 2.0 * lc) + abs(lnw + lsw - 2.0 * lw);
    bool horiz = edgeH >= edgeV;
    float l1 = horiz ? ln : lw;
    float l2 = horiz ? ls : le;
    float g1 = abs(l1 - lc);
    float g2 = abs(l2 - lc);
    float stepDir = (g1 >= g2) ? -1.0 : 1.0;
    float2 stepV = horiz ? float2(0.0, stepDir) : float2(stepDir, 0.0);
    float3 side = tA.SampleLevel(sLin, (float2(ip) + 0.5 + stepV * 0.5) / srcSize, 0).rgb;
    float blend = saturate(max(g1, g2) / rng) * 0.85;
    float3 r = lerp(c, side, blend);
    r = lerp(r, box3, subpx);
    return float4(r, 1);
}

// ---------------------------------------------------------------- edge-adaptive upscale
float4 PSUp(VSOut i) : SV_Target
{
    float2 p = i.pos.xy;
    float2 sp = (p - offset) / scale;
    if (sp.x < 0 || sp.y < 0 || sp.x >= srcSize.x || sp.y >= srcSize.y) return float4(0, 0, 0, 1);

    // 1:1 -> exact copy; downscale -> hardware filter (the 4x4 kernel only suits upscaling).
    if (abs(scale - 1.0) < 0.001) return float4(tA.Load(int3(int2(sp), 0)).rgb, 1);
    if (scale < 1.0) return float4(tA.SampleLevel(sLin, sp / srcSize, 0).rgb, 1);

    float2 q = sp - 0.5;
    int2 p0 = int2(floor(q));
    float2 f = q - float2(p0);

    float3 col[16];
    float lum[16];
    [unroll] for (int j = 0; j < 4; j++)
    {
        [unroll] for (int k = 0; k < 4; k++)
        {
            float3 v = LdSrc(p0 + int2(k - 1, j - 1));
            lum[j * 4 + k] = Lum(v);
            col[j * 4 + k] = v * v;  // approximate linear light (gamma 2.0)
        }
    }

    // Structure tensor of luma, bilinearly weighted over the 4 centre pixels.
    float wt[4] = {(1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y};
    int cx[4] = {1, 2, 1, 2};
    int cy[4] = {1, 1, 2, 2};
    float jxx = 0, jyy = 0, jxy = 0;
    [unroll] for (int m = 0; m < 4; m++)
    {
        int ix = cx[m], iy = cy[m];
        float gx = lum[iy * 4 + ix + 1] - lum[iy * 4 + ix - 1];
        float gy = lum[(iy + 1) * 4 + ix] - lum[(iy - 1) * 4 + ix];
        jxx += wt[m] * gx * gx;
        jyy += wt[m] * gy * gy;
        jxy += wt[m] * gx * gy;
    }
    float tr = jxx + jyy;
    float dif = jxx - jyy;
    float len = sqrt(dif * dif + 4.0 * jxy * jxy);
    float aniso = len / (tr + 1e-5);
    float a = aniso * smoothstep(0.003, 0.05, tr);

    float c2 = dif / (len + 1e-6);
    float cs = sqrt(saturate(0.5 * (1.0 + c2)));
    float sn = sqrt(saturate(0.5 * (1.0 - c2))) * (jxy < 0.0 ? -1.0 : 1.0);
    float2 gdir = float2(cs, sn);          // across the edge
    float2 edir = float2(-sn, cs);         // along the edge
    float sAcross = 1.0 + 0.9 * a;         // narrower across the edge -> sharper
    float sAlong = 1.0 - 0.4 * a;          // longer along the edge -> smoother lines

    float3 sum = 0;
    float wsum = 0;
    [unroll] for (int y = 0; y < 4; y++)
    {
        [unroll] for (int x = 0; x < 4; x++)
        {
            float2 o = float2(x - 1, y - 1) - f;
            float ac = dot(o, gdir);
            float al = dot(o, edir);
            float dd = sqrt(ac * ac * sAcross + al * al * sAlong);
            float wk = 0.0;
            if (dd < 1e-4)
                wk = 1.0;
            else if (dd < 2.0)
                wk = 2.0 * sin(3.14159265 * dd) * sin(1.5707963 * dd) / (9.8696044 * dd * dd);
            sum += col[y * 4 + x] * wk;
            wsum += wk;
        }
    }
    float3 r = sum / max(wsum, 1e-4);

    // De-ringing: stay inside the range of the 4 nearest source pixels.
    float3 lo = min(min(col[5], col[6]), min(col[9], col[10]));
    float3 hi = max(max(col[5], col[6]), max(col[9], col[10]));
    r = clamp(r, lo, hi);
    return float4(sqrt(max(r, 0.0)), 1);
}

// ---------------------------------------------------------------- OLED / colour
static const float kBlack[4] = {0.0, 0.02, 0.04, 0.07};
static const float kS[4] = {0.0, 0.15, 0.30, 0.45};
static const float kSat[4] = {0.0, 0.08, 0.15, 0.25};
static const float kSpec[4] = {0.0, 0.03, 0.06, 0.10};
static const float kLocal[4] = {0.0, 0.10, 0.20, 0.30};

float SkinMask(float3 c)
{
    float rb = c.r - c.b;
    return step(c.g, c.r) * step(c.b, c.g) * smoothstep(0.05, 0.15, rb) * (1.0 - smoothstep(0.35, 0.6, rb));
}

float3 Oled(float3 c, float3 blur)
{
    int lv = clamp(int(oledLevel + 0.5), 0, 3);
    if (lv == 0) return c;
    float a = saturate(oledIntensity);

    float y = max(Lum(c), 1e-5);
    float m = Lum(blur);
    float yl = saturate(y + kLocal[lv] * a * (y - m));  // deepen darks next to bright areas

    float k = max(kBlack[lv] * a, 1e-4);
    float y2 = yl * yl * (1.0 + k) / (yl + k);
    float sc = kS[lv] * a;
    float y3 = y2 + (y2 * y2 * (3.0 - 2.0 * y2) - y2) * sc;
    float t = saturate((y3 - 0.75) / 0.25);
    float ss = t * t * (3.0 - 2.0 * t);
    float y4 = y3 + kSpec[lv] * a * ss * (1.0 - y3);
    c *= y4 / y;

    float g = Lum(c);
    float satK = 1.0 + kSat[lv] * a * (1.0 - 0.6 * SkinMask(c));
    c = lerp(float3(g, g, g), c, satK);
    return saturate(c);
}

float3 LdUp(int2 p)
{
    return tA.Load(int3(clamp(p, int2(0, 0), int2(outSize) - 1), 0)).rgb;
}

float4 PSFinal(VSOut i) : SV_Target
{
    float2 p = i.pos.xy;
    int2 ip = int2(p);
    if (splitPreview > 0.5 && p.x < outSize.x * 0.5)
    {
        float2 sourcePixel = (p - offset) / scale;
        if (any(sourcePixel < 0) || any(sourcePixel >= srcSize)) return float4(0,0,0,1);
        return float4(tB.SampleLevel(sLin, sourcePixel / srcSize, 0).rgb, 1);
    }
    float3 c = LdUp(ip);
    float3 n = LdUp(ip + int2(0, -1));
    float3 s = LdUp(ip + int2(0, 1));
    float3 e = LdUp(ip + int2(1, 0));
    float3 w = LdUp(ip + int2(-1, 0));

    // Contrast-adaptive sharpening: strong on soft detail, backs off where already contrasty (no halos).
    float3 mn = min(min(min(n, e), min(s, w)), c);
    float3 mx = max(max(max(n, e), max(s, w)), c);
    float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-4)));
    float3 wg = -amp * (0.2 * saturate(sharp));
    c = (c + wg * (n + e + s + w)) / (1.0 + 4.0 * wg);
    c = saturate(c);

    // Vibrance: lifts dull colours more than already-saturated ones, spares skin tones.
    float g = Lum(c);
    float chroma = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    float vk = 1.0 + 0.6 * saturate(vibrance) * (1.0 - chroma) * (1.0 - 0.7 * SkinMask(c));
    c = saturate(lerp(float3(g, g, g), c, vk));

    if (oledLevel > 0.5)
    {
        float2 ro = 1.0 / outSize;
        float3 blur = 0;
        blur += tA.SampleLevel(sLin, (p + float2(6, 0)) * ro, 0).rgb;
        blur += tA.SampleLevel(sLin, (p + float2(-6, 0)) * ro, 0).rgb;
        blur += tA.SampleLevel(sLin, (p + float2(0, 6)) * ro, 0).rgb;
        blur += tA.SampleLevel(sLin, (p + float2(0, -6)) * ro, 0).rgb;
        blur += tA.SampleLevel(sLin, (p + float2(4, 4)) * ro, 0).rgb;
        blur += tA.SampleLevel(sLin, (p + float2(-4, 4)) * ro, 0).rgb;
        blur += tA.SampleLevel(sLin, (p + float2(4, -4)) * ro, 0).rgb;
        blur += tA.SampleLevel(sLin, (p + float2(-4, -4)) * ro, 0).rgb;
        c = Oled(c, blur * 0.125);
    }

    // User colour controls, fused into the existing final pass.
    float colourLuma = Lum(c);
    c = lerp(float3(colourLuma, colourLuma, colourLuma), c, saturation);
    c = saturate((c - 0.5) * contrast + 0.5 + brightness);
    c = pow(c, 1.0 / max(gamma, 0.5));

    // Triangular dither before the 8-bit back buffer: removes banding in gradients.
    float n1 = frac(sin(dot(p + frame, float2(12.9898, 78.233))) * 43758.5453);
    float n2 = frac(sin(dot(p + frame + 17.0, float2(39.346, 11.135))) * 24634.6345);
    c += (n1 + n2 - 1.0) * (1.0 / 255.0);
    return float4(saturate(c), 1);
}
