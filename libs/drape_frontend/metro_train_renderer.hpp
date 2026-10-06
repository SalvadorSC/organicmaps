#pragma once

#include "drape_frontend/frame_values.hpp"
#include "drape_frontend/metro_train_marker.hpp"
#include "drape_frontend/render_node.hpp"

#include "shaders/program_manager.hpp"

#include "drape/graphics_context.hpp"
#include "drape/pointers.hpp"
#include "drape/texture_manager.hpp"

#include "geometry/screenbase.hpp"

#include <unordered_map>
#include <vector>

namespace df
{
// Draws train markers in the frontend render pass, with the same ScreenBase as the
// map. An Android view reprojects a viewport that the render thread publishes a
// frame later, so markers trail pans and flings. These markers cannot.
class MetroTrainRenderer
{
public:
  void SetTrains(std::vector<MetroTrainMarker> trains);
  void ResetGpu();
  void Render(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures,
              ref_ptr<gpu::ProgramManager> mng, ScreenBase const & screen, int zoomLevel,
              FrameValues const & frameValues);

private:
  void Rebuild(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::TextureManager> textures);

  std::vector<MetroTrainMarker> m_trains;
  std::unordered_map<uint32_t, RenderNode> m_meshes;
  bool m_dirty = false;
};
}  // namespace df
