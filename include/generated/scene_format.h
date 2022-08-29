// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_SCENE_R3D_H_
#define FLATBUFFERS_GENERATED_SCENE_R3D_H_

#include "flatbuffers/flatbuffers.h"

namespace r3d {

struct Vector2;

struct Vector3;

struct Vector4;

struct Matrix3x4;

struct ResVertex;

struct SubResource;
struct SubResourceBuilder;

struct ResTexture;
struct ResTextureBuilder;

struct ResMaterial;

struct ResMesh;
struct ResMeshBuilder;

struct ResInstance;

struct ResLight;

struct ResScene;
struct ResSceneBuilder;

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(4) Vector2 FLATBUFFERS_FINAL_CLASS {
 private:
  float x_;
  float y_;

 public:
  Vector2()
      : x_(0),
        y_(0) {
  }
  Vector2(float _x, float _y)
      : x_(flatbuffers::EndianScalar(_x)),
        y_(flatbuffers::EndianScalar(_y)) {
  }
  float x() const {
    return flatbuffers::EndianScalar(x_);
  }
  float y() const {
    return flatbuffers::EndianScalar(y_);
  }
};
FLATBUFFERS_STRUCT_END(Vector2, 8);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(4) Vector3 FLATBUFFERS_FINAL_CLASS {
 private:
  float x_;
  float y_;
  float z_;

 public:
  Vector3()
      : x_(0),
        y_(0),
        z_(0) {
  }
  Vector3(float _x, float _y, float _z)
      : x_(flatbuffers::EndianScalar(_x)),
        y_(flatbuffers::EndianScalar(_y)),
        z_(flatbuffers::EndianScalar(_z)) {
  }
  float x() const {
    return flatbuffers::EndianScalar(x_);
  }
  float y() const {
    return flatbuffers::EndianScalar(y_);
  }
  float z() const {
    return flatbuffers::EndianScalar(z_);
  }
};
FLATBUFFERS_STRUCT_END(Vector3, 12);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(4) Vector4 FLATBUFFERS_FINAL_CLASS {
 private:
  float x_;
  float y_;
  float z_;
  float w_;

 public:
  Vector4()
      : x_(0),
        y_(0),
        z_(0),
        w_(0) {
  }
  Vector4(float _x, float _y, float _z, float _w)
      : x_(flatbuffers::EndianScalar(_x)),
        y_(flatbuffers::EndianScalar(_y)),
        z_(flatbuffers::EndianScalar(_z)),
        w_(flatbuffers::EndianScalar(_w)) {
  }
  float x() const {
    return flatbuffers::EndianScalar(x_);
  }
  float y() const {
    return flatbuffers::EndianScalar(y_);
  }
  float z() const {
    return flatbuffers::EndianScalar(z_);
  }
  float w() const {
    return flatbuffers::EndianScalar(w_);
  }
};
FLATBUFFERS_STRUCT_END(Vector4, 16);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(4) Matrix3x4 FLATBUFFERS_FINAL_CLASS {
 private:
  r3d::Vector4 row0_;
  r3d::Vector4 row1_;
  r3d::Vector4 row2_;

 public:
  Matrix3x4()
      : row0_(),
        row1_(),
        row2_() {
  }
  Matrix3x4(const r3d::Vector4 &_row0, const r3d::Vector4 &_row1, const r3d::Vector4 &_row2)
      : row0_(_row0),
        row1_(_row1),
        row2_(_row2) {
  }
  const r3d::Vector4 &row0() const {
    return row0_;
  }
  const r3d::Vector4 &row1() const {
    return row1_;
  }
  const r3d::Vector4 &row2() const {
    return row2_;
  }
};
FLATBUFFERS_STRUCT_END(Matrix3x4, 48);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(4) ResVertex FLATBUFFERS_FINAL_CLASS {
 private:
  r3d::Vector3 Position_;
  r3d::Vector3 Normal_;
  r3d::Vector3 Tangent_;
  r3d::Vector2 TexCoord_;

 public:
  ResVertex()
      : Position_(),
        Normal_(),
        Tangent_(),
        TexCoord_() {
  }
  ResVertex(const r3d::Vector3 &_Position, const r3d::Vector3 &_Normal, const r3d::Vector3 &_Tangent, const r3d::Vector2 &_TexCoord)
      : Position_(_Position),
        Normal_(_Normal),
        Tangent_(_Tangent),
        TexCoord_(_TexCoord) {
  }
  const r3d::Vector3 &Position() const {
    return Position_;
  }
  const r3d::Vector3 &Normal() const {
    return Normal_;
  }
  const r3d::Vector3 &Tangent() const {
    return Tangent_;
  }
  const r3d::Vector2 &TexCoord() const {
    return TexCoord_;
  }
};
FLATBUFFERS_STRUCT_END(ResVertex, 44);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(4) ResMaterial FLATBUFFERS_FINAL_CLASS {
 private:
  uint32_t BaseColor_;
  uint32_t Normal_;
  uint32_t Orm_;
  uint32_t Emissive_;
  float IntIor_;
  float ExtIor_;
  r3d::Vector2 UvScale_;
  r3d::Vector2 UvScroll_;

 public:
  ResMaterial()
      : BaseColor_(0),
        Normal_(0),
        Orm_(0),
        Emissive_(0),
        IntIor_(0),
        ExtIor_(0),
        UvScale_(),
        UvScroll_() {
  }
  ResMaterial(uint32_t _BaseColor, uint32_t _Normal, uint32_t _Orm, uint32_t _Emissive, float _IntIor, float _ExtIor, const r3d::Vector2 &_UvScale, const r3d::Vector2 &_UvScroll)
      : BaseColor_(flatbuffers::EndianScalar(_BaseColor)),
        Normal_(flatbuffers::EndianScalar(_Normal)),
        Orm_(flatbuffers::EndianScalar(_Orm)),
        Emissive_(flatbuffers::EndianScalar(_Emissive)),
        IntIor_(flatbuffers::EndianScalar(_IntIor)),
        ExtIor_(flatbuffers::EndianScalar(_ExtIor)),
        UvScale_(_UvScale),
        UvScroll_(_UvScroll) {
  }
  uint32_t BaseColor() const {
    return flatbuffers::EndianScalar(BaseColor_);
  }
  uint32_t Normal() const {
    return flatbuffers::EndianScalar(Normal_);
  }
  uint32_t Orm() const {
    return flatbuffers::EndianScalar(Orm_);
  }
  uint32_t Emissive() const {
    return flatbuffers::EndianScalar(Emissive_);
  }
  float IntIor() const {
    return flatbuffers::EndianScalar(IntIor_);
  }
  float ExtIor() const {
    return flatbuffers::EndianScalar(ExtIor_);
  }
  const r3d::Vector2 &UvScale() const {
    return UvScale_;
  }
  const r3d::Vector2 &UvScroll() const {
    return UvScroll_;
  }
};
FLATBUFFERS_STRUCT_END(ResMaterial, 40);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(4) ResInstance FLATBUFFERS_FINAL_CLASS {
 private:
  uint32_t MeshIndex_;
  uint32_t MaterialIndex_;
  r3d::Matrix3x4 Transform_;

 public:
  ResInstance()
      : MeshIndex_(0),
        MaterialIndex_(0),
        Transform_() {
  }
  ResInstance(uint32_t _MeshIndex, uint32_t _MaterialIndex, const r3d::Matrix3x4 &_Transform)
      : MeshIndex_(flatbuffers::EndianScalar(_MeshIndex)),
        MaterialIndex_(flatbuffers::EndianScalar(_MaterialIndex)),
        Transform_(_Transform) {
  }
  uint32_t MeshIndex() const {
    return flatbuffers::EndianScalar(MeshIndex_);
  }
  uint32_t MaterialIndex() const {
    return flatbuffers::EndianScalar(MaterialIndex_);
  }
  const r3d::Matrix3x4 &Transform() const {
    return Transform_;
  }
};
FLATBUFFERS_STRUCT_END(ResInstance, 56);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(4) ResLight FLATBUFFERS_FINAL_CLASS {
 private:
  uint32_t Type_;
  r3d::Vector3 Color_;
  r3d::Vector3 Position_;
  float Radius_;

 public:
  ResLight()
      : Type_(0),
        Color_(),
        Position_(),
        Radius_(0) {
  }
  ResLight(uint32_t _Type, const r3d::Vector3 &_Color, const r3d::Vector3 &_Position, float _Radius)
      : Type_(flatbuffers::EndianScalar(_Type)),
        Color_(_Color),
        Position_(_Position),
        Radius_(flatbuffers::EndianScalar(_Radius)) {
  }
  uint32_t Type() const {
    return flatbuffers::EndianScalar(Type_);
  }
  const r3d::Vector3 &Color() const {
    return Color_;
  }
  const r3d::Vector3 &Position() const {
    return Position_;
  }
  float Radius() const {
    return flatbuffers::EndianScalar(Radius_);
  }
};
FLATBUFFERS_STRUCT_END(ResLight, 32);

struct SubResource FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  typedef SubResourceBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_WIDTH = 4,
    VT_HEIGHT = 6,
    VT_MIPINDEX = 8,
    VT_PITCH = 10,
    VT_SLICEPITCH = 12,
    VT_PIXELS = 14
  };
  uint32_t Width() const {
    return GetField<uint32_t>(VT_WIDTH, 0);
  }
  uint32_t Height() const {
    return GetField<uint32_t>(VT_HEIGHT, 0);
  }
  uint32_t MipIndex() const {
    return GetField<uint32_t>(VT_MIPINDEX, 0);
  }
  uint32_t Pitch() const {
    return GetField<uint32_t>(VT_PITCH, 0);
  }
  uint32_t SlicePitch() const {
    return GetField<uint32_t>(VT_SLICEPITCH, 0);
  }
  const flatbuffers::Vector<uint8_t> *Pixels() const {
    return GetPointer<const flatbuffers::Vector<uint8_t> *>(VT_PIXELS);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint32_t>(verifier, VT_WIDTH) &&
           VerifyField<uint32_t>(verifier, VT_HEIGHT) &&
           VerifyField<uint32_t>(verifier, VT_MIPINDEX) &&
           VerifyField<uint32_t>(verifier, VT_PITCH) &&
           VerifyField<uint32_t>(verifier, VT_SLICEPITCH) &&
           VerifyOffset(verifier, VT_PIXELS) &&
           verifier.VerifyVector(Pixels()) &&
           verifier.EndTable();
  }
};

