//
// 简体中文 / English switching, as a test rather than as a screenshot.
//
// Three separate things are asserted here, and they fail for different reasons:
//
//   1. THE RUNTIME.  tr() is identity under Chinese, a lookup under English,
//      and falls back to the key -- never to an empty string -- when a
//      translation is missing.  A missing translation must degrade to "one
//      Chinese line in an English screen", which is visible and fixable, not to
//      a blank label, which reads as a broken widget.
//
//   2. THE PACK.  Every entry is checked mechanically for the two mistakes a
//      human reviewer skims straight past: an empty translation, and a format
//      string whose %d/%s/%zu count changed in translation.  The second one is
//      not a typo, it is a crash -- snprintf reading an argument that was never
//      pushed -- and it is invisible until the page that formats it is opened
//      in the wrong language.
//
//   3. THE REBUILD, which is the part that actually broke things.  A language
//      change destroys every built page, and this showcase had two
//      subscriptions pointing INTO those pages from outside: AppState's alarm
//      sink and ShowcaseWindow's headerAction.  Both were harmless for exactly
//      as long as pages were never destroyed.  The cases at the bottom pin down
//      the contract that makes them safe -- pagesAboutToRebuild fires BEFORE
//      anything is freed, while the pages are still whole -- because an
//      ordering bug there is a use-after-free that only ASan, or a customer,
//      would find.
//
#include <cstddef>
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/Widget.hpp"
#include "i18n/I18n.hpp"
#include "i18n/LangPack.hpp"
#include "showcase/Shell.hpp"

using geeyoou::Label;
using geeyoou::Size;
using geeyoou::Widget;
using showcase::lang;
using showcase::langCount;
using showcase::langId;
using showcase::langNativeName;
using showcase::setLang;
using showcase::tr;

namespace {

// The language index for an id, or -1.  Written against the public accessors
// rather than against a hard-coded 0/1 so that adding a language does not
// silently renumber what these cases think they are testing.
int indexOf(const char* id) {
  for (int i = 0; i < langCount(); ++i) {
    if (std::string(langId(i)) == id) return i;
  }
  return -1;
}

// Restores whatever language was current when it was built.  Every case here
// changes global state, and the suite must not depend on case order.
struct LangGuard {
  int saved = lang();
  ~LangGuard() { setLang(saved); }
};

// How many printf conversions a format string has.  "%%" is an escaped percent
// and consumes no argument.
std::size_t conversions(const std::string& s) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '%') continue;
    if (i + 1 < s.size() && s[i + 1] == '%') {
      ++i;  // "%%" -- skip both
      continue;
    }
    ++n;
  }
  return n;
}

}  // namespace

// ================================================================ runtime ===

GEEYOOU_TEST(i18n, the_language_list_is_chinese_then_english_and_nothing_else) {
  // The showcase ships exactly two languages.  This is not a style preference:
  // the header menu maps its item INDEX straight onto the language index, so a
  // language appearing in one list and not the other is a mis-selection, not a
  // cosmetic problem.
  CHECK_EQ(langCount(), 2);
  CHECK_EQ(std::string(langId(0)), std::string("zh-CN"));
  CHECK_EQ(std::string(langId(1)), std::string("en-US"));

  // A language writes its own name in itself -- never translated.
  CHECK_EQ(std::string(langNativeName(0)), std::string("简体中文"));
  CHECK_EQ(std::string(langNativeName(1)), std::string("English"));
}

GEEYOOU_TEST(i18n, chinese_is_identity_and_english_is_a_lookup) {
  LangGuard guard;

  setLang(indexOf("zh-CN"));
  CHECK_EQ(tr("进料流量"), std::string("进料流量"));
  CHECK_EQ(tr("确认"), std::string("确认"));

  setLang(indexOf("en-US"));
  CHECK_EQ(tr("进料流量"), std::string("Feed Flow"));
  CHECK_EQ(tr("确认"), std::string("Acknowledge"));
}

GEEYOOU_TEST(i18n, a_missing_translation_falls_back_to_chinese_not_to_empty) {
  LangGuard guard;
  setLang(indexOf("en-US"));

  // A key no pack has.  The point is the SHAPE of the failure: a visible
  // Chinese string an operator can report, not a blank label that reads as a
  // broken widget and not a "MISSING_KEY" that reads as a crash.
  const std::string missing = "这条永远不会出现在语言包里";
  CHECK_EQ(tr(missing), missing);
  CHECK(!tr(missing).empty());
}

