//
// Win32 backend -- the ONLY file in GeeyoouUI that includes <windows.h>.
//
// It knows nothing about the renderer.  A top-down 32-bit DIB section is the
// window's backing store; this file hands its raw pixels upwards as a Surface
// and lets render/Canvas decide what to draw with them.  The only copy is the
// DIB -> window BitBlt, restricted to the damaged rectangle.
//
// Note the include list: no <blend2d.h>, no Painter.  That is the fix for the
// platform -> render dependency recorded in docs/architecture.md section 2.
//
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <imm.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "geeyoou/platform/Platform.hpp"

namespace geeyoou {
namespace {

const wchar_t* kWindowClass = L"GeeyoouUIWindow";

std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring out(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
  return out;
}

std::string wideToUtf8(const wchar_t* s, int len) {
  if (!s || len <= 0) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
  std::string out(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), n, nullptr, nullptr);
  return out;
}

// Virtual-key -> platform-independent Key.  This table is the reason widgets
// never see a VK_* constant; an X11 backend supplies its own mapping and every
// control above keeps working unchanged.
Key mapVirtualKey(WPARAM vk) {
  switch (vk) {
    case VK_TAB:      return Key::Tab;
    case VK_RETURN:   return Key::Enter;
    case VK_ESCAPE:   return Key::Escape;
    case VK_SPACE:    return Key::Space;
    case VK_BACK:     return Key::Backspace;
    case VK_DELETE:   return Key::Delete;
    case VK_LEFT:     return Key::Left;
    case VK_RIGHT:    return Key::Right;
    case VK_UP:       return Key::Up;
    case VK_DOWN:     return Key::Down;
    case VK_HOME:     return Key::Home;
    case VK_END:      return Key::End;
    case VK_PRIOR:    return Key::PageUp;
    case VK_NEXT:     return Key::PageDown;
    case VK_OEM_MINUS: case VK_SUBTRACT: return Key::Minus;
    case VK_OEM_PERIOD: case VK_DECIMAL: return Key::Period;
    default: break;
  }
  if (vk >= '0' && vk <= '9') {
    return Key(std::uint16_t(Key::Digit0) + std::uint16_t(vk - '0'));
  }
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
    return Key(std::uint16_t(Key::Digit0) + std::uint16_t(vk - VK_NUMPAD0));
  }
  if (vk >= 'A' && vk <= 'Z') {
    return Key(std::uint16_t(Key::KeyA) + std::uint16_t(vk - 'A'));
  }
  return Key::Unknown;
}

// --- timers -----------------------------------------------------------------
//
// Two tables rather than one because the two directions are asked for by two
// different parties: the TIMERPROC only knows the id Win32 assigned, while
// stopTimer() only knows the id WE handed out.  Handing out the native id
// directly would be one table less and one real bug more -- Win32 recycles
// UINT_PTRs, so a stale handle could kill a timer somebody else had just
// started, which is exactly the class of mistake this whole interface exists
// to close.
//
// No locking: SetTimer(nullptr, ...) posts WM_TIMER to the calling thread's
// queue, so every one of these entries is created, fired and destroyed on the
// UI thread (docs/architecture.md section 3.11).
struct TimerSlot {
  TimerId id = 0;
  // Behind a shared_ptr so the callback survives being cancelled from inside
  // itself -- which is precisely what Window's animation tick does when a slot
  // it drives closes the window.
  std::shared_ptr<std::function<void()>> fn;
};

std::map<UINT_PTR, TimerSlot>& timersByNative() {
  static std::map<UINT_PTR, TimerSlot> t;
  return t;
}

std::map<TimerId, UINT_PTR>& nativeByTimerId() {
  static std::map<TimerId, UINT_PTR> t;
  return t;
}

void CALLBACK timerThunk(HWND, UINT, UINT_PTR native, DWORD) {
  auto& table = timersByNative();
  const auto it = table.find(native);
  if (it == table.end()) return;
  // Copied out before the call: stopping this timer from within its own
  // callback erases the map node the std::function lives in.
  const std::shared_ptr<std::function<void()>> fn = it->second.fn;
  if (fn && *fn) (*fn)();
}

