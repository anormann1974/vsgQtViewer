#version 450
#extension GL_ARB_separate_shader_objects : enable

const float PI = 3.14159265359;
const float RECIPROCAL_PI = 0.31830988618;
const float RECIPROCAL_PI2 = 0.15915494;
const float EPSILON = 1e-6;

layout(binding = 0) uniform sampler2D diffuseMap;
layout(binding = 1) uniform sampler2D mrMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 3) uniform sampler2D aoMap;
layout(binding = 4) uniform sampler2D emissiveMap;

layout(binding = 10) uniform PbrData
{
    vec4 albedo;
    vec4 emissive;
    vec4 metallicRoughness;
} pbr;

layout(location = 1) in vec3 normalDir;
layout(location = 2) in vec2 texCoord0;
layout(location = 5) in vec3 viewDir;
layout(location = 6) in vec3 lightDir;
layout(location = 7) in mat3 tbn;

layout(location = 0) out vec4 outColor;

//////////////////////////////////////////////////////////////////////////////
// ATTRIBUTES
//////////////////////////////////////////////////////////////////////////////

struct	FragmentState
{
    //inputs
    vec3	vertexEye;
    float	vertexEyeDistance;
    vec2	vertexTexCoord;
    vec4	vertexColor;
    vec3	vertexNormal;
    vec3	vertexTangent;
    vec3	vertexBitangent;

    //state
    vec3	shadow;
    vec4	albedo;
    vec3	normal;
    float	gloss;
    vec3	reflectivity;
    vec3	fresnel;
    vec3	diffuseLight;
    vec3	specularLight;
    vec3	emissiveLight;

    //final outputs
    vec4	output0;
};

//////////////////////////////////////////////////////////////////////////////
// BRDF FUNCTIONS
//////////////////////////////////////////////////////////////////////////////

vec3 srgb_to_linear(vec3 sRGB)
{
//    return pow(sRGB, vec3(2.4));

    bvec3 cutoff = lessThan(sRGB, vec3(0.04045));
    vec3 higher = pow((sRGB + vec3(0.055))/vec3(1.055), vec3(2.4));
    vec3 lower = sRGB/vec3(12.92);
    return mix(higher, lower, cutoff);
}

vec3 linear_to_srgb(vec3 linearRGB)
{
//    return pow(linearRGB, vec3(1.0 / 2.4));

    bvec3 cutoff = lessThan(linearRGB, vec3(0.0031308));
    vec3 higher = vec3(1.055)*pow(linearRGB, vec3(1.0/2.4)) - vec3(0.055);
    vec3 lower = linearRGB * vec3(12.92);
    return mix(higher, lower, cutoff);
}

float pow2(const in float value)
{
    return value * value;
}

void Surface(inout FragmentState s)
{
    vec3 normalTangentSpace = 2.0 * texture(normalMap, s.vertexTexCoord).rgb - 1.0;
    normalTangentSpace = normalize(normalTangentSpace * vec3(1.0, 1.0, 1.0));
    vec3 viewNormal = normalize(tbn * normalTangentSpace);

    //store our results
    s.normal = viewNormal;
    s.vertexTangent = tbn[0];
    s.vertexBitangent = tbn[1];
    s.vertexNormal = tbn[2];
    s.albedo.a = 1.0;
}

void Microsurface(inout FragmentState s)
{
    vec2 glossScaleBias = vec2(1.0, 1.0);
    float g = pbr.metallicRoughness.g;
    g *= texture(mrMap, s.vertexTexCoord).g;
    s.gloss = glossScaleBias.x * g + glossScaleBias.y;

    float glossHorizonSmooth = 1.0;
    float h = clamp(dot(s.normal, s.vertexEye), 0.0, 1.0);
    h = glossHorizonSmooth - h * glossHorizonSmooth;
    s.gloss = mix( s.gloss, 1.0, h*h );
}

void Albedo(inout FragmentState state)
{
    vec4 albedo = texture(diffuseMap, state.vertexTexCoord);

    state.albedo = pbr.albedo;
    state.albedo *= vec4(srgb_to_linear(albedo.rgb), albedo.a);
}

