//-----------------------------------------------------------------------------
// File : OBJLoader.cpp
// Desc : Wavefront Alias OBJ format.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <OBJLoader.h>
#include <fnd/asdxMisc.h>
#include <fnd/asdxLogger.h>
#include <fstream>

//-----------------------------------------------------------------------------
// Constant Values.
//-----------------------------------------------------------------------------
static const uint32_t OBJ_BUFFER_LENGTH = 2048;


namespace {

//-----------------------------------------------------------------------------
//      法線ベクトルを計算します.
//-----------------------------------------------------------------------------
void CalcNormals(MeshOBJ& mesh)
{
    auto vertexCount = mesh.Vertices.size();
    std::vector<asdx::Vector3> normals;
    normals.resize(vertexCount);

    // 法線データ初期化.
    for(size_t i=0; i<vertexCount; ++i)
    {
        normals[i] = asdx::Vector3(0.0f, 0.0f, 1.0f);
    }

    auto indexCount = mesh.Indices.size();
    for(size_t i=0; i<indexCount - 3; i+=3)
    {
        auto i0 = mesh.Indices[i + 0];
        auto i1 = mesh.Indices[i + 1];
        auto i2 = mesh.Indices[i + 2];

        const auto& p0 = mesh.Vertices[i0].Position;
        const auto& p1 = mesh.Vertices[i1].Position;
        const auto& p2 = mesh.Vertices[i2].Position;

        // エッジ.
        auto e0 = p1 - p0;
        auto e1 = p2 - p0;

        // 面法線を算出.
        auto fn = asdx::Vector3::Cross(e0, e1);
        fn = asdx::Vector3::SafeNormalize(fn, fn);

        // 面法線を加算.
        normals[i0] += fn;
        normals[i1] += fn;
        normals[i2] += fn;
    }

    // 加算した法線を正規化し，頂点法線を求める.
    for(size_t i=0; i<vertexCount; ++i)
    {
        normals[i] = asdx::Vector3::SafeNormalize(normals[i], normals[i]);
    }

    const auto SMOOTHING_ANGLE = 59.7f;
    auto cosSmooth = cosf(asdx::ToDegree(SMOOTHING_ANGLE));

    // スムージング処理.
    for(size_t i=0; i<indexCount - 3; i+=3)
    {
        auto i0 = mesh.Indices[i + 0];
        auto i1 = mesh.Indices[i + 1];
        auto i2 = mesh.Indices[i + 2];

        const auto& p0 = mesh.Vertices[i0].Position;
        const auto& p1 = mesh.Vertices[i1].Position;
        const auto& p2 = mesh.Vertices[i2].Position;

        // エッジ.
        auto e0 = p1 - p0;
        auto e1 = p2 - p0;

        // 面法線を算出.
        auto fn = asdx::Vector3::Cross(e0, e1);
        fn = asdx::Vector3::SafeNormalize(fn, fn);

        // 頂点法線と面法線のなす角度を算出.
        auto c0 = asdx::Vector3::Dot(normals[i0], fn);
        auto c1 = asdx::Vector3::Dot(normals[i1], fn);
        auto c2 = asdx::Vector3::Dot(normals[i2], fn);

        // スムージング処理.
        mesh.Vertices[i0].Normal = (c0 >= cosSmooth) ? normals[i0] : fn;
        mesh.Vertices[i1].Normal = (c1 >= cosSmooth) ? normals[i1] : fn;
        mesh.Vertices[i2].Normal = (c2 >= cosSmooth) ? normals[i2] : fn;
    }

    normals.clear();
}

//-----------------------------------------------------------------------------
//      接線ベクトルを計算します.
//-----------------------------------------------------------------------------
void CalcTangents(MeshOBJ& mesh)
{
    auto vertexCount = mesh.Vertices.size();

    // 接線ベクトルを初期化.
    for(size_t i=0; i<vertexCount; ++i)
    {
        mesh.Vertices[i].Tangent = asdx::Vector3(1.0f, 0.0f, 0.0f);
    }

    auto indexCount = mesh.Indices.size();
    for(size_t i=0; i<indexCount - 3; i+=3)
    {
        auto i0 = mesh.Indices[i + 0];
        auto i1 = mesh.Indices[i + 1];
        auto i2 = mesh.Indices[i + 2];

        const auto& p0 = mesh.Vertices[i0].Position;
        const auto& p1 = mesh.Vertices[i1].Position;
        const auto& p2 = mesh.Vertices[i2].Position;

        const auto& t0 = mesh.Vertices[i0].TexCoord;
        const auto& t1 = mesh.Vertices[i1].TexCoord;
        const auto& t2 = mesh.Vertices[i2].TexCoord;

        asdx::Vector3 e0, e1;
        e0.x = p1.x - p0.x;
        e0.y = t1.x - t0.x;
        e0.z = t1.y - t0.y;

        e1.x = p2.x - p0.x;
        e1.y = t2.x - t0.x;
        e1.z = t2.y - t0.y;

        auto crs = asdx::Vector3::Cross(e0, e1);
        crs = asdx::Vector3::SafeNormalize(crs, crs);
        if (fabs(crs.x) < 1e-4f)
        { crs.x = 1.0f; }

        asdx::Vector3 tan0;
        asdx::Vector3 tan1;
        asdx::Vector3 tan2;

        auto tanX = -crs.y / crs.x;

        tan0.x = tanX;
        tan1.x = tanX;
        tan2.x = tanX;

        e0.x = p1.y - p0.y;
        e1.x = p2.y - p0.y;
        crs = asdx::Vector3::Cross(e0, e1);
        crs = asdx::Vector3::SafeNormalize(crs, crs);
        if (fabs(crs.x) < 1e-4f) 
        { crs.x = 1.0f; }

        auto tanY = -crs.y / crs.x;
        tan0.y = tanY;
        tan1.y = tanY;
        tan2.y = tanY;

        e0.x = p1.z - p0.z;
        e1.x = p2.z - p0.z;
        crs = asdx::Vector3::Cross(e0, e1);
        crs = asdx::Vector3::SafeNormalize(crs, crs);
        if (fabs(crs.x) < 1e-4f) 
        { crs.x = 1.0f; }

        auto tanZ = -crs.y / crs.x;
        tan0.z = tanZ;
        tan1.z = tanZ;
        tan2.z = tanZ;

        const auto& n0 = mesh.Vertices[i0].Normal;
        const auto& n1 = mesh.Vertices[i1].Normal;
        const auto& n2 = mesh.Vertices[i2].Normal;

        auto dp0 = asdx::Vector3::Dot(tan0, n0);
        auto dp1 = asdx::Vector3::Dot(tan1, n1);
        auto dp2 = asdx::Vector3::Dot(tan2, n2);

        tan0 -= n0 * dp0;
        tan1 -= n1 * dp1;
        tan2 -= n2 * dp2;

        asdx::Vector3 T0, T1, T2;
        asdx::Vector3 B0, B1, B2;

        asdx::CalcONB(n0, T0, B1);
        asdx::CalcONB(n1, T1, B1);
        asdx::CalcONB(n2, T2, B2);

        tan0 = asdx::Vector3::SafeNormalize(tan0, T0);
        tan1 = asdx::Vector3::SafeNormalize(tan1, T1);
        tan2 = asdx::Vector3::SafeNormalize(tan2, T2);

        mesh.Vertices[i0].Tangent = tan0;
        mesh.Vertices[i1].Tangent = tan1;
        mesh.Vertices[i2].Tangent = tan2;
    }
}

//-----------------------------------------------------------------------------
//      接線ベクトルを計算します.
//-----------------------------------------------------------------------------
void CalcTangentRoughly(MeshOBJ& mesh)
{
    auto vertexCount = mesh.Vertices.size();
    for(size_t i=0; i<vertexCount; ++i)
    {
        asdx::Vector3 T, B;
        asdx::CalcONB(mesh.Vertices[i].Normal, T, B);
        mesh.Vertices[i].Tangent = T;
    }
}

} // namespace


//-----------------------------------------------------------------------------
//      ロードします.
//-----------------------------------------------------------------------------
bool OBJLoader::Load(const char* path, ModelOBJ& model)
{
    if (path == nullptr)
    {
        ELOGA("Error : Invalid Argument.");
        return false;
    }

    // ディレクトリパス取得.
    m_DirectoryPath = asdx::GetDirectoryPathA(path);

    // OBJファイルをロード.
    return LoadOBJ(path, model);
}

//-----------------------------------------------------------------------------
//      OBJファイルをロードします.
//-----------------------------------------------------------------------------
bool OBJLoader::LoadOBJ(const char* path, ModelOBJ& model)
{
    std::ifstream stream;
    stream.open(path, std::ios::in);

    if (!stream.is_open())
    {
        ELOGA("Error : File Open Failed. path = %s", path);
        return false;
    }

    char buf[OBJ_BUFFER_LENGTH] = {};
    std::string group;

    uint32_t faceIndex = 0;
    uint32_t faceCount = 0;

    std::vector<asdx::Vector3>  positions;
    std::vector<asdx::Vector3>  normals;
    std::vector<asdx::Vector2>  texcoords;
    std::vector<IndexOBJ>       indices;
    std::vector<SubsetOBJ>      subsets;

    for(;;)
    {
        stream >> buf;
        if (!stream || stream.eof())
            break;

        if (0 == strcmp(buf, "#"))
        {
            /* DO_NOTHING */
        }
        else if (0 == strcmp(buf, "v"))
        {
            asdx::Vector3 v;
            stream >> v.x >> v.y >> v.z;
            positions.push_back(v);
        }
        else if (0 == strcmp(buf, "vt"))
        {
            asdx::Vector2 vt;
            stream >> vt.x >> vt.y;
            texcoords.push_back(vt);
        }
        else if (0 == strcmp(buf, "vn"))
        {
            asdx::Vector3 vn;
            stream >> vn.x >> vn.y >> vn.z;
            normals.push_back(vn);
        }
        else if (0 == strcmp(buf, "g"))
        {
            stream >> group;
        }
        else if (0 == strcmp(buf, "f"))
        {
            uint32_t ip, it, in;
            uint32_t p[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            uint32_t t[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
            uint32_t n[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };

            uint32_t count = 0;
            uint32_t index = 0;

            faceIndex++;
            faceCount++;

            for(auto i=0; i<4; ++i)
            {
                count++;

                // 位置座標インデックス.
                stream >> ip;
                p[i] = ip - 1;

                if ('/' == stream.peek())
                {
                    stream.ignore();

                    // テクスチャ座標インデックス.
                    if ('/' != stream.peek())
                    {
                        stream >> it;
                        t[i] = it - 1;
                    }

                    // 法線インデックス.
                    if ('/' == stream.peek())
                    {
                        stream.ignore();

                        stream >> in;
                        n[i] = in - 1;
                    }
                }

                if (count <= 3)
                {
                    IndexOBJ f0 = { p[i], t[i], n[i] };
                    indices.push_back(f0);
                }

                if ('\n' == stream.peek() || '\r' == stream.peek())
                    break;
            }

            // 四角形.
            if (count > 3)
            {
                assert(count == 4);

                faceIndex++;
                faceCount++;

                IndexOBJ f0 = { p[2], t[2], n[2] };
                IndexOBJ f1 = { p[3], t[3], n[3] };
                IndexOBJ f2 = { p[0], t[0], n[0] };

                indices.push_back(f0);
                indices.push_back(f1);
                indices.push_back(f2);
            }
        }
        else if (0 == strcmp(buf, "mtllib"))
        {
            char path[256] = {};
            stream >> path;
            if (strlen(path) > 0)
            {
                if (!LoadMTL(path, model))
                {
                    ELOGA("Error : Material Load Failed.");
                    return false;
                }
            }
        }
        else if (0 == strcmp(buf, "usemtl"))
        {
            SubsetOBJ subset = {};
            stream >> subset.MaterialName;

            if (group.empty())
            { group = "group" + std::to_string(subsets.size()); }

            subset.MeshName   = group;
            subset.IndexStart = faceIndex * 3;

            auto index = subsets.size() - 1;
            subsets.push_back(subset);

            group.clear();

            if (subsets.size() > 1)
            {
                subsets[index].IndexCount = faceCount * 3;
                faceCount = 0;
            }
        }

        stream.ignore(OBJ_BUFFER_LENGTH, '\n');
    }

    if (subsets.size() > 0)
    {
        auto index = subsets.size();
        subsets[index - 1].IndexCount = faceCount * 3;
    }

    stream.close();

    model.Meshes.resize(subsets.size());

    for(size_t i=0; i<subsets.size(); ++i)
    {
        auto& subset = subsets[i];
        auto& mesh = model.Meshes[i];

        mesh.Name         = subset.MeshName;
        mesh.MaterialName = subset.MaterialName;

        mesh.Vertices.resize(subset.IndexCount);
        mesh.Indices .resize(subset.IndexCount);

        for(size_t j=0; j<subset.IndexCount; ++j)
        {
            auto id = subset.IndexStart + j;
            auto& index = indices[id];

            mesh.Vertices[j].Position = positions[index.P];
            mesh.Indices[j] = uint32_t(j);

            if (!normals.empty())
            { mesh.Vertices[j].Normal = normals[index.N]; }

            if (!texcoords.empty())
            { mesh.Vertices[j].TexCoord = texcoords[index.T]; }
        }

        if (!normals.empty())
        { CalcNormals(mesh); }

        if (!texcoords.empty())
        { CalcTangents(mesh); }
        else
        { CalcTangentRoughly(mesh); }
    }

    positions.clear();
    normals  .clear();
    texcoords.clear();
    indices  .clear();
    subsets  .clear();

    return true;
}

//-----------------------------------------------------------------------------
//      MTLファイルをロードします.
//-----------------------------------------------------------------------------
bool OBJLoader::LoadMTL(const char* path, ModelOBJ& model)
{
    std::ifstream stream;

    std::string filename = m_DirectoryPath + "/" + path;

    stream.open(filename.c_str(), std::ios::in);

    if (!stream.is_open())
    {
        ELOGA("Error : File Open Failed. path = %s", path);
        return false;
    }

    char buf[OBJ_BUFFER_LENGTH] = {};
    int32_t index = -1;

    for(;;)
    {
        stream >> buf;

        if (!stream || stream.eof())
            break;

        if (0 == strcmp(buf, "newmtl"))
        {
            index++;
            MaterialOBJ mat;
            model.Materials.push_back(mat);
            stream >> model.Materials[index].Name;
        }
        else if (0 == strcmp(buf, "Ka"))
        { stream >> model.Materials[index].Ka.x >> model.Materials[index].Ka.y >> model.Materials[index].Ka.z; }
        else if (0 == strcmp(buf, "Kd"))
        { stream >> model.Materials[index].Kd.x >> model.Materials[index].Kd.y >> model.Materials[index].Kd.z; }
        else if (0 == strcmp(buf, "Ks"))
        { stream >> model.Materials[index].Ks.x >> model.Materials[index].Ks.y >> model.Materials[index].Ks.z; }
        else if (0 == strcmp(buf, "Ke"))
        { stream >> model.Materials[index].Ke.x >> model.Materials[index].Ke.y >> model.Materials[index].Ke.z; }
        else if (0 == strcmp(buf, "d") || 0 == strcmp(buf, "Tr"))
        { stream >> model.Materials[index].Tr; }
        else if (0 == strcmp(buf, "Ns"))
        { stream >> model.Materials[index].Ns; }
        else if (0 == strcmp(buf, "map_Ka"))
        { stream >> model.Materials[index].map_Ka; }
        else if (0 == strcmp(buf, "map_Kd"))
        { stream >> model.Materials[index].map_Kd; }
        else if (0 == strcmp(buf, "map_Ks"))
        { stream >> model.Materials[index].map_Ks; }
        else if (0 == strcmp(buf, "map_Ke"))
        { stream >> model.Materials[index].map_Ke; }
        else if (0 == _stricmp(buf, "map_bump") || 0 == strcmp(buf, "bump"))
        { stream >> model.Materials[index].map_bump; }
        else if (0 == strcmp(buf, "disp"))
        { stream >> model.Materials[index].disp; }
        else if (0 == strcmp(buf, "norm"))
        { stream >> model.Materials[index].norm; }

        stream.ignore(OBJ_BUFFER_LENGTH, '\n');
    }

    // ファイルを閉じる.
    stream.close();

    // メモリ最適化.
    model.Materials.shrink_to_fit();

    // 正常終了.
    return true;
}
