#include "ui/panels/library_panel.hpp"

#include "app/controller.hpp"
#include "common/paths.hpp"
#include "ui/theme.hpp"
#include "ui/widgets/kit.hpp"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace racg::ui {

using namespace theme;
using gestures::Kind;

namespace {

// A clickable row: grey5, grey4 on hover, radius 4 (the mock's .row).
class Row : public QWidget {
 public:
  explicit Row(QWidget *parent = nullptr) : QWidget(parent) {
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
  }
  std::function<void()> onClick;
  std::function<void(QPoint)> onMenu;
  bool dim = false;

 protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(underMouse() && onClick ? grey4 : grey5);
    p.drawRoundedRect(rect(), radius, radius);
  }
  void mouseReleaseEvent(QMouseEvent *e) override {
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && onClick) onClick();
  }
  void contextMenuEvent(QContextMenuEvent *e) override {
    if (onMenu) onMenu(e->globalPos());
  }
  void enterEvent(QEnterEvent *) override { update(); }
  void leaveEvent(QEvent *) override { update(); }
};

// The meme's 52 x 52 thumbnail: its picture (a GIF's first frame), or a tile with its kind until
// the video decoder gives a frame.
class Thumb : public QWidget {
 public:
  Thumb(const QString &file, const QString &kind, QWidget *parent) : QWidget(parent), kind_(kind) {
    setFixedSize(52, 52);
    QImageReader r(file);
    r.setAutoTransform(true);
    if (r.canRead()) {
      QSize s = r.size();
      if (s.isValid()) r.setScaledSize(s.scaled(104, 104, Qt::KeepAspectRatioByExpanding));
      img_ = r.read();
    }
  }

 protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 6, 6);
    p.setClipPath(clip);
    p.fillRect(rect(), grey7);
    if (!img_.isNull()) {
      const QSizeF s = QSizeF(img_.size()).scaled(size(), Qt::KeepAspectRatioByExpanding);
      p.drawImage(QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height()), img_);
    } else {
      QFont f = font();
      f.setPixelSize(11);
      f.setWeight(QFont::ExtraBold);
      p.setFont(f);
      p.setPen(textLight);
      p.drawText(rect(), Qt::AlignCenter, kind_);
    }
  }

 private:
  QImage img_;
  QString kind_;
};

QLabel *elided(const QString &t, QWidget *parent, bool small = false) {
  auto *l = new QLabel(t, parent);
  l->setTextFormat(Qt::PlainText);
  l->setMinimumWidth(10);
  l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  if (small) l->setProperty("muted", true);
  return l;
}

}  // namespace

LibraryPanel::LibraryPanel(app::Controller &c, QWidget *parent) : QWidget(parent), c_(c) {
  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);

  // Tabs: two flat buttons with a blue underline on the current one.
  auto *tabs = new QWidget(this);
  tabs->setObjectName("tabs");
  auto *th = new QHBoxLayout(tabs);
  th->setContentsMargins(0, 0, 0, 0);
  th->setSpacing(0);
  const char *names[] = {"Controls", "Reactions", "Memes"};
  for (int i = 0; i < 3; ++i) {
    tabBtn_[i] = new QPushButton(names[i], tabs);
    tabBtn_[i]->setObjectName("tab");
    tabBtn_[i]->setCheckable(true);
    tabBtn_[i]->setCursor(Qt::PointingHandCursor);
    connect(tabBtn_[i], &QPushButton::clicked, this, [this, i] { setTab(i); });
    th->addWidget(tabBtn_[i]);
  }
  v->addWidget(tabs);

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_ = new QWidget;
  listLay_ = new QVBoxLayout(list_);
  listLay_->setContentsMargins(8, 8, 8, 8);
  listLay_->setSpacing(6);
  scroll->setWidget(list_);
  v->addWidget(scroll, 1);

  foot_ = new QStackedWidget(this);
  foot_->setObjectName("sideFoot");
  {
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(8, 8, 8, 8);
    auto *b = new QPushButton("+ Record gesture", w);
    b->setProperty("on", true);
    connect(b, &QPushButton::clicked, this, &LibraryPanel::recordNew);
    h->addWidget(b, 1);
    foot_->addWidget(w);
  }
  {
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(8, 8, 8, 8);
    h->setSpacing(6);
    auto *add = new QPushButton("+ Add files...", w);
    add->setProperty("on", true);
    connect(add, &QPushButton::clicked, this, &LibraryPanel::addMemes);
    auto *imp = new QPushButton("Import from itsgiving...", w);
    connect(imp, &QPushButton::clicked, this, &LibraryPanel::importItsgiving);
    h->addWidget(add, 1);
    h->addWidget(imp);
    foot_->addWidget(w);
  }
  foot_->setFixedHeight(foot_->sizeHint().height());
  v->addWidget(foot_);

  connect(&c_, &app::Controller::libraryChanged, this, &LibraryPanel::rebuild);
  connect(&c_, &app::Controller::statusChanged, this, [this] {
    // The situation decides the "needs OBS" text; rebuild only when it changes.
    const int s = int(c_.situation());
    if (s != lastSituation_) {
      lastSituation_ = s;
      rebuild();
    }
  });
  setTab(0);
}

bool LibraryPanel::memesTab() const { return tab_ == Memes; }

void LibraryPanel::setTab(int t) {
  tab_ = t;
  for (int i = 0; i < 3; ++i) tabBtn_[i]->setChecked(i == t);
  foot_->setCurrentIndex(t == Memes ? 1 : 0);   // Controls and Reactions: + Record gesture
  if (t != Memes) Q_EMIT spotMeme({});
  rebuild();
}

void LibraryPanel::rebuild() {
  setUpdatesEnabled(false);
  while (QLayoutItem *it = listLay_->takeAt(0)) {
    if (it->widget()) it->widget()->deleteLater();
    delete it;
  }
  if (tab_ == Controls) buildControls();
  else if (tab_ == Reactions) buildGestures();
  else buildMemes();
  listLay_->addStretch(1);
  setUpdatesEnabled(true);
}

void LibraryPanel::buildGestures() {
  const struct {
    Kind kind;
    const char *title;
  } groups[] = {{Kind::Mine, "Mine"}, {Kind::Ready, "itsgiving's reactions"}, {Kind::Sign, "Google's hand signs"}};
  for (int gi = 0; gi < 3; ++gi) {
    QStringList ids;
    for (const auto &g : c_.gestures().gestures)
      if (g.kind == groups[gi].kind) ids << g.id;
    auto *hdr = new GroupHeader(groups[gi].title, int(ids.size()), open_[gi], list_);
    connect(hdr, &GroupHeader::toggled, this, [this, gi] {
      open_[gi] = !open_[gi];
      rebuild();
    });
    listLay_->addWidget(hdr);
    if (!open_[gi]) continue;
    if (ids.isEmpty() && groups[gi].kind == Kind::Mine)
      listLay_->addWidget(note("None yet: + Record gesture makes one from a few takes.", list_));
    for (const auto &id : ids) listLay_->addWidget(gestureRow(id));
  }
}

QWidget *LibraryPanel::gestureRow(const QString &id) {
  const gestures::GestureDef *g = c_.gestures().find(id);
  auto *row = new Row(list_);
  auto *h = new QHBoxLayout(row);
  h->setContentsMargins(7, 5, 7, 5);
  h->setSpacing(8);
  auto *ic = new QLabel(g->icon, row);
  ic->setProperty("icon", true);
  ic->setFixedWidth(26);
  ic->setAlignment(Qt::AlignCenter);
  h->addWidget(ic);

  auto *t = new QVBoxLayout;
  t->setSpacing(0);
  auto *name = elided(g->name, row);
  t->addWidget(name);
  QString sub;
  bool needs = false;
  const auto s = c_.situation();
  const auto *control = c_.gestures().controls.byTrigger(id);
  if (control) {
    sub = "runs " + gestures::controlLabel(control->id) + " (Controls tab)";
  } else if (!g->action.isEmpty()) {
    const auto inf = actions::info(g->action, s, c_.memes());
    needs = !actions::available(inf.need, s);
    sub = "→ " + inf.label + (needs ? " (" + actions::needText(inf.need) + ")" : QString()) +
          (g->confirm ? " · needs confirm" : "");
  } else if (g->kind == Kind::Sign && g->sign() == c_.settings().confirmSign) {
    sub = "your confirm sign";
  } else {
    sub = "no action yet";
  }
  auto *subL = elided(sub, row, true);
  if (needs) subL->setProperty("need", true);
  t->addWidget(subL);
  h->addLayout(t, 1);

  auto *sw = new Switch({}, row);
  sw->setChecked(g->on);
  sw->setToolTip(control ? "Turned on and off with its control" : "On/off");
  sw->setEnabled(!control);
  connect(sw, &Switch::toggled, this, [this, id](bool on) {
    if (auto *gg = c_.gestures().find(id)) gg->on = on;
    c_.gesturesEdited();
  });
  h->addWidget(sw);

  if (!g->on) {
    auto *fx = new QGraphicsOpacityEffect(row);
    fx->setOpacity(0.55);
    row->setGraphicsEffect(fx);
  }
  row->onClick = [this, id] { Q_EMIT openGesture(id); };
  return row;
}

// ---- Controls ------------------------------------------------------------------------------------

void LibraryPanel::buildControls() {
  using gestures::ControlGroup;
  const char *titles[] = {"Framing", "Camera and mic", "OBS", "Always"};
  for (int gi = 0; gi < 4; ++gi) {
    listLay_->addWidget(sectionTitle(titles[gi], list_));
    for (const auto &k : c_.gestures().controls.list)
      if (const auto *s = gestures::controlSpec(k.id); s && int(s->group) == gi) listLay_->addWidget(controlRow(k.id));
    if (gi == int(ControlGroup::Obs)) listLay_->addWidget(addObsControl());
    if (gi == int(ControlGroup::Always)) {
      listLay_->addWidget(signRow(false));
      listLay_->addWidget(signRow(true));
    }
  }
  listLay_->addWidget(note("One gesture, one job: a gesture used here can't show a meme. + Record gesture makes your own "
                           "to pick here.",
                           list_));
}

QWidget *LibraryPanel::controlRow(const QString &id) {
  const auto &ctl = c_.gestures().controls;
  const gestures::Control *k = ctl.find(id);
  const gestures::ControlSpec *s = gestures::controlSpec(id);
  auto *row = new Row(list_);
  row->setCursor(Qt::ArrowCursor);
  auto *h = new QHBoxLayout(row);
  h->setContentsMargins(7, 5, 7, 5);
  h->setSpacing(8);

  auto *t = new QVBoxLayout;
  t->setSpacing(0);
  t->addWidget(elided(gestures::controlLabel(id), row));
  QString sub = s->how;
  const auto sit = c_.situation();
  const auto inf = actions::info(id, sit, c_.memes());
  const bool needs = !gestures::isMove(id) && !actions::available(inf.need, sit);
  if (needs) sub = actions::needText(inf.need) + (sub.isEmpty() ? QString() : " · " + sub);
  if (!k->on && *s->conflicts && ctl.moveOn(s->conflicts))
    sub = "off: it reads the same move as " + gestures::controlLabel(s->conflicts);
  if (s->confirm)
    sub += (sub.isEmpty() ? QString() : QString(" · ")) + "then " + vision::signLabel(c_.settings().confirmSign) +
           " with the other hand";
  if (!sub.isEmpty()) {
    auto *subL = elided(sub, row, true);
    subL->setToolTip(sub);
    if (needs) subL->setProperty("need", true);
    t->addWidget(subL);
  }
  h->addLayout(t, 1);

  if (!gestures::isMove(id)) h->addWidget(triggerCombo(id));
  if (s->addable) {
    auto *x = new QPushButton("×", row);
    x->setToolTip("Remove this control");
    x->setFixedWidth(26);
    connect(x, &QPushButton::clicked, this, [this, id] { c_.removeControl(id); });
    h->addWidget(x);
  }
  auto *sw = new Switch({}, row);
  sw->setChecked(k->on);
  sw->setToolTip("On/off");
  connect(sw, &Switch::toggled, this, [this, id](bool on) { c_.setControlOn(id, on); });
  h->addWidget(sw);
  if (!k->on) {
    auto *fx = new QGraphicsOpacityEffect(row);
    fx->setOpacity(0.55);
    row->setGraphicsEffect(fx);
  }
  return row;
}

QComboBox *LibraryPanel::triggerCombo(const QString &id) {
  const auto &lib = c_.gestures();
  const gestures::Control *k = lib.controls.find(id);
  auto *cb = new QComboBox(list_);
  cb->addItem("No gesture", QString());
  auto *model = qobject_cast<QStandardItemModel *>(cb->model());
  const QString confirmId = QString("sign:") + vision::signId(c_.settings().confirmSign);
  for (const auto &g : lib.gestures) {
    // Google's signs and your own; itsgiving's face and body reactions show memes.
    if (g.kind == gestures::Kind::Ready || g.id == confirmId) continue;
    QString text = g.icon + " " + g.name;
    const gestures::Control *other = lib.controls.byTrigger(g.id);
    if (other && other->id != id) text += " (now " + gestures::controlLabel(other->id) + ")";
    cb->addItem(text, g.id);
    if (g.action.startsWith("meme:") && model) {
      QStandardItem *it = model->item(cb->count() - 1);
      it->setEnabled(false);
      it->setToolTip("It shows a meme: take its meme off on the Reactions tab first");
    }
  }
  cb->setCurrentIndex(std::max(0, cb->findData(k->trigger)));
  cb->setMinimumWidth(90);
  cb->setMaximumWidth(160);
  cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  cb->setToolTip("The gesture that runs it. Moving one here takes it off its other control.");
  connect(cb, &QComboBox::activated, this, [this, id, cb] { c_.setControlTrigger(id, cb->currentData().toString()); });
  return cb;
}

