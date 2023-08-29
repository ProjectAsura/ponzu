//-----------------------------------------------------------------------------
// File : CameraSequence.cpp
// Desc : Camera Sequence Data.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <CameraSequence.h>
#include <fnd/asdxLogger.h>
#include <Windows.h>
#include <generated/camera_format.h>

#if !CAMP_RELEASE
#include <fnd/asdxMisc.h>
#include <fstream>
#include <ctime>
#endif//!CAMP_RELEASE


namespace r3d {

//-----------------------------------------------------------------------------
//      asdx形式に変換します.
//-----------------------------------------------------------------------------
asdx::Vector3 Convert(const r3d::Vector3& value)
{ return asdx::Vector3(value.x(), value.y(), value.z()); }


///////////////////////////////////////////////////////////////////////////////
// CameraSequence class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      バイナリをロードします.
//-----------------------------------------------------------------------------
bool CameraSequence::Init(const char* path)
{
    // ファイル読み込み.
    {
        auto hFile = CreateFileA(
            path,
            GENERIC_READ,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            ELOGA("Error : File Open Failed. path = %s");
            return false;
        }

        auto size = GetFileSize(hFile, NULL);
        m_pBinary = malloc(size);
        DWORD readSize = 0;
        auto ret = ReadFile(hFile, m_pBinary, size, &readSize, NULL);
        if (!ret)
        { ELOGA("Error : Read Failed. path = %s", path); }

        CloseHandle(hFile);
    }

    // インデックスをリセット.
    m_FrameIndex = 0;
    m_ParamIndex = 0;

    return false;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void CameraSequence::Term()
{
    if (m_pBinary != nullptr)
    {
        free(m_pBinary);
        m_pBinary = nullptr;
    }
}

//-----------------------------------------------------------------------------
//      位置座標を取得します.
//-----------------------------------------------------------------------------
asdx::Vector3 CameraSequence::GetPosition() const
{
    assert(m_pBinary != nullptr);
    auto resSequence = GetResCameraSequence(m_pBinary);
    auto& pos = resSequence->params()->Get(m_ParamIndex)->position();
    return asdx::Vector3(pos.x(), pos.y(), pos.z());
}

//-----------------------------------------------------------------------------
//      ニアクリップ平面を取得します.
//-----------------------------------------------------------------------------
float CameraSequence::GetNearClip() const
{
    assert(m_pBinary != nullptr);
    auto resSequence = GetResCameraSequence(m_pBinary);
    return resSequence->params()->Get(m_ParamIndex)->nearClip();
}

//-----------------------------------------------------------------------------
//      ファークリップ平面を取得します.
//-----------------------------------------------------------------------------
float CameraSequence::GetFarlip() const
{
    assert(m_pBinary != nullptr);
    auto resSequence = GetResCameraSequence(m_pBinary);
    return resSequence->params()->Get(m_ParamIndex)->farClip();
}

//-----------------------------------------------------------------------------
//      カメラ更新処理を行います.
//-----------------------------------------------------------------------------
void CameraSequence::Update(uint32_t frameIndex, float aspectRatio)
{
    assert(m_pBinary != nullptr);

    // フレーム番号を更新.
    m_FrameIndex = frameIndex;

    auto resSequence = GetResCameraSequence(m_pBinary);
    auto maxIndex = resSequence->params()->size();

    auto nextIndex = asdx::Clamp(m_ParamIndex + 1u, 0u, maxIndex);

    // 更新フレームかどうかチェック.
    auto param = resSequence->params()->Get(nextIndex);
    bool changed = param->frameIndex() == m_FrameIndex;

    if (!changed)
    { return; }

    m_ParamIndex = nextIndex;

    auto position = Convert(param->position());
    auto target   = Convert(param->target());
    auto upward   = Convert(param->upward());

    // ビュー行列を更新.
    m_PrevView = m_CurrView;
    m_CurrView = asdx::Matrix::CreateLookAt(position, target, upward);

    // 射影行列を更新.
    m_PrevProj = m_CurrProj;
    m_CurrProj = asdx::Matrix::CreatePerspectiveFieldOfView(
        param->fieldOfView(),
        aspectRatio,
        param->nearClip(),
        param->farClip());
}

#if !CAMP_RELEASE
///////////////////////////////////////////////////////////////////////////////
// CameraSequenceExporter class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      テキストファイルからデータをロードします.
//-----------------------------------------------------------------------------
bool CameraSequenceExporter::LoadFromTXT(const char* path)
{
    std::string inputPath;
    if (!asdx::SearchFilePathA(path, inputPath))
    {
        ELOGA("Error : File Not Found. path = %s", path);
        return false;
    }

    std::ifstream stream;
    stream.open(inputPath.c_str(), std::ios::in);

    if (!stream.is_open())
    {
        ELOGA("Error : File Open Failed. path = %s", inputPath.c_str());
        return false;
    }

    const uint32_t BUFFER_SIZE = 4096;
    char buf[BUFFER_SIZE] = {};
    std::string exportPath;

    for(;;)
    {
        stream >> buf;
        if (!stream || stream.eof())
        { break; }

        if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
        { /* DO_NOTHING */ }
        else if (0 == _stricmp(buf, "camera"))
        {
            CameraParam param = {};

            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }

                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-FrameIndex:"))
                { stream >> param.FrameIndex; }
                else if (0 == _stricmp(buf, "-Position:"))
                { stream >> param.Position.x >> param.Position.y >> param.Position.z; }
                else if (0 == _stricmp(buf, "-Target:"))
                { stream >> param.Target.x >> param.Target.y >> param.Target.z; }
                else if (0 == _stricmp(buf, "-Upward:"))
                { stream >> param.Upward.x >> param.Upward.y >> param.Upward.z; }
                else if (0 == _stricmp(buf, "-FieldOfView:"))
                { stream >> param.FieldOfView; }
                else if (0 == _stricmp(buf, "-NearClip:"))
                { stream >> param.NearClip; }
                else if (0 == _stricmp(buf, "-FarClip:"))
                { stream >> param.FarClip; }

                stream.ignore(BUFFER_SIZE, '\n');
            }

            m_Params.push_back(param);
        }
        else if (0 == _stricmp(buf, "export"))
        {
            for(;;)
            {
                stream >> buf;
                if (!stream || stream.eof())
                { break; }
                if (0 == strcmp(buf, "};"))
                { break; }
                else if (0 == strcmp(buf, "#") || 0 == strcmp(buf, "//"))
                { /* DO_NOTHING */ }
                else if (0 == _stricmp(buf, "-Path:"))
                { stream >> exportPath; }

                stream.ignore(BUFFER_SIZE, '\n');
            }
        }

        stream.ignore(BUFFER_SIZE, '\n');
    }
    stream.close();

