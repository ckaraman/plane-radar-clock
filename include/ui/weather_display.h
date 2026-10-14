#pragma once

namespace ui {

/** Clear the panel and draw the weather screen (now + about two hours ahead). */
void weatherDisplayEnter();

/** Cheap to call often: redraws only when a new forecast has arrived. */
void weatherDisplayTick();

}  // namespace ui