QWidget *LibraryPanel::addObsControl() {
  auto *cb = new QComboBox(list_);
  cb->addItem("+ Add a scene or hotkey...", QString());
  const auto &ctl = c_.gestures().controls;
  for (const auto &sc : c_.obsScenes())
    if (!ctl.find("obs.scene|" + sc)) cb->addItem("Scene: " + sc, "obs.scene|" + sc);
  for (const auto &hk : c_.obsHotkeys())
    if (!ctl.find("obs.hotkey|" + hk)) cb->addItem("Hotkey: " + hk, "obs.hotkey|" + hk);
  if (cb->count() == 1) {
    cb->setItemText(0, "+ Add a scene or hotkey (OBS hasn't listed any)");
    cb->setEnabled(false);
  }
  connect(cb, &QComboBox::activated, this, [this, cb] {
    const QString id = cb->currentData().toString();
    if (!id.isEmpty()) c_.addControl(id);
  });
  return cb;
}

QWidget *LibraryPanel::signRow(bool confirm) {
  auto *row = new Row(list_);
  row->setCursor(Qt::ArrowCursor);
  auto *h = new QHBoxLayout(row);
  h->setContentsMargins(7, 5, 7, 5);
  h->setSpacing(8);
  auto *t = new QVBoxLayout;
  t->setSpacing(0);
  t->addWidget(elided(confirm ? "Confirm sign" : "Stop sign", row));
  t->addWidget(elided(confirm ? "the other hand's yes, for controls that ask for it"
                              : "held 1 s: ends what's running and puts the framing back",
                      row, true));
  h->addLayout(t, 1);
  auto *cb = new QComboBox(row);
  cb->setMinimumWidth(90);
  cb->setMaximumWidth(160);
  cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  Settings &s = c_.settings();
  if (confirm) {
    auto *model = qobject_cast<QStandardItemModel *>(cb->model());
    for (const auto &g : c_.gestures().gestures) {
      if (g.kind != gestures::Kind::Sign) continue;
      QString text = g.icon + " " + g.name;
      const gestures::Control *k = c_.gestures().controls.byTrigger(g.id);
      if (k) text += " (runs " + gestures::controlLabel(k->id) + ")";
      cb->addItem(text, g.ref);
      if (k && model) model->item(cb->count() - 1)->setEnabled(false);
    }
    cb->setCurrentIndex(std::max(0, cb->findData(int(s.confirmSign))));
    connect(cb, &QComboBox::activated, this, [this, cb] {
      c_.settings().confirmSign = vision::Sign(cb->currentData().toInt());
      c_.settingsEdited();
      Q_EMIT c_.libraryChanged();
    });
  } else {
    cb->addItem("Both palms open", int(gestures::StopSign::BothPalms));
    cb->addItem("Crossed arms", int(gestures::StopSign::CrossedArms));
    cb->setCurrentIndex(std::max(0, cb->findData(int(s.stopSign))));
    connect(cb, &QComboBox::activated, this, [this, cb] {
      c_.settings().stopSign = gestures::StopSign(cb->currentData().toInt());
      c_.settingsEdited();
    });
  }
  h->addWidget(cb);
  return row;
}

// ---- Memes ---------------------------------------------------------------------------------------

void LibraryPanel::buildMemes() {
  for (const auto &m : c_.memes().memes) listLay_->addWidget(memeRow(m.id));
  listLay_->addWidget(note("Pictures (PNG, JPG), GIFs and video (MP4, WebM, MOV; transparency kept). itsgiving's own "
                           "pictures come from its GitHub on first start; Import takes them from your own clone instead.",
                           list_));
}

