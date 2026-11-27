// The live preview: the output as viewers get it, plus what only the preview shows: the overlays
// that are on for the preview, the 3-2-1 and hold bar while recording, the calibration band, and
// short notes (toasts). A meme's fixed spot is dragged here.
#pragma once

#include <QElapsedTimer>
#include <QWidget>

namespace racg::app {
class Controller;
}

namespace racg::ui {

class Preview : public QWidget {
  Q_OBJECT
 public:
  explicit Preview(app::Controller &c, QWidget *parent = nullptr);
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override { return w * 9 / 16; }
  QSize sizeHint() const override { return {760, 428}; }

  void showToast(const QString &text);
  // While set, dragging on the preview moves (and the wheel resizes) that meme's fixed spot.
  void setSpotMeme(const QString &memeId);

 Q_SIGNALS:
  void spotMoved();

 protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;

 private:
  QRectF pictureRect(QSize frame) const;
  app::Controller &c_;
  QString toast_;
  QElapsedTimer toastAt_;
  QString spotMeme_;
  bool dragging_ = false;
  QPointF dragOffset_;
};

}  // namespace racg::ui