// ---------------------------------------------------------------------------
class Win32Window final : public PlatformWindow {
 public:
  Win32Window(const std::string& title, int logicalWidth, int logicalHeight,
              const WindowOptions& opts)
      : opts_(opts) {
    registerClassOnce();

    // A frameless window keeps WS_OVERLAPPEDWINDOW rather than becoming a
    // WS_POPUP.  That is the whole trick: the window stays an ordinary managed
    // top-level window -- snap layouts, the minimise animation, the alt-tab
    // thumbnail and the taskbar preview all keep working -- and WM_NCCALCSIZE
    // below simply stops the frame from being DRAWN.  A WS_POPUP would have to
    // reimplement every one of those, badly.
    style_ = WS_OVERLAPPEDWINDOW;
    if (!opts_.resizable) style_ &= ~DWORD(WS_THICKFRAME | WS_MAXIMIZEBOX);

    // Create at the default DPI first, then correct the size once the window
    // exists and we can ask which monitor it landed on.
    hwnd_ = CreateWindowExW(0, kWindowClass, utf8ToWide(title).c_str(), style_,
                            CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr,
                            nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return;

    if (opts_.frameless) {
      // Extending the frame by a single pixel is what keeps the drop shadow:
      // DWM only draws one for windows that have SOME frame, and a window whose
      // non-client area we have entirely eaten has none.  The pixel is never
      // visible -- Blend2D writes alpha 255 over the whole client area.
      const MARGINS m{0, 0, 1, 0};
      DwmExtendFrameIntoClientArea(hwnd_, &m);
      // Force one frame recalculation now, so the first paint already has the
      // full-bleed client area rather than briefly showing the OS caption.
      SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOACTIVATE);
    }

    dpi_ = float(GetDpiForWindow(hwnd_));
    resizeToLogical(logicalWidth, logicalHeight);
  }

  ~Win32Window() override {
    releaseBackingStore();
    if (hwnd_) DestroyWindow(hwnd_);
  }

  void show() override {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
  }

  void close() override { PostMessageW(hwnd_, WM_CLOSE, 0, 0); }

  void setTitle(const std::string& utf8) override {
    // Still worth setting on a frameless window: it is what the taskbar button,
    // the alt-tab card and any screen reader announce.
    SetWindowTextW(hwnd_, utf8ToWide(utf8).c_str());
  }

  void minimize() override { ShowWindow(hwnd_, SW_MINIMIZE); }
  void maximize() override { ShowWindow(hwnd_, SW_MAXIMIZE); }
  void restore() override { ShowWindow(hwnd_, SW_RESTORE); }
  bool isMaximized() const override { return hwnd_ && IsZoomed(hwnd_) != 0; }
  bool isFrameless() const override { return opts_.frameless; }

  void setMinimumSize(Size logical) override {
    opts_.minSize = logical;
    // Nudge the window so Windows re-asks WM_GETMINMAXINFO; without this a new
    // minimum only takes effect the next time the user grabs a border.
    RECT rc{};
    if (GetWindowRect(hwnd_, &rc)) {
      SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }

  Size clientSize() const override {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float s = scaleFactor();
    return {float(rc.right - rc.left) / s, float(rc.bottom - rc.top) / s};
  }

  float scaleFactor() const override { return dpi_ / 96.0f; }

  void invalidate(const Rect& logicalRect) override {
    if (!hwnd_ || logicalRect.isEmpty()) return;
    const float s = scaleFactor();
    // Expand outwards by one physical pixel: logical->physical rounding plus
    // antialiasing can bleed a fraction of a pixel past the reported bounds,
    // and a repaint that is one pixel too small leaves visible crumbs.
    RECT rc;
    rc.left = LONG(std::floor(logicalRect.left() * s)) - 1;
    rc.top = LONG(std::floor(logicalRect.top() * s)) - 1;
    rc.right = LONG(std::ceil(logicalRect.right() * s)) + 1;
    rc.bottom = LONG(std::ceil(logicalRect.bottom() * s)) + 1;
    InvalidateRect(hwnd_, &rc, FALSE);
  }

  void* nativeHandle() override { return hwnd_; }

  void setImeCaret(const Rect& caret) override {
    if (!hwnd_) return;
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) return;
    const float s = scaleFactor();

    // CFS_EXCLUDE tells the IME "here is the caret AND here is the rectangle
    // you must not cover", so the candidate list flips above the field when
    // there is no room below it.  CFS_POINT alone lets it sit on top of the
    // text being edited.
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_EXCLUDE;
    cf.ptCurrentPos.x = LONG(caret.x() * s);
    cf.ptCurrentPos.y = LONG(caret.y() * s);
    cf.rcArea.left = LONG(caret.left() * s);
    cf.rcArea.top = LONG(caret.top() * s);
    cf.rcArea.right = LONG(caret.right() * s);
    cf.rcArea.bottom = LONG(caret.bottom() * s);
    ImmSetCompositionWindow(himc, &cf);
    ImmReleaseContext(hwnd_, himc);
  }