QWidget *LibraryPanel::memeRow(const QString &id) {
  const memes::Meme *m = c_.memes().find(id);
  auto *row = new Row(list_);
  row->setCursor(Qt::ArrowCursor);
  auto *h = new QHBoxLayout(row);
  h->setContentsMargins(7, 7, 7, 7);
  h->setSpacing(8);
  h->addWidget(new Thumb(QDir(paths::mediaDir()).filePath(m->file), m->kindLabel().section(',', 0, 0), row), 0, Qt::AlignTop);

  auto *meta = new QVBoxLayout;
  meta->setSpacing(4);
  auto *l1 = new QHBoxLayout;
  l1->setSpacing(6);
  auto *name = elided(m->name(), row);
  QFont bf = name->font();
  bf.setBold(true);
  name->setFont(bf);
  l1->addWidget(name, 1);
  l1->addWidget(new Chip(m->kindLabel(), Chip::Plain, row));
  meta->addLayout(l1);

  auto *l2 = new QHBoxLayout;
  l2->setSpacing(6);
  auto *place = new QComboBox(row);
  place->addItem("On my head", int(memes::Place::Head));
  place->addItem("Full frame", int(memes::Place::Full));
  place->addItem("Fixed spot (drag on preview)", int(memes::Place::Spot));
  place->setCurrentIndex(place->findData(int(m->place)));
  place->setMinimumWidth(60);
  place->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  connect(place, &QComboBox::activated, this, [this, id, place] {
    memes::Meme *mm = c_.memes().find(id);
    if (!mm) return;
    mm->place = memes::Place(place->currentData().toInt());
    c_.memes().save(paths::memesFile());
    Q_EMIT spotMeme(mm->place == memes::Place::Spot ? id : QString());
  });
  l2->addWidget(place, 1);
  if (m->hasSound()) {
    auto *spk = new QLabel("🔊", row);
    spk->setToolTip("Volume");
    l2->addWidget(spk);
    auto *vol = new QSlider(Qt::Horizontal, row);
    vol->setRange(0, 100);
    vol->setValue(m->volume);
    vol->setFixedWidth(70);
    vol->setToolTip(QString("Volume %1 %").arg(m->volume));
    connect(vol, &QSlider::valueChanged, this, [this, id, vol](int v) {
      if (memes::Meme *mm = c_.memes().find(id)) mm->volume = v;
      vol->setToolTip(QString("Volume %1 %").arg(v));
    });
    connect(vol, &QSlider::sliderReleased, this, [this] { c_.memes().save(paths::memesFile()); });
    l2->addWidget(vol);
  } else {
    auto *snd = new QPushButton("+ sound", row);
    snd->setToolTip("Attach a sound file");
    snd->setStyleSheet("padding: 1px 7px;");
    connect(snd, &QPushButton::clicked, this, [this, id] {
      const QString f = QFileDialog::getOpenFileName(this, "Attach a sound", {}, "Sounds (*.mp3 *.wav *.ogg *.m4a *.aac *.flac *.opus)");
      if (f.isEmpty()) return;
      if (c_.memes().attachSound(id, f)) c_.memesEdited();
    });
    l2->addWidget(snd);
  }
  meta->addLayout(l2);

  QStringList users;
  for (const auto &g : c_.gestures().gestures)
    if (g.action == "meme:" + id) users << g.name;
  meta->addWidget(elided("used by: " + (users.isEmpty() ? QString("nothing yet") : users.join(", ")), row, true));
  h->addLayout(meta, 1);

  row->onMenu = [this, id](QPoint at) {
    QMenu menu(this);
    memes::Meme *mm = c_.memes().find(id);
    if (!mm) return;
    if (mm->place == memes::Place::Spot) menu.addAction("Place on the preview", this, [this, id] { Q_EMIT spotMeme(id); });
    menu.addAction(mm->sound.isEmpty() ? "Attach a sound..." : "Replace the sound...", this, [this, id] {
      const QString f = QFileDialog::getOpenFileName(this, "Attach a sound", {}, "Sounds (*.mp3 *.wav *.ogg *.m4a *.aac *.flac *.opus)");
      if (!f.isEmpty() && c_.memes().attachSound(id, f)) c_.memesEdited();
    });
    menu.addSeparator();
    menu.addAction("Remove from the library", this, [this, id] {
      for (auto &g : c_.gestures().gestures)
        if (g.action == "meme:" + id) g.action.clear();
      c_.memes().remove(id);
      Q_EMIT spotMeme({});
      c_.gesturesEdited();
      c_.memesEdited();
    });
    menu.exec(at);
  };
  return row;
}

}  // namespace racg::ui
