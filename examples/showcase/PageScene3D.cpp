// 三维设备视图 —— 一套反应釜撬块，部件带状态，可旋转、可点选。
//
// WHAT THIS PAGE IS DEMONSTRATING, in the order it matters:
//
//   1. A plant object built from PRIMITIVES.  There is no model file: the skid
//      below is boxes, cylinders, spheres and pipes, which is what an industrial
//      schematic is actually made of.  Anything that came out of a CAD exporter
//      would arrive through the same Mesh window, owned by the application --
//      see the note at the top of Mesh.hpp.
//   2. STATUS ON A PART.  The vessel goes amber and then red as the process
//      value climbs, from the same DataHub every other page reads.  That is the
//      whole point of the control: the operator's question is "which one is
//      red", and the answer is on the object rather than in a list beside it.
//   3. THE TABLE AND THE VIEW DRIVING EACH OTHER.  Clicking a part selects its
//      row; clicking a row selects the part.  Two controls from two different
//      rounds, neither of which knows the other exists -- they are wired here,
//      in the page, through signals.
//
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "AppState.hpp"
#include "Pages.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/scene3d/Mesh.hpp"
#include "geeyoou/scene3d/Scene3D.hpp"
#include "geeyoou/scene3d/View3D.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/TableModel.hpp"
#include "geeyoou/widget/TableView.hpp"
#include "geeyoou/widget/ToggleSwitch.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
constexpr float kBandGap = 16.0f;
constexpr float kItemGap = 10.0f;

BoxLayout* stack(Widget* host, float spacing) {
  auto* b = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  b->setSpacing(spacing);
  return b;
}

BoxLayout* line(Widget* host, float spacing) {
  auto* b = host->setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  b->setSpacing(spacing);
  return b;
}

Widget* band(Widget* parent, BoxLayout* into, std::uint16_t stretch = 0) {
  Widget* w = parent->add<Widget>();
  into->addWidget(w, stretch);
  return w;
}

const char* stateText(PartState s) {
  switch (s) {
    case PartState::Running: return "运行";
    case PartState::Stopped: return "停机";
    case PartState::Fault: return "故障";
    case PartState::Maintenance: return "维护";
    case PartState::Disabled: return "未投用";
    case PartState::Normal:
    default: return "正常";
  }
}

Color stateColor(PartState s) {
  const Theme& t = Theme::current();
  switch (s) {
    case PartState::Running: return t.success;
    case PartState::Stopped: return t.textDim;
    case PartState::Fault: return t.danger;
    case PartState::Maintenance: return t.warn;
    case PartState::Disabled: return t.textDisabled;
    case PartState::Normal:
    default: return t.accent;
  }
}

// Holds the geometry and the scene, and exists ONLY for where it sits in the
// panel's base list.
//
// The View3D below is a CHILD widget holding a raw Scene3D*, and the Scene3D
// holds Mesh windows onto the builder's vectors.  Members are destroyed before
// base classes, so making these members of a Widget subclass would destroy the
// scene first and leave ~Widget tearing down a view that points at it.  Bases go
// last and in reverse order, so with the holder declared FIRST the widget half
// dies first -- the reader before the thing it reads.  Identical to
// TableModelHolder in TableDemoData.hpp, and for identical reasons: this
// codebase has paid for the other arrangement once already, with an ASan report.
// ⚠️ NOT `geometry_`, and not `scene_` either.  Widget has a private member
// called geometry_, and with two base classes the name is AMBIGUOUS at lookup
// time even though the other one is private -- C++ resolves names before it
// checks access.  Third naming collision of this project (the door lint's P1
// virtual names, its P2 `add`, and now a base class member), and the same rule
// settles all three: a new name goes somewhere the surroundings do not already
// own.
class SkidHolder {
 protected:
  MeshBuilder skidMesh_;
  Scene3D skidScene_;
};

// The skid, and the live status that drives it.
class SkidPanel : public SkidHolder, public Widget {
 public:
  explicit SkidPanel(AppState& app) : app_(&app) {
    buildParts();
    buildGeometry();
    skidScene_.addNode(skidMesh_.mesh());
    buildAnnotations();

    view_ = add<View3D>();
    view_->setScene(&skidScene_);
    view_->resetView();
  }