void Reflectivity(inout FragmentState s)
{
    float m = pbr.metallicRoughness.r;

    m *= texture(mrMap, s.vertexTexCoord).b;

    float spec = 0.04;

    #ifdef METALNESS_ADVANCED
        spec = dot( texture2D( tSpecMap, s.vertexTexCoord ), uSpecSwizzle );
        spec = (uSpecCurveAdjust > 0) ? (spec*spec) : spec;
        spec = uSpecScaleBias.x * spec + uSpecScaleBias.y;
    #endif

    s.reflectivity = mix(vec3(spec), s.albedo.xyz, m);
    s.albedo.xyz = s.albedo.xyz - m * s.albedo.xyz;
    s.fresnel = vec3( 1.0, 1.0, 1.0 );
}

void Diffusion(inout FragmentState state)
{
    float lambert = clamp(RECIPROCAL_PI * dot(state.normal, lightDir), 0.0, 1.0);
    state.diffuseLight = (lambert * state.albedo.xyz);
}

void Reflection(inout FragmentState s)
{
    //roughness
    float roughness = 1.0 - s.gloss;
    float a = max(roughness * roughness, 2e-3);
    float a2 = a * a;

    //light params
    //LightParams l = getLight( s.vertexPosition );
    //adjustAreaLightSpecular( l, reflect( -s.vertexEye, s.normal ), rcp(3.141592 * a2) );

    //various dot products
    vec3 H = normalize(lightDir + s.vertexEye);
    float NdotH = clamp(dot(s.normal,H), 0.0, 1.0);
    float VdotN = clamp(dot(s.vertexEye,s.normal), 0.0, 1.0);
    float LdotN = clamp(dot(lightDir,s.normal), 0.0, 1.0);
    float VdotH = clamp(dot(s.vertexEye,H), 0.0, 1.0);

    //horizon
    float atten = 1.0; //l.attenuation;
    float horizon = 1.0 - LdotN;
    horizon *= horizon; horizon *= horizon;
    atten = atten - atten * horizon;

    //incident light
    //vec3 spec = l.color * s.shadow * (atten * LdotN);
    vec3 spec = vec3(1.0) * s.shadow * (atten * LdotN);

    //microfacet distribution
    float d = ( NdotH * a2 - NdotH ) * NdotH + 1.0;
          d *= d;
    float D = a2 / (3.141593 * d);

    //geometric / visibility
    float k = a * 0.5;
    float G_SmithL = LdotN * (1.0 - k) + k;
    float G_SmithV = VdotN * (1.0 - k) + k;
    float G = 0.25 / ( G_SmithL * G_SmithV );

    //fresnel
    vec3 reflectivity = s.reflectivity, fresn = s.fresnel;
    vec3 F = reflectivity + (fresn - fresn*reflectivity) * exp2( (-5.55473 * VdotH - 6.98316) * VdotH );

    //final
    s.specularLight += (D * G) * (F * spec);
}

void Emissive(inout FragmentState state)
{
    state.emissiveLight = pbr.emissive.xyz;
    state.emissiveLight *= srgb_to_linear(texture(emissiveMap, state.vertexTexCoord).rgb);
}

void Occlusion(inout FragmentState s)
{
    float ao = 1.0;
    ao *= texture(aoMap, s.vertexTexCoord).r * 1.0; //pbr.occlusionTextureStrength;

    s.diffuseLight *= ao;
    s.specularLight *= ao;
}

void Merge(inout FragmentState state)
{
    state.output0.xyz = linear_to_srgb(state.diffuseLight + state.specularLight + state.emissiveLight);
    state.output0.a = state.albedo.a;
}

//////////////////////////////////////////////////////////////////////////////
// MAIN BLOCK
//////////////////////////////////////////////////////////////////////////////

void main()
{
    FragmentState state;
    state.vertexEye = normalize(viewDir);
    state.vertexEyeDistance = length(viewDir);
    state.vertexColor = vec4(1.0);
    state.vertexNormal = tbn[2];
    state.vertexTangent = tbn[0];
    state.vertexBitangent = tbn[1];
    state.vertexTexCoord = texCoord0;
    state.shadow = vec3(1.0,1.0,1.0);
    state.albedo = vec4(1.0,1.0,1.0,1.0);
    state.normal = normalize(state.vertexNormal);
    state.gloss = 0.5;
    state.reflectivity =
    state.fresnel = vec3(1.0,1.0,1.0);
    state.diffuseLight =
    state.specularLight =
    state.emissiveLight = vec3(0.0,0.0,0.0);
    state.output0 = vec4(0.0,0.0,0.0,1.0);

    Surface(state);
    Microsurface(state);
    Albedo(state);
    Reflectivity(state);
    Diffusion(state);
    Reflection(state);
    Emissive(state);
    Occlusion(state);
    Merge(state);

    outColor = state.output0;
}
