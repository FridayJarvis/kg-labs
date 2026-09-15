cbuffer FrameCB : register(b0)
{
    float4x4 gModel;
    float4x4 gViewProjection;
    float4x4 gInverseViewProjection;
    float3 gCameraPosition; float gAmbient;
    float3 gDirectionalDirection; float gDirectionalIntensity;
    float3 gDirectionalColor; float gFramePadding;
    float2 gTextureTiling;
    float2 gTextureOffset;
};

cbuffer MaterialCB : register(b1)
{
    float4 gMaterialColor;
    float gMaterialShininess;
};

Texture2D gMaterialTexture : register(t0);
Texture2D gAlbedoBuffer : register(t0);
Texture2D gNormalBuffer : register(t1);
Texture2D gDepthBuffer : register(t2);
SamplerState gLinearWrap : register(s0);

struct LocalLight
{
    float3 Position; float Radius;
    float3 Direction; float ConeCosine;
    float3 Color; float Intensity;
    int Type; float3 Padding;
};
StructuredBuffer<LocalLight> gLocalLights : register(t3);

struct GeometryInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct GeometryVaryings
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
};

GeometryVaryings GeometryVS(GeometryInput input)
{
    GeometryVaryings output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), gModel);
    output.Position = mul(worldPosition, gViewProjection);
    output.Normal = normalize(mul(input.Normal, (float3x3)gModel));
    output.TexCoord = input.TexCoord * gTextureTiling + gTextureOffset;
    return output;
}

struct GeometryTargets
{
    float4 Albedo : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

GeometryTargets GeometryPS(GeometryVaryings input)
{
    float4 texel = gMaterialTexture.Sample(gLinearWrap, input.TexCoord);
    clip(texel.a - 0.1f);
    GeometryTargets output;
    output.Albedo = float4(texel.rgb * gMaterialColor.rgb, 1.0f);
    output.Normal = float4(normalize(input.Normal), saturate(gMaterialShininess / 256.0f));
    return output;
}

struct ScreenVaryings { float4 Position : SV_POSITION; };

ScreenVaryings FullscreenVS(uint vertexId : SV_VertexID)
{
    static const float2 vertices[3] = {
        float2(-1.0f, -1.0f), float2(-1.0f, 3.0f), float2(3.0f, -1.0f)
    };
    ScreenVaryings output;
    output.Position = float4(vertices[vertexId], 0.0f, 1.0f);
    return output;
}

float3 WorldPosition(float2 pixel, float depth)
{
    uint width, height;
    gDepthBuffer.GetDimensions(width, height);
    float2 uv = (pixel + 0.5f) / float2(width, height);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 position = mul(float4(ndc, depth, 1.0f), gInverseViewProjection);
    return position.xyz / position.w;
}

float3 Brdf(float3 albedo, float3 normal, float3 position, float3 toLight,
    float3 radiance, float shininess)
{
    float3 view = normalize(gCameraPosition - position);
    float diffuse = saturate(dot(normal, toLight));
    float specular = pow(saturate(dot(normalize(view + toLight), normal)), max(shininess, 2.0f));
    return radiance * (albedo * diffuse + specular * 0.18f);
}

float4 DirectionalPS(ScreenVaryings input) : SV_TARGET
{
    int3 pixel = int3(input.Position.xy, 0);
    float4 albedo = gAlbedoBuffer.Load(pixel);
    if (albedo.a == 0.0f) discard;
    float4 packedNormal = gNormalBuffer.Load(pixel);
    float3 position = WorldPosition(input.Position.xy, gDepthBuffer.Load(pixel).r);
    float3 lightDirection = normalize(-gDirectionalDirection);
    float3 color = albedo.rgb * gAmbient;
    color += Brdf(albedo.rgb, normalize(packedNormal.xyz), position, lightDirection,
        gDirectionalColor * gDirectionalIntensity, packedNormal.a * 256.0f);
    return float4(color, 1.0f);
}

struct VolumeVaryings
{
    float4 Position : SV_POSITION;
    nointerpolation uint LightIndex : TEXCOORD0;
};

VolumeVaryings LocalLightVS(float3 unitPosition : POSITION, uint instanceId : SV_InstanceID)
{
    LocalLight light = gLocalLights[instanceId];
    VolumeVaryings output;
    output.Position = mul(float4(light.Position + unitPosition * light.Radius, 1.0f),
        gViewProjection);
    output.LightIndex = instanceId;
    return output;
}

float4 LocalLightPS(VolumeVaryings input) : SV_TARGET
{
    int3 pixel = int3(input.Position.xy, 0);
    float4 albedo = gAlbedoBuffer.Load(pixel);
    if (albedo.a == 0.0f) discard;

    LocalLight light = gLocalLights[input.LightIndex];
    float3 position = WorldPosition(input.Position.xy, gDepthBuffer.Load(pixel).r);
    float3 lightVector = light.Position - position;
    float distance = length(lightVector);
    if (distance >= light.Radius) discard;

    float3 toLight = lightVector / max(distance, 0.0001f);
    float attenuation = saturate(1.0f - distance / light.Radius);
    attenuation *= attenuation;

    if (light.Type == 1)
    {
        float cone = dot(-toLight, normalize(light.Direction));
        float softEdge = max(0.02f, (1.0f - light.ConeCosine) * 0.18f);
        attenuation *= smoothstep(light.ConeCosine, light.ConeCosine + softEdge, cone);
    }
    if (attenuation <= 0.0001f) discard;

    float4 packedNormal = gNormalBuffer.Load(pixel);
    float3 color = Brdf(albedo.rgb, normalize(packedNormal.xyz), position, toLight,
        light.Color * light.Intensity * attenuation, packedNormal.a * 256.0f);
    return float4(color, 1.0f);
}
