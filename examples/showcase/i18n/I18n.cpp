#include "I18n.hpp"

#include <set>
#include <unordered_map>

#include "LangPack.hpp"

namespace showcase {
namespace {

// 装载哪些语言，以及语言菜单里的先后顺序。
//
// 加一种语言就在这里加一项 —— 这是整个 i18n 里唯一一处「知道所有语言」的地方，
// 也是故意的：语言列表要能被人一眼读完，而不是散落在各个包的自注册代码里，靠链
// 接顺序决定谁先谁后。
const LangPack* const* packs(std::size_t& count) {
  // 函数内静态：首次调用时才初始化，绕开跨翻译单元的静态初始化顺序问题。
  static const LangPack* const kPacks[] = {
      &zhCNPack(),
      &enUSPack(),
  };
  count = sizeof(kPacks) / sizeof(kPacks[0]);
  return kPacks;
}

std::size_t packCount() {
  std::size_t n = 0;
  packs(n);
  return n;
}

const LangPack& packAt(int i) {
  std::size_t n = 0;
  const LangPack* const* p = packs(n);
  if (i < 0 || std::size_t(i) >= n) i = 0;
  return *p[std::size_t(i)];
}

struct State {
  int current = 0;

  // 当前语言的查找表。key 是 string_view，指向语言包里的字符串字面量 —— 那些是
  // .rdata 里的常量，活得比进程还久，所以这里不持有任何字符串的所有权。
  std::unordered_map<std::string_view, const char*> table;

  // 当前语言下 tr() 过但没命中的 key。用 set 去重，报的是「有几条没翻」而不是
  // 「没命中发生了几次」—— 后者只反映哪个页面被打开得勤。
  //
  // 这里存 std::string 而不是 string_view，虽然实际调用点全是 tr("字面量")。
  // 因为 tr() 的签名收的是 string_view，调用方完全可以传一个临时 std::string 的
  // view；存下那个 view 就是把悬垂指针塞进一张活到进程结束的表里。漏翻是罕见路
  // 径，一次拷贝换掉一整类只在特定调用点才现形的 UAF，很划算。
  std::set<std::string> missing;

  geeyoou::Signal<> changed;
};

void rebuild(State& s) {
  s.table.clear();
  s.missing.clear();
  const LangPack& p = packAt(s.current);
  s.table.reserve(p.count * 2);
  for (std::size_t i = 0; i < p.count; ++i) {
    s.table.emplace(p.entries[i].zh, p.entries[i].tr);
  }
}

State& st() {
  static State s = [] {
    State init;
    rebuild(init);
    return init;
  }();
  return s;
}

}  // namespace

int lang() { return st().current; }

int langCount() { return int(packCount()); }

const char* langId(int index) { return packAt(index).id; }

const char* langNativeName(int index) { return packAt(index).nativeName; }

void setLang(int index) {
  State& s = st();
  if (index < 0 || std::size_t(index) >= packCount()) return;
  if (index == s.current) return;  // 不白重建一次界面
  s.current = index;
  rebuild(s);
  s.changed.emit();
}

bool setLangById(std::string_view id) {
  const int n = langCount();
  for (int i = 0; i < n; ++i) {
    if (id == langId(i)) {
      setLang(i);
      return true;
    }
  }
  return false;
}

geeyoou::Signal<>& langChanged() { return st().changed; }

std::string tr(std::string_view zh) {
  State& s = st();
  // 中文包是恒等映射，压根没有条目 —— 中文下 tr() 就是一次拷贝，没有查表。
  if (s.table.empty()) return std::string(zh);

  const auto it = s.table.find(zh);
  if (it != s.table.end()) return std::string(it->second);

  // 漏翻：退化成显示中文，并记一笔。这是刻意的 —— 英文界面里混一句中文是能看见
  // 也能改的瑕疵，而显示空字符串或 key 名会让人以为控件坏了。
  s.missing.emplace(zh);
  return std::string(zh);
}

std::size_t missingTranslationCount() { return st().missing.size(); }

}  // namespace showcase
