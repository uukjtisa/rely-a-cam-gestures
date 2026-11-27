// Settings, per the mock: outputs, meme sound, the confirm and stop signs, calibration, OBS's
// WebSocket, start with Windows. Every change applies at once; Done just closes.
#pragma once

#include <QDialog>

namespace racg::app {
class Controller;
}

namespace racg::ui {

class SettingsDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SettingsDialog(app::Controller &c, QWidget *parent = nullptr);

 Q_SIGNALS:
  void calibrate();

 private:
  app::Controller &c_;
};

}  // namespace racg::ui