struct SubResourceBuilder {
  typedef SubResource Table;
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_Width(uint32_t Width) {
    fbb_.AddElement<uint32_t>(SubResource::VT_WIDTH, Width, 0);
  }
  void add_Height(uint32_t Height) {
    fbb_.AddElement<uint32_t>(SubResource::VT_HEIGHT, Height, 0);
  }
  void add_MipIndex(uint32_t MipIndex) {
    fbb_.AddElement<uint32_t>(SubResource::VT_MIPINDEX, MipIndex, 0);
  }
  void add_Pitch(uint32_t Pitch) {
    fbb_.AddElement<uint32_t>(SubResource::VT_PITCH, Pitch, 0);
  }
  void add_SlicePitch(uint32_t SlicePitch) {
    fbb_.AddElement<uint32_t>(SubResource::VT_SLICEPITCH, SlicePitch, 0);
  }
  void add_Pixels(flatbuffers::Offset<flatbuffers::Vector<uint8_t>> Pixels) {
    fbb_.AddOffset(SubResource::VT_PIXELS, Pixels);
  }
  explicit SubResourceBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  flatbuffers::Offset<SubResource> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<SubResource>(end);
    return o;
  }
};

inline flatbuffers::Offset<SubResource> CreateSubResource(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t Width = 0,
    uint32_t Height = 0,
    uint32_t MipIndex = 0,
    uint32_t Pitch = 0,
    uint32_t SlicePitch = 0,
    flatbuffers::Offset<flatbuffers::Vector<uint8_t>> Pixels = 0) {
  SubResourceBuilder builder_(_fbb);
  builder_.add_Pixels(Pixels);
  builder_.add_SlicePitch(SlicePitch);
  builder_.add_Pitch(Pitch);
  builder_.add_MipIndex(MipIndex);
  builder_.add_Height(Height);
  builder_.add_Width(Width);
  return builder_.Finish();
}

inline flatbuffers::Offset<SubResource> CreateSubResourceDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t Width = 0,
    uint32_t Height = 0,
    uint32_t MipIndex = 0,
    uint32_t Pitch = 0,
    uint32_t SlicePitch = 0,
    const std::vector<uint8_t> *Pixels = nullptr) {
  auto Pixels__ = Pixels ? _fbb.CreateVector<uint8_t>(*Pixels) : 0;
  return r3d::CreateSubResource(
      _fbb,
      Width,
      Height,
      MipIndex,
      Pitch,
      SlicePitch,
      Pixels__);
}

struct ResTexture FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  typedef ResTextureBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_DIMENSION = 4,
    VT_WIDTH = 6,
    VT_HEIGHT = 8,
    VT_DEPTH = 10,
    VT_FORMAT = 12,
    VT_MIPLEVELS = 14,
    VT_SURFACECOUNT = 16,
    VT_OPTION = 18,
    VT_RESOURCES = 20
  };
  uint32_t Dimension() const {
    return GetField<uint32_t>(VT_DIMENSION, 0);
  }
  uint32_t Width() const {
    return GetField<uint32_t>(VT_WIDTH, 0);
  }
  uint32_t Height() const {
    return GetField<uint32_t>(VT_HEIGHT, 0);
  }
  uint32_t Depth() const {
    return GetField<uint32_t>(VT_DEPTH, 0);
  }
  uint32_t Format() const {
    return GetField<uint32_t>(VT_FORMAT, 0);
  }
  uint32_t MipLevels() const {
    return GetField<uint32_t>(VT_MIPLEVELS, 0);
  }
  uint32_t SurfaceCount() const {
    return GetField<uint32_t>(VT_SURFACECOUNT, 0);
  }
  uint32_t Option() const {
    return GetField<uint32_t>(VT_OPTION, 0);
  }
  const flatbuffers::Vector<flatbuffers::Offset<r3d::SubResource>> *Resources() const {
    return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<r3d::SubResource>> *>(VT_RESOURCES);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint32_t>(verifier, VT_DIMENSION) &&
           VerifyField<uint32_t>(verifier, VT_WIDTH) &&
           VerifyField<uint32_t>(verifier, VT_HEIGHT) &&
           VerifyField<uint32_t>(verifier, VT_DEPTH) &&
           VerifyField<uint32_t>(verifier, VT_FORMAT) &&
           VerifyField<uint32_t>(verifier, VT_MIPLEVELS) &&
           VerifyField<uint32_t>(verifier, VT_SURFACECOUNT) &&
           VerifyField<uint32_t>(verifier, VT_OPTION) &&
           VerifyOffset(verifier, VT_RESOURCES) &&
           verifier.VerifyVector(Resources()) &&
           verifier.VerifyVectorOfTables(Resources()) &&
           verifier.EndTable();
  }
};