  View3D* view() const { return view_; }
  Scene3D& scene() { return skidScene_; }

  PartId vessel() const { return vessel_; }
  PartId pump() const { return pump_; }
  PartId motor() const { return motor_; }
  PartId valve() const { return valve_; }
  PartId exchanger() const { return exchanger_; }
  PartId piping() const { return piping_; }
  PartId skid() const { return skid_; }

  // "Everything is fine again" -- what the demo's reset button calls.
  void setNominal() {
    skidScene_.setPartState(vessel_, PartState::Normal);
    skidScene_.setPartState(motor_, PartState::Running);
    skidScene_.setPartState(pump_, PartState::Running);
    skidScene_.setPartState(valve_, PartState::Running);
    skidScene_.setPartState(exchanger_, PartState::Normal);
    view_->update();
  }

  Signal<> stateChanged;

 protected:
  void onGeometryChanged() override {
    if (view_) view_->setGeometry(localRect());
  }

  SizeHint sizeHint() const override {
    SizeHint h;
    h.preferred = Size{620.0f, 460.0f};
    h.min = Size{280.0f, 220.0f};
    return h;
  }

  // The live half.  The acquisition thread only ever pushed into the DataHub
  // (architecture 3.11); this runs on the UI thread's animation tick and turns a
  // process value into a part state.  The THRESHOLDS ARE PROCESS ENGINEERING and
  // live here, in the application -- scene3d has no opinion about temperature.
  void onAnimationTick() override {
    if (!app_ || !view_) return;
    if (++tick_ < 6) return;  // ~5 Hz is plenty for a status colour
    tick_ = 0;

    const double temp = app_->hub.lastValue(app_->chTemp);
    const double flow = app_->hub.lastValue(app_->chFlow);

    // The label carries the NUMBER; the colour carries the JUDGEMENT.  An
    // operator reading a plant screen wants both, and wants them in the same
    // place -- which is what a callout on the object is for.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f °C", temp);
    skidScene_.setAnnotationValue(noteVessel_, buf);
    std::snprintf(buf, sizeof(buf), "%.1f m³/h", flow);
    skidScene_.setAnnotationValue(notePump_, buf);

    // Also feed the heat-map mode, so switching to it shows something真实
    // rather than a demo constant.
    skidScene_.setPartValue(vessel_, float((temp - 90.0) / 90.0));
    skidScene_.setPartValue(pump_, float(flow / 90.0));
    view_->update();

    const PartState want = temp >= 158.0   ? PartState::Fault
                           : temp >= 148.0 ? PartState::Maintenance
                                           : PartState::Normal;
    if (want == skidScene_.partState(vessel_)) return;
    skidScene_.setPartState(vessel_, want);
    stateChanged.emit();
  }

 private:
  void buildParts() {
    // Materials are ABSOLUTE colours -- steel is grey under every skin -- while
    // the STATE colours come from the theme at paint time.  The split is what
    // keeps the captured-theme bug from coming back through the back door.
    skid_ = skidScene_.addPart("SKID-01", Color::rgb(0x8A, 0x93, 0xA6));
    vessel_ = skidScene_.addPart("V-101 反应釜", Color::rgb(0x8E, 0x9B, 0xB2));
    motor_ = skidScene_.addPart("M-101 搅拌电机", Color::rgb(0x77, 0x84, 0x9B));
    pump_ = skidScene_.addPart("P-101 进料泵", Color::rgb(0x77, 0x84, 0x9B));
    valve_ = skidScene_.addPart("XV-101 进料阀", Color::rgb(0x9A, 0xA6, 0xB8));
    exchanger_ = skidScene_.addPart("E-101 冷凝器", Color::rgb(0x8E, 0x9B, 0xB2));
    piping_ = skidScene_.addPart("工艺管线", Color::rgb(0x6B, 0x77, 0x8C));

    skidScene_.setPartState(motor_, PartState::Running);
    skidScene_.setPartState(pump_, PartState::Running);
    skidScene_.setPartState(valve_, PartState::Running);
  }

  // Callouts.  The offset lifts each label clear of the body it names -- the
  // anchor itself is the part's centre, which for a vessel is inside it.
  void buildAnnotations() {
    noteVessel_ = skidScene_.addAnnotation(vessel_, "V-101 反应釜", {0.0f, 1.6f, 0.0f});
    notePump_ = skidScene_.addAnnotation(pump_, "P-101 进料泵", {0.0f, 0.9f, 0.0f});
    noteValve_ = skidScene_.addAnnotation(valve_, "XV-101 进料阀", {0.0f, 1.1f, 0.0f});
    noteExch_ = skidScene_.addAnnotation(exchanger_, "E-101 冷凝器", {0.0f, 1.0f, 0.0f});
    skidScene_.setAnnotationValue(noteValve_, "开度 100%");
    skidScene_.setAnnotationValue(noteExch_, "循环水 32 °C");

    // Static values so the heat-map mode is meaningful before the first tick.
    skidScene_.setPartValue(vessel_, 0.55f);
    skidScene_.setPartValue(pump_, 0.40f);
    skidScene_.setPartValue(valve_, 0.30f);
    skidScene_.setPartValue(exchanger_, 0.18f);
    skidScene_.setPartValue(motor_, 0.62f);
    skidScene_.setPartValue(piping_, 0.35f);
    skidScene_.setPartValue(skid_, 0.05f);
  }

