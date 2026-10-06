#include "drape_frontend/metro_train_renderer.hpp"

#include "drape_frontend/batcher_bucket.hpp"
#include "drape_frontend/map_shape.hpp"
#include "drape_frontend/render_state_extension.hpp"
#include "drape_frontend/screen_operations.hpp"
#include "drape_frontend/shape_view_params.hpp"
#include "drape_frontend/text_layout.hpp"
#include "drape_frontend/tile_utils.hpp"
#include "drape_frontend/visual_params.hpp"

#include "shaders/programs.hpp"

#include "drape/batcher.hpp"
#include "drape/constants.hpp"
#include "drape/drape_global.hpp"
#include "drape/font_constants.hpp"
#include "drape/glsl_func.hpp"
#include "drape/glsl_types.hpp"
#include "drape/utils/vertex_decl.hpp"

#include "geometry/mercator.hpp"

#include "coding/string_utf8_multilang.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace df
{
namespace
{
struct TrainDiscVertex
{
  TrainDiscVertex() = default;
  TrainDiscVertex(glsl::vec2 const & normal, glsl::vec2 const & texCoord) : m_normal(normal), m_texCoord(texCoord) {}

  glsl::vec2 m_normal;
  glsl::vec2 m_texCoord;
};

dp::BindingInfo DiscBinding()
{
  dp::BindingInfo info(2);
  dp::BindingDecl & normal = info.GetBindingDecl(0);
  normal.m_attributeName = "a_normal";
  normal.m_componentCount = 2;
  normal.m_componentType = gl_const::GLFloatType;
  normal.m_offset = 0;
  normal.m_stride = sizeof(TrainDiscVertex);

  dp::BindingDecl & texCoord = info.GetBindingDecl(1);
  texCoord.m_attributeName = "a_colorTexCoords";
  texCoord.m_componentCount = 2;
  texCoord.m_componentType = gl_const::GLFloatType;
  texCoord.m_offset = sizeof(glsl::vec2);
  texCoord.m_stride = sizeof(TrainDiscVertex);
  return info;
}

void AppendDisc(std::vector<TrainDiscVertex> & verts, glsl::vec2 const & tex, float radiusPx)
{
  constexpr int kSegments = 16;
  constexpr float kPi = 3.14159265f;
  glsl::vec2 const center(0.f, 0.f);
  for (int i = 0; i < kSegments; ++i)
  {
    float const a0 = static_cast<float>(i) * (2.f * kPi) / kSegments;
    float const a1 = static_cast<float>(i + 1) * (2.f * kPi) / kSegments;
    glsl::vec2 const p0(std::cos(a0) * radiusPx, std::sin(a0) * radiusPx);
    glsl::vec2 const p1(std::cos(a1) * radiusPx, std::sin(a1) * radiusPx);
    // Both windings: the pipeline culls back faces, and projection flips Y.
    verts.emplace_back(center, tex);
    verts.emplace_back(p0, tex);
    verts.emplace_back(p1, tex);
    verts.emplace_back(center, tex);
    verts.emplace_back(p1, tex);
    verts.emplace_back(p0, tex);
  }
}

dp::Color ContrastFor(dp::Color const & fill)
{
  float const lum = 0.299f * fill.GetRedF() + 0.587f * fill.GetGreenF() + 0.114f * fill.GetBlueF();
  return lum > 0.62f ? dp::Color(20, 20, 20) : dp::Color::White();
}

uint64_t DiscKey(dp::Color const & color, int radiusPx)
{
  return (static_cast<uint64_t>(color.GetRGBA()) << 16) | static_cast<uint64_t>(std::max(radiusPx, 0));
}

std::string LabelKey(std::string const & label, dp::Color const & text)
{
  return label + ":" + std::to_string(text.GetRGBA());
}

drape_ptr<dp::VertexArrayBuffer> UploadTriangles(ref_ptr<dp::GraphicsContext> context, dp::RenderState const & state,
                                                 std::vector<TrainDiscVertex> & verts)
{
  drape_ptr<dp::VertexArrayBuffer> buffer;
  dp::Batcher batcher(static_cast<uint32_t>(verts.size()), static_cast<uint32_t>(verts.size()));
  batcher.SetBatcherHash(static_cast<uint64_t>(BatcherBucket::Default));
  dp::SessionGuard guard(context, batcher, [&buffer](dp::RenderState const &, drape_ptr<dp::RenderBucket> && bucket)
  { buffer = bucket->MoveBuffer(); });
  dp::AttributeProvider provider(1, static_cast<uint32_t>(verts.size()));
  provider.InitStream(0, DiscBinding(), make_ref(verts.data()));
  batcher.InsertTriangleList(context, state, make_ref(&provider), nullptr);
  return buffer;
}

// Ribbon in local shape coordinates around pivot, about 18 m wide on the ground.
void AppendRibbon(std::vector<gpu::AreaVertex> & verts, glsl::vec2 const & tex, m2::PointD const & pivot,
                  std::vector<m2::PointD> const & line)
{
  if (line.size() < 2)
    return;
  double constexpr kHalfWidthM = 9.0;
  auto const local = [&](m2::PointD const & point)
  {
    m2::PointD const shifted = (point - pivot) * kShapeCoordScalar;
    return glsl::vec3(static_cast<float>(shifted.x), static_cast<float>(shifted.y), 0.f);
  };
  auto const side = [&](m2::PointD const & a, m2::PointD const & b)
  {
    m2::PointD const delta = b - a;
    double const meters = std::max(mercator::DistanceOnEarth(a, b), 1.0);
    double const half = kHalfWidthM / meters;
    return m2::PointD(-delta.y * half, delta.x * half);
  };
  for (size_t i = 0; i + 1 < line.size(); ++i)
  {
    m2::PointD const offset = side(line[i], line[i + 1]);
    glsl::vec3 const a0 = local(line[i] - offset);
    glsl::vec3 const a1 = local(line[i] + offset);
    glsl::vec3 const b0 = local(line[i + 1] - offset);
    glsl::vec3 const b1 = local(line[i + 1] + offset);
    verts.emplace_back(a0, tex);
    verts.emplace_back(a1, tex);
    verts.emplace_back(b1, tex);
    verts.emplace_back(a0, tex);
    verts.emplace_back(b1, tex);
    verts.emplace_back(b0, tex);
    verts.emplace_back(a0, tex);
    verts.emplace_back(b1, tex);
    verts.emplace_back(a1, tex);
    verts.emplace_back(a0, tex);
    verts.emplace_back(b0, tex);
    verts.emplace_back(b1, tex);
  }
}

math::Matrix<float, 4, 4> LabelView(ScreenBase const & screen, TileKey const & key, m2::PointF const & local)
{
  auto model = key.GetTileBasedModelView(screen);
  model(0, 3) += local.x * model(0, 0) + local.y * model(0, 1);
  model(1, 3) += local.x * model(1, 0) + local.y * model(1, 1);
  return model;
}
}  // namespace

void MetroTrainRenderer::SetTrains(std::vector<MetroTrainMarker> trains)
{
  m_trains = std::move(trains);
  m_dirty = true;
}

void MetroTrainRenderer::SetStrokes(std::vector<MetroTrainStroke> strokes)
{
  m_strokes = std::move(strokes);
  m_strokesDirty = true;
}

void MetroTrainRenderer::ResetGpu()
{
  m_meshes.clear();
  m_labels.clear();
  m_strokeMeshes.clear();
  m_dirty = true;
  m_strokesDirty = true;
}

void MetroTrainRenderer::Rebuild(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures)
{
  m_meshes.clear();
  m_labels.clear();
  m_labelRadius.clear();
  float const vs = static_cast<float>(VisualParams::Instance().GetVisualScale());
  float const fontScale = static_cast<float>(std::max(0.5, VisualParams::Instance().GetFontScale()));
  m_dotRadius = std::max(1, static_cast<int>(std::lround(7.5f * vs)));
  if (m_trains.empty())
  {
    m_dirty = false;
    return;
  }

  struct DiscStyle
  {
    dp::Color m_color;
    int m_radius = 0;
  };
  std::unordered_map<uint64_t, DiscStyle> discs;
  for (auto const & train : m_trains)
  {
    int radius = m_dotRadius;
    if (!train.m_label.empty())
    {
      auto const known = m_labelRadius.find(train.m_label);
      if (known != m_labelRadius.end())
        radius = known->second;
      else
      {
        float const fontPx = 9.5f * vs;
        float const fontSize = fontPx * static_cast<float>(dp::kBaseFontSizePixels) / fontScale;
        StraightTextLayout const layout(train.m_label, fontSize, textures, dp::Center, true,
                                        StringUtf8Multilang::kDefaultCode);
        float const longest = std::max(layout.GetPixelLength(), layout.GetPixelHeight());
        radius = std::max(m_dotRadius + 4, static_cast<int>(std::lround(0.5f * longest + 3.6f * vs)));
        m_labelRadius.emplace(train.m_label, radius);

        if (layout.GetGlyphCount() > 0)
        {
          dp::Color const textColor = ContrastFor(train.m_color);
          std::string const key = LabelKey(train.m_label, textColor);
          if (m_labels.find(key) == m_labels.end())
          {
            dp::TextureManager::ColorRegion color;
            textures->GetColorRegion(textColor, color);
            StraightTextLayout mutableLayout = layout;
            mutableLayout.SetBasePosition(glm::vec4(0.f, 0.f, dp::depth::kMyPositionMarkDepth, 0.f), glm::vec2(0.f));
            gpu::TTextStaticVertexBuffer staticBuffer;
            gpu::TTextDynamicVertexBuffer dynamicBuffer;
            mutableLayout.CacheStaticGeometry(color, staticBuffer);
            mutableLayout.CacheDynamicGeometry(glsl::vec2(0.f), dynamicBuffer);
            if (!staticBuffer.empty() && staticBuffer.size() == dynamicBuffer.size() && color.GetTexture() != nullptr &&
                mutableLayout.GetMaskTexture() != nullptr)
            {
              auto state = CreateRenderState(gpu::Program::Text, DepthLayer::OverlayLayer);
              state.SetProgram3d(gpu::Program::TextBillboard);
              state.SetDepthTestEnabled(false);
              state.SetColorTexture(color.GetTexture());
              state.SetMaskTexture(mutableLayout.GetMaskTexture());
              drape_ptr<dp::VertexArrayBuffer> buffer;
              {
                uint32_t const count = static_cast<uint32_t>(staticBuffer.size());
                dp::Batcher batcher(count, count);
                batcher.SetBatcherHash(static_cast<uint64_t>(BatcherBucket::Default));
                dp::SessionGuard guard(context, batcher,
                                       [&buffer](dp::RenderState const &, drape_ptr<dp::RenderBucket> && bucket)
                { buffer = bucket->MoveBuffer(); });
                dp::AttributeProvider provider(2, count);
                provider.InitStream(0, gpu::TextStaticVertex::GetBindingInfo(), make_ref(staticBuffer.data()));
                provider.InitStream(1, gpu::TextDynamicVertex::GetBindingInfo(), make_ref(dynamicBuffer.data()));
                batcher.InsertListOfStrip(context, state, make_ref(&provider), 4);
              }
              if (buffer != nullptr)
                m_labels.emplace(key, LabelMesh{state, std::move(buffer), false});
            }
          }
        }
      }
    }
    discs.emplace(DiscKey(train.m_color, radius), DiscStyle{train.m_color, radius});
  }

  float const grow = 2.6f * vs;
  for (auto const & [key, style] : discs)
  {
    dp::TextureManager::ColorRegion fillRegion;
    dp::TextureManager::ColorRegion outlineRegion;
    textures->GetColorRegion(style.m_color, fillRegion);
    textures->GetColorRegion(ContrastFor(style.m_color), outlineRegion);
    std::vector<TrainDiscVertex> verts;
    verts.reserve(16 * 6 * 2);
    AppendDisc(verts, glsl::ToVec2(outlineRegion.GetTexRect().Center()), static_cast<float>(style.m_radius) + grow);
    AppendDisc(verts, glsl::ToVec2(fillRegion.GetTexRect().Center()), static_cast<float>(style.m_radius));
    auto state = CreateRenderState(gpu::Program::MyPosition, DepthLayer::OverlayLayer);
    state.SetDepthTestEnabled(false);
    state.SetColorTexture(fillRegion.GetTexture());
    auto buffer = UploadTriangles(context, state, verts);
    if (buffer != nullptr)
      m_meshes.emplace(key, RenderNode(state, std::move(buffer)));
  }
  m_dirty = false;
}

void MetroTrainRenderer::RebuildStrokes(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures)
{
  m_strokeMeshes.clear();
  for (auto const & stroke : m_strokes)
  {
    if (stroke.m_mercator.size() < 2)
      continue;
    dp::TextureManager::ColorRegion region;
    textures->GetColorRegion(stroke.m_color, region);
    glsl::vec2 const tex = glsl::ToVec2(region.GetTexRect().Center());
    m2::PointD const pivot = stroke.m_mercator.front();
    std::vector<gpu::AreaVertex> verts;
    verts.reserve(stroke.m_mercator.size() * 12);
    AppendRibbon(verts, tex, pivot, stroke.m_mercator);
    if (verts.empty())
      continue;
    auto state = CreateRenderState(gpu::Program::Area, DepthLayer::OverlayLayer);
    state.SetDepthTestEnabled(false);
    state.SetColorTexture(region.GetTexture());
    drape_ptr<dp::VertexArrayBuffer> buffer;
    {
      uint32_t const count = static_cast<uint32_t>(verts.size());
      dp::Batcher batcher(count, count);
      batcher.SetBatcherHash(static_cast<uint64_t>(BatcherBucket::Default));
      dp::SessionGuard guard(context, batcher, [&buffer](dp::RenderState const &, drape_ptr<dp::RenderBucket> && bucket)
      { buffer = bucket->MoveBuffer(); });
      dp::AttributeProvider provider(1, count);
      provider.InitStream(0, gpu::AreaVertex::GetBindingInfo(), make_ref(verts.data()));
      batcher.InsertTriangleList(context, state, make_ref(&provider), nullptr);
    }
    if (buffer != nullptr)
      m_strokeMeshes.emplace_back(state, std::move(buffer), pivot);
  }
  m_strokesDirty = false;
}

void MetroTrainRenderer::Render(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures,
                                ref_ptr<gpu::ProgramManager> mng, ScreenBase const & screen, int zoomLevel,
                                FrameValues const & frameValues)
{
  if (m_strokesDirty)
    RebuildStrokes(context, textures);
  for (auto & stroke : m_strokeMeshes)
  {
    gpu::MapProgramParams params;
    frameValues.SetTo(params);
    params.m_modelView = glsl::make_mat4(screen.GetModelView(stroke.m_pivot, kShapeCoordScalar).m_data);
    params.m_opacity = 0.92f;
    stroke.m_node.Render(context, mng, params);
  }

  if (m_trains.empty())
    return;
  if (m_dirty || m_meshes.empty())
    Rebuild(context, textures);

  auto const & glyph = VisualParams::Instance().GetGlyphVisualParams();
  for (auto const & train : m_trains)
  {
    int const radius = train.m_label.empty() ? m_dotRadius : m_labelRadius[train.m_label];
    auto const mesh = m_meshes.find(DiscKey(train.m_color, radius));
    if (mesh == m_meshes.end())
      continue;
    m2::PointD const adjusted = AdjustPointForViewport(train.m_mercator, screen);
    TileKey const key = GetTileKeyByPoint(adjusted, ClipTileZoomByMaxDataZoom(zoomLevel));
    auto const local =
        static_cast<m2::PointF>(MapShape::ConvertToLocal(adjusted, key.GetGlobalRect().Center(), kShapeCoordScalar));

    gpu::ShapesProgramParams discParams;
    frameValues.SetTo(discParams);
    discParams.m_modelView = glsl::make_mat4(key.GetTileBasedModelView(screen).m_data);
    discParams.m_position = glsl::vec3(local.x, local.y, dp::depth::kMyPositionMarkDepth);
    discParams.m_azimut = 0.f;
    discParams.m_opacity = 1.0f;
    mesh->second.Render(context, mng, discParams);

    if (train.m_label.empty())
      continue;
    auto const label = m_labels.find(LabelKey(train.m_label, ContrastFor(train.m_color)));
    if (label == m_labels.end() || label->second.m_buffer == nullptr)
      continue;
    auto const programId = screen.isPerspective() ? label->second.m_state.GetProgram3d<gpu::Program>()
                                                  : label->second.m_state.GetProgram<gpu::Program>();
    auto program = mng->GetProgram(programId);
    if (program == nullptr)
      continue;
    program->Bind();
    if (!label->second.m_built)
    {
      label->second.m_buffer->Build(context, program);
      label->second.m_built = true;
    }
    dp::ApplyState(context, program, label->second.m_state);
    gpu::MapProgramParams textParams;
    frameValues.SetTo(textParams);
    textParams.m_modelView = glsl::make_mat4(LabelView(screen, key, local).m_data);
    textParams.m_contrastGamma = glsl::vec2(glyph.m_contrast, glyph.m_gamma);
    textParams.m_isOutlinePass = 0.f;
    textParams.m_opacity = 1.0f;
    mng->GetParamsSetter()->Apply(context, program, textParams);
    label->second.m_buffer->Render(context, false);
  }
}
}  // namespace df