struct ResTextureBuilder {
  typedef ResTexture Table;
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_Dimension(uint32_t Dimension) {
    fbb_.AddElement<uint32_t>(ResTexture::VT_DIMENSION, Dimension, 0);
  }
  void add_Width(uint32_t Width) {
    fbb_.AddElement<uint32_t>(ResTexture::VT_WIDTH, Width, 0);
  }
  void add_Height(uint32_t Height) {
    fbb_.AddElement<uint32_t>(ResTexture::VT_HEIGHT, Height, 0);
  }
  void add_Depth(uint32_t Depth) {
    fbb_.AddElement<uint32_t>(ResTexture::VT_DEPTH, Depth, 0);
  }
  void add_Format(uint32_t Format) {
    fbb_.AddElement<uint32_t>(ResTexture::VT_FORMAT, Format, 0);
  }
  void add_MipLevels(uint32_t MipLevels) {
    fbb_.AddElement<uint32_t>(ResTexture::VT_MIPLEVELS, MipLevels, 0);
  }
  void add_SurfaceCount(uint32_t SurfaceCount) {
    fbb_.AddElement<uint32_t>(ResTexture::VT_SURFACECOUNT, SurfaceCount, 0);
  }
  void add_Option(uint32_t Option) {
    fbb_.AddElement<uint32_t>(ResTexture::VT_OPTION, Option, 0);
  }
  void add_Resources(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<r3d::SubResource>>> Resources) {
    fbb_.AddOffset(ResTexture::VT_RESOURCES, Resources);
  }
  explicit ResTextureBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  flatbuffers::Offset<ResTexture> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<ResTexture>(end);
    return o;
  }
};

