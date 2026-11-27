// Start with Windows: the per-user Run key, starting hidden in the tray. Off unless ticked.
#pragma once

namespace racg::autostart {

bool enabled();
void set(bool on);

}  // namespace racg::autostart
