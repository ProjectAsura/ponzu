//-----------------------------------------------------------------------------
// File : scene.cpp
// Desc : Scene Data.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <scene.h>
#include <fnd/asdxLogger.h>
#include "../external/tinyxml2/tinyxml2.h"


namespace {

///////////////////////////////////////////////////////////////////////////////
// ElementMesh structure
///////////////////////////////////////////////////////////////////////////////
struct ElementMesh
{
    std::string         Name;
    std::string         Path;
};

///////////////////////////////////////////////////////////////////////////////
// ElementIstance structure
///////////////////////////////////////////////////////////////////////////////
struct ElementInstance
{
    std::string         Mesh;
    asdx::Transform3x4  Transform;
};

///////////////////////////////////////////////////////////////////////////////
// ElementIBL structure
///////////////////////////////////////////////////////////////////////////////
struct ElementIBL
{
    std::string     Path;
    float           Intensity;
};


//-----------------------------------------------------------------------------
//      変換行列をシリアライズします.
//-----------------------------------------------------------------------------
tinyxml2::XMLElement* Serialize(tinyxml2::XMLDocument* doc, const asdx::Transform3x4& src)
{
    auto element = doc->NewElement("transform");

    element->SetAttribute("m00", src.m[0][0]);
    element->SetAttribute("m01", src.m[0][1]);
    element->SetAttribute("m02", src.m[0][2]);
    element->SetAttribute("m03", src.m[0][3]);

    element->SetAttribute("m10", src.m[1][0]);
    element->SetAttribute("m11", src.m[1][1]);
    element->SetAttribute("m12", src.m[1][2]);
    element->SetAttribute("m13", src.m[1][3]);

    element->SetAttribute("m20", src.m[2][0]);
    element->SetAttribute("m21", src.m[2][1]);
    element->SetAttribute("m22", src.m[2][2]);
    element->SetAttribute("m23", src.m[2][3]);

    return element;
}

//-----------------------------------------------------------------------------
//      メッシュエレメントをシリアライズします.
//-----------------------------------------------------------------------------
tinyxml2::XMLElement* Serialize(tinyxml2::XMLDocument* doc, const ElementMesh& src)
{
    auto element = doc->NewElement("mesh");
    element->SetAttribute("name", src.Name.c_str());
    element->SetAttribute("path", src.Path.c_str());
    return element;
}

//-----------------------------------------------------------------------------
//      IBLエレメントをシリアライズします.
//-----------------------------------------------------------------------------
tinyxml2::XMLElement* Serialize(tinyxml2::XMLDocument* doc, const ElementIBL& src)
{
    auto element = doc->NewElement("ibl");
    element->SetAttribute("path", src.Path.c_str());
    element->SetAttribute("intensity", src.Intensity);
    return element;
}

//-----------------------------------------------------------------------------
//      インスタンスエレメントをシリアライズします.
//-----------------------------------------------------------------------------
tinyxml2::XMLElement* Serialize(tinyxml2::XMLDocument* doc, const ElementInstance& src)
{
    auto element = doc->NewElement("instance");
    element->SetAttribute("mesh", src.Mesh.c_str());
    element->InsertEndChild(Serialize(doc, src.Transform));
    return element;
}

//-----------------------------------------------------------------------------
//      変換行列をデシリアライズします.
//-----------------------------------------------------------------------------
void Deserialize(tinyxml2::XMLElement* element, asdx::Transform3x4& dst)
{
    auto e = element->FirstChildElement("transform");
    if (e == nullptr)
    { return; }

    dst.m[0][0] = e->FloatAttribute("m00", 1.0f);
    dst.m[0][1] = e->FloatAttribute("m01", 0.0f);
    dst.m[0][2] = e->FloatAttribute("m02", 0.0f);
    dst.m[0][3] = e->FloatAttribute("m03", 0.0f);

    dst.m[1][0] = e->FloatAttribute("m10", 0.0f);
    dst.m[1][1] = e->FloatAttribute("m11", 1.0f);
    dst.m[1][2] = e->FloatAttribute("m12", 0.0f);
    dst.m[1][3] = e->FloatAttribute("m13", 0.0f);

    dst.m[2][0] = e->FloatAttribute("m20", 0.0f);
    dst.m[2][1] = e->FloatAttribute("m21", 0.0f);
    dst.m[2][2] = e->FloatAttribute("m22", 1.0f);
    dst.m[2][3] = e->FloatAttribute("m23", 0.0f);
}

//-----------------------------------------------------------------------------
//      メッシュエレメントをデシリアライズします.
//-----------------------------------------------------------------------------
void Deserialize(tinyxml2::XMLElement* element, ElementMesh& dst)
{
    auto e = element->FirstChildElement("mesh");
    if (e == nullptr)
    { return; }

    dst.Name = e->Attribute("name", "");
    dst.Path = e->Attribute("path", "");
}

//-----------------------------------------------------------------------------
//      IBLエレメントをデシリアライズします.
//-----------------------------------------------------------------------------
void Deserialize(tinyxml2::XMLElement* element, ElementIBL& dst)
{
    auto e = element->FirstChildElement("ibl");
    if (e == nullptr)
    { return; }

    dst.Path      = e->Attribute("path", "");
    dst.Intensity = e->FloatAttribute("intensity", 1.0f);
}

//-----------------------------------------------------------------------------
//      インスタンスエレメントをデシリアライズします.
//-----------------------------------------------------------------------------
void Deserialize(tinyxml2::XMLElement* element, ElementInstance& dst)
{
    auto e = element->FirstChildElement("instance");
    if (e == nullptr)
    { return; }

    dst.Mesh = e->Attribute("mesh", "");
    Deserialize(e, dst.Transform);
}

} // namespace