inline flatbuffers::Offset<ResTexture> CreateResTexture(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t Dimension = 0,
    uint32_t Width = 0,
    uint32_t Height = 0,
    uint32_t Depth = 0,
    uint32_t Format = 0,
    uint32_t MipLevels = 0,
    uint32_t SurfaceCount = 0,
    uint32_t Option = 0,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<r3d::SubResource>>> Resources = 0) {
  ResTextureBuilder builder_(_fbb);
  builder_.add_Resources(Resources);
  builder_.add_Option(Option);
  builder_.add_SurfaceCount(SurfaceCount);
  builder_.add_MipLevels(MipLevels);
  builder_.add_Format(Format);
  builder_.add_Depth(Depth);
  builder_.add_Height(Height);
  builder_.add_Width(Width);
  builder_.add_Dimension(Dimension);
  return builder_.Finish();
}

inline flatbuffers::Offset<ResTexture> CreateResTextureDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t Dimension = 0,
    uint32_t Width = 0,
    uint32_t Height = 0,
    uint32_t Depth = 0,
    uint32_t Format = 0,
    uint32_t MipLevels = 0,
    uint32_t SurfaceCount = 0,
    uint32_t Option = 0,
    const std::vector<flatbuffers::Offset<r3d::SubResource>> *Resources = nullptr) {
  auto Resources__ = Resources ? _fbb.CreateVector<flatbuffers::Offset<r3d::SubResource>>(*Resources) : 0;
  return r3d::CreateResTexture(
      _fbb,
      Dimension,
      Width,
      Height,
      Depth,
      Format,
      MipLevels,
      SurfaceCount,
      Option,
      Resources__);
}

struct ResMesh FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  typedef ResMeshBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_VERTEXCOUNT = 4,
    VT_INDEXCOUNT = 6,
    VT_VERTICES = 8,
    VT_INDICES = 10
  };
  uint32_t VertexCount() const {
    return GetField<uint32_t>(VT_VERTEXCOUNT, 0);
  }
  uint32_t IndexCount() const {
    return GetField<uint32_t>(VT_INDEXCOUNT, 0);
  }
  const flatbuffers::Vector<const r3d::ResVertex *> *Vertices() const {
    return GetPointer<const flatbuffers::Vector<const r3d::ResVertex *> *>(VT_VERTICES);
  }
  const flatbuffers::Vector<uint32_t> *Indices() const {
    return GetPointer<const flatbuffers::Vector<uint32_t> *>(VT_INDICES);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint32_t>(verifier, VT_VERTEXCOUNT) &&
           VerifyField<uint32_t>(verifier, VT_INDEXCOUNT) &&
           VerifyOffset(verifier, VT_VERTICES) &&
           verifier.VerifyVector(Vertices()) &&
           VerifyOffset(verifier, VT_INDICES) &&
           verifier.VerifyVector(Indices()) &&
           verifier.EndTable();
  }
};