 private:
  static void registerClassOnce() {
    static bool done = false;
    if (done) return;
    done = true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Win32Window::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // we paint every pixel ourselves
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
  }

  void resizeToLogical(int logicalWidth, int logicalHeight) {
    const float s = scaleFactor();
    RECT rc{0, 0, LONG(logicalWidth * s), LONG(logicalHeight * s)};
    // On a frameless window client == window, so the usual frame adjustment
    // would hand back a window that is a title bar too tall.
    if (!opts_.frameless) {
      AdjustWindowRectExForDpi(&rc, style_, FALSE, 0, UINT(dpi_));
    }
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  // Thickness of the invisible grip strip along each edge of a frameless
  // window, in physical pixels.  The OS frame is only ~4px at 100%, which is a
  // genuinely hard target with a mouse, so this is deliberately wider.
  int resizeGripPx() const {
    return std::max(4, int(std::lround(7.0 * double(scaleFactor()))));
  }

  // Where a frameless window's edges sit once the OS has stopped drawing them.
  // Returns HTNOWHERE when the point is not on an edge.
  LRESULT frameHitTest(POINT clientPt) const {
    if (!opts_.resizable || IsZoomed(hwnd_)) return HTNOWHERE;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int g = resizeGripPx();
    const bool l = clientPt.x < g;
    const bool r = clientPt.x >= rc.right - g;
    const bool t = clientPt.y < g;
    const bool b = clientPt.y >= rc.bottom - g;
    if (t && l) return HTTOPLEFT;
    if (t && r) return HTTOPRIGHT;
    if (b && l) return HTBOTTOMLEFT;
    if (b && r) return HTBOTTOMRIGHT;
    if (t) return HTTOP;
    if (b) return HTBOTTOM;
    if (l) return HTLEFT;
    if (r) return HTRIGHT;
    return HTNOWHERE;
  }

  // WM_MOUSELEAVE is not delivered unless it is asked for, once, per excursion
  // into the client area.  Without it a button the cursor slid off -- straight
  // out of the window, or up into the caption zone -- stays lit forever.
  void trackMouseLeave() {
    if (trackingLeave_) return;
    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd_;
    if (TrackMouseEvent(&tme)) trackingLeave_ = true;
  }

  // --- backing store ------------------------------------------------------
  void ensureBackingStore(int pxWidth, int pxHeight) {
    if (pxWidth == bmpWidth_ && pxHeight == bmpHeight_ && dibBits_) return;
    releaseBackingStore();
    if (pxWidth <= 0 || pxHeight <= 0) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = pxWidth;
    bi.bmiHeader.biHeight = -pxHeight;  // negative = top-down, matching Blend2D
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dib_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    if (!dib_ || !dibBits_) {
      releaseBackingStore();
      return;
    }
    bmpWidth_ = pxWidth;
    bmpHeight_ = pxHeight;
  }

  void releaseBackingStore() {
    if (dib_) {
      DeleteObject(dib_);
      dib_ = nullptr;
    }
    dibBits_ = nullptr;
    bmpWidth_ = bmpHeight_ = 0;
  }

  void paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);
    ensureBackingStore(client.right, client.bottom);

