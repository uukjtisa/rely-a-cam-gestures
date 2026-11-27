// itsgiving's reaction pictures, downloaded from its GitHub repo at a pinned commit.
// They carry no licence and are mostly other people's images, so they are never shipped in this
// repo or the installer: each copy of the app fetches them from itsgiving itself, once.
#pragma once

#include <QString>
#include <QTemporaryDir>

#include <functional>
#include <memory>

class QObject;

namespace racg::memes {

// The itsgiving commit the 14 reactions were ported from (its_giving_v2.py).
inline constexpr const char *kItsgivingRepo = "gazijarin/itsgiving";
inline constexpr const char *kItsgivingCommit = "591a36326f9e18b3547c567795cb3a2c6acc6a22";

// Lists itsgiving's assets folder at that commit and downloads its pictures into a temporary
// folder. done() runs on context's thread with the folder, or with nullptr and the reason.
using FetchDone = std::function<void(std::shared_ptr<QTemporaryDir> folder, const QString &error)>;
void fetchItsgiving(QObject *context, FetchDone done);

}  // namespace racg::memes
