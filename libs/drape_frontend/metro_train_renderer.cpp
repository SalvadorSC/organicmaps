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
#include "drape/drape_global.hpp"
#include "drape/glsl_func.hpp"
#include "drape/glsl_types.hpp"
#include "drape/utils/vertex_decl.hpp"

#include "geometry/mercator.hpp"

#include <algorithm>
#include <cctype>
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

uint64_t DiscKey(dp::Color const & color, int radiusPx, int outlinePx)
{
  return (static_cast<uint64_t>(color.GetRGBA()) << 24) | (static_cast<uint64_t>(std::max(radiusPx, 0)) << 8) |
         static_cast<uint64_t>(std::max(outlinePx, 0) & 0xFF);
}

int OutlinePx(bool labelled, float vs)
{
  // Metro dots keep a thicker ring. Labelled badges stay tight so the code fits.
  float const dp = labelled ? 1.15f : 2.6f;
  return std::max(1, static_cast<int>(std::lround(dp * vs)));
}

std::string LabelKey(std::string const & label, dp::Color const & text)
{
  return label + ":" + std::to_string(text.GetRGBA());
}

// 5×7 block letters. The map SDF path was dropping the letter of codes such as
// S1 and R4 on a small badge and leaving only the digit. These stamps are drawn
// with the same pixel shader as the disc, so every lit cell of the letter and
// the digit stays inside the circle.
uint8_t const * Glyph5x7(char ch)
{
  // Bit 4 is the leftmost pixel. Row 0 is the top.
  static uint8_t constexpr kDigit[10][7] = {
      {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
      {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, {0x1F, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1F},
      {0x11, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x01}, {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
      {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
      {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
  };
  static uint8_t constexpr kLetter[26][7] = {
      {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
      {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
      {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
      {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
      {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
      {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
      {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
      {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
      {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
      {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
      {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
  };
  if (ch >= '0' && ch <= '9')
    return kDigit[ch - '0'];
  if (ch >= 'A' && ch <= 'Z')
    return kLetter[ch - 'A'];
  return nullptr;
}

struct BadgeLayout
{
  int m_radius = 0;
  std::vector<TrainDiscVertex> m_cells;
};

void AppendCell(std::vector<TrainDiscVertex> & verts, glsl::vec2 const & tex, float x0, float y0, float x1, float y1)
{
  glsl::vec2 const a(x0, y0);
  glsl::vec2 const b(x1, y0);
  glsl::vec2 const c(x1, y1);
  glsl::vec2 const d(x0, y1);
  verts.emplace_back(a, tex);
  verts.emplace_back(b, tex);
  verts.emplace_back(c, tex);
  verts.emplace_back(a, tex);
  verts.emplace_back(c, tex);
  verts.emplace_back(d, tex);
  verts.emplace_back(a, tex);
  verts.emplace_back(c, tex);
  verts.emplace_back(b, tex);
  verts.emplace_back(a, tex);
  verts.emplace_back(d, tex);
  verts.emplace_back(c, tex);
}

BadgeLayout LayoutBadge(std::string const & label, float vs, glsl::vec2 const & tex)
{
  std::string text;
  text.reserve(label.size());
  for (unsigned char const ch : label)
  {
    char const upper = static_cast<char>(std::toupper(ch));
    if (Glyph5x7(upper) != nullptr)
      text.push_back(upper);
  }
  BadgeLayout badge;
  if (text.empty())
    return badge;
  float const cell = std::max(1.8f, 1.28f * vs);
  float const gap = cell * 0.85f;
  float const width = static_cast<float>(text.size()) * 5.f * cell + static_cast<float>(text.size() - 1) * gap;
  float const height = 7.f * cell;
  badge.m_radius = std::max(1, static_cast<int>(std::lround(0.5f * std::max(width, height) + 1.8f * vs)));
  // Smaller y is the visual top, matching the map text shader.
  float const top = -0.5f * height;
  float x = -0.5f * width;
  float const bleed = 0.35f;
  for (char const ch : text)
  {
    uint8_t const * rows = Glyph5x7(ch);
    for (int row = 0; row < 7; ++row)
    {
      for (int col = 0; col < 5; ++col)
      {
        if ((rows[row] & (1 << (4 - col))) == 0)
          continue;
        float const x0 = x + static_cast<float>(col) * cell;
        float const y0 = top + static_cast<float>(row) * cell;
        AppendCell(badge.m_cells, tex, x0 - bleed, y0 - bleed, x0 + cell + bleed, y0 + cell + bleed);
      }
    }
    x += 5.f * cell + gap;
  }
  return badge;
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
    int m_outline = 0;
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
        dp::Color const textColor = ContrastFor(train.m_color);
        dp::TextureManager::ColorRegion textRegion;
        textures->GetColorRegion(textColor, textRegion);
        BadgeLayout const badge = LayoutBadge(train.m_label, vs, glsl::ToVec2(textRegion.GetTexRect().Center()));
        radius = std::max(m_dotRadius, badge.m_radius);
        m_labelRadius.emplace(train.m_label, radius);
        std::string const key = LabelKey(train.m_label, textColor);
        if (!badge.m_cells.empty() && m_labels.find(key) == m_labels.end() && textRegion.GetTexture() != nullptr)
        {
          auto cells = badge.m_cells;
          auto state = CreateRenderState(gpu::Program::MyPosition, DepthLayer::OverlayLayer);
          state.SetDepthTestEnabled(false);
          state.SetColorTexture(textRegion.GetTexture());
          auto buffer = UploadTriangles(context, state, cells);
          if (buffer != nullptr)
            m_labels.emplace(key, RenderNode(state, std::move(buffer)));
        }
      }
    }
    int const outline = OutlinePx(!train.m_label.empty(), vs);
    discs.emplace(DiscKey(train.m_color, radius, outline), DiscStyle{train.m_color, radius, outline});
  }

  for (auto const & [key, style] : discs)
  {
    dp::TextureManager::ColorRegion fillRegion;
    dp::TextureManager::ColorRegion outlineRegion;
    textures->GetColorRegion(style.m_color, fillRegion);
    textures->GetColorRegion(ContrastFor(style.m_color), outlineRegion);
    std::vector<TrainDiscVertex> verts;
    verts.reserve(16 * 6 * 2);
    AppendDisc(verts, glsl::ToVec2(outlineRegion.GetTexRect().Center()),
               static_cast<float>(style.m_radius + style.m_outline));
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

  for (auto const & train : m_trains)
  {
    bool const labelled = !train.m_label.empty();
    int const radius = labelled ? m_labelRadius[train.m_label] : m_dotRadius;
    float const vs = static_cast<float>(VisualParams::Instance().GetVisualScale());
    auto const mesh = m_meshes.find(DiscKey(train.m_color, radius, OutlinePx(labelled, vs)));
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
    if (label == m_labels.end())
      continue;
    label->second.Render(context, mng, discParams);
  }
}
}  // namespace df