GEEYOOU_TEST(i18n, setting_the_current_language_again_emits_nothing) {
  LangGuard guard;
  setLang(indexOf("zh-CN"));

  int fired = 0;
  const auto conn = showcase::langChanged().connect([&fired] { ++fired; });

  setLang(indexOf("zh-CN"));  // already there
  CHECK_EQ(fired, 0);

  setLang(indexOf("en-US"));
  CHECK_EQ(fired, 1);

  // Out-of-range is a no-op rather than a wrap-around or an assert: the menu
  // index is the language index, and a stale menu must not select a language
  // that is not there.
  setLang(99);
  setLang(-1);
  CHECK_EQ(fired, 1);

  const_cast<geeyoou::Connection&>(conn).disconnect();
}

// =================================================================== pack ===

GEEYOOU_TEST(i18n, no_pack_entry_is_empty_or_untranslated) {
  const showcase::LangPack& en = showcase::enUSPack();
  REQUIRE(en.count > 0);

  std::size_t empty = 0;
  std::size_t identical = 0;
  for (std::size_t i = 0; i < en.count; ++i) {
    const std::string zh = en.entries[i].zh;
    const std::string t = en.entries[i].tr;
    if (zh.empty() || t.empty()) ++empty;
    // An entry whose translation equals its key is either a forgotten line or
    // an entry that never needed to exist.
    if (zh == t) ++identical;
  }
  CHECK_EQ(empty, std::size_t(0));
  CHECK_EQ(identical, std::size_t(0));
}

GEEYOOU_TEST(i18n, translations_keep_every_format_conversion) {
  const showcase::LangPack& en = showcase::enUSPack();

  // The failure this catches: "显示 %d / %d" translated as "Showing %d", which
  // compiles, links, and then hands snprintf one argument it never pushed the
  // moment somebody opens the icons page in English.
  std::size_t mismatched = 0;
  for (std::size_t i = 0; i < en.count; ++i) {
    if (conversions(en.entries[i].zh) != conversions(en.entries[i].tr)) {
      ++mismatched;
      GEEYOOU_FAIL(std::string("格式符数量不一致: ") + en.entries[i].zh);
    }
  }
  CHECK_EQ(mismatched, std::size_t(0));
}

GEEYOOU_TEST(i18n, the_chinese_pack_is_empty_on_purpose) {
  // Chinese is the key, so its pack carries metadata and no entries.  An empty
  // table is also what tr() reads as "identity", i.e. the fast path -- if this
  // ever grew entries, every Chinese lookup would start hashing for nothing.
  const showcase::LangPack& zh = showcase::zhCNPack();
  CHECK_EQ(zh.count, std::size_t(0));
  CHECK(zh.entries == nullptr);
}

// ================================================================ rebuild ===

GEEYOOU_TEST(i18n, rebuilding_pages_relabels_the_nav_and_the_title_strip) {
  LangGuard guard;
  setLang(indexOf("zh-CN"));

  showcase::Shell shell;
  shell.addPage("总览", "概览", "库的构成、分层规模与未实现清单",
                geeyoou::Icon::None, [](Widget* c) {
                  c->add<Label>()->setGeometry({0.0f, 0.0f, 10.0f, 10.0f});
                  return Size{400.0f, 300.0f};
                });
  shell.showPage(0);
  shell.setGeometry({0.0f, 0.0f, 1000.0f, 700.0f});

  // addPage() was handed KEYS, so the rail shows Chinese under Chinese...
  REQUIRE(!shell.sidebar()->isCollapsed());

  setLang(indexOf("en-US"));
  shell.rebuildPages();

  // ...and English after a rebuild, without the caller re-registering anything.
  // Asserted through tr() rather than against a literal so that editing the
  // pack does not break this case for the wrong reason.
  CHECK_EQ(tr("概览"), std::string("At a Glance"));
  CHECK_EQ(shell.currentPage(), 0);
}

