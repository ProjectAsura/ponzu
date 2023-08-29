//-----------------------------------------------------------------------------
// File : CameraSequence.h
// Desc : Camera Sequence Data.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------
#pragma once

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <fnd/asdxMath.h>

#if !CAMP_RELEASE
#include <vector>
#endif


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// CameraSequence class
///////////////////////////////////////////////////////////////////////////////
class CameraSequence
{
    //=========================================================================
    // list of friend classes and methods.
    //=========================================================================
    /* NOTHING */

public:
    //=========================================================================
    // public variables.
    //=========================================================================
    /* NOTHING */

    //=========================================================================
    // public methods.
    //=========================================================================
    bool Init(const char* path, float aspectRatio);
    void Term();

    const asdx::Matrix&  GetCurrView () const { return m_CurrView; }
    const asdx::Matrix&  GetPrevView () const { return m_PrevView; }
    const asdx::Matrix&  GetCurrProj () const { return m_CurrProj; }
    const asdx::Matrix&  GetPrevProj () const { return m_PrevProj; }
    asdx::Vector3        GetPosition () const;
    float                GetFovY     () const;
    float                GetNearClip () const;
    float                GetFarlip   () const;
    asdx::Vector3        GetCameraDir() const;

    bool Update(uint32_t frameIndex, float aspectRatio);

private:
    //=========================================================================
    // private variables.
    //=========================================================================
    void*           m_pBinary       = nullptr;
    uint32_t        m_ParamIndex    = 0;
    uint32_t        m_FrameIndex    = 0;
    asdx::Matrix    m_CurrView;
    asdx::Matrix    m_PrevView;
    asdx::Matrix    m_CurrProj;
    asdx::Matrix    m_PrevProj;

    //=========================================================================
    // private methods.
    //=========================================================================
    /* NOTHING */
};

#if !CAMP_RELEASE
///////////////////////////////////////////////////////////////////////////////
// CameraParam structure
///////////////////////////////////////////////////////////////////////////////
struct CameraParam
{
    uint32_t        FrameIndex;
    asdx::Vector3   Position;
    asdx::Vector3   Target;
    asdx::Vector3   Upward;
    float           FieldOfView;
    float           NearClip;
    float           FarClip;
};

///////////////////////////////////////////////////////////////////////////////
// CameraSequenceExporter class
///////////////////////////////////////////////////////////////////////////////
class CameraSequenceExporter
{
    //=========================================================================
    // list of friend classes and methods.
    //=========================================================================
    /* NOTHING */

public:
    //=========================================================================
    // public variables.
    //=========================================================================
    /* NOTHING */

    //=========================================================================
    // public methods.
    //=========================================================================
    bool LoadFromTXT(const char* path, std::string& exportPath);
    bool Export(const char* path);
    void Reset();

private:
    //=========================================================================
    // private variables.
    //=========================================================================
    std::vector<CameraParam>    m_Params;

    //=========================================================================
    // private methods.
    //=========================================================================
    /* NOTHING */
};
#endif

} // namespace r3d