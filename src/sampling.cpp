//-----------------------------------------------------------------------------
// File : sampling.cpp
// Desc : Sampling Utility.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <sampling.h>
#include <algorithm>


namespace r3d {

std::vector<float> MakePiecewiseConstantPDF(const std::vector<float>& pdf)
{
    // [Shirley 2019], Peter Shirley, Samuli Laine, Dvaid Hart, Matt Pharr,
    // Petrik Clarberg, Eric Haines, Matthias Raab, David Cline,
    // "Sampling Transformations Zoo", Ray Tracing Gems Ⅰ, pp.223-246, 2019.

    float total = 0.0f;

    // CDF is one greater than PDF.
    std::vector<float> cdf{ 0.0f };

    // Compute the cumulative sum.
    for(auto& value : pdf)
    { cdf.push_back(total += value); }

    // Normalize.
    for(auto& value : cdf)
    { value /= total; }

    return cdf;
}

int64_t SamplePiecewiseConstantArray(float u, const std::vector<float>& cdf, float* uRemapped)
{
    // [Shirley 2019], Peter Shirley, Samuli Laine, Dvaid Hart, Matt Pharr,
    // Petrik Clarberg, Eric Haines, Matthias Raab, David Cline,
    // "Sampling Transformations Zoo", Ray Tracing Gems Ⅰ, pp.223-246, 2019.

    // Use our (sorted) CDF to find the data point ot the left ouf our sample u.
    int64_t offset = std::upper_bound(cdf.begin(), cdf.end(), u) - cdf.begin() - 1;
    *uRemapped = (u - cdf[offset]) / (cdf[offset + 1] - cdf[offset]);
    return offset;
}

float SampleLinear(float u, float a, float b)
{
    // [Shirley 2019], Peter Shirley, Samuli Laine, Dvaid Hart, Matt Pharr,
    // Petrik Clarberg, Eric Haines, Matthias Raab, David Cline,
    // "Sampling Transformations Zoo", Ray Tracing Gems Ⅰ, pp.223-246, 2019.

    if (a == b)
    { return u; }

    return asdx::Saturate((a - sqrt(asdx::Lerp(u, a * a, b * b))) / (a - b));
}

float LinearPDF(float x, float a, float b)
{
    // [Shirley 2019], Peter Shirley, Samuli Laine, Dvaid Hart, Matt Pharr,
    // Petrik Clarberg, Eric Haines, Matthias Raab, David Cline,
    // "Sampling Transformations Zoo", Ray Tracing Gems Ⅰ, pp.223-246, 2019.

    if (x < 0.0f || x > 1.0f)
    { return 0.0f; }

    return asdx::Lerp(x, a, b) / ((a + b) / 2);
}

float Luminance(asdx::Vector3 value)
{
    return asdx::Vector3::Dot(value, asdx::Vector3(0.2126f, 0.7152f, 0.0722f));
}


} // namespace r3d
