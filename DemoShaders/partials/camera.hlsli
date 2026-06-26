#ifndef CAMERA_HLSLI
#define CAMERA_HLSLI

#include "common.hlsli"

//
// Camera position
//
cbuffer Camera : register(b1)
{
    float3 u_CameraPos;
    float3 u_CameraUp;
    float3 u_CameraFront;
    float3 u_CameraRight;
};

#endif // CAMERA_HLSLI