    if (dibBits_ && onPaint) {
      Surface surface;
      surface.pixels = dibBits_;
      surface.width = bmpWidth_;
      surface.height = bmpHeight_;
      surface.stride = std::intptr_t(bmpWidth_) * 4;
      surface.dpr = scaleFactor();

      const Rect dirtyPhysical(float(ps.rcPaint.left), float(ps.rcPaint.top),
                               float(ps.rcPaint.right - ps.rcPaint.left),
                               float(ps.rcPaint.bottom - ps.rcPaint.top));
      onPaint(surface, dirtyPhysical);

      HDC mem = CreateCompatibleDC(hdc);
      HGDIOBJ prev = SelectObject(mem, dib_);
      BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
             ps.rcPaint.right - ps.rcPaint.left,
             ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left,
             ps.rcPaint.top, SRCCOPY);
      SelectObject(mem, prev);
      DeleteDC(mem);
    }

    EndPaint(hwnd_, &ps);
  }

  // --- input --------------------------------------------------------------
  void emitMouse(MouseAction action, MouseButton button, LPARAM lp,
                 float wheel = 0.0f) {
    if (!onMouse) return;
    const float s = scaleFactor();
    MouseEvent e;
    e.action = action;
    e.button = button;
    e.windowPos = {float(GET_X_LPARAM(lp)) / s, float(GET_Y_LPARAM(lp)) / s};
    e.pos = e.windowPos;
    e.wheelDelta = wheel;
    e.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    e.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    e.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    onMouse(e);
  }

  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case WM_ERASEBKGND:
        return 1;  // suppress GDI's background erase; we own every pixel

      case WM_PAINT:
        paint();
        return 0;

      // --- frameless chrome -------------------------------------------------
      //
      // Returning 0 without letting DefWindowProc touch the rectangle means the
      // client area IS the window rectangle: no caption, no border, nothing
      // drawn by the OS.  Everything else about the window is unchanged.
      case WM_NCCALCSIZE: {
        if (!opts_.frameless || wp == FALSE) break;
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        RECT& rc = params->rgrc[0];
        if (IsZoomed(hwnd_)) {
          // A maximised window is sized to the work area PLUS the frame, on the
          // assumption that the frame will be drawn outside the visible area.
          // With the frame eaten, that surplus becomes real pixels hanging off
          // every edge of the monitor -- so take it back by hand.
          const UINT d = UINT(dpi_);
          const int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, d);
          const int bx = GetSystemMetricsForDpi(SM_CXFRAME, d) + pad;
          const int by = GetSystemMetricsForDpi(SM_CYFRAME, d) + pad;
          rc.left += bx;
          rc.right -= bx;
          rc.top += by;
          rc.bottom -= by;
        }
        return 0;
      }

      case WM_NCHITTEST: {
        if (!opts_.frameless) break;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        // Edges win over content: a drop-down that happens to reach the window
        // border must not steal the resize grip.
        const LRESULT edge = frameHitTest(pt);
        if (edge != HTNOWHERE) return edge;
        if (onHitTest) {
          const float s = scaleFactor();
          if (onHitTest({float(pt.x) / s, float(pt.y) / s}) == HitZone::Caption) {
            return HTCAPTION;
          }
        }
        return HTCLIENT;
      }

      case WM_NCACTIVATE:
        // lParam -1 tells DefWindowProc "do not repaint the non-client area".
        // Without it, activating or deactivating the window paints a ghost of
        // the caption we just removed across the top of our own header.
        if (opts_.frameless) return DefWindowProcW(hwnd_, msg, wp, -1);
        break;

      case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        const float s = scaleFactor();
        RECT rc{0, 0, LONG(opts_.minSize.width * s),
                LONG(opts_.minSize.height * s)};
        if (!opts_.frameless) {
          AdjustWindowRectExForDpi(&rc, style_, FALSE, 0, UINT(dpi_));
        }
        mmi->ptMinTrackSize.x = rc.right - rc.left;
        mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        return 0;
      }

      case WM_SIZE: {
        const bool zoomed = IsZoomed(hwnd_) != 0;
        if (zoomed != wasZoomed_) {
          wasZoomed_ = zoomed;
          // Fires for the snap gestures and the double-click on the caption
          // too, neither of which goes through maximize().
          if (onWindowStateChanged) onWindowStateChanged();
        }
        if (onResize) {
          ResizeEvent e;
          e.size = clientSize();
          e.scale = scaleFactor();
          onResize(e);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }

      case WM_DPICHANGED: {
        dpi_ = float(HIWORD(wp));
        const RECT* sug = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, sug->left, sug->top, sug->right - sug->left,
                     sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
        releaseBackingStore();  // physical size changed; rebuild on next paint
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }

      case WM_MOUSEMOVE:
        trackMouseLeave();
        emitMouse(MouseAction::Move, MouseButton::None, lp);
        return 0;

      case WM_MOUSELEAVE: {
        trackingLeave_ = false;
        // Also fires when the pointer crosses into a region we hit-tested as
        // HTCAPTION, which is exactly the case that used to leave the close
        // button glowing after the operator dragged the window away.
        if (onMouse) {
          MouseEvent e;
          e.action = MouseAction::Leave;
          // Far outside the client area: no widget can claim to contain it.
          e.windowPos = {-1.0f, -1.0f};
          e.pos = e.windowPos;
          onMouse(e);
        }
        return 0;
      }

      case WM_LBUTTONDOWN:
        SetCapture(hwnd_);
        emitMouse(MouseAction::Press, MouseButton::Left, lp);
        return 0;
      case WM_LBUTTONUP:
        ReleaseCapture();
        emitMouse(MouseAction::Release, MouseButton::Left, lp);
        return 0;
      case WM_RBUTTONDOWN:
        emitMouse(MouseAction::Press, MouseButton::Right, lp);
        return 0;
      case WM_RBUTTONUP:
        emitMouse(MouseAction::Release, MouseButton::Right, lp);
        return 0;

      case WM_MOUSEWHEEL: {
        // Wheel coordinates arrive in SCREEN space, unlike every other mouse
        // message.  Forgetting this is a classic Win32 bug.
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        emitMouse(MouseAction::Wheel, MouseButton::None,
                  MAKELPARAM(pt.x, pt.y),
                  float(GET_WHEEL_DELTA_WPARAM(wp)) / float(WHEEL_DELTA));
        return 0;
      }

      // WM_SYSKEY* carries any chord that includes Alt.  Without these cases
      // Alt+1..9 never reaches the app AND DefWindowProc opens the window menu,
      // so the shortcut looks broken twice over.
      case WM_SYSKEYDOWN:
      case WM_SYSKEYUP:
      case WM_KEYDOWN:
      case WM_KEYUP: {
        const bool isDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        if (onKey) {
          KeyEvent e;
          e.key = mapVirtualKey(wp);
          e.nativeCode = std::uint32_t(wp);
          e.pressed = isDown;
          e.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
          e.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
          // Bit 29 of lParam is set for WM_SYSKEY*; trust it over GetKeyState,
          // which can miss the transition when messages are posted.
          e.alt = ((lp & (1 << 29)) != 0) || (GetKeyState(VK_MENU) & 0x8000) != 0;
          onKey(e);
        }
        // F10 and lone Alt still belong to the system menu; everything else is
        // ours, and letting DefWindowProc see it would beep or open the menu.
        if (wp == VK_MENU || wp == VK_F10) break;
        return 0;
      }

      case WM_CHAR: {
        // Delivered by TranslateMessage after WM_KEYDOWN (and by the IME on
        // commit), carrying the character the layout actually produced.
        // Emitted as a separate event with key == Unknown: controls that only
        // care about key identity ignore it, text controls read `character`.
        //
        // WM_CHAR is UTF-16, so a codepoint above the BMP arrives as two
        // messages holding a surrogate pair.  We buffer the high half and emit
        // once the low half lands -- dropping them would silently mangle emoji
        // and rare CJK extension characters that do appear in recipe names.
        const auto unit = std::uint32_t(wp);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
          highSurrogate_ = unit;
          return 0;
        }
        std::uint32_t cp = unit;
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
          if (highSurrogate_ == 0) return 0;  // orphaned low half
          cp = 0x10000u + ((highSurrogate_ - 0xD800u) << 10) + (unit - 0xDC00u);
          highSurrogate_ = 0;
        } else {
          highSurrogate_ = 0;
        }

        // Filter control characters; Enter/Tab/Backspace are handled as keys.
        if (onKey && cp >= 0x20 && cp != 0x7F) {
          KeyEvent e;
          e.character = cp;
          e.nativeCode = cp;
          e.pressed = true;
          onKey(e);
        }
        return 0;
      }

      case WM_CLOSE:
        if (onClose) onClose();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
  }

  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      auto* self = static_cast<Win32Window*>(cs->lpCreateParams);
      self->hwnd_ = hwnd;
      return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = reinterpret_cast<Win32Window*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->handle(msg, wp, lp);
  }

  HWND hwnd_ = nullptr;
  float dpi_ = 96.0f;
  WindowOptions opts_;
  DWORD style_ = WS_OVERLAPPEDWINDOW;
  bool wasZoomed_ = false;
  bool trackingLeave_ = false;

  HBITMAP dib_ = nullptr;
  void* dibBits_ = nullptr;
  int bmpWidth_ = 0;
  int bmpHeight_ = 0;
  std::uint32_t highSurrogate_ = 0;  // pending high half of a UTF-16 pair
};