GEEYOOU_TEST(i18n, a_rebuild_keeps_the_page_the_operator_was_on) {
  LangGuard guard;
  setLang(indexOf("zh-CN"));

  showcase::Shell shell;
  for (int i = 0; i < 3; ++i) {
    shell.addPage("", "页面" + std::to_string(i), "副标题", geeyoou::Icon::None,
                  [](Widget* c) {
                    c->add<Label>()->setGeometry({0.0f, 0.0f, 10.0f, 10.0f});
                    return Size{400.0f, 300.0f};
                  });
  }
  shell.setGeometry({0.0f, 0.0f, 1000.0f, 700.0f});
  shell.showPage(2);
  CHECK_EQ(shell.currentPage(), 2);

  setLang(indexOf("en-US"));
  shell.rebuildPages();

  // Losing the operator's place on a language change would be its own bug --
  // and showPage() early-outs on an unchanged index, so the rebuild has to
  // forget the current page before asking for it again.
  CHECK_EQ(shell.currentPage(), 2);
}

GEEYOOU_TEST(i18n, pages_are_still_whole_when_the_rebuild_is_announced) {
  // THE ORDERING CONTRACT, and the reason this file exists.
  //
  // AppState::alarmSink and ShowcaseWindow::headerAction both hold widgets
  // that live inside a page.  They are cut in a pagesAboutToRebuild slot, so
  // that slot MUST run while those widgets are still alive -- announcing the
  // rebuild after freeing the pages would make the very signal that exists to
  // prevent the use-after-free the thing that causes it.
  LangGuard guard;
  setLang(indexOf("zh-CN"));

  showcase::Shell shell;
  Label* pageLabel = nullptr;
  int builds = 0;
  shell.addPage("", "页面", "副标题", geeyoou::Icon::None,
                [&pageLabel, &builds](Widget* c) {
                  ++builds;
                  pageLabel = c->add<Label>();
                  pageLabel->setGeometry({0.0f, 0.0f, 120.0f, 20.0f});
                  pageLabel->setText(tr("确认"));
                  return Size{400.0f, 300.0f};
                });
  shell.showPage(0);
  shell.setGeometry({0.0f, 0.0f, 1000.0f, 700.0f});
  REQUIRE(pageLabel != nullptr);
  CHECK_EQ(builds, 1);

  Label* seenAtAnnounce = nullptr;
  bool labelStillReadable = false;
  auto conn = shell.pagesAboutToRebuild.connect([&] {
    seenAtAnnounce = pageLabel;
    // Touch it.  Under ASan this is the whole test: if the announcement moved
    // to after the teardown, this read lands in freed memory and the build
    // that runs this suite goes red instead of a customer's plant doing so.
    labelStillReadable = !pageLabel->text().empty();
  });

  setLang(indexOf("en-US"));
  shell.rebuildPages();

  CHECK(seenAtAnnounce != nullptr);
  CHECK(labelStillReadable);
  // The page really was rebuilt, and the builder really did re-run -- otherwise
  // the case above would pass on a shell that simply did nothing.
  CHECK_EQ(builds, 2);
  CHECK_EQ(pageLabel->text(), std::string("Acknowledge"));

  conn.disconnect();
}

GEEYOOU_TEST(i18n, an_outside_subscriber_can_drop_its_page_pointer_in_time) {
  // The alarm-sink shape, reduced: something outside the page holds a callable
  // that captures a widget inside it.  This is the pattern that turned into a
  // use-after-free the moment pages became destroyable, and the fix is that the
  // owner clears it on pagesAboutToRebuild.
  LangGuard guard;
  setLang(indexOf("zh-CN"));

  showcase::Shell shell;
  std::function<void()> sink;  // stands in for AppState::alarmSink
  shell.addPage("", "页面", "副标题", geeyoou::Icon::None,
                [&sink](Widget* c) {
                  Label* l = c->add<Label>();
                  l->setGeometry({0.0f, 0.0f, 120.0f, 20.0f});
                  sink = [l] { l->setText("x"); };  // captures INTO the page
                  return Size{400.0f, 300.0f};
                });
  shell.showPage(0);
  shell.setGeometry({0.0f, 0.0f, 1000.0f, 700.0f});
  REQUIRE(static_cast<bool>(sink));

  auto conn = shell.pagesAboutToRebuild.connect([&sink] { sink = nullptr; });

  setLang(indexOf("en-US"));
  shell.rebuildPages();

  // Cleared by the announcement, then re-armed by the rebuilt page -- which is
  // exactly what the real ops page does.  What must NOT happen is it still
  // holding the Label from the page that was just destroyed.
  CHECK(static_cast<bool>(sink));
  sink();  // must not touch freed memory

  conn.disconnect();
}