  // EVERY CALL BELOW EMITS ONE CONVEX CLOSED BODY, AND NO TWO OF THEM OVERLAP.
  //
  // That second half is a real constraint on the numbers, not a slogan, and the
  // first version of this page got it wrong: it capped the vessel with whole
  // SPHERES, half of each buried in the cylinder.  Bodies that interpenetrate
  // are the one arrangement the painter's algorithm sorts wrongly, and the
  // vessel duly came out looking soft and hollow -- a rendering artefact caused
  // entirely by the model.  Heads are domes now, and every joint below is
  // arithmetic that makes two surfaces MEET rather than pass through each other.
  void buildGeometry() {
    skidMesh_.reserve(4096, 6144);

    // --- skid frame and legs ---
    skidMesh_.addBox({0.0f, 0.15f, 0.0f}, {11.0f, 0.30f, 6.4f}, skid_);
    for (int i = 0; i < 4; ++i) {
      const float x = (i & 1) ? 5.0f : -5.0f;
      const float z = (i & 2) ? 2.8f : -2.8f;
      skidMesh_.addBox({x, -0.25f, z}, {0.45f, 0.5f, 0.45f}, skid_);
    }

    // --- V-101: bottom head, shell, top head.  The heads' rims sit exactly on
    //     the shell's ends, so the three share two circles and nothing else.
    const float vx = -3.1f;
    const float vz = 0.0f;
    const float vBottom = 1.5f;
    const float vShell = 2.6f;
    const float vTop = vBottom + vShell;
    const float vR = 1.35f;
    for (int i = 0; i < 4; ++i) {
      const float a = 0.785f + 1.5708f * float(i);
      const float lx = vx + std::cos(a) * (vR * 0.78f);
      const float lz = vz + std::sin(a) * (vR * 0.78f);
      skidMesh_.addPipe({lx, 0.30f, lz}, {lx, vBottom, lz}, 0.13f, skid_, 10);
    }
    skidMesh_.addDome({vx, vBottom, vz}, vR, -0.85f, vessel_, 22, 6);
    skidMesh_.addCylinder({vx, vBottom, vz}, vR, vShell, vessel_, 22);
    skidMesh_.addDome({vx, vTop, vz}, vR, 0.85f, vessel_, 22, 6);

    // --- M-101: shaft from the top head to the stool, stool under the motor ---
    const float stoolBase = vTop + 0.85f;      // the head's tip
    const float stoolTop = stoolBase + 0.32f;
    skidMesh_.addPipe({vx, stoolBase - 0.9f, vz}, {vx, stoolBase, vz}, 0.13f, motor_, 10);
    skidMesh_.addCylinder({vx, stoolBase, vz}, 0.55f, 0.32f, motor_, 16);
    skidMesh_.addBox({vx, stoolTop + 0.45f, vz}, {1.0f, 0.9f, 1.0f}, motor_);

    // --- P-101: volute, then the motor barrel starting where the volute ends ---
    const float px = 3.2f;
    const float pz = 2.0f;
    const float pR = 0.60f;
    skidMesh_.addCylinder({px, 0.30f, pz}, pR, 0.72f, pump_, 18);
    skidMesh_.addPipe({px + pR, 0.66f, pz}, {px + pR + 1.5f, 0.66f, pz}, 0.40f,
                      pump_, 16);
    skidMesh_.addFlange({px + pR + 1.5f + 0.06f, 0.66f, pz}, {1.0f, 0.0f, 0.0f},
                        0.52f, 0.12f, pump_, 16);

    // --- XV-101: body, two flanges that abut it, stem and handwheel ---
    const float xvx = 0.6f;
    const float xvy = 2.5f;
    const float xvz = pz;
    const float xvHalf = 0.31f;
    skidMesh_.addBox({xvx, xvy, xvz}, {xvHalf * 2.0f, 0.72f, xvHalf * 2.0f}, valve_);
    skidMesh_.addFlange({xvx - xvHalf - 0.05f, xvy, xvz}, {1.0f, 0.0f, 0.0f}, 0.38f,
                        0.10f, valve_, 14);
    skidMesh_.addFlange({xvx + xvHalf + 0.05f, xvy, xvz}, {1.0f, 0.0f, 0.0f}, 0.38f,
                        0.10f, valve_, 14);
    skidMesh_.addPipe({xvx, xvy + 0.36f, xvz}, {xvx, xvy + 0.94f, xvz}, 0.09f,
                      valve_, 10);
    skidMesh_.addBox({xvx, xvy + 1.00f, xvz}, {0.80f, 0.12f, 0.12f}, valve_);

    // --- E-101: shell with a head bolted on each end, on one leg ---
    const float ex = 3.4f;
    const float ey = 3.6f;
    const float ez = -1.8f;
    const float eHalf = 1.5f;
    const float eR = 0.58f;
    skidMesh_.addPipe({ex - eHalf, ey, ez}, {ex + eHalf, ey, ez}, eR, exchanger_, 18);
    skidMesh_.addFlange({ex - eHalf - 0.08f, ey, ez}, {1.0f, 0.0f, 0.0f}, 0.72f,
                        0.16f, exchanger_, 18);
    skidMesh_.addFlange({ex + eHalf + 0.08f, ey, ez}, {1.0f, 0.0f, 0.0f}, 0.72f,
                        0.16f, exchanger_, 18);
    skidMesh_.addPipe({ex, ey - eR, ez}, {ex, 0.30f, ez}, 0.20f, skid_, 12);

    // --- process piping.  Each run starts on the SURFACE of what it leaves and
    //     ends on the surface of what it reaches, which is why the numbers below
    //     carry a radius rather than a centre.
    const float feedY = 0.66f;
    skidMesh_.addPipe({px + pR + 1.5f + 0.12f, feedY, pz}, {xvx, feedY, pz}, 0.18f,
                      piping_, 12);
    skidMesh_.addPipe({xvx, feedY, pz}, {xvx, xvy - 0.36f, xvz}, 0.18f, piping_, 12);
    skidMesh_.addPipe({xvx, xvy + 0.36f, xvz}, {xvx, 3.9f, xvz}, 0.18f, piping_, 12);
    skidMesh_.addPipe({xvx, 3.9f, xvz}, {xvx, 3.9f, vz}, 0.18f, piping_, 12);
    skidMesh_.addPipe({xvx, 3.9f, vz}, {vx + vR, 3.9f, vz}, 0.18f, piping_, 12);

    const float outY = 0.75f;
    skidMesh_.addPipe({vx, outY, vz + vR * 0.55f}, {vx, outY, ez}, 0.20f, piping_, 12);
    skidMesh_.addPipe({vx, outY, ez}, {ex - eHalf - 0.16f, outY, ez}, 0.20f, piping_,
                      12);
    skidMesh_.addPipe({ex - eHalf - 0.16f, outY, ez}, {ex - eHalf - 0.16f, ey - 0.72f, ez},
                      0.20f, piping_, 12);
  }