struct ResMeshBuilder {
  typedef ResMesh Table;
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_VertexCount(uint32_t VertexCount) {
    fbb_.AddElement<uint32_t>(ResMesh::VT_VERTEXCOUNT, VertexCount, 0);
  }
  void add_IndexCount(uint32_t IndexCount) {
    fbb_.AddElement<uint32_t>(ResMesh::VT_INDEXCOUNT, IndexCount, 0);
  }
  void add_Vertices(flatbuffers::Offset<flatbuffers::Vector<const r3d::ResVertex *>> Vertices) {
    fbb_.AddOffset(ResMesh::VT_VERTICES, Vertices);
  }
  void add_Indices(flatbuffers::Offset<flatbuffers::Vector<uint32_t>> Indices) {
    fbb_.AddOffset(ResMesh::VT_INDICES, Indices);
  }
  explicit ResMeshBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  flatbuffers::Offset<ResMesh> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<ResMesh>(end);
    return o;
  }
};

inline flatbuffers::Offset<ResMesh> CreateResMesh(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t VertexCount = 0,
    uint32_t IndexCount = 0,
    flatbuffers::Offset<flatbuffers::Vector<const r3d::ResVertex *>> Vertices = 0,
    flatbuffers::Offset<flatbuffers::Vector<uint32_t>> Indices = 0) {
  ResMeshBuilder builder_(_fbb);
  builder_.add_Indices(Indices);
  builder_.add_Vertices(Vertices);
  builder_.add_IndexCount(IndexCount);
  builder_.add_VertexCount(VertexCount);
  return builder_.Finish();
}

inline flatbuffers::Offset<ResMesh> CreateResMeshDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t VertexCount = 0,
    uint32_t IndexCount = 0,
    const std::vector<r3d::ResVertex> *Vertices = nullptr,
    const std::vector<uint32_t> *Indices = nullptr) {
  auto Vertices__ = Vertices ? _fbb.CreateVectorOfStructs<r3d::ResVertex>(*Vertices) : 0;
  auto Indices__ = Indices ? _fbb.CreateVector<uint32_t>(*Indices) : 0;
  return r3d::CreateResMesh(
      _fbb,
      VertexCount,
      IndexCount,
      Vertices__,
      Indices__);
}

struct ResScene FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  typedef ResSceneBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_MESHCOUNT = 4,
    VT_INSTANCECOUNT = 6,
    VT_TEXTURECOUNT = 8,
    VT_MATERIALCOUNT = 10,
    VT_LIGHTCOUNT = 12,
    VT_IBLTEXTURE = 14,
    VT_MESHES = 16,
    VT_INSTANCES = 18,
    VT_TEXTURES = 20,
    VT_MATERIALS = 22,
    VT_LIGHTS = 24
  };
  uint32_t MeshCount() const {
    return GetField<uint32_t>(VT_MESHCOUNT, 0);
  }
  uint32_t InstanceCount() const {
    return GetField<uint32_t>(VT_INSTANCECOUNT, 0);
  }
  uint32_t TextureCount() const {
    return GetField<uint32_t>(VT_TEXTURECOUNT, 0);
  }
  uint32_t MaterialCount() const {
    return GetField<uint32_t>(VT_MATERIALCOUNT, 0);
  }
  uint32_t LightCount() const {
    return GetField<uint32_t>(VT_LIGHTCOUNT, 0);
  }
  const r3d::ResTexture *IblTexture() const {
    return GetPointer<const r3d::ResTexture *>(VT_IBLTEXTURE);
  }
  const flatbuffers::Vector<flatbuffers::Offset<r3d::ResMesh>> *Meshes() const {
    return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<r3d::ResMesh>> *>(VT_MESHES);
  }
  const flatbuffers::Vector<const r3d::ResInstance *> *Instances() const {
    return GetPointer<const flatbuffers::Vector<const r3d::ResInstance *> *>(VT_INSTANCES);
  }
  const flatbuffers::Vector<flatbuffers::Offset<r3d::ResTexture>> *Textures() const {
    return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<r3d::ResTexture>> *>(VT_TEXTURES);
  }
  const flatbuffers::Vector<const r3d::ResMaterial *> *Materials() const {
    return GetPointer<const flatbuffers::Vector<const r3d::ResMaterial *> *>(VT_MATERIALS);
  }
  const flatbuffers::Vector<const r3d::ResLight *> *Lights() const {
    return GetPointer<const flatbuffers::Vector<const r3d::ResLight *> *>(VT_LIGHTS);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint32_t>(verifier, VT_MESHCOUNT) &&
           VerifyField<uint32_t>(verifier, VT_INSTANCECOUNT) &&
           VerifyField<uint32_t>(verifier, VT_TEXTURECOUNT) &&
           VerifyField<uint32_t>(verifier, VT_MATERIALCOUNT) &&
           VerifyField<uint32_t>(verifier, VT_LIGHTCOUNT) &&
           VerifyOffset(verifier, VT_IBLTEXTURE) &&
           verifier.VerifyTable(IblTexture()) &&
           VerifyOffset(verifier, VT_MESHES) &&
           verifier.VerifyVector(Meshes()) &&
           verifier.VerifyVectorOfTables(Meshes()) &&
           VerifyOffset(verifier, VT_INSTANCES) &&
           verifier.VerifyVector(Instances()) &&
           VerifyOffset(verifier, VT_TEXTURES) &&
           verifier.VerifyVector(Textures()) &&
           verifier.VerifyVectorOfTables(Textures()) &&
           VerifyOffset(verifier, VT_MATERIALS) &&
           verifier.VerifyVector(Materials()) &&
           VerifyOffset(verifier, VT_LIGHTS) &&
           verifier.VerifyVector(Lights()) &&
           verifier.EndTable();
  }
};

