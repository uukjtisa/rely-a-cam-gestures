// One log file (<data>\log.txt, kept under 1 MB with one older copy), fed by Qt's message handler,
// so qInfo / qWarning anywhere end up in it with a time.
#pragma once

namespace racg::log {

void install();

}  // namespace racg::log