  AppState* app_ = nullptr;
  View3D* view_ = nullptr;
  int tick_ = 0;

  PartId skid_ = kNoPart;
  PartId vessel_ = kNoPart;
  PartId motor_ = kNoPart;
  PartId pump_ = kNoPart;
  PartId valve_ = kNoPart;
  PartId exchanger_ = kNoPart;
  PartId piping_ = kNoPart;

  AnnotationId noteVessel_ = kNoAnnotation;
  AnnotationId notePump_ = kNoAnnotation;
  AnnotationId noteValve_ = kNoAnnotation;
  AnnotationId noteExch_ = kNoAnnotation;
};

// The part list beside the view, as a table.
//
// It reads the SCENE, not a copy of it -- FunctionTableModel is a pull model, so
// a state the 3D view changed shows up in the list on the next repaint with
// nothing to keep in sync.  That property is why the two controls can be wired
// together with two signals and no shared state at all.
//
// Holder base for the same reason as SkidHolder above: the table is a CHILD
// widget holding a raw model pointer, so the model has to outlive it, and bases
// are destroyed after members.  The first attempt kept the model alive inside
// the click lambda instead -- which does not even compile, because Signal takes
// a std::function and a unique_ptr capture is not copy-constructible.  A
// compiler error was the cheap version of this lesson; the ASan report in the
// table round was the expensive one.
class PartListHolder {
 protected:
  std::unique_ptr<FunctionTableModel> model_;
};

class PartListPanel : public PartListHolder, public Widget {
 public:
  explicit PartListPanel(Scene3D& scene) {
    model_ = std::make_unique<FunctionTableModel>();
    model_->rowCount = int(scene.partCount());
    Scene3D* sp = &scene;
    model_->text = [sp](int r, int c) -> std::string {
      if (!sp->validPart(PartId(r))) return {};
      const Scene3D::Part& p = sp->part(PartId(r));
      return c == 0 ? p.name : stateText(p.state);
    };
    // Evaluated AT PAINT TIME, so the chip follows both the part's state and the
    // active skin without anybody being told to refresh.
    model_->accent = [sp](int r, int c) -> Color {
      if (c != 1 || !sp->validPart(PartId(r))) return Color::rgba(0, 0, 0, 0);
      return stateColor(sp->part(PartId(r)).state);
    };

    table_ = add<TableView>();
    std::vector<TableView::Column> cols;
    TableView::Column name;
    name.title = "部件";
    name.width = 0.0f;
    cols.push_back(name);
    TableView::Column st;
    st.title = "状态";
    st.width = 84.0f;
    st.kind = CellKind::Chip;
    st.align = HAlign::Center;
    cols.push_back(st);
    table_->setColumns(cols);
    table_->setModel(model_.get());
    table_->setSelectionMode(TableView::SelectionMode::Single);
    table_->setRowHeight(30.0f);
    table_->rowsReset();
  }

  TableView* table() const { return table_; }

 protected:
  void onGeometryChanged() override {
    if (table_) table_->setGeometry(localRect());
  }

  SizeHint sizeHint() const override {
    SizeHint h;
    h.preferred = Size{280.0f, 260.0f};
    h.min = Size{200.0f, 140.0f};
    return h;
  }

 private:
  TableView* table_ = nullptr;
};

}  // namespace