    // エクスポート名が無ければタイムスタンプを付ける
    if (exportPath.empty())
    {
        tm local_time = {};
        auto t   = time(nullptr);
        auto err = localtime_s( &local_time, &t );

        char timeStamp[256] = {};
        sprintf_s(timeStamp, "%04d%02d%02d_%02d%02d%02d",
            local_time.tm_year + 1900,
            local_time.tm_mon + 1,
            local_time.tm_mday,
            local_time.tm_hour,
            local_time.tm_min,
            local_time.tm_sec);

        exportPath = "../res/scene/camera_";
        exportPath += timeStamp;
        exportPath += ".cam";
    }

    if (!Export(exportPath.c_str()))
    {
        ELOG("Error : Camera Sequence Data Export Failed.");
        return false;
    }

    ILOGA("Info : Camera Sequence Data Exported!! path = %s", exportPath.c_str());
    return true;
}

//-----------------------------------------------------------------------------
//      バイナリに出力します.
//-----------------------------------------------------------------------------
bool CameraSequenceExporter::Export(const char* path)
{
    std::vector<r3d::ResCameraParam> params;
    params.resize(m_Params.size());

    // データ変換.
    for(size_t i=0; i<m_Params.size(); ++i)
    {
        r3d::Vector3 position(m_Params[i].Position.x, m_Params[i].Position.y, m_Params[i].Position.z);
        r3d::Vector3 target(m_Params[i].Target.x, m_Params[i].Target.y, m_Params[i].Target.z);
        r3d::Vector3 upward(m_Params[i].Upward.x, m_Params[i].Upward.y, m_Params[i].Upward.z);

        params[i] = r3d::ResCameraParam(
            m_Params[i].FrameIndex,
            position,
            target,
            upward,
            m_Params[i].FieldOfView,
            m_Params[i].NearClip,
            m_Params[i].FarClip);
    }

    flatbuffers::FlatBufferBuilder builder(2048);
    auto dstSequence = r3d::CreateResCameraSequenceDirect(
        builder,
        &params);

    builder.Finish(dstSequence);

    auto buffer = builder.GetBufferPointer();
    auto size   = builder.GetSize();

    FILE* fp = nullptr;
    auto err = fopen_s(&fp, path, "wb");
    if (err != 0)
    {
        ELOG("Error : File Open Failed. path = %s", path);
        return false;
    }

    fwrite(buffer, size, 1, fp);
    fclose(fp);

    return true;
}

//-----------------------------------------------------------------------------
//      データをリセットします.
//-----------------------------------------------------------------------------
void CameraSequenceExporter::Reset()
{
    m_Params.clear();
    m_Params.shrink_to_fit();
}
#endif

} // namespace r3d
