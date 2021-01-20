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


// Encapsulate the various inputs used by the various functions in the shading equation
// We store values in this struct to simplify the integration of alternative implementations
// of the shading terms, outlined in the Readme.MD Appendix.
struct PBRInfo
{
    float NdotL;                  // cos angle between normal and light direction
    float NdotV;                  // cos angle between normal and view direction
    float NdotH;                  // cos angle between normal and half vector
    float LdotH;                  // cos angle between light direction and half vector
    float VdotH;                  // cos angle between view direction and half vector
    float VdotL;                  // cos angle between view direction and light direction
    float perceptualRoughness;    // roughness value, as authored by the model creator (input to shader)
    float metalness;              // metallic value at the surface
    vec3 reflectance0;            // full reflectance color (normal incidence angle)
    vec3 reflectance90;           // reflectance color at grazing angle
    float alphaRoughness;         // roughness mapped to a more linear change in the roughness (proposed by [2])
    vec3 diffuseColor;            // color contribution from diffuse lighting
    vec3 specularColor;           // color contribution from specular lighting
};


vec4 SRGBtoLINEAR(vec4 srgbIn)
{
    vec3 linOut = pow(srgbIn.xyz, vec3(2.2));
    return vec4(linOut,srgbIn.w);
}

vec4 LINEARtoSRGB(vec4 srgbIn)
{
    vec3 linOut = pow(srgbIn.xyz, vec3(1.0 / 2.2));
    return vec4(linOut, srgbIn.w);
}

float rcp(const in float value)
{
    return 1.0 / value;
}

float pow5(const in float value)
{
    return value * value * value * value * value;
}

// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
vec3 getNormal()
{
    // Perturb normal, see http://www.thetenthplanet.de/archives/1180
    vec3 tangentNormal = texture(normalMap, texCoord0).xyz * 2.0 - 1.0;

    tangentNormal = normalize(tangentNormal * vec3(1.0, 1.0, 1.0));
    return normalize(tbn * tangentNormal);
}

// Basic Lambertian diffuse
// Implementation from Lambert's Photometria https://archive.org/details/lambertsphotome00lambgoog
// See also [1], Equation 1
vec3 BRDF_Diffuse_Lambert(PBRInfo pbrInputs)
{
    return pbrInputs.diffuseColor * RECIPROCAL_PI;
}

vec3 BRDF_Diffuse_Custom_Lambert(PBRInfo pbrInputs)
{
    return pbrInputs.diffuseColor * RECIPROCAL_PI * pow(pbrInputs.NdotV, 0.5 + 0.3 * pbrInputs.perceptualRoughness);
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
vec3 BRDF_Diffuse_OrenNayar(PBRInfo pbrInputs)
{
    float a = pbrInputs.alphaRoughness;
    float s = a;// / ( 1.29 + 0.5 * a );
    float s2 = s * s;
    float VoL = 2 * pbrInputs.VdotH * pbrInputs.VdotH - 1;		// double angle identity
    float Cosri = pbrInputs.VdotL - pbrInputs.NdotV * pbrInputs.NdotL;
    float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
    float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * ( Cosri >= 0 ? 1.0 / max(pbrInputs.NdotL, pbrInputs.NdotV) : 1 );
    return pbrInputs.diffuseColor / PI * ( C1 + C2 ) * ( 1 + pbrInputs.perceptualRoughness * 0.5 );
}

// [Gotanda 2014, "Designing Reflectance Models for New Consoles"]
vec3 BRDF_Diffuse_Gotanda(PBRInfo pbrInputs)
{
    float a = pbrInputs.alphaRoughness;
    float a2 = a * a;
    float F0 = 0.04;
    float VoL = 2 * pbrInputs.VdotH * pbrInputs.VdotH - 1;		// double angle identity
    float Cosri = VoL - pbrInputs.NdotV * pbrInputs.NdotL;
    float a2_13 = a2 + 1.36053;
    float Fr = ( 1 - ( 0.542026*a2 + 0.303573*a ) / a2_13 ) * ( 1 - pow( 1 - pbrInputs.NdotV, 5 - 4*a2 ) / a2_13 ) * ( ( -0.733996*a2*a + 1.50912*a2 - 1.16402*a ) * pow( 1 - pbrInputs.NdotV, 1 + rcp(39*a2*a2+1) ) + 1 );
    //float Fr = ( 1 - 0.36 * a ) * ( 1 - pow( 1 - NoV, 5 - 4*a2 ) / a2_13 ) * ( -2.5 * Roughness * ( 1 - NoV ) + 1 );
    float Lm = ( max( 1 - 2*a, 0 ) * ( 1 - pow5( 1 - pbrInputs.NdotL ) ) + min( 2*a, 1 ) ) * ( 1 - 0.5*a * (pbrInputs.NdotL - 1) ) * pbrInputs.NdotL;
    float Vd = ( a2 / ( (a2 + 0.09) * (1.31072 + 0.995584 * pbrInputs.NdotV) ) ) * ( 1 - pow( 1 - pbrInputs.NdotL, ( 1 - 0.3726732 * pbrInputs.NdotV * pbrInputs.NdotV ) / ( 0.188566 + 0.38841 * pbrInputs.NdotV ) ) );
    float Bp = Cosri < 0 ? 1.4 * pbrInputs.NdotV * pbrInputs.NdotL * Cosri : Cosri;
    float Lr = (21.0 / 20.0) * (1 - F0) * ( Fr * Lm + Vd + Bp );
    return pbrInputs.diffuseColor * RECIPROCAL_PI * Lr;
}

vec3 BRDF_Diffuse_Burley(PBRInfo pbrInputs)
{
    float energyBias = mix(pbrInputs.perceptualRoughness, 0.0, 0.5);
    float energyFactor = mix(pbrInputs.perceptualRoughness, 1.0, 1.0 / 1.51);
    float fd90 = energyBias + 2.0 * pbrInputs.VdotH * pbrInputs.VdotH * pbrInputs.perceptualRoughness;
    float f0 = 1.0;
    float lightScatter = f0 + (fd90 - f0) * pow(1.0 - pbrInputs.NdotL, 5.0);
    float viewScatter = f0 + (fd90 - f0) * pow(1.0 - pbrInputs.NdotV, 5.0);

    return pbrInputs.diffuseColor * lightScatter * viewScatter * energyFactor;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from [4], Equation 15
vec3 specularReflection(PBRInfo pbrInputs)
{
    //return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
    return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance90*pbrInputs.reflectance0) * exp2((-5.55473 * pbrInputs.VdotH - 6.98316) * pbrInputs.VdotH);
}

// This calculates the specular geometric attenuation (aka G()),
// where rougher material will reflect less light back to the viewer.
// This implementation is based on [1] Equation 4, and we adopt their modifications to
// alphaRoughness as input as originally proposed in [2].
float geometricOcclusion(PBRInfo pbrInputs)
{
    float NdotL = pbrInputs.NdotL;
    float NdotV = pbrInputs.NdotV;
    float r = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;

    float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r + (1.0 - r) * (NdotL * NdotL)));
    float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r + (1.0 - r) * (NdotV * NdotV)));
    return attenuationL * attenuationV;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float microfacetDistribution(PBRInfo pbrInputs)
{
    float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
    float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
    return roughnessSq / (PI * f * f);
}