namespace r3d {

///////////////////////////////////////////////////////////////////////////////
// Scene class
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//      XMLからロードします.
//-----------------------------------------------------------------------------
bool Scene::InitFromXml(const char* path)
{
    if (path == nullptr)
    {
        ELOGA("Error : Invalid Argument.");
        return false;
    }

    // モデルマネージャ初期化.
    if (!m_ModelMgr.Init(UINT16_MAX, UINT16_MAX))
    {
        ELOGA("Error : ModelMgr::Init() Failed.");
        return false;
    }

    tinyxml2::XMLDocument doc;
    auto err = doc.LoadFile(path);
    if (err != tinyxml2::XML_SUCCESS)
    {
        ELOGA("Error : Load XML Failed. path = %s", path);
        return false;
    }

    auto root = doc.FirstChildElement("root");
    if (root == nullptr)
    {
        ELOGA("Error : Root Element Not Found.");
        doc.Clear();
        return false;
    }

    std::vector<ElementMesh> elementMeshes;
    for(auto element = root->FirstChildElement("mesh");
        element != nullptr;
        element = element->NextSiblingElement("mesh"))
    {
        ElementMesh temp;
        Deserialize(element, temp);
        elementMeshes.emplace_back(temp);
    }

    std::vector<ElementInstance> elementInstances;
    for(auto element = root->FirstChildElement("instance");
        element != nullptr;
        element = element->NextSiblingElement("instance"))
    {
        ElementInstance temp;
        Deserialize(element, temp);
        elementInstances.emplace_back(temp);
    }

    ElementIBL elementIbl;
    Deserialize(root, elementIbl);

    // IBLデータ生成.
    if (!elementIbl.Path.empty())
    {
        asdx::ResTexture texture;
    }

    std::vector<Material> resMaterials;
    std::vector<Mesh> resMeshes;
    if (!elementMeshes.empty())
    {
    }

    // インスタンスデータ構築.
    if (!elementInstances.empty())
    {
    }

    // 定数バッファ作成.
    {
    }


    return true;
}

//-----------------------------------------------------------------------------
//      終了処理を行います.
//-----------------------------------------------------------------------------
void Scene::Term()
{
    for(size_t i=0; i<m_BLAS.size(); ++i)
    { m_BLAS[i].Term(); }
    m_BLAS.clear();

    m_TLAS    .Term();
    m_IBL     .Term();
    m_Param   .Term();
    m_ModelMgr.Term();

    m_DrawCalls.clear();
}

//-----------------------------------------------------------------------------
//      定数バッファを取得します.
//-----------------------------------------------------------------------------
asdx::IConstantBufferView* Scene::GetParamCBV() const
{ return m_Param.GetView(); }

//-----------------------------------------------------------------------------
//      インスタンスバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetIB() const
{ return m_ModelMgr.GetIB(); }

//-----------------------------------------------------------------------------
//      トランスフォームバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetTB() const
{ return m_ModelMgr.GetTB(); }

//-----------------------------------------------------------------------------
//      マテリアルバッファのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetMB() const
{ return m_ModelMgr.GetMB(); }

//-----------------------------------------------------------------------------
//      IBLのシェーダリソースビューを取得します.
//-----------------------------------------------------------------------------
asdx::IShaderResourceView* Scene::GetIBL() const
{ return m_IBL.GetView(); }

//-----------------------------------------------------------------------------
//      描画処理を行います.
//-----------------------------------------------------------------------------
void Scene::Draw(ID3D12GraphicsCommandList* pCmdList)
{
    pCmdList->SetGraphicsRootConstantBufferView(0, m_Param.GetResource()->GetGPUVirtualAddress());
    pCmdList->SetGraphicsRootShaderResourceView(2, m_ModelMgr.GetAddressTB());
    pCmdList->SetGraphicsRootShaderResourceView(3, m_ModelMgr.GetAddressMB());
    pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto count = m_DrawCalls.size();
    for(size_t i=0; i<count; ++i)
    {
        auto& dc = m_DrawCalls[i];
        pCmdList->IASetVertexBuffers(0, 1, &dc.VBV);
        pCmdList->SetGraphicsRoot32BitConstant(1, dc.InstanceId, 0);
        pCmdList->DrawIndexedInstanced(dc.IndexCount, 1, 0, 0, 0);
    }
}

} // namespace r3d
