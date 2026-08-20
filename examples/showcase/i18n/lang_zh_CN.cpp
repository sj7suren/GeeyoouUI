//
// 简体中文语言包。
//
// 这个包**没有一条translation entry**，而且这不是"还没写"，是设计的结果：翻译表
// 的 key 就是中文原文，所以中文的译文永远等于 key 本身。给它列 603 条 A->A 的映
// 射，只会得到一张必须和其它语言包同步维护、却不产生任何效果的表。
//
// 它仍然是一个正经的语言包，而不是 I18n.cpp 里的一个 if：语言列表、菜单显示名、
// 「当前是哪种语言」全都统一从 LangPack 来。空表在 tr() 里被识别为恒等映射，那
// 条分支同时也是中文下的快路径 —— 中文根本不查表。
//
#include "LangPack.hpp"

namespace showcase {

const LangPack& zhCNPack() {
  static const LangPack kPack{
      "zh-CN",
      "简体中文",
      nullptr,
      0,
  };
  return kPack;
}

}  // namespace showcase
