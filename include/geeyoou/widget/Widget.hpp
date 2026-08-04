#pragma once
//
// Widget: the node type of the UI tree.
//
// Ownership: a parent owns its children through unique_ptr; children hold a raw
// back-pointer.  Non-visual objects (data channels, protocol parsers, worker
// threads) deliberately do NOT live in this tree -- unlike Qt, where everything
// inherits QObject.  See docs/architecture.md section 3.3.
//
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "geeyoou/core/Event.hpp"
#include "geeyoou/core/Signal.hpp"
#include "geeyoou/core/Types.hpp"
#include "geeyoou/render/StyleSheet.hpp"

namespace geeyoou {

class Painter;
class Window;

// Declares a widget's type name to the style engine, and keeps type selectors
// inheritance-aware: with this macro a `PushButton { }` rule also styles
// IconButton and MenuButton, exactly as QSS does with QPushButton.
//
// Put it in the PUBLIC section of every widget that should be addressable by
// name from a style sheet.  A class that omits it is still styleable through
// its base's type name, its .class and its #id -- it just has no name of its
// own.  No RTTI is involved: this is a virtual call and a string compare.
#define GEEYOOU_STYLE_TYPE(Self, Base)                                 \
  const char* styleType() const override { return #Self; }             \
  bool styleMatchesType(std::string_view n) const override {           \
    return n == #Self || Base::styleMatchesType(n);                    \
  }

// Whether and how a widget can take keyboard focus.
enum class FocusPolicy : std::uint8_t {
  None,   // never focusable (labels, separators, decorative panels)
  Click,  // focusable by clicking, but skipped by Tab traversal
  Tab,    // focusable by clicking AND reachable with Tab / Shift+Tab
};

class Widget : public StyleSubject {
 public:
  Widget() = default;
  // Out of line, and NOT defaulted: it has to cancel any in-flight event bubble
  // standing on this widget.  The removal API announces that on its way through
  // (Widget.cpp, announceDetached), but a widget can die without ever passing
  // through it -- a Window is a stack object, the unique_ptr takeChild() hands
  // back can simply be dropped, and any subclass destructor gets here too.  The
  // bubble is what would then read a freed parent pointer.
  ~Widget() override;

  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  // Constructs a child in place and returns a non-owning pointer to it.
  template <class T, class... Args>
  T* add(Args&&... args) {
    static_assert(std::is_base_of_v<Widget, T>, "T must derive from Widget");
    auto owned = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = owned.get();
    raw->parent_ = this;
    children_.push_back(std::move(owned));
    raw->update();
    return raw;
  }

  // --- removing children ---------------------------------------------------
  //
  // Hands `child` back to the caller, who becomes its owner; the subtree under
  // it comes along and its parent() becomes null.  Returns nullptr when `child`
  // is not one of ours -- including the case where a handler run during the
  // removal got to it first.
  //
  // Before anything is unlinked, EVERY node of the departing subtree is
  // announced to the Window (Window::widgetDetached), which drops any focus,
  // hover, mouse-grab or popup pointer aimed into it.  Per node rather than
  // just at the root, because those pointers routinely name a grandchild: a
  // root-only notification would leave the window dereferencing freed memory on
  // the very next event.
  //
  // Legal from inside an event handler or a slot -- an in-flight event bubble
  // that is standing on a removed widget is cancelled rather than left walking
  // into freed memory.  The one thing a slot still may NOT do is remove the
  // object that owns the signal it is running inside: that is contract D7 (see
  // core/Signal.hpp), and nothing here can make destroying a Signal mid-emit
  // safe.
  std::unique_ptr<Widget> takeChild(Widget* child);

  // takeChild() and let the result die: `child` and its subtree are destroyed.
  void removeChild(Widget* child);

  // Removes every child, last one first -- reverse order of construction, the
  // same order the compiler would have used.
  void clearChildren();

  // --- geometry (logical pixels, relative to the parent) -------------------
  void setGeometry(const Rect& r);
  const Rect& geometry() const { return geometry_; }
  Rect localRect() const { return {0.0f, 0.0f, geometry_.width(), geometry_.height()}; }
  Rect windowRect() const;
  Point mapToWindow(Point local) const;

  void setVisible(bool on);
  bool isVisible() const { return visible_; }

  // --- enabled state ------------------------------------------------------
  // A disabled widget takes no input and neither do its descendants: disabling
  // a GroupBox greys out the whole parameter block, which is exactly what an
  // interlocked HMI screen needs.
  void setEnabled(bool on);
  bool isEnabled() const { return enabled_; }
  bool isEffectivelyEnabled() const;

  // --- focus --------------------------------------------------------------
  void setFocusPolicy(FocusPolicy p) { focusPolicy_ = p; }
  FocusPolicy focusPolicy() const { return focusPolicy_; }
  bool isFocusable() const;

  void setFocus();
  void clearFocus();
  bool hasFocus() const;

  // --- repaint ------------------------------------------------------------
  // Marks this widget (or a sub-region of it) dirty.  Cheap and safe to call
  // often -- the dirty region is coalesced and the actual repaint happens once
  // per platform paint cycle.
  void update();
  void update(const Rect& localRect);

  // --- tree ---------------------------------------------------------------
  Widget* parent() const { return parent_; }
  Window* window();
  const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

  // --- style identity -----------------------------------------------------
  //
  // The three things a selector can address, mirroring QSS: the type name
  // (declared with GEEYOOU_STYLE_TYPE), an object name (`#startPump`) and any
  // number of style classes (`.danger`).  All optional -- a widget with none of
  // them is still styled by its type and by `*`.
  void setObjectName(std::string name);
  const std::string& objectName() const { return objectName_; }

  // Space-separated, replacing whatever was there: setStyleClasses("danger big")
  void setStyleClasses(std::string_view spaceSeparated);
  void addStyleClass(std::string cls);
  void removeStyleClass(std::string_view cls);
  bool hasStyleClass(std::string_view cls) const;
  const std::vector<std::string>& styleClasses() const { return classes_; }

  // Properties the active style sheet resolves for this widget in `state`.
  // Cached against the global style generation, so the cascade runs once per
  // widget per skin change rather than once per paint.
  const StyleProps& style(StyleState state = StyleState::None) const;

  // --- StyleSubject -------------------------------------------------------
  virtual const char* styleType() const { return "Widget"; }
  bool styleMatchesType(std::string_view n) const override { return n == "Widget"; }
  bool styleMatchesClass(std::string_view c) const override { return hasStyleClass(c); }
  const std::string& styleObjectName() const override { return objectName_; }
  const StyleSubject* styleParentSubject() const override { return parent_; }
  // What the base class can know on its own.  A widget that also tracks hover
  // or pressed passes those in through style(state) when it paints; this is the
  // fallback used when the widget appears as an ANCESTOR in a descendant rule.
  StyleState styleState() const override;

  // --- scrolling ----------------------------------------------------------
  // Children are laid out at (their geometry - contentOffset), so a container
  // scrolls its contents by moving this rather than by rewriting every child's
  // geometry.  ScrollArea is the intended user; nothing stops a custom
  // container from driving it directly.
  void setContentOffset(Point offset);
  Point contentOffset() const { return contentOffset_; }

  // --- internal dispatch (called by Window) -------------------------------
  // `clipInWindow` is the accumulated visible region of every ancestor.  It is
  // what actually clips a child to its parent -- widget bounds alone would let
  // a scrolled row draw straight over the container that owns it.
  void paintTree(Painter& p, const Rect& dirtyInWindow, const Rect& clipInWindow);
  Widget* hitTest(Point windowPos);
  void dispatchMouse(const MouseEvent& windowEvent);
  void dispatchKey(const KeyEvent& e);
  void collectFocusable(std::vector<Widget*>& out);
  void animationTickTree();

 protected:
  // `dirtyLocal` is the damaged region in this widget's own coordinate space.
  // The painter is already translated to the widget origin and clipped to its
  // bounds, so a widget can always draw as if it owned the whole surface.
  virtual void onPaint(Painter& p, const Rect& dirtyLocal) { (void)p; (void)dirtyLocal; }
  virtual void onMouse(const MouseEvent& e) { (void)e; }
  virtual void onKey(const KeyEvent& e) { (void)e; }
  virtual void onFocusChanged(bool focused) { (void)focused; update(); }
  virtual void onEnabledChanged() { update(); }
  virtual void onGeometryChanged() {}

  // Called on every visible widget when Window::enableAnimations() is on.
  // Default is a no-op, and an animating widget must call update() ITSELF --
  // the tick alone never repaints anything, so an idle screen stays idle even
  // with the animation clock running.
  //
  // One timer owned by the Window, walked over the tree, rather than a timer
  // per widget: docs/architecture.md forbids widgets owning timers, and a
  // registry of animated widgets would need unregistration on destruction.
  virtual void onAnimationTick() {}

 private:
  virtual Window* asWindow() { return nullptr; }

  Rect geometry_;
  Point contentOffset_;
  Widget* parent_ = nullptr;
  std::vector<std::unique_ptr<Widget>> children_;
  bool visible_ = true;
  bool enabled_ = true;
  FocusPolicy focusPolicy_ = FocusPolicy::None;

  std::string objectName_;
  std::vector<std::string> classes_;
  // One-entry cache.  A widget paints in one state per frame, so a single slot
  // hits essentially always -- a map would cost more than the cascade it saves.
  mutable StyleProps styleCache_;
  mutable StyleState styleCacheState_ = StyleState::None;
  mutable std::uint64_t styleCacheGen_ = 0;

  friend class Window;
};

}  // namespace geeyoou
