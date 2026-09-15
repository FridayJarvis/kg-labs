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
};

cbuffer MaterialCB : register(b1)
{
    float4 gMaterialDiffuse;
    float gMaterialShininess;
};

Texture2D gDiffuseMap : register(t0);
SamplerState gTextureSampler : register(s0);

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

    float ndotl = saturate(dot(N, L));
    float3 lit = (0.15f + 0.85f * ndotl);

    float3 H = normalize(L + V);
    float spec = pow(saturate(dot(N, H)), max(gMaterialShininess, 1.0f));

    float3 color = baseColor * lit + spec.xxx * 0.30f;
    return float4(color, texel.a * gMaterialDiffuse.a);
}
