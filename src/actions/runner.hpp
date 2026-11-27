// Does what the engine says: starts an action, remembers how to end it, and keeps the undo list.
// Undo ends the last action if it's still running, or reverts it if it was a one-shot (a snapshot
// is deleted, a zoom step or scene switch goes back). An action whose situation is missing just
// says so instead of failing quietly.
#pragma once

#include "actions/registry.hpp"
#include "gestures/engine.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <vector>

namespace racg {
struct Settings;
}
namespace racg::output {
class Pipeline;
}

namespace racg::actions {

class Runner : public QObject {
  Q_OBJECT
 public:
  // What an action handler returns: how to end it (stateful), how to take it back (one-shot), and
  // the words for the Undo button. A handler that couldn't run returns ok = false and says why.
  struct Done {
    bool ok = true;
    std::function<void()> end;
    std::function<void()> revert;
    QString label;
  };
  using Handler = std::function<Done(const QString &arg)>;

  struct Deps {
    Settings *settings = nullptr;
    output::Pipeline *pipeline = nullptr;
    memes::Library *memes = nullptr;
    std::function<Situation()> situation;
    std::function<bool()> paused;
    std::function<void(bool)> setPaused;
    std::function<void()> overlaysChanged;
  };
  explicit Runner(Deps d);

  // Handlers are keyed by the id before '|' (or "meme" for memes). Other modules add theirs.
  void handle(const QString &base, Handler h) { handlers_[base] = std::move(h); }

  void start(const gestures::Command &c);
  void end(const gestures::Command &c);
  void endAll();
  // Runs an action by hand (the window's own buttons), as if a gesture did it.
  void run(const QString &action);

  void undo();
  bool canUndo() const { return !undo_.empty(); }
  QString undoLabel() const { return undo_.empty() ? QString() : undo_.back().label; }

 Q_SIGNALS:
  void message(const QString &text);
  void forgotten(const QString &gestureId);   // undo ended it: the engine forgets it's running
  void changed();

 private:
  void addBuiltIns();
  struct Entry {
    QString label, gesture;
    std::function<void()> revert;
  };
  Deps d_;
  std::map<QString, Handler> handlers_;
  std::map<QString, std::function<void()>> running_;   // gesture id -> its action's end
  std::vector<Entry> undo_;
};

}  // namespace racg::actions
