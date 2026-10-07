#pragma once

#include "drape_frontend/frame_values.hpp"
#include "drape_frontend/metro_train_marker.hpp"
#include "drape_frontend/render_node.hpp"

#include "shaders/program_manager.hpp"

#include "drape/graphics_context.hpp"
#include "drape/pointers.hpp"
#include "drape/texture_manager.hpp"
#include "drape/vertex_array_buffer.hpp"

#include "geometry/screenbase.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace df
{
// Draws train markers in the frontend render pass, with the same ScreenBase as the
// map. Discs and their labels are rebuilt only when the train list changes. Each
// frame only updates the model-view uniform, so pans do not trail the markers.
class MetroTrainRenderer
{
public:
  void SetTrains(std::vector<MetroTrainMarker> trains);
  void SetStrokes(std::vector<MetroTrainStroke> strokes);
  void ResetGpu();
  void Render(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures,
              ref_ptr<gpu::ProgramManager> mng, ScreenBase const & screen, int zoomLevel,
              FrameValues const & frameValues);

private:
  void Rebuild(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures);
  void RebuildStrokes(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures);

  struct StrokeMesh
  {
    StrokeMesh(dp::RenderState const & state, drape_ptr<dp::VertexArrayBuffer> buffer, m2::PointD const & pivot)
      : m_node(state, std::move(buffer))
      , m_pivot(pivot)
    {}

    RenderNode m_node;
    m2::PointD m_pivot;
  };

  std::vector<MetroTrainMarker> m_trains;
  std::unordered_map<uint64_t, RenderNode> m_meshes;
  std::unordered_map<std::string, int> m_labelRadius;
  std::unordered_map<std::string, RenderNode> m_labels;
  int m_dotRadius = 0;
  std::vector<MetroTrainStroke> m_strokes;
  std::vector<StrokeMesh> m_strokeMeshes;
  bool m_dirty = false;
  bool m_strokesDirty = false;
};
}  // namespace df
