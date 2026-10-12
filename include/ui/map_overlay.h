#pragma once

#include <LovyanGFX.hpp>

namespace ui::mapoverlay {

/** Draws the stored coast/road lines (see services/map_store.h); no-op without a map. */
void draw(lgfx::LGFXBase& gfx);

}  // namespace ui::mapoverlay
