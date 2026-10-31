// Pass 1 (PSMid): generate the in-between frame with a per-pixel confidence fallback.
// Pass 2 (PSFinal): Catmull-Rom upscale + halo-limited sharpen + OLED emulation, one pass.
Texture2D<float4> tA : register(t0);   // PSMid: previous frame   | PSFinal: frame to display
Texture2D<float4> tB : register(t1);   // PSMid: current frame
Texture2D<float4> tFlow : register(t2);
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
    float pad;
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
    float3 xfade = 0.5 * (a0 + b0);

    // Object at current position C was at C+v in the previous frame; midpoint M = C + v/2.
    float3 pw = tA.SampleLevel(sLin, (pc + 0.5 * v) / srcSize, 0).rgb;
    float3 cw = tB.SampleLevel(sLin, (pc - 0.5 * v) / srcSize, 0).rgb;
    float3 warped = 0.5 * (pw + cw);

    float d = dot(abs(pw - cw), float3(0.3333, 0.3333, 0.3333));
    float cColor = saturate(1.0 - d / 0.08);
    float cVar = saturate(1.0 - varMax / 5.0);
    float cSad = saturate(1.0 - f0.z / 0.06);
    float cFast = saturate(1.0 - (length(v) - 40.0) / 20.0);
    float conf = cColor * cVar * cSad * cFast;
    conf = smoothstep(0.35, 0.8, conf);

    // Low confidence -> plain cross-fade, never a warped smear.
    return float4(lerp(xfade, warped, conf), 1);
}

float3 CatmullRom(float2 uv)
{
    float2 samplePos = uv * srcSize;
    float2 tp1 = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - tp1;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 off12 = w2 / w12;
    float2 tp0 = (tp1 - 1.0) / srcSize;
    float2 tp3 = (tp1 + 2.0) / srcSize;
    float2 tp12 = (tp1 + off12) / srcSize;

    float3 r = 0;
    r += tA.SampleLevel(sLin, float2(tp0.x, tp0.y), 0).rgb * (w0.x * w0.y);
    r += tA.SampleLevel(sLin, float2(tp12.x, tp0.y), 0).rgb * (w12.x * w0.y);
    r += tA.SampleLevel(sLin, float2(tp3.x, tp0.y), 0).rgb * (w3.x * w0.y);
    r += tA.SampleLevel(sLin, float2(tp0.x, tp12.y), 0).rgb * (w0.x * w12.y);
    r += tA.SampleLevel(sLin, float2(tp12.x, tp12.y), 0).rgb * (w12.x * w12.y);
    r += tA.SampleLevel(sLin, float2(tp3.x, tp12.y), 0).rgb * (w3.x * w12.y);
    r += tA.SampleLevel(sLin, float2(tp0.x, tp3.y), 0).rgb * (w0.x * w3.y);
    r += tA.SampleLevel(sLin, float2(tp12.x, tp3.y), 0).rgb * (w12.x * w3.y);
    r += tA.SampleLevel(sLin, float2(tp3.x, tp3.y), 0).rgb * (w3.x * w3.y);
    return max(r, 0.0);
}

static const float kBlack[4] = {0.0, 0.02, 0.04, 0.07};
static const float kS[4] = {0.0, 0.15, 0.30, 0.45};
static const float kSat[4] = {0.0, 0.08, 0.15, 0.25};
static const float kSpec[4] = {0.0, 0.03, 0.06, 0.10};
static const float kLocal[4] = {0.0, 0.10, 0.20, 0.30};

float3 Oled(float3 c, float3 blur, float2 p)
{
    int L = clamp(int(oledLevel + 0.5), 0, 3);
    if (L == 0) return c;
    float a = saturate(oledIntensity);

    float y = max(dot(c, kLuma), 1e-5);
    float m = dot(blur, kLuma);
    // Local contrast: deepen darks next to bright areas (emulates per-zone dimming).
    float yl = saturate(y + kLocal[L] * a * (y - m));

    float k = max(kBlack[L] * a, 1e-4);
    float y2 = yl * yl * (1.0 + k) / (yl + k);
    float s = kS[L] * a;
    float y3 = y2 + (y2 * y2 * (3.0 - 2.0 * y2) - y2) * s;
    float t = saturate((y3 - 0.75) / 0.25);
    float ss = t * t * (3.0 - 2.0 * t);
    float y4 = y3 + kSpec[L] * a * ss * (1.0 - y3);

    c *= y4 / y;

    // Saturation boost, reduced on skin-like hues.
    float g = dot(c, kLuma);
    float rb = c.r - c.b;
    float skin = step(c.g, c.r) * step(c.b, c.g) * smoothstep(0.05, 0.15, rb) * (1.0 - smoothstep(0.35, 0.6, rb));
    float satK = 1.0 + kSat[L] * a * (1.0 - 0.6 * skin);
    c = lerp(float3(g, g, g), c, satK);

    // Dither dark gradients to avoid banding.
    float n1 = frac(sin(dot(p + frame, float2(12.9898, 78.233))) * 43758.5453);
    float n2 = frac(sin(dot(p + frame + 17.0, float2(39.346, 11.135))) * 24634.6345);
    c += (n1 + n2 - 1.0) * (1.0 / 255.0) * (1.0 - smoothstep(0.1, 0.35, g));
    return saturate(c);
}

float4 PSFinal(VSOut i) : SV_Target
{
    float2 p = i.pos.xy;
    float2 sp = (p - offset) / scale;
    if (sp.x < 0 || sp.y < 0 || sp.x >= srcSize.x || sp.y >= srcSize.y) return float4(0, 0, 0, 1);

    float2 uv = sp / srcSize;
    float3 c = CatmullRom(uv);

    float2 t = 1.0 / srcSize;
    float3 n = tA.SampleLevel(sLin, uv + float2(0, -t.y), 0).rgb;
    float3 e = tA.SampleLevel(sLin, uv + float2(t.x, 0), 0).rgb;
    float3 so = tA.SampleLevel(sLin, uv + float2(0, t.y), 0).rgb;
    float3 w = tA.SampleLevel(sLin, uv + float2(-t.x, 0), 0).rgb;
    float3 blur = (n + e + so + w) * 0.25;
    float3 mn = min(min(min(n, e), min(so, w)), c);
    float3 mx = max(max(max(n, e), max(so, w)), c);
    float3 sh = c + sharp * 1.2 * (c - blur);
    float lim = 0.04 * sharp;                       // halo suppression
    c = clamp(sh, mn - lim, mx + lim);
    c = saturate(c);

    c = Oled(c, blur, p);
    return float4(c, 1);
}
