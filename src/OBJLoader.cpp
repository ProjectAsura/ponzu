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


//-----------------------------------------------------------------------------
//      ロードします.
//-----------------------------------------------------------------------------
bool OBJLoader::Load(const char* path, MeshOBJ& mesh)
{
    if (path == nullptr)
    {
        ELOGA("Error : Invalid Argument.");
        return false;
    }

    // ディレクトリパス取得.
    m_DirectoryPath = asdx::GetDirectoryPathA(path);

    // OBJファイルをロード.
    return LoadOBJ(path, mesh);
}

//-----------------------------------------------------------------------------
//      OBJファイルをロードします.
//-----------------------------------------------------------------------------
bool OBJLoader::LoadOBJ(const char* path, MeshOBJ& mesh)
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
            mesh.Positions.push_back(v);
        }
        else if (0 == strcmp(buf, "vt"))
        {
            asdx::Vector2 vt;
            stream >> vt.x >> vt.y;
            mesh.TexCoords.push_back(vt);
        }
        else if (0 == strcmp(buf, "vn"))
        {
            asdx::Vector3 vn;
            stream >> vn.x >> vn.y >> vn.z;
            mesh.Normals.push_back(vn);
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

                if (count < 3)
                {
                    IndexOBJ f0 = { p[i], t[i], n[i] };
                    mesh.Indices.push_back(f0);
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

                mesh.Indices.push_back(f0);
                mesh.Indices.push_back(f1);
                mesh.Indices.push_back(f2);
            }
        }
        else if (0 == strcmp(buf, "mtllib"))
        {
            char path[256] = {};
            stream >> path;
            if (strlen(path) > 0)
            {
                if (!LoadMTL(path, mesh))
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
            { group = "group" + std::to_string(mesh.Subsets.size()); }

            subset.MeshName   = group;
            subset.IndexStart = faceIndex * 3;

            auto index = mesh.Subsets.size();
            mesh.Subsets.push_back(subset);

            group.clear();

            if (mesh.Subsets.size() > 1)
            {
                mesh.Subsets[index].IndexCount = faceCount * 3;
                faceCount = 0;
            }
        }

        stream.ignore(OBJ_BUFFER_LENGTH, '\n');
    }

    if (mesh.Subsets.size() > 0)
    {
        auto index = mesh.Subsets.size();
        mesh.Subsets[index - 1].IndexCount = faceCount * 3;
    }

    stream.close();

    // メモリ最適化.
    mesh.Positions  .shrink_to_fit();
    mesh.Normals    .shrink_to_fit();
    mesh.TexCoords  .shrink_to_fit();
    mesh.Subsets    .shrink_to_fit();
    mesh.Materials  .shrink_to_fit();

    return true;
}

//-----------------------------------------------------------------------------
//      MTLファイルをロードします.
//-----------------------------------------------------------------------------
bool OBJLoader::LoadMTL(const char* path, MeshOBJ& mesh)
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
            mesh.Materials.push_back(mat);
            stream >> mesh.Materials[index].Name;
        }
        else if (0 == strcmp(buf, "Ka"))
        {
            stream >> mesh.Materials[index].Ka.x >> mesh.Materials[index].Ka.y >> mesh.Materials[index].Ka.z;
        }
        else if (0 == strcmp(buf, "Kd"))
        {
            stream >> mesh.Materials[index].Kd.x >> mesh.Materials[index].Kd.y >> mesh.Materials[index].Kd.z;
        }
        else if (0 == strcmp(buf, "Ks"))
        {
            stream >> mesh.Materials[index].Ks.x >> mesh.Materials[index].Ks.y >> mesh.Materials[index].Ks.z;
        }
        else if (0 == strcmp(buf, "Ke"))
        {
            stream >> mesh.Materials[index].Ke.x >> mesh.Materials[index].Ke.y >> mesh.Materials[index].Ke.z;
        }
        else if (0 == strcmp(buf, "d") || 0 == strcmp(buf, "Tr"))
        {
            stream >> mesh.Materials[index].Tr;
        }
        else if (0 == strcmp(buf, "Ns"))
        {
            stream >> mesh.Materials[index].Ns;
        }
        else if (0 == strcmp(buf, "map_Ka"))
        {
            stream >> mesh.Materials[index].map_Ka;
        }
        else if (0 == strcmp(buf, "map_Kd"))
        {
            stream >> mesh.Materials[index].map_Kd;
        }
        else if (0 == strcmp(buf, "map_Ks"))
        {
            stream >> mesh.Materials[index].map_Ks;
        }
        else if (0 == strcmp(buf, "map_Ke"))
        {
            stream >> mesh.Materials[index].map_Ke;
        }
        else if (0 == _stricmp(buf, "map_bump") || 0 == strcmp(buf, "bump"))
        {
            stream >> mesh.Materials[index].map_bump;
        }
        else if (0 == strcmp(buf, "disp"))
        {
            stream >> mesh.Materials[index].disp;
        }
        else if (0 == strcmp(buf, "norm"))
        {
            stream >> mesh.Materials[index].norm;
        }

        stream.ignore(OBJ_BUFFER_LENGTH, '\n');
    }

    // ファイルを閉じる.
    stream.close();

    // 正常終了.
    return true;
}
