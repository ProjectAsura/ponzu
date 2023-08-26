//-----------------------------------------------------------------------------
// File : SceneParam.hlsli
// Desc : Scene Parameter.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#ifndef SCENE_PARAM_HLSLI
#define SCENE_PARAM_HLSLI

///////////////////////////////////////////////////////////////////////////////
// SceneParameter structure
///////////////////////////////////////////////////////////////////////////////
struct SceneParameter
{
    float4x4 View;
    float4x4 Proj;
    float4x4 InvView;
    float4x4 InvProj;
    float4x4 InvViewProj;

    float4x4 PrevView;
    float4x4 PrevProj;
    float4x4 PrevInvView;
    float4x4 PrevInvProj;
    float4x4 PrevInvViewProj;

    uint    MaxBounce;
    uint    MinBounce;
    uint    FrameIndex;
    float   SkyIntensity;

    bool    EnableAccumulation;
    uint    AccumulatedFrames;
    float   ExposureAdjustment;
    uint    LightCount;

    float4  Size;   // x:Width, y:Height, z:1.0f/Width, w:1.0f/Height.
    float3  CameraDir;
    uint    MaxIteration;

    float   AnimationTime;
    float   FovY;
    float   NearClip;
    float   FarClip;
};

#endif//SCENE_PARAM_HLSLI