void main()
{
    float perceptualRoughness;
    float metallic;
    vec3 diffuseColor;
    vec4 baseColor;

    vec3 f0 = vec3(0.04);

    baseColor = SRGBtoLINEAR(texture(diffuseMap, texCoord0)) * pbr.albedo;

    perceptualRoughness = pbr.metallicRoughness.g;
    metallic = pbr.metallicRoughness.r;

    vec4 mrSample = texture(mrMap, texCoord0);
    perceptualRoughness = mrSample.g * perceptualRoughness;
    metallic = mrSample.b * metallic;

    diffuseColor = baseColor.rgb * (vec3(1.0) - f0);
    diffuseColor *= 1.0 - metallic;

    float alphaRoughness = perceptualRoughness * perceptualRoughness;

    vec3 specularColor = mix(f0, baseColor.rgb, metallic);

    // Compute reflectance.
    float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

    // For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
    // For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
    float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
    vec3 specularEnvironmentR0 = specularColor.rgb;
    vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;

    vec3 n = getNormal();
    vec3 v = normalize(viewDir);    // Vector from surface point to camera
    vec3 l = normalize(lightDir);     // Vector from surface point to light
    vec3 h = normalize(l+v);                        // Half vector between both l and v
    vec3 reflection = -normalize(reflect(v, n));
    reflection.y *= -1.0f;

    float NdotL = clamp(dot(n, l), 0.001, 1.0);
    float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
    float NdotH = clamp(dot(n, h), 0.0, 1.0);
    float LdotH = clamp(dot(l, h), 0.0, 1.0);
    float VdotH = clamp(dot(v, h), 0.0, 1.0);
    float VdotL = clamp(dot(v, l), 0.0, 1.0);

    PBRInfo pbrInputs = PBRInfo(NdotL,
                                NdotV,
                                NdotH,
                                LdotH,
                                VdotH,
                                VdotL,
                                perceptualRoughness,
                                metallic,
                                specularEnvironmentR0,
                                specularEnvironmentR90,
                                alphaRoughness,
                                diffuseColor,
                                specularColor);

    // Calculate the shading terms for the microfacet specular shading model
    vec3 F = specularReflection(pbrInputs);
    float G = geometricOcclusion(pbrInputs);
    float D = microfacetDistribution(pbrInputs);

    const vec3 u_LightColor = vec3(1.0);

    // Calculation of analytical lighting contribution
    vec3 diffuseContrib = (1.0 - F) * BRDF_Diffuse_Burley(pbrInputs);
    vec3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
    // Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
    vec3 color = NdotL * u_LightColor * (diffuseContrib + specContrib);

    const float u_OcclusionStrength = 1.0f;
    // Apply optional PBR terms for additional (optional) shading
    float ao = texture(aoMap, texCoord0).r;
    color = mix(color, color * ao, u_OcclusionStrength);

    vec3 emissive = SRGBtoLINEAR(texture(emissiveMap, texCoord0)).rgb * pbr.emissive.rgb;
    color += emissive;

    outColor = LINEARtoSRGB(vec4(color, baseColor.a));
}