struct ResSceneBuilder {
  typedef ResScene Table;
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_MeshCount(uint32_t MeshCount) {
    fbb_.AddElement<uint32_t>(ResScene::VT_MESHCOUNT, MeshCount, 0);
  }
  void add_InstanceCount(uint32_t InstanceCount) {
    fbb_.AddElement<uint32_t>(ResScene::VT_INSTANCECOUNT, InstanceCount, 0);
  }
  void add_TextureCount(uint32_t TextureCount) {
    fbb_.AddElement<uint32_t>(ResScene::VT_TEXTURECOUNT, TextureCount, 0);
  }
  void add_MaterialCount(uint32_t MaterialCount) {
    fbb_.AddElement<uint32_t>(ResScene::VT_MATERIALCOUNT, MaterialCount, 0);
  }
  void add_LightCount(uint32_t LightCount) {
    fbb_.AddElement<uint32_t>(ResScene::VT_LIGHTCOUNT, LightCount, 0);
  }
  void add_IblTexture(flatbuffers::Offset<r3d::ResTexture> IblTexture) {
    fbb_.AddOffset(ResScene::VT_IBLTEXTURE, IblTexture);
  }
  void add_Meshes(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<r3d::ResMesh>>> Meshes) {
    fbb_.AddOffset(ResScene::VT_MESHES, Meshes);
  }
  void add_Instances(flatbuffers::Offset<flatbuffers::Vector<const r3d::ResInstance *>> Instances) {
    fbb_.AddOffset(ResScene::VT_INSTANCES, Instances);
  }
  void add_Textures(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<r3d::ResTexture>>> Textures) {
    fbb_.AddOffset(ResScene::VT_TEXTURES, Textures);
  }
  void add_Materials(flatbuffers::Offset<flatbuffers::Vector<const r3d::ResMaterial *>> Materials) {
    fbb_.AddOffset(ResScene::VT_MATERIALS, Materials);
  }
  void add_Lights(flatbuffers::Offset<flatbuffers::Vector<const r3d::ResLight *>> Lights) {
    fbb_.AddOffset(ResScene::VT_LIGHTS, Lights);
  }
  explicit ResSceneBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  flatbuffers::Offset<ResScene> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<ResScene>(end);
    return o;
  }
};

