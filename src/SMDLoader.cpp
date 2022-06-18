//-----------------------------------------------------------------------------
// File : SMDLoader.cpp
// Desc : Salty Model Data Format Loader.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <SMDLoader.h>
#include <fnd/asdxMisc.h>
#include <fnd/asdxLogger.h>


namespace {

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
static const uint8_t    SMD_MAGIC[4]             = { 'S', 'M', 'D', '\0' };
static const uint32_t   SMD_VERSION              = 0x00000002;
static const uint32_t   SMD_DATA_HEADER_SIZE     = 24;
static const uint32_t   SMD_TEXTURE_STRUCT_SIZE  = 256;
static const uint32_t   SMD_MATERIAL_STRUCT_SIZE = 168;
static const uint32_t   SMD_TRIANGLE_STRUCT_SIZE = 100;


///////////////////////////////////////////////////////////////////////////////
// SMD_DATA_HEADER structure    
///////////////////////////////////////////////////////////////////////////////
struct SMD_DATA_HEADER
{
    uint32_t NumTriangles;              //!< 三角形数です.
    uint32_t NumMaterials;              //!< マテリアル数です.
    uint32_t NumTextures;               //!< テクスチャ数です.
    uint32_t TriangleStructSize;        //!< 三角形構造体のサイズです.
    uint32_t MaterialStructSize;        //!< マテリアル構造体のサイズです.
    uint32_t TextureStructSize;         //!< テクスチャ構造体のサイズです.
};

///////////////////////////////////////////////////////////////////////////////
// SMD_FILE_HEADER structure
///////////////////////////////////////////////////////////////////////////////
struct SMD_FILE_HEADER
{
    uint8_t         Magic[4];             //!< マジックです.
    uint32_t        Version;                //!< ファイルバージョンです.
    uint32_t        DataHeaderSize;         //!< データヘッダのサイズです.
    SMD_DATA_HEADER DataHeader;             //!< データヘッダです.
};

///////////////////////////////////////////////////////////////////////////////
// SMD_TRIANGLE structure
///////////////////////////////////////////////////////////////////////////////
struct SMD_TRIANGLE
{
    VertexSMD   Vertex[3];        //!< 頂点座標です.
    int         MaterialId;       //!< マテリアルインデックスです.
};

} // namespace


///////////////////////////////////////////////////////////////////////////////
// SMDLoader class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      ファイルからロードします.
//-----------------------------------------------------------------------------
bool SMDLoader::Load(const char* path, ModelSMD& model)
{
    if (path == nullptr)
    {
        ELOGA("Error : Invalid Argument.");
        return false;
    }

    // ディレクトリパス.
    m_DirectoryPath = asdx::GetDirectoryPathA(path);

    FILE* pFile;
    auto err = fopen_s(&pFile, path, "rb");
    if (err != 0)
    {
        ELOGA("Error : SMDLoader::Load() Failed. path = %s", path);
        return true;
    }

    // ファイルヘッダ読み込み.
    SMD_FILE_HEADER fileHeader = {};
    fread(&fileHeader, sizeof(fileHeader), 1, pFile);

    // ファイルマジックチェック.
    if (memcmp(fileHeader.Magic, SMD_MAGIC, sizeof(uint8_t) * 4) != 0)
    {
        ELOGA("Error : Invalid File.");
        fclose(pFile);
        return false;
    }

    // ファイルバージョンチェック.
    if (fileHeader.Version != SMD_VERSION)
    {
        ELOGA("Error : Invalid File Version.");
        fclose(pFile);
        return false;
    }

    // データヘッダサイズチェック.
    if (fileHeader.DataHeaderSize != SMD_DATA_HEADER_SIZE)
    {
        ELOGA("Error : Invalid Data Header Size.");
        fclose(pFile);
        return false;
    }

    // テクスチャ構造体サイズチェック.
    if (fileHeader.DataHeader.TextureStructSize != SMD_TEXTURE_STRUCT_SIZE)
    {
        ELOGA("Error : Invalid Texture Struct Size.");
        fclose(pFile);
        return false;
    }

    // 全マテリアル構造体サイズの合算サイズをチェック.
    if (fileHeader.DataHeader.MaterialStructSize != SMD_MATERIAL_STRUCT_SIZE)
    {
        ELOGA("Error : Invalid Material Struct Size.");
        fclose(pFile);
        return false;
    }

    // 三角形構造体サイズチェック.
    if (fileHeader.DataHeader.TriangleStructSize != SMD_TRIANGLE_STRUCT_SIZE)
    {
        ELOGA("Error : Invalid Triangle Struct Size.");
        fclose(pFile);
        return false;
    }

    model.Textures .resize(fileHeader.DataHeader.NumTextures);
    model.Materials.resize(fileHeader.DataHeader.NumMaterials);

    // テクスチャデータ読み取り.
    for(size_t i=0; i<model.Textures.size(); ++i)
    {
        TextureSMD texture;
        fread(&texture, sizeof(texture), 1, pFile);
        model.Textures[i].Path = m_DirectoryPath + "/" + texture.Path;
    }

    // マテリアルデータ読み取り.
    for(size_t i=0; i<model.Materials.size(); ++i)
    {
        int type = -1;
        fread(&type, sizeof(type), 1, pFile);

        model.Materials[i].Type = SMD_MATERIAL_TYPE(type);

        switch(type)
        {
        case SMD_MATERIAL_TYPE_MATTE:
            { fread(&model.Materials[i].Param.Matte, sizeof(MatteSMD), 1, pFile); }
            break;

        case SMD_MATERIAL_TYPE_MIRROR:
            { fread(&model.Materials[i].Param.Mirror, sizeof(MirrorSMD), 1, pFile); }
            break;

        case SMD_MATERIAL_TYPE_DIELECTRIC:
            { fread(&model.Materials[i].Param.Dielectrics, sizeof(DielectricSMD), 1, pFile); }
            break;

        case SMD_MATERIAL_TYPE_GLOSSY:
            { fread(&model.Materials[i].Param.Glossy, sizeof(GlossySMD), 1, pFile); }
            break;

        case SMD_MATERILA_TYPE_PLASTIC:
            { fread(&model.Materials[i].Param.Plastic, sizeof(PlasticSMD), 1, pFile); }
            break;
        }
    }

    // 三角形データ読み取り.
    {
        auto count = fileHeader.DataHeader.NumTriangles;
        model.Vertices.resize(size_t(count) * 3);

        int      materialId = -1;
        uint32_t subsetId   = 0;

        for(uint32_t i=0; i<count; ++i)
        {
            SMD_TRIANGLE triangle = {};
            fread(&triangle, sizeof(triangle), 1, pFile);

            if (materialId != triangle.MaterialId)
            {
                // マテリアルID更新.
                materialId = triangle.MaterialId;

                // サブセットを登録.
                SubsetSMD subset = {};
                subset.IndexOffset = i;
                subset.IndexCount  = 0;
                subset.MaterialId  = materialId;

                subsetId = uint32_t(model.Subsets.size());
                model.Subsets.push_back(subset);
            }

            // 頂点データ登録.
            auto idx = size_t(i) * 3;
            model.Vertices[idx + 0] = triangle.Vertex[0];
            model.Vertices[idx + 1] = triangle.Vertex[1];
            model.Vertices[idx + 2] = triangle.Vertex[2];

            // インデックス数をインクリメント.
            model.Subsets[subsetId].IndexCount++;
        }
    }

    fclose(pFile);
    return true;
}
