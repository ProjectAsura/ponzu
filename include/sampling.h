//-----------------------------------------------------------------------------
// File : sampling.h
// Desc : Sampling Utility.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <vector>
#include <fnd/asdxMath.h>


namespace r3d {

std::vector<float> MakePiecewiseConstantPDF(const std::vector<float>& pdf);

int64_t SamplePiecewiseConstantArray(float u, const std::vector<float>& cdf, float* uRemapped);

float SampleLinear(float u, float a, float b);

float LinearPDF(float x, float a, float b);

float Luminance(asdx::Vector3 value);


} // namespace r3d
