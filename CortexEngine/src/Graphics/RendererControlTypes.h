#pragma once

namespace Cortex::Graphics {

struct RendererDebugControlState {
    float exposure = 1.0f;
    float shadowBias = 0.0005f;
    float shadowPCFRadius = 1.5f;
    float cascadeLambda = 0.5f;
    float cascade0ResolutionScale = 1.0f;
    float bloomIntensity = 0.25f;

    float fractalAmplitude = 0.0f;
    float fractalFrequency = 0.5f;
    float fractalOctaves = 4.0f;
    float fractalCoordMode = 1.0f;
    float fractalScaleX = 1.0f;
    float fractalScaleZ = 1.0f;
    float fractalLacunarity = 2.0f;
    float fractalGain = 0.5f;
    float fractalWarpStrength = 0.0f;
    float fractalNoiseType = 0.0f;

    bool shadowsEnabled = true;
    bool pcssEnabled = false;
    bool fxaaEnabled = true;
    bool taaEnabled = false;
    bool ssrEnabled = true;
    bool ssaoEnabled = true;
    bool iblEnabled = false;
    bool fogEnabled = false;
    bool rayTracingEnabled = false;
};

} // namespace Cortex::Graphics