Size buildScene3DPage(Widget* content, AppState& app) {
  BoxLayout* page = stack(content, kBandGap);
  Widget* row = band(content, page, /*stretch=*/1);
  BoxLayout* rowL = line(row, kBandGap);

  // ------------------------------------------------------------ 3D 视图 ---
  auto* gView = row->add<GroupBox>();
  gView->setTitle("三维设备视图 · 左键拖动旋转 · 滚轮缩放 · Shift+左键平移 · 单击选中部件");
  rowL->addWidget(gView, 3);
  BoxLayout* viewStack = stack(gView, kItemGap);

  auto* skid = gView->add<SkidPanel>(app);
  viewStack->addWidget(skid, 1);
  View3D* view = skid->view();

  auto* hint = gView->add<Label>();
  hint->addStyleClass("caption");
  hint->setPixelSize(11.0f);
  hint->setText("软件渲染，无 GPU 依赖 · 视口有自己的中间调底色，"
                "所以浅色皮肤下白色模型依然看得见");
  viewStack->addWidget(hint);

  // ------------------------------------------------------------ 侧面板 ---
  auto* gSide = row->add<GroupBox>();
  gSide->setTitle("部件与状态");
  rowL->addWidget(gSide, 1);
  BoxLayout* sideStack = stack(gSide, kItemGap);

  Scene3D* scene = &skid->scene();

  auto* list = gSide->add<PartListPanel>(*scene);
  sideStack->addWidget(list, 1);
  TableView* table = list->table();

  table->rowClicked.connect([view](int row) { view->setSelectedPart(PartId(row)); });

  auto* selected = gSide->add<Label>();
  selected->addStyleClass("caption");
  selected->setPixelSize(11.0f);
  selected->setText("未选中部件 —— 在三维视图里点一个试试");
  sideStack->addWidget(selected);

  // The two controls know nothing about each other; this is the entire wiring.
  view->partClicked.connect([scene, table, selected](PartId id) {
    if (!scene->validPart(id)) {
      selected->setText("点到了空处");
      table->clearSelection();
      return;
    }
    const Scene3D::Part& p = scene->part(id);
    selected->setText("已选中：" + p.name + " · " + stateText(p.state));
    table->setCurrentCell(int(id), 0);
    table->selectRow(int(id), true);
  });

  // ------------------------------------------------------------ 按钮 ---
  Widget* buttons = band(gSide, sideStack);
  BoxLayout* buttonsL = line(buttons, kItemGap);

  auto* btnReset = buttons->add<PushButton>();
  btnReset->setText("复位视角");
  btnReset->clicked.connect([view] { view->resetView(); });
  buttonsL->addWidget(btnReset, 1);

  auto* btnNominal = buttons->add<PushButton>();
  btnNominal->setText("全部正常");
  btnNominal->clicked.connect([skid, table] {
    skid->setNominal();
    table->rowsChanged();
  });
  buttonsL->addWidget(btnNominal, 1);

  Widget* buttons2 = band(gSide, sideStack);
  BoxLayout* buttons2L = line(buttons2, kItemGap);

  auto* btnFault = buttons2->add<PushButton>();
  btnFault->setText("模拟泵故障");
  btnFault->setVariant(ButtonVariant::Danger);
  btnFault->clicked.connect([skid, view, table] {
    skid->scene().setPartState(skid->pump(), PartState::Fault);
    view->setSelectedPart(skid->pump());
    view->update();
    table->rowsChanged();
  });
  buttons2L->addWidget(btnFault, 1);

  auto* btnMaint = buttons2->add<PushButton>();
  btnMaint->setText("阀门检修");
  btnMaint->setVariant(ButtonVariant::Warning);
  btnMaint->clicked.connect([skid, view, table] {
    skid->scene().setPartState(skid->valve(), PartState::Maintenance);
    view->update();
    table->rowsChanged();
  });
  buttons2L->addWidget(btnMaint, 1);

  // ---------------------------------------------------------- 着色模式 ---
  //
  // Three questions, three colourings.  Worth exposing as a control rather than
  // as a build-time choice: an operator switches between "which one is red" and
  // "where is it hot" several times in one shift.
  auto* modeCaption = gSide->add<Label>();
  modeCaption->addStyleClass("caption");
  modeCaption->setPixelSize(11.0f);
  modeCaption->setText("着色模式");
  sideStack->addWidget(modeCaption);

  Widget* modes = band(gSide, sideStack);
  BoxLayout* modesL = line(modes, kItemGap);

  auto* btnStatus = modes->add<PushButton>();
  btnStatus->setText("按状态");
  btnStatus->setVariant(ButtonVariant::Primary);
  modesL->addWidget(btnStatus, 1);

  auto* btnMaterial = modes->add<PushButton>();
  btnMaterial->setText("按材质");
  modesL->addWidget(btnMaterial, 1);

  auto* btnHeat = modes->add<PushButton>();
  btnHeat->setText("热力图");
  modesL->addWidget(btnHeat, 1);

  auto setMode = [view, btnStatus, btnMaterial, btnHeat](ColorMode m) {
    view->setColorMode(m);
    btnStatus->setVariant(m == ColorMode::Status ? ButtonVariant::Primary
                                                 : ButtonVariant::Default);
    btnMaterial->setVariant(m == ColorMode::Material ? ButtonVariant::Primary
                                                     : ButtonVariant::Default);
    btnHeat->setVariant(m == ColorMode::Value ? ButtonVariant::Primary
                                              : ButtonVariant::Default);
  };
  btnStatus->clicked.connect([setMode] { setMode(ColorMode::Status); });
  btnMaterial->clicked.connect([setMode] { setMode(ColorMode::Material); });
  btnHeat->clicked.connect([setMode] { setMode(ColorMode::Value); });

  // ---------------------------------------------------------- 部件上色 ---
  auto* paintCaption = gSide->add<Label>();
  paintCaption->addStyleClass("caption");
  paintCaption->setPixelSize(11.0f);
  paintCaption->setText("给选中部件上色（切到「按材质」查看）");
  sideStack->addWidget(paintCaption);

  Widget* swatches = band(gSide, sideStack);
  BoxLayout* swatchesL = line(swatches, kItemGap);

  struct Swatch {
    const char* label;
    Color color;
  };
  // ABSOLUTE colours, deliberately: a material is what a thing is painted, and
  // that does not change when the skin does.  The status colours next door are
  // the ones that follow the theme.
  const Swatch kSwatches[] = {
      {"钢灰", Color::rgb(0x9A, 0xA6, 0xB8)},
      {"工程蓝", Color::rgb(0x3D, 0x7E, 0xC4)},
      {"设备绿", Color::rgb(0x46, 0x9E, 0x72)},
      {"安全橙", Color::rgb(0xD2, 0x86, 0x2E)},
  };
  for (const Swatch& sw : kSwatches) {
    auto* b = swatches->add<PushButton>();
    b->setText(sw.label);
    const Color c = sw.color;
    b->clicked.connect([view, scene, setMode, c, selected] {
      const PartId id = view->selectedPart();
      if (!scene->validPart(id)) {
        selected->setText("先在三维视图里选中一个部件，再上色");
        return;
      }
      scene->setPartMaterial(id, c);
      setMode(ColorMode::Material);
      selected->setText("已上色：" + scene->part(id).name);
    });
    swatchesL->addWidget(b, 1);
  }

  Widget* toggles = band(gSide, sideStack);
  BoxLayout* togglesL = line(toggles, kItemGap);

  auto* swGrid = toggles->add<ToggleSwitch>();
  swGrid->setText("地面网格");
  swGrid->setChecked(true);
  swGrid->toggled.connect([view](bool on) { view->setGridVisible(on); });
  togglesL->addWidget(swGrid);

  auto* swNotes = toggles->add<ToggleSwitch>();
  swNotes->setText("标注");
  swNotes->setChecked(true);
  swNotes->toggled.connect([view](bool on) { view->setAnnotationsVisible(on); });
  togglesL->addWidget(swNotes);

  auto* swHover = toggles->add<ToggleSwitch>();
  swHover->setText("悬停高亮");
  swHover->setChecked(true);
  swHover->toggled.connect([view](bool on) { view->setHoverHighlight(on); });
  togglesL->addWidget(swHover);

  auto* live = gSide->add<Label>();
  live->addStyleClass("caption");
  live->setPixelSize(11.0f);
  live->setText("V-101 的颜色由釜内温度驱动：≥148 转维护色，≥158 转故障色");
  sideStack->addWidget(live);

  // The live status changed, so the LIST has to be repainted too -- the model
  // pulls, so nothing has to be copied, but somebody still has to say "look
  // again".
  skid->stateChanged.connect([table] { table->rowsChanged(); });

  return content->sizeHint().preferred;
}

}  // namespace showcase
