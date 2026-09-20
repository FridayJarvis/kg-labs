cbuffer SceneCB : register(b0)
{
    float4x4 gModel;
    float4x4 gMVP;

    float3 gCameraPos;
    float _p0;
    float3 gSunDir;
    float _p1;

    float4 gAmbientColor;
    float4 gDiffuseColor;
    float4 gSpecularColor;
    float gShininess;
    float3 _p2;

    float2 gTextureTiling;
    float2 gTextureOffset;

    float gTime;          // 0..2*PI, periodic (used for Voronoi animation)
    float gVoronoiScale;  // cells per unit on the sphere
    float gVoronoiEdge;   // border width in cell-distance units
    float _p3;
};

cbuffer MaterialCB : register(b1)
{
    float4 gMaterialDiffuse;
    float gMaterialShininess;
    float gMaterialMode;   // 0 = texture from disk, 1 = procedural Voronoi
};

Texture2D gDiffuseMap : register(t0);
SamplerState gTextureSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
//  Procedural 3D Voronoi (Worley) texture
// ─────────────────────────────────────────────────────────────────────────────
float3 Hash33(float3 p)
{
    p = float3(dot(p, float3(127.1f, 311.7f,  74.7f)),
               dot(p, float3(269.5f, 183.3f, 246.1f)),
               dot(p, float3(113.5f, 271.9f, 124.6f)));
    return frac(sin(p) * 43758.5453123f);
}

// x = F1 (distance to nearest feature point), y = F2 (to second nearest),
// z = random id of the nearest cell in [0,1)
float3 Voronoi(float3 p, float t)
{
    float3 ip = floor(p);
    float3 fp = frac(p);

    float f1 = 8.0f, f2 = 8.0f, id = 0.0f;

    [unroll]
    for (int k = -1; k <= 1; ++k)
    [unroll]
    for (int j = -1; j <= 1; ++j)
    [unroll]
    for (int i = -1; i <= 1; ++i)
    {
        float3 g = float3(i, j, k);
        float3 h = Hash33(ip + g);
        // feature point wanders inside its cell; period 2*PI keeps the loop seamless
        float3 o = 0.5f + 0.5f * sin(t + 6.2831853f * h);
        float3 d = g + o - fp;
        float dist = dot(d, d);

        if (dist < f1)
        {
            f2 = f1;
            f1 = dist;
            id = h.x;
        }
        else if (dist < f2)
        {
            f2 = dist;
        }
    }
    return float3(sqrt(f1), sqrt(f2), id);
}

// Cosine palette: smooth, saturated colours from a scalar
float3 Palette(float t)
{
    return 0.50f + 0.50f * cos(6.2831853f * (float3(1.0f, 1.0f, 1.0f) * t + float3(0.00f, 0.10f, 0.20f)));
}

float3 VoronoiColor(float3 N)
{
    // The unit normal of a sphere is a perfect seamless 3D domain (no UV seams / pole pinching)
    float3 v = Voronoi(N * gVoronoiScale, gTime);

    float3 cell = lerp(Palette(v.z * 0.85f + 0.55f), float3(1, 1, 1), 0.15f);

    // Border: F2-F1 -> 0 exactly on the boundary between two cells
    float edge = smoothstep(0.0f, gVoronoiEdge, v.y - v.x);

    // Slight darkening towards the cell border + bright glowing seam
    float3 body = cell * (0.55f + 0.45f * saturate(v.x * 1.4f));
    float3 seam = float3(0.05f, 0.05f, 0.08f);
    return lerp(seam, body, edge);
}

struct VertexIn
{
    float3 LocalPos : POSITION;
    float3 LocalNormal : NORMAL;
    float2 TexC : TEXCOORD;
};

struct PixelIn
{
    float4 ClipPos : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 WorldNormal : NORMAL;
    float2 TexC : TEXCOORD;
};

PixelIn VertexMain(VertexIn input)
{
    PixelIn output;

    float4 posW = mul(float4(input.LocalPos, 1.0f), gModel);
    output.WorldPos = posW.xyz;
    output.WorldNormal = normalize(mul(input.LocalNormal, (float3x3) gModel));
    output.ClipPos = mul(float4(input.LocalPos, 1.0f), gMVP);
    output.TexC = input.TexC * gTextureTiling + gTextureOffset;

    return output;
}

float4 PixelMain(PixelIn pxIn) : SV_TARGET
{
    float3 N = normalize(pxIn.WorldNormal);
    float3 L = normalize(-gSunDir);
    float3 V = normalize(gCameraPos - pxIn.WorldPos);

    float4 texel = gDiffuseMap.Sample(gTextureSampler, pxIn.TexC);
    clip(texel.a - 0.1f);
    float3 baseColor = texel.rgb * gMaterialDiffuse.rgb;

    if (gMaterialMode > 0.5f)
    {
        baseColor = VoronoiColor(N);
        texel.a = 1.0f;
    }

    float ndotl = saturate(dot(N, L));
    float3 lit = (0.15f + 0.85f * ndotl);

    float3 H = normalize(L + V);
    float spec = pow(saturate(dot(N, H)), max(gMaterialShininess, 1.0f));

    float3 color = baseColor * lit + spec.xxx * 0.30f;
    float alpha = (gMaterialMode > 0.5f) ? 1.0f : texel.a * gMaterialDiffuse.a;
    return float4(color, alpha);
}
