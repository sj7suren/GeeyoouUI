#pragma once
//
// 一个语言包 = 一个翻译单元。
//
// 每种语言住在自己的 lang_<id>.cpp 里，互不相识，也不认识 I18n.cpp。加一种语言
// 是**新增一个文件**，而不是把一张越来越长的表塞进公共文件里 —— 后者会让两个人
// 同时加两种语言必然冲突，也会让「这条译文属于哪种语言」只能靠缩进来判断。
//
// 装载方式是**显式**的：每个包导出一个 xxxPack() 函数，I18n.cpp 在初始化时按名
// 字取用。看起来不如「静态对象自注册」优雅，但自注册依赖静态初始化在链接单元里
// 真的被保留 —— 一旦这些 .cpp 哪天被打包进静态库，链接器会把「没人引用」的目标
// 文件整个丢掉，语言就悄无声息地消失了。显式取用换来的是：少一种只在 Release
// 打包后才出现的故障。
//
#include <cstddef>

namespace showcase {

// 一条翻译。zh 是 key（中文原文），tr 是该语言的译文。
//
// 用裸 const char* 而不是 std::string：整张表是编译期常量，进不了堆，也不参与
// 静态初始化顺序 —— 一个语言包的代价就是它在 .rdata 里占的那几 KB。
struct LangEntry {
  const char* zh;
  const char* tr;
};

struct LangPack {
  const char* id;          // "zh-CN" / "en-US"，稳定标识
  const char* nativeName;  // 该语言写自己的名字："简体中文" / "English"
  const LangEntry* entries;
  std::size_t count;
};

// 内置语言包。新增语言：加一个 lang_xx_YY.cpp，在这里加一行声明，并在
// I18n.cpp 的 builtinPacks() 里加进数组 —— 三处，全部是编译期可查的。
const LangPack& zhCNPack();
const LangPack& enUSPack();

}  // namespace showcase
