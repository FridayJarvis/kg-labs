struct Particle
{
    float3 Position;
    float Age;
    float3 Velocity;
    float Lifetime;
    float4 Color;
    float Size;
    float OrbitRadius;
    uint Seed;
    float Padding;
};

cbuffer ParticleConstants : register(b0)
{
    float4x4 gViewProjection;
    float3 gCameraPosition;
    float gDeltaTime;
    float gTotalTime;
    float3 gEmitterPosition;
};

ConsumeStructuredBuffer<Particle> gConsumeParticles : register(u0);
AppendStructuredBuffer<Particle> gAppendParticles : register(u1);
AppendStructuredBuffer<Particle> gInitialParticles : register(u2);
RWByteAddressBuffer gCounter : register(u3);
StructuredBuffer<Particle> gDrawParticles : register(t0);

static const uint kParticleCount = 2048;
static const float kTwoPi = 6.28318530718f;

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float Random01(inout uint state)
{
    state = Hash(state);
    return (state & 0x00ffffffu) / 16777216.0f;
}

float3 VortexColor(float phase)
{
    const float3 cyan = float3(0.08f, 0.92f, 1.0f);
    const float3 violet = float3(0.72f, 0.18f, 1.0f);
    return lerp(cyan, violet, phase);
}

Particle SpawnParticle(uint particleIndex, float ageFraction, uint seed)
{
    uint randomState = Hash(seed + particleIndex * 747796405u);
    const float phase = Random01(randomState);
    const float angle = kTwoPi * (phase + ageFraction * 2.4f);
    const float radius = lerp(0.55f, 2.35f, Random01(randomState));
    const float lifetime = lerp(5.0f, 9.0f, Random01(randomState));
    const float age = lifetime * ageFraction;
    const float rise = ageFraction * lerp(4.5f, 7.0f, Random01(randomState));
    const float3 radial = float3(cos(angle), 0.0f, sin(angle));
    const float3 tangent = float3(-radial.z, 0.0f, radial.x);

    Particle particle;
    particle.Position = gEmitterPosition + radial * radius +
        float3(0.0f, rise, 0.0f);
    particle.Age = age;
    particle.Velocity = tangent * lerp(1.2f, 2.8f, Random01(randomState)) +
        float3(0.0f, lerp(0.55f, 1.15f, Random01(randomState)), 0.0f);
    particle.Lifetime = lifetime;
    particle.Color = float4(VortexColor(phase), 1.0f);
    particle.Size = lerp(0.10f, 0.28f, Random01(randomState));
    particle.OrbitRadius = radius;
    particle.Seed = randomState;
    particle.Padding = 0.0f;
    return particle;
}

[numthreads(1, 1, 1)]
void ResetCounterCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gCounter.Store(0, 0u);
}

[numthreads(64, 1, 1)]
void InitializeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint particleIndex = dispatchThreadId.x;
    if (particleIndex >= kParticleCount)
        return;

    const float ageFraction = (particleIndex + 0.5f) / kParticleCount;
    gInitialParticles.Append(SpawnParticle(particleIndex, ageFraction,
        0x9e3779b9u));
}

[numthreads(64, 1, 1)]
void SimulateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= kParticleCount)
        return;

    Particle particle = gConsumeParticles.Consume();
    particle.Age += gDeltaTime;

    const float3 offset = particle.Position - gEmitterPosition;
    const float horizontalLength = max(length(offset.xz), 0.001f);
    const float3 radial = float3(offset.x / horizontalLength, 0.0f,
        offset.z / horizontalLength);
    const float3 tangent = float3(-radial.z, 0.0f, radial.x);
    const float radialCorrection = particle.OrbitRadius - horizontalLength;
    const float flutter = sin(gTotalTime * 3.1f + particle.Seed * 0.000013f);
    const float3 acceleration = tangent * (1.8f + 0.45f * flutter) +
        radial * radialCorrection * 3.2f +
        float3(0.0f, 0.25f + 0.18f * flutter, 0.0f);

    particle.Velocity += acceleration * gDeltaTime;
    particle.Velocity *= 1.0f / (1.0f + 0.65f * gDeltaTime);
    particle.Position += particle.Velocity * gDeltaTime;

    if (particle.Age >= particle.Lifetime || offset.y > 7.5f)
    {
        const uint newSeed = Hash(particle.Seed + asuint(gTotalTime) +
            dispatchThreadId.x);
        particle = SpawnParticle(dispatchThreadId.x, 0.0f, newSeed);
    }

    gAppendParticles.Append(particle);
}

struct VertexOutput
{
    float3 PositionWorld : POSITION;
    float4 Color : COLOR;
    float Size : SIZE;
};

VertexOutput ParticleVS(uint vertexId : SV_VertexID)
{
    const Particle particle = gDrawParticles[vertexId];
    VertexOutput output;
    output.PositionWorld = particle.Position;
    output.Color = particle.Color;
    output.Size = particle.Size;
    return output;
}

struct GeometryOutput
{
    float4 PositionClip : SV_POSITION;
    float3 NormalWorld : NORMAL;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};

[maxvertexcount(4)]
void BillboardGS(point VertexOutput input[1],
    inout TriangleStream<GeometryOutput> outputStream)
{
    const float3 center = input[0].PositionWorld;
    const float3 normal = normalize(gCameraPosition - center);
    const float3 referenceUp = abs(normal.y) > 0.98f
        ? float3(1.0f, 0.0f, 0.0f)
        : float3(0.0f, 1.0f, 0.0f);
    const float3 right = normalize(cross(referenceUp, normal));
    const float3 up = normalize(cross(normal, right));
    const float halfSize = input[0].Size * 0.5f;
    const float3 corners[4] =
    {
        center - right * halfSize - up * halfSize,
        center - right * halfSize + up * halfSize,
        center + right * halfSize - up * halfSize,
        center + right * halfSize + up * halfSize
    };
    const float2 texCoords[4] =
    {
        float2(0.0f, 1.0f), float2(0.0f, 0.0f),
        float2(1.0f, 1.0f), float2(1.0f, 0.0f)
    };

    [unroll]
    for (uint corner = 0; corner < 4; ++corner)
    {
        GeometryOutput output;
        output.PositionClip = mul(float4(corners[corner], 1.0f),
            gViewProjection);
        output.NormalWorld = normal;
        output.Color = input[0].Color;
        output.TexCoord = texCoords[corner];
        outputStream.Append(output);
    }
}

struct GBufferOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

GBufferOutput ParticlePS(GeometryOutput input)
{
    const float2 fromCenter = input.TexCoord * 2.0f - 1.0f;
    const float radiusSquared = dot(fromCenter, fromCenter);
    clip(1.0f - radiusSquared);

    const float glow = saturate(1.2f - radiusSquared);
    GBufferOutput output;
    output.Albedo = float4(input.Color.rgb * (0.65f + 0.35f * glow), 1.0f);
    output.Normal = float4(normalize(input.NormalWorld), 1.0f);
    return output;
}
