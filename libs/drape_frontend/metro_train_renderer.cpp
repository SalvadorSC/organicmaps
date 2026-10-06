#include "drape_frontend/metro_train_renderer.hpp"

#include "drape_frontend/metro_train_heading.hpp"

#include "drape_frontend/batcher_bucket.hpp"
#include "drape_frontend/map_shape.hpp"
#include "drape_frontend/render_state_extension.hpp"
#include "drape_frontend/screen_operations.hpp"
#include "drape_frontend/shape_view_params.hpp"
#include "drape_frontend/tile_utils.hpp"
#include "drape_frontend/visual_params.hpp"

#include "shaders/programs.hpp"

#include "drape/batcher.hpp"
#include "drape/constants.hpp"
#include "drape/glsl_func.hpp"
#include "drape/glsl_types.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace df
{
namespace
{
struct MetroArrowVertex
{
  MetroArrowVertex() = default;
  MetroArrowVertex(glsl::vec2 const & normal, glsl::vec2 const & texCoord) : m_normal(normal), m_texCoord(texCoord) {}

  glsl::vec2 m_normal;
  glsl::vec2 m_texCoord;
};

dp::BindingInfo MarkerBinding()
{
  dp::BindingInfo info(2);
  dp::BindingDecl & normal = info.GetBindingDecl(0);
  normal.m_attributeName = "a_normal";
  normal.m_componentCount = 2;
  normal.m_componentType = gl_const::GLFloatType;
  normal.m_offset = 0;
  normal.m_stride = sizeof(MetroArrowVertex);

  dp::BindingDecl & texCoord = info.GetBindingDecl(1);
  texCoord.m_attributeName = "a_colorTexCoords";
  texCoord.m_componentCount = 2;
  texCoord.m_componentType = gl_const::GLFloatType;
  texCoord.m_offset = sizeof(glsl::vec2);
  texCoord.m_stride = sizeof(MetroArrowVertex);
  return info;
}

// Tip at -Y, which is north on a north-up screen. The shader rotates this by u_azimut.
void AppendArrow(std::vector<MetroArrowVertex> & verts, glsl::vec2 const & tex, float vs, float grow)
{
  float const s = vs;
  glsl::vec2 const tip(0.f, (-14.f - grow) * s);
  glsl::vec2 const right((8.f + grow) * s, (-1.f + grow * 0.2f) * s);
  glsl::vec2 const tailR((4.5f + grow * 0.6f) * s, (11.f + grow) * s);
  glsl::vec2 const tailL((-4.5f - grow * 0.6f) * s, (11.f + grow) * s);
  glsl::vec2 const left((-8.f - grow) * s, (-1.f + grow * 0.2f) * s);
  glsl::vec2 const mid(0.f, 2.f * s);
  glsl::vec2 const fan[] = {tip, right, tailR, tailL, left};
  for (int i = 0; i < 5; ++i)
  {
    glsl::vec2 const a = fan[i];
    glsl::vec2 const b = fan[(i + 1) % 5];
    // Both windings: the pipeline culls back faces, and projection flips Y.
    verts.emplace_back(mid, tex);
    verts.emplace_back(a, tex);
    verts.emplace_back(b, tex);
    verts.emplace_back(mid, tex);
    verts.emplace_back(b, tex);
    verts.emplace_back(a, tex);
  }
}

// Round marker. Azimuth stays 0; the disc does not encode a heading.
void AppendDisc(std::vector<MetroArrowVertex> & verts, glsl::vec2 const & tex, float vs, float grow)
{
  float const radius = (7.5f + grow) * vs;
  constexpr int kSegments = 16;
  constexpr float kPi = 3.14159265f;
  glsl::vec2 const center(0.f, 0.f);
  for (int i = 0; i < kSegments; ++i)
  {
    float const a0 = static_cast<float>(i) * (2.f * kPi) / kSegments;
    float const a1 = static_cast<float>(i + 1) * (2.f * kPi) / kSegments;
    glsl::vec2 const p0(std::cos(a0) * radius, std::sin(a0) * radius);
    glsl::vec2 const p1(std::cos(a1) * radius, std::sin(a1) * radius);
    // Both windings: the pipeline culls back faces, and projection flips Y.
    verts.emplace_back(center, tex);
    verts.emplace_back(p0, tex);
    verts.emplace_back(p1, tex);
    verts.emplace_back(center, tex);
    verts.emplace_back(p1, tex);
    verts.emplace_back(p0, tex);
  }
}

dp::Color OutlineFor(dp::Color const & fill)
{
  float const lum = 0.299f * fill.GetRedF() + 0.587f * fill.GetGreenF() + 0.114f * fill.GetBlueF();
  return lum > 0.62f ? dp::Color(20, 20, 20) : dp::Color::White();
}

uint64_t MeshKey(dp::Color const & color, bool directional)
{
  return (static_cast<uint64_t>(color.GetRGBA()) << 1) | (directional ? 1ull : 0ull);
}

}  // namespace

void MetroTrainRenderer::SetTrains(std::vector<MetroTrainMarker> trains)
{
  m_trains = std::move(trains);
  m_dirty = true;
}

void MetroTrainRenderer::ResetGpu()
{
  m_meshes.clear();
  m_dirty = true;
}

void MetroTrainRenderer::Rebuild(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures)
{
  m_meshes.clear();
  if (m_trains.empty())
  {
    m_dirty = false;
    return;
  }
  float const vs = static_cast<float>(VisualParams::Instance().GetVisualScale());
  struct Style
  {
    dp::Color m_color;
    bool m_directional = false;
  };
  std::unordered_map<uint64_t, Style> styles;
  for (auto const & train : m_trains)
    styles.emplace(MeshKey(train.m_color, train.m_directional), Style{train.m_color, train.m_directional});

  for (auto const & [key, style] : styles)
  {
    dp::Color const fill = style.m_color;
    dp::Color const outline = OutlineFor(fill);
    dp::TextureManager::ColorRegion fillRegion;
    dp::TextureManager::ColorRegion outlineRegion;
    textures->GetColorRegion(fill, fillRegion);
    textures->GetColorRegion(outline, outlineRegion);
    m2::PointF const fillTex = fillRegion.GetTexRect().Center();
    m2::PointF const outlineTex = outlineRegion.GetTexRect().Center();

    std::vector<MetroArrowVertex> verts;
    verts.reserve(16 * 6 * 2);
    if (style.m_directional)
    {
      AppendArrow(verts, glsl::ToVec2(outlineTex), vs, 2.6f);
      AppendArrow(verts, glsl::ToVec2(fillTex), vs, 0.f);
    }
    else
    {
      AppendDisc(verts, glsl::ToVec2(outlineTex), vs, 2.6f);
      AppendDisc(verts, glsl::ToVec2(fillTex), vs, 0.f);
    }

    auto state = CreateRenderState(gpu::Program::MyPosition, DepthLayer::OverlayLayer);
    state.SetDepthTestEnabled(false);
    state.SetColorTexture(fillRegion.GetTexture());

    drape_ptr<dp::VertexArrayBuffer> buffer;
    {
      dp::Batcher batcher(static_cast<uint32_t>(verts.size()), static_cast<uint32_t>(verts.size()));
      batcher.SetBatcherHash(static_cast<uint64_t>(BatcherBucket::Default));
      dp::SessionGuard guard(context, batcher, [&buffer](dp::RenderState const &, drape_ptr<dp::RenderBucket> && bucket)
      { buffer = bucket->MoveBuffer(); });
      dp::AttributeProvider provider(1, static_cast<uint32_t>(verts.size()));
      provider.InitStream(0, MarkerBinding(), make_ref(verts.data()));
      batcher.InsertTriangleList(context, state, make_ref(&provider), nullptr);
    }
    if (buffer == nullptr)
      continue;
    m_meshes.emplace(key, RenderNode(state, std::move(buffer)));
  }
  m_dirty = false;
}

void MetroTrainRenderer::Render(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures,
                                ref_ptr<gpu::ProgramManager> mng, ScreenBase const & screen, int zoomLevel,
                                FrameValues const & frameValues)
{
  if (m_trains.empty())
    return;
  if (m_dirty || m_meshes.empty())
    Rebuild(context, textures);

  for (auto const & train : m_trains)
  {
    auto const mesh = m_meshes.find(MeshKey(train.m_color, train.m_directional));
    if (mesh == m_meshes.end())
      continue;
    m2::PointD const adjusted = AdjustPointForViewport(train.m_mercator, screen);
    gpu::ShapesProgramParams params;
    frameValues.SetTo(params);
    TileKey const key = GetTileKeyByPoint(adjusted, ClipTileZoomByMaxDataZoom(zoomLevel));
    params.m_modelView = glsl::make_mat4(key.GetTileBasedModelView(screen).m_data);
    auto const pos =
        static_cast<m2::PointF>(MapShape::ConvertToLocal(adjusted, key.GetGlobalRect().Center(), kShapeCoordScalar));
    params.m_position = glsl::vec3(pos.x, pos.y, dp::depth::kMyPositionMarkDepth);
    params.m_azimut = train.m_directional ? ChevronScreenAzimuth(screen, adjusted, train.m_headingRad) : 0.f;
    params.m_opacity = 1.0f;
    mesh->second.Render(context, mng, params);
  }
}
}  // namespace df
