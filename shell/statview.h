// Worker-side INFO publication and last-published row revalidation.
#pragma once
#include "shell/statpanelwidget.h"
#include "shell/statthumbnails.h"
namespace fugue::shell {
StatView buildStatView(const model::World& world, StatThumbnails& thumbnails, AtomTrace* diagnostic = nullptr);
bool statRowCurrent(const model::World& world, const StatView& published, int row, quint64 generation);
}
