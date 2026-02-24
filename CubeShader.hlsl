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
};

struct VertexIn
{
    float3 LocalPos : POSITION;
    float3 LocalNormal : NORMAL;
};

struct PixelIn
{
    float4 ClipPos : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 WorldNormal : NORMAL;
};

PixelIn VertexMain(VertexIn input)
{
    PixelIn output;

    float4 posW = mul(float4(input.LocalPos, 1.0f), gModel);
    output.WorldPos = posW.xyz;
    output.WorldNormal = normalize(mul(input.LocalNormal, (float3x3) gModel));
    output.ClipPos = mul(float4(input.LocalPos, 1.0f), gMVP);

    return output;
}

float4 PixelMain(PixelIn pxIn) : SV_TARGET
{
    float3 N = normalize(pxIn.WorldNormal);
    float3 L = normalize(-gSunDir);
    float3 V = normalize(gCameraPos - pxIn.WorldPos);

    float t = saturate(pxIn.WorldPos.y * 0.5f + 0.5f);

    float3 colTeal = float3(0.15f, 0.55f, 0.65f);
    float3 colCyan = float3(0.30f, 0.80f, 0.90f);
    float3 colCream = float3(0.95f, 0.95f, 0.85f);

    float3 baseColor;
    if (t < 0.5f)
    {
        float u = smoothstep(0.0f, 0.5f, t);
        baseColor = lerp(colTeal, colCyan, u);
    }
    else
    {
        float u = smoothstep(0.5f, 1.0f, t);
        baseColor = lerp(colCyan, colCream, u);
    }

    float ndotl = saturate(dot(N, L));
    float3 lit = (0.15f + 0.85f * ndotl);

    float3 H = normalize(L + V);
    float spec = pow(saturate(dot(N, H)), 48.0f);

    float3 color = baseColor * lit + spec.xxx * 0.30f;
    return float4(color, 1.0f);
}