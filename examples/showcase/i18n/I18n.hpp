#pragma once
//
// 简体中文 / English 运行时切换。
//
// 只有一条规则：**翻译表的 key 就是中文原文**。
//
//     lbl->setText(tr("进料流量"));
//
// 不发明 kFeedFlowLabel 这类符号名，是因为六百个符号名就是六百次命名决策和六百
// 次拼写风险，换来的「key 与文案解耦」在一个演示程序里根本用不上。用原文当 key
// 的代价是改中文文案要同步改语言包，收益是漏翻只会退化成显示中文，永远不会显示
// 成 MISSING_KEY_47，更不会崩。
//
// 生效方式：setLang() 换掉当前语言并发出 langChanged()。**已经建好的控件不会自
// 己变** —— 文案在 buildXxxPage() 里就已经烧进 Label 了。订阅方的做法是把整个页
// 面树销毁重建，见 Shell::rebuildPages()。
//
#include <cstddef>
#include <string>
#include <string_view>

#include "geeyoou/core/Signal.hpp"

namespace showcase {

// 当前语言的索引，对应 langCount() / langId() 那组访问器。
// 进程启动时是 0（简体中文）。
int lang();
void setLang(int index);

// 按 id 切换，找不到就什么都不做。给「记住上次选择」这类持久化用。
bool setLangById(std::string_view id);

// 已装载的语言。顺序即语言菜单的显示顺序，由 I18n.cpp 的 builtinPacks() 决定。
int langCount();
const char* langId(int index);          // "zh-CN"
const char* langNativeName(int index);  // "简体中文"

// 语言变更信号。和库里其它信号一样，UI 线程专用。
geeyoou::Signal<>& langChanged();

// 中文原文 -> 当前语言。当前语言是中文时原样返回；否则查表，查不到返回原文。
//
// 返回 std::string 而不是 const&：调用点大量是 tr("已选 ") + n + tr(" 行") 这类
// 拼接，按值返回省掉到处写 std::string(...) 的噪音。所有调用都发生在建页面的时
// 候，不在绘制热路径上，这点拷贝无关紧要。
std::string tr(std::string_view zh);

// 漏翻自查：运行期真正 tr() 过、却没在当前语言包里命中的 key 数量。
// 中文（key 即译文）永远是 0。
std::size_t missingTranslationCount();

}  // namespace showcase
