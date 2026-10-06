#include "drape_frontend/metro_train_renderer.hpp"

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

#include "geometry/mercator.hpp"

#include <cmath>
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
  // A short chevron: readable at ~20 dp, pointed end is the direction of travel.
  glsl::vec2 const tip(0.f, (-14.f - grow) * s);
  glsl::vec2 const right((8.f + grow) * s, (-1.f + grow * 0.2f) * s);
  glsl::vec2 const tailR((4.5f + grow * 0.6f) * s, (11.f + grow) * s);
  glsl::vec2 const tailL((-4.5f - grow * 0.6f) * s, (11.f + grow) * s);
  glsl::vec2 const left((-8.f - grow) * s, (-1.f + grow * 0.2f) * s);
  glsl::vec2 const mid(0.f, (2.f) * s);
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

dp::Color OutlineFor(dp::Color const & fill)
{
  float const lum = 0.299f * fill.GetRedF() + 0.587f * fill.GetGreenF() + 0.114f * fill.GetBlueF();
  return lum > 0.62f ? dp::Color(20, 20, 20) : dp::Color::White();
}

// u_azimut rotates a -Y tip onto the on-screen step of the geographic heading.
// GtoP already includes map rotation, so a rotated map keeps the arrow on the line.
float ScreenAzimuth(ScreenBase const & screen, m2::PointD const & mercator, float headingRad)
{
  ms::LatLon const ll = mercator::ToLatLon(mercator);
  double constexpr kDegToRad = 0.017453292519943295;
  double constexpr kStep = 1.0 / 111320.0;
  double const cosLat = std::cos(ll.m_lat * kDegToRad);
  double const north = std::cos(static_cast<double>(headingRad)) * kStep;
  double const east = std::sin(static_cast<double>(headingRad)) * kStep;
  double const dLon = cosLat == 0 ? 0 : east / cosLat;
  m2::PointD const ahead = mercator::FromLatLon(ms::LatLon(ll.m_lat + north, ll.m_lon + dLon));
  m2::PointD const s0 = screen.GtoP(mercator);
  m2::PointD const s1 = screen.GtoP(ahead);
  double const dx = s1.x - s0.x;
  double const dy = s1.y - s0.y;
  if (dx * dx + dy * dy < 1e-12)
    return 0;
  return static_cast<float>(std::atan2(dx, -dy));
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
  std::unordered_map<uint32_t, dp::Color> colors;
  for (auto const & train : m_trains)
    colors.emplace(train.m_color.GetRGBA(), train.m_color);

  for (auto const & [rgba, fill] : colors)
  {
    dp::Color const outline = OutlineFor(fill);
    dp::TextureManager::ColorRegion fillRegion;
    dp::TextureManager::ColorRegion outlineRegion;
    textures->GetColorRegion(fill, fillRegion);
    textures->GetColorRegion(outline, outlineRegion);
    m2::PointF const fillTex = fillRegion.GetTexRect().Center();
    m2::PointF const outlineTex = outlineRegion.GetTexRect().Center();

    std::vector<MetroArrowVertex> verts;
    verts.reserve(60);
    AppendArrow(verts, glsl::ToVec2(outlineTex), vs, 2.6f);
    AppendArrow(verts, glsl::ToVec2(fillTex), vs, 0.f);

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
    m_meshes.emplace(rgba, RenderNode(state, std::move(buffer)));
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
    auto const mesh = m_meshes.find(train.m_color.GetRGBA());
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
    params.m_azimut = ScreenAzimuth(screen, adjusted, train.m_headingRad);
    params.m_opacity = 1.0f;
    mesh->second.Render(context, mng, params);
  }
}
}  // namespace df