// ---------------------------------------------------------------------------
class Win32Platform final : public Platform {
 public:
  Win32Platform() {
    // Per-Monitor v2 must be set before the first window is created.  Doing it
    // here (in the singleton's constructor) guarantees that ordering.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  }

  std::unique_ptr<PlatformWindow> createWindow(
      const std::string& title, int w, int h,
      const WindowOptions& options) override {
    return std::make_unique<Win32Window>(title, w, h, options);
  }

  int runEventLoop() override {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    return int(msg.wParam);
  }

  void quit(int exitCode) override { PostQuitMessage(exitCode); }

  TimerId startTimer(int intervalMs, std::function<void()> fn) override {
    // A thread timer (hwnd == nullptr) rather than a window timer: it does not
    // tie the tick to any particular window's lifetime.
    const UINT_PTR native = SetTimer(nullptr, 0, UINT(intervalMs), &timerThunk);
    if (!native) return 0;

    const TimerId id = ++lastTimerId_;
    timersByNative()[native] =
        TimerSlot{id, std::make_shared<std::function<void()>>(std::move(fn))};
    nativeByTimerId()[id] = native;
    return id;
  }

  void stopTimer(TimerId id) override {
    if (id == 0) return;
    auto& byId = nativeByTimerId();
    const auto it = byId.find(id);
    if (it == byId.end()) return;  // already stopped, or never ours

    const UINT_PTR native = it->second;
    // KillTimer's first argument must match SetTimer's, and ours was nullptr.
    KillTimer(nullptr, native);
    byId.erase(it);
    timersByNative().erase(native);
  }

  std::string clipboardText() override {
    if (!OpenClipboard(nullptr)) return {};
    std::string out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
      if (auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
        out = wideToUtf8(w, int(std::wcslen(w)));
        GlobalUnlock(h);
      }
    }
    CloseClipboard();
    return out;
  }

  void setClipboardText(const std::string& utf8) override {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const std::wstring w = utf8ToWide(utf8);
    const SIZE_T bytes = (w.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
      if (void* dst = GlobalLock(h)) {
        std::memcpy(dst, w.c_str(), bytes);
        GlobalUnlock(h);
        // On success the clipboard OWNS the handle -- freeing it here would be
        // a double free the moment anything pastes.
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
      } else {
        GlobalFree(h);
      }
    }
    CloseClipboard();
  }

 private:
  // Monotonic and never reused, so a handle kept past its timer's death names
  // nothing rather than naming a stranger.
  TimerId lastTimerId_ = 0;
};

}  // namespace

Platform& platform() {
  static Win32Platform p;
  return p;
}

}  // namespace geeyoou
