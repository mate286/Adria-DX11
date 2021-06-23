
#include "../Globals/GlobalsPS.hlsli"
#include "../Util/DitherUtil.hlsli"

#include "../Util/ShadowUtil.hlsli"




Texture2D        normalMetallicTx       : register(t0);
Texture2D        diffuseRoughnessTx     : register(t1);
Texture2D<float> depthTx                : register(t2);

Texture2D       shadowDepthMap  : register(t4);
TextureCube     depthCubeMap    : register(t5);
Texture2DArray  cascadeDepthMap : register(t6);


//has artifacts, fix later
//https://panoskarabelas.com/posts/screen_space_shadows/
static const uint SSS_MAX_STEPS = 16; // Max ray steps, affects quality and performance.
static const float SSS_RAY_MAX_DISTANCE = 0.05f; // Max shadow length, longer shadows are less accurate.
static const float SSS_THICKNESS = 0.01f; // Depth testing thickness.
static const float SSS_STEP_LENGTH = SSS_RAY_MAX_DISTANCE / SSS_MAX_STEPS;

float ScreenSpaceShadows(float2 uv)
{
    float depth = depthTx.Sample(linear_wrap_sampler, uv);
    float3 ray_position = GetPositionVS(uv, depth);
    
    float3 ray_direction = -current_light.direction.xyz;
    float3 ray_step = ray_direction * SSS_STEP_LENGTH;
    ray_position += ray_step * dither(uv);
    
     // Ray march towards the light
    float occlusion = 0.0;
    float2 ray_uv = 0.0f;
    
    for (uint i = 0; i < SSS_MAX_STEPS; i++)
    {
        // Step the ray
        ray_position += ray_step;

        float4 ray_projection = mul(float4(ray_position, 1.0), projection);
        ray_uv = ray_projection.xy / ray_projection.w;
        ray_uv.xy = 0.5 * ray_uv.xy + 0.5;
        ray_uv.y = 1.0 - ray_uv.y;

        // Ensure the UV coordinates are inside the screen
        if (IsSaturated(ray_uv))
        {
            float depth_z = depthTx.Sample(linear_wrap_sampler, ray_uv);
            
            // Compute the difference between the ray's and the camera's depth
            float depth_linear = ConvertZToLinearDepth(depth_z);
            float depth_delta = ray_position.z - depth_linear;

            // Check if the camera can't "see" the ray (ray depth must be larger than the camera depth, so positive depth_delta)
            if ((depth_delta > 0.0f) && (depth_delta < SSS_THICKNESS))
            {
                // Mark as occluded
                occlusion = 1.0f;

                // Fade out as we approach the edges of the screen
                occlusion *= ScreenFade(ray_uv);

                break;
            }
        }
    }

    // Convert to visibility
    return 1.0f - occlusion;
}




struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEX;
};


float4 main(VertexOut pin) : SV_TARGET
{

    //unpack gbuffer
    float4 NormalMetallic = normalMetallicTx.Sample(linear_wrap_sampler, pin.Tex);
    float3 Normal = 2 * NormalMetallic.rgb - 1.0;
    float metallic = NormalMetallic.a;
    float depth = depthTx.Sample(linear_wrap_sampler, pin.Tex);
    float3 Position = GetPositionVS(pin.Tex, depth);
    float4 AlbedoRoughness = diffuseRoughnessTx.Sample(linear_wrap_sampler, pin.Tex);
    float3 V = normalize(0.0f.xxx - Position);
    float roughness = AlbedoRoughness.a;
    

    float3 Lo = float3(0.0f, 0.0f, 0.0f);
    switch (current_light.type)
    {
        case DIRECTIONAL_LIGHT:
            Lo = DoDirectionalLightPBR(current_light, Position, Normal, V, AlbedoRoughness.rgb, metallic, roughness);
            break;
        case POINT_LIGHT:
            Lo = DoPointLightPBR(current_light, Position, Normal, V, AlbedoRoughness.rgb, metallic, roughness);
            break;
        case SPOT_LIGHT:
            Lo = DoSpotLightPBR(current_light, Position, Normal, V, AlbedoRoughness.rgb, metallic, roughness);
            break;
        default:
            return float4(1, 0, 0, 1);
    }

    if (current_light.casts_shadows)
    {

        float shadow_factor = 1.0f;
        if (current_light.type == POINT_LIGHT)
        {
            const float zf = current_light.range;
            const float zn = 0.5f;
            const float c1 = zf / (zf - zn);
            const float c0 = -zn * zf / (zf - zn);
            
            float3 light_to_pixelWS = mul(float4(Position - current_light.position.xyz, 0.0f), inverse_view);

            const float3 m = abs(light_to_pixelWS).xyz;
            const float major = max(m.x, max(m.y, m.z));
            float fragment_depth = (c1 * major + c0) / major;
            shadow_factor = depthCubeMap.SampleCmpLevelZero(shadow_sampler, normalize(light_to_pixelWS.xyz), fragment_depth);
        }
        else if (current_light.type == DIRECTIONAL_LIGHT && current_light.use_cascades)
        {
            float viewDepth = Position.z;
            matrix shadow_matrices[3] = { shadow_matrix1, shadow_matrix2, shadow_matrix3 };
            
            for (uint i = 0; i < 3; ++i)
            {

                matrix light_space_matrix = shadow_matrices[i];

                if (viewDepth < splits[i])
                {
                    float4 posShadowMap = mul(float4(Position, 1.0), light_space_matrix);
        
                    float3 UVD = posShadowMap.xyz / posShadowMap.w;

                    UVD.xy = 0.5 * UVD.xy + 0.5;
                    UVD.y = 1.0 - UVD.y;
                    
                    shadow_factor = CSMCalcShadowFactor_PCF3x3(shadow_sampler, cascadeDepthMap, i, UVD, shadow_map_size, softness);
                    break;
                }
            }    
        }
        else
        {
            float4 posShadowMap = mul(float4(Position, 1.0), shadow_matrix1);
            float3 UVD = posShadowMap.xyz / posShadowMap.w;

            UVD.xy = 0.5 * UVD.xy + 0.5;
            UVD.y = 1.0 - UVD.y;
                
            shadow_factor = CalcShadowFactor_PCF3x3(shadow_sampler, shadowDepthMap, UVD, shadow_map_size, softness);
        }
        Lo = Lo * shadow_factor;
    }
    
    if (current_light.screen_space_shadows)
        Lo = Lo * ScreenSpaceShadows(pin.Tex);

    return float4(Lo, 1.0f);
}


