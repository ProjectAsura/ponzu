//-----------------------------------------------------------------------------
// File : test_scene.cpp
// Desc : Test Scene.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <RendererApp.h>
#include <fnd/asdxLogger.h>


namespace r3d {

bool Renderer::BuildTestScene()
{
    auto pDevice = asdx::GetD3D12Device();

    Light dirLight = {};
    dirLight.Type       = LIGHT_TYPE_DIRECTIONAL;
    dirLight.Position   = asdx::Vector3(0.0f, -1.0f, 1.0f);
    dirLight.Intensity  = asdx::Vector3(1.0f, 1.0f, 1.0f) * 2.0f;
    dirLight.Radius     = 1.0f;

    std::vector<r3d::MeshInfo> infos;
    std::vector<r3d::Mesh> meshes;
    if (!LoadMesh("../res/model/dosei_quad.obj", meshes, infos))
    {
        ELOGA("Error : LoadMesh() Failed.");
        return false;
    }

    std::vector<r3d::CpuInstance> instances;
    instances.resize(meshes.size());

    for(size_t i=0; i<meshes.size(); ++i)
    {
        instances[i].MaterialId = 0;
        instances[i].MeshId     = uint32_t(i);
        instances[i].Transform  = asdx::FromMatrix(asdx::Matrix::CreateTranslation(-1.0f, 3.0f, 0.0f) * asdx::Matrix::CreateRotationY(asdx::F_PIDIV2));
    }

    //if (instances.size() >= 3)
    //{ instances[2].MaterialId = 1; } // pole

    //if (instances.size() >= 11)
    //{ instances[10].MaterialId = 2; } // wave

    //if (instances.size() >= 2)
    //{ instances[1].MaterialId = 0; } // bridge

    //if (instances.size() >= 1)
    //{ instances[0].MaterialId = 3; } // bottom.

    //instances[3].MaterialId = 4;
    //instances[4].MaterialId = 5;
    //instances[5].MaterialId = 6;
    //instances[6].MaterialId = 7;
    //instances[7].MaterialId = 8;
    //instances[8].MaterialId = 9;
    //instances[9].MaterialId = 10;

    Material dummy0 = Material::Default();
    dummy0.Ior = 1.23f;

    // mat0
    Material planks = Material::Default();
    planks.BaseColorMap = 0;
    planks.NormalMap    = 1;
    planks.OrmMap       = 2; 

    // mat1
    Material poles = Material::Default();
    poles.BaseColorMap = 3;
    poles.NormalMap    = 4;
    poles.OrmMap       = 5;

    // mat2
    Material wave = Material::Default();
    wave.BaseColorMap = INVALID_MATERIAL_MAP;
    wave.NormalMap    = 9;
    wave.OrmMap       = INVALID_MATERIAL_MAP;
    wave.EmissiveMap  = INVALID_MATERIAL_MAP;
    wave.Ior          = 1.2f;

    //// mat3
    //Material ground = Material::Default();
    //ground.BaseColor0 = 6;
    //ground.Normal0    = 7;
    //ground.ORM0       = 8;
    //ground.UvScale0   = asdx::Vector2(500.0f, 500.0f);
    ////ground.UvScroll  = asdx::Vector2(0.1f, 0.0f);

    //// mat4
    //Material aft = Material::Default();
    //aft.BaseColor0 = 11;
    //aft.Normal0 = 12;
    //aft.ORM0 = 13;

    //// mat5
    //Material deck = Material::Default();
    //deck.BaseColor0 = 14;
    //deck.Normal0 = 15;
    //deck.ORM0 = 16;

    //// mat6
    //Material details = Material::Default();
    //details.BaseColor0 = 17;
    //details.Normal0 = 18;
    //details.ORM0 = 19;

    //// mat7
    //Material hull = Material::Default();
    //hull.BaseColor0 = 20;
    //hull.Normal0 = 21;
    //hull.ORM0 = 22;

    //// mat8
    //Material interior = Material::Default();
    //interior.BaseColor0 = 23;
    //interior.Normal0 = 24;
    //interior.ORM0 = 25;

    //// mat9
    //Material rigging = Material::Default();
    //rigging.BaseColor0 = 26;
    //rigging.Normal0 = 27;
    //rigging.ORM0 = 28;

    //// mat10
    //Material sails = Material::Default();
    //sails.BaseColor0 = 29;
    ////sails.Normal0 = 30;
    //sails.ORM0 = 31;

    //instances[1].MaterialId = 0;
    //instances[2].MaterialId = 1;
    //instances[3].MaterialId = 2;
    //instances[0].MaterialId = 3;


    SceneExporter exporter;
    exporter.SetIBL("../res/ibl/modern_buildings_2_2k.dds");
    //exporter.AddTexture("../res/texture/modular_wooden_pier_planks_diff_2k.dds"); // 0
    //exporter.AddTexture("../res/texture/modular_wooden_pier_planks_nor_gl_2k.dds"); // 1
    //exporter.AddTexture("../res/texture/modular_wooden_pier_planks_arm_2k.dds"); // 2
    //exporter.AddTexture("../res/texture/modular_wooden_pier_poles_diff_2k.dds"); // 3
    //exporter.AddTexture("../res/texture/modular_wooden_pier_poles_nor_gl_2k.dds"); // 4
    //exporter.AddTexture("../res/texture/modular_wooden_pier_poles_arm_2k.dds"); // 5
    //exporter.AddTexture("../res/texture/coral_mud_01_diff_2k.dds"); // 6
    //exporter.AddTexture("../res/texture/coral_mud_01_nor_gl_2k.dds"); // 7
    //exporter.AddTexture("../res/texture/coral_mud_01_rough_2k.dds"); // 8
    //exporter.AddTexture("../res/texture/wave_normal.dds"); // 9
    //exporter.AddTexture("../res/texture/wave_normal1.dds"); // 10
    //exporter.AddTexture("../res/texture/ship_pinnace_aft_diff_1k.dds"); // 11
    //exporter.AddTexture("../res/texture/ship_pinnace_aft_nor_gl_1k.dds"); // 12
    //exporter.AddTexture("../res/texture/ship_pinnace_aft_arm_1k.dds"); // 13
    //exporter.AddTexture("../res/texture/ship_pinnace_deck_diff_1k.dds"); // 14
    //exporter.AddTexture("../res/texture/ship_pinnace_deck_nor_gl_1k.dds"); // 15
    //exporter.AddTexture("../res/texture/ship_pinnace_deck_arm_1k.dds"); // 16
    //exporter.AddTexture("../res/texture/ship_pinnace_details_diff_1k.dds"); // 17
    //exporter.AddTexture("../res/texture/ship_pinnace_details_nor_gl_1k.dds"); // 18
    //exporter.AddTexture("../res/texture/ship_pinnace_details_arm_1k.dds"); // 19
    //exporter.AddTexture("../res/texture/ship_pinnace_hull_diff_1k.dds"); // 20
    //exporter.AddTexture("../res/texture/ship_pinnace_hull_nor_gl_1k.dds"); // 21
    //exporter.AddTexture("../res/texture/ship_pinnace_hull_arm_1k.dds"); // 22
    //exporter.AddTexture("../res/texture/ship_pinnace_interior_diff_1k.dds"); // 23
    //exporter.AddTexture("../res/texture/ship_pinnace_interior_nor_gl_1k.dds"); // 24
    //exporter.AddTexture("../res/texture/ship_pinnace_interior_arm_1k.dds"); // 25
    //exporter.AddTexture("../res/texture/ship_pinnace_rigging_diff_1k.dds"); // 26
    //exporter.AddTexture("../res/texture/ship_pinnace_rigging_nor_gl_1k.dds"); // 27
    //exporter.AddTexture("../res/texture/ship_pinnace_rigging_arm_1k.dds"); // 28
    //exporter.AddTexture("../res/texture/ship_pinnace_sails_diff_1k.dds"); // 29
    //exporter.AddTexture("../res/texture/ship_pinnace_sails_nor_gl_1k.dds"); // 30
    //exporter.AddTexture("../res/texture/ship_pinnace_sails_orm_1k.dds"); // 31
    exporter.AddMeshes(meshes);
    //exporter.AddMaterial(planks);   // 0
    //exporter.AddMaterial(poles);    // 1
    //exporter.AddMaterial(wave);     // 2
    //exporter.AddMaterial(ground);   // 3
    //exporter.AddMaterial(aft);      // 4
    //exporter.AddMaterial(deck);     // 5
    //exporter.AddMaterial(details);  // 6
    //exporter.AddMaterial(hull);     // 7
    //exporter.AddMaterial(interior); // 8
    //exporter.AddMaterial(rigging);  // 9
    //exporter.AddMaterial(sails);    // 10
    exporter.AddMaterial(dummy0);
    //exporter.AddMaterial(dummy1);
    //exporter.AddMaterial(dummy2);
    exporter.AddInstances(instances);
    exporter.AddLight(dirLight);

    const char* exportPath = "../res/scene/test.scn";

    if (!exporter.Export(exportPath))
    {
        ELOGA("Error : SceneExporter::Export() Failed.");
        return false;
    }

    // シーン構築.
    {
        if (!m_Scene.Init(exportPath, m_GfxCmdList.GetCommandList()))
        {
            ELOGA("Error : Scene::Init() Failed.");
            return false;
        }
    }

    //// IBL読み込み.
    //{
    //    std::string path;
    //    if (!asdx::SearchFilePathA("../res/ibl/studio_garden_2k.dds", path))
    //    {
    //        ELOGA("Error : IBL File Not Found.");
    //        return false;
    //    }

    //    asdx::ResTexture res;
    //    if (!res.LoadFromFileA(path.c_str()))
    //    {
    //        ELOGA("Error : IBL Load Failed.");
    //        return false;
    //    }

    //    if (!m_IBL.Init(m_GfxCmdList, res))
    //    {
    //        ELOGA("Error : IBL Init Failed.");
    //        return false;
    //    }
    //}

    //// テクスチャ.
    //{
    //    std::string path;
    //    if (!asdx::SearchFilePathA("../res/texture/floor_tiles_08_diff_2k.dds", path))
    //    {
    //        ELOGA("Error : Texture Not Found.");
    //        return false;
    //    }

    //    asdx::ResTexture res;
    //    if (!res.LoadFromFileA(path.c_str()))
    //    {
    //        ELOGA("Error : Texture Load Failed.");
    //        return false;
    //    }

    //    if (!m_PlaneBC.Init(m_GfxCmdList, res))
    //    {
    //        ELOGA("Error : Texture Init Failed.");
    //        return false;
    //    }
    //}

    //// Test
    //{
    //    const char* rawPath = "../res/model/dosei_with_ground.obj";
    //    ModelOBJ model;
    //    OBJLoader loader;
    //    std::string path;
    //    if (!asdx::SearchFilePathA(rawPath, path))
    //    {
    //        ELOGA("Error : File Path Not Found. path = %s", rawPath);
    //        return false;
    //    }

    //    if (!loader.Load(path.c_str(), model))
    //    {
    //        ELOGA("Error : Model Load Failed.");
    //        return false;
    //    }

    //    Material dummy0 = {};
    //    dummy0.Normal    = INVALID_MATERIAL_MAP;
    //    dummy0.BaseColor = INVALID_MATERIAL_MAP;
    //    dummy0.ORM       = INVALID_MATERIAL_MAP;
    //    dummy0.Emissive  = INVALID_MATERIAL_MAP;
    //    dummy0.IntIor    = 1.4f;
    //    dummy0.ExtIor    = 1.0f;
    //    dummy0.UvScale   = asdx::Vector2(1.0f, 1.0f);
    //    m_ModelMgr.AddMaterials(&dummy0, 1);

    //    Material dummy1 = {};
    //    dummy1.Normal    = INVALID_MATERIAL_MAP;
    //    dummy1.BaseColor = m_PlaneBC.GetView()->GetDescriptorIndex();
    //    dummy1.ORM       = INVALID_MATERIAL_MAP;
    //    dummy1.Emissive  = INVALID_MATERIAL_MAP;
    //    //dummy0.IntIor    = 1.4f;
    //    //dummy0.ExtIor    = 1.0f;
    //    dummy1.UvScale   = asdx::Vector2(10.0f, 10.0f);
    //    m_ModelMgr.AddMaterials(&dummy1, 1);

    //    auto meshCount = model.Meshes.size();

    //    m_BLAS.resize(meshCount);

    //    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    //    instanceDescs.resize(meshCount);

    //    m_MeshDrawCalls.resize(meshCount);
 
    //    for(size_t i=0; i<meshCount; ++i)
    //    {
    //        r3d::Mesh mesh = {};
    //        mesh.VertexCount = uint32_t(model.Meshes[i].Vertices.size());
    //        mesh.Vertices    = reinterpret_cast<Vertex*>(model.Meshes[i].Vertices.data());
    //        mesh.IndexCount  = uint32_t(model.Meshes[i].Indices.size());
    //        mesh.Indices     = model.Meshes[i].Indices.data();

    //        auto geometryHandle = m_ModelMgr.AddMesh(mesh);

    //        r3d::CpuInstance instance = {};
    //        instance.Transform      = asdx::Transform3x4();
    //        instance.MeshId         = uint32_t(i);
    //        instance.MaterialId     = (i != 3) ? 0 : 1;
    //        //instance.MaterialId     = 0;

    //        auto instanceHandle = m_ModelMgr.AddInstance(instance);

    //        D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
    //        desc.Type                                   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    //        desc.Flags                                  = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    //        desc.Triangles.Transform3x4                 = instanceHandle.AddressTB;
    //        desc.Triangles.IndexFormat                  = DXGI_FORMAT_R32_UINT;
    //        desc.Triangles.IndexCount                   = mesh.IndexCount;
    //        desc.Triangles.IndexBuffer                  = geometryHandle.AddressIB;
    //        desc.Triangles.VertexFormat                 = DXGI_FORMAT_R32G32B32_FLOAT;
    //        desc.Triangles.VertexBuffer.StartAddress    = geometryHandle.AddressVB;
    //        desc.Triangles.VertexBuffer.StrideInBytes   = sizeof(Vertex);
    //        desc.Triangles.VertexCount                  = mesh.VertexCount;

    //        if (!m_BLAS[i].Init(
    //            pDevice,
    //            1,
    //            &desc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE))
    //        {
    //            ELOGA("Error : Blas::Init() Failed.");
    //            return false;
    //        }

    //        // ビルドコマンドを積んでおく.
    //        m_BLAS[i].Build(m_GfxCmdList.GetCommandList());

    //        memcpy(instanceDescs[i].Transform, instance.Transform.m, sizeof(float) * 12);
    //        instanceDescs[i].InstanceID                             = instanceHandle.InstanceId;
    //        instanceDescs[i].InstanceMask                           = 0xFF;
    //        instanceDescs[i].InstanceContributionToHitGroupIndex    = 0;
    //        instanceDescs[i].Flags                                  = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    //        instanceDescs[i].AccelerationStructure                  = m_BLAS[i].GetResource()->GetGPUVirtualAddress();

    //        D3D12_VERTEX_BUFFER_VIEW vbv = {};
    //        vbv.BufferLocation = geometryHandle.AddressVB;
    //        vbv.SizeInBytes    = sizeof(Vertex) * mesh.VertexCount;
    //        vbv.StrideInBytes  = sizeof(Vertex);

    //        D3D12_INDEX_BUFFER_VIEW ibv = {};
    //        ibv.BufferLocation = geometryHandle.AddressIB;
    //        ibv.SizeInBytes    = sizeof(uint32_t) * mesh.IndexCount;
    //        ibv.Format         = DXGI_FORMAT_R32_UINT;

    //        m_MeshDrawCalls[i].StartIndex = 0;
    //        m_MeshDrawCalls[i].IndexCount = mesh.IndexCount;
    //        m_MeshDrawCalls[i].BaseVertex = 0;
    //        m_MeshDrawCalls[i].InstanceId = instanceHandle.InstanceId;
    //        m_MeshDrawCalls[i].VBV        = vbv;
    //        m_MeshDrawCalls[i].IBV        = ibv;
    //    }

    //    auto instanceCount = uint32_t(instanceDescs.size());
    //    if (!m_TLAS.Init(
    //        pDevice,
    //        instanceCount,
    //        instanceDescs.data(),
    //        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE))
    //    {
    //        ELOGA("Error : Tlas::Init() Failed.");
    //        return false;
    //    }

    //    // ビルドコマンドを積んでおく.
    //    m_TLAS.Build(m_GfxCmdList.GetCommandList());
    //}

    //// ライト生成.
    //{
    //}


    return true;
}


} // namespace