inline flatbuffers::Offset<ResScene> CreateResScene(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t MeshCount = 0,
    uint32_t InstanceCount = 0,
    uint32_t TextureCount = 0,
    uint32_t MaterialCount = 0,
    uint32_t LightCount = 0,
    flatbuffers::Offset<r3d::ResTexture> IblTexture = 0,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<r3d::ResMesh>>> Meshes = 0,
    flatbuffers::Offset<flatbuffers::Vector<const r3d::ResInstance *>> Instances = 0,
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<r3d::ResTexture>>> Textures = 0,
    flatbuffers::Offset<flatbuffers::Vector<const r3d::ResMaterial *>> Materials = 0,
    flatbuffers::Offset<flatbuffers::Vector<const r3d::ResLight *>> Lights = 0) {
  ResSceneBuilder builder_(_fbb);
  builder_.add_Lights(Lights);
  builder_.add_Materials(Materials);
  builder_.add_Textures(Textures);
  builder_.add_Instances(Instances);
  builder_.add_Meshes(Meshes);
  builder_.add_IblTexture(IblTexture);
  builder_.add_LightCount(LightCount);
  builder_.add_MaterialCount(MaterialCount);
  builder_.add_TextureCount(TextureCount);
  builder_.add_InstanceCount(InstanceCount);
  builder_.add_MeshCount(MeshCount);
  return builder_.Finish();
}

inline flatbuffers::Offset<ResScene> CreateResSceneDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t MeshCount = 0,
    uint32_t InstanceCount = 0,
    uint32_t TextureCount = 0,
    uint32_t MaterialCount = 0,
    uint32_t LightCount = 0,
    flatbuffers::Offset<r3d::ResTexture> IblTexture = 0,
    const std::vector<flatbuffers::Offset<r3d::ResMesh>> *Meshes = nullptr,
    const std::vector<r3d::ResInstance> *Instances = nullptr,
    const std::vector<flatbuffers::Offset<r3d::ResTexture>> *Textures = nullptr,
    const std::vector<r3d::ResMaterial> *Materials = nullptr,
    const std::vector<r3d::ResLight> *Lights = nullptr) {
  auto Meshes__ = Meshes ? _fbb.CreateVector<flatbuffers::Offset<r3d::ResMesh>>(*Meshes) : 0;
  auto Instances__ = Instances ? _fbb.CreateVectorOfStructs<r3d::ResInstance>(*Instances) : 0;
  auto Textures__ = Textures ? _fbb.CreateVector<flatbuffers::Offset<r3d::ResTexture>>(*Textures) : 0;
  auto Materials__ = Materials ? _fbb.CreateVectorOfStructs<r3d::ResMaterial>(*Materials) : 0;
  auto Lights__ = Lights ? _fbb.CreateVectorOfStructs<r3d::ResLight>(*Lights) : 0;
  return r3d::CreateResScene(
      _fbb,
      MeshCount,
      InstanceCount,
      TextureCount,
      MaterialCount,
      LightCount,
      IblTexture,
      Meshes__,
      Instances__,
      Textures__,
      Materials__,
      Lights__);
}

inline const r3d::ResScene *GetResScene(const void *buf) {
  return flatbuffers::GetRoot<r3d::ResScene>(buf);
}

inline const r3d::ResScene *GetSizePrefixedResScene(const void *buf) {
  return flatbuffers::GetSizePrefixedRoot<r3d::ResScene>(buf);
}

inline bool VerifyResSceneBuffer(
    flatbuffers::Verifier &verifier) {
  return verifier.VerifyBuffer<r3d::ResScene>(nullptr);
}

inline bool VerifySizePrefixedResSceneBuffer(
    flatbuffers::Verifier &verifier) {
  return verifier.VerifySizePrefixedBuffer<r3d::ResScene>(nullptr);
}

inline void FinishResSceneBuffer(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<r3d::ResScene> root) {
  fbb.Finish(root);
}

inline void FinishSizePrefixedResSceneBuffer(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<r3d::ResScene> root) {
  fbb.FinishSizePrefixed(root);
}

}  // namespace r3d

#endif  // FLATBUFFERS_GENERATED_SCENE_R3D_H_
