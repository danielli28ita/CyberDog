// core/i18n.h — 界面文案的三语表（中文、英语、意大利语）。1.7，作者要求。
//
// 约束：本文件及整个 core 层不得包含 <windows.h>、D3D、UE 的任何头文件。
//
// 用法：tr(Str::X) 返回当前语言的 UTF-8 串（静态存储，随便存指针）。带 % 的是 printf 模板。
// 加一句话：Str 末尾加一个名字，kTable 里加一行三种语言。顺序必须一致，有 static_assert 兜着。
// 语言由宿主设（存档 lang=zh|en|it，第一次按系统界面语言猜）。

#pragma once

#include <cstdint>

#include "core/action.h"

namespace pet {

enum class Lang : std::uint8_t { Zh, En, It, Count };

const char* lang_code(Lang l);            // "zh" / "en" / "it"
Lang        lang_from_code(const char* c); // 认不出返回 Zh
const char* lang_native_name(Lang l);     // 菜单里显示：中文 / English / Italiano

void set_language(Lang l);
Lang language();

enum class Str : std::uint16_t {
    // 宿主：气泡与提示
    Colon,               // "："
    IntroFmt,            // "我是%s"
    RenamedFmt,          // "我叫%s了"
    NameReminder,        // 命名提醒那句
    AffinityUp,          // "好感度 +1"
    SeenIt,              // "好，你看到了"
    AutostartOn, AutostartOff, AutostartFail,
    MemoBadTime, MemoEmpty,
    WeatherHint,
    // 托盘菜单
    MenuShow, MenuHide, MenuStats, MenuMemo, MenuRename, MenuSound, MenuNameReminder,
    MenuWeatherUnset, MenuWeatherFmt, MenuAutostart, MenuLanguage, MenuExit,
    MenuPersonality,
    // 对话框
    Ok, Cancel, RenameTitle, RenameHint, WeatherTitle,
    MemoTitle, MemoAdd, MemoRemove, MemoClose, MemoContent, MemoTime, MemoHintDefault, MemoHintAdded,
    // 属性面板
    StatsWindow, StatsTitleFmt, StatsCloseHint,
    HeadBasic, HeadAffinity, HeadPersonality, HeadGrowth,
    RowName, RowBreed, RowToday, RowAdopted, AdoptedDaysFmt, RowStatus, RowAffinity, RowStage,
    RowLaunch, RowPetsHits, RowBalls, RowSave, TimesFmt, PetsHitsFmt,
    StatusSleep, StatusIdle, StatusObedientFmt,
    Tier0, Tier1, Tier2, Tier3, Tier4, Tier5, Tier6, Tier7, Tier8, Tier9,
    TraitMischief, TraitCuriosity, TraitCharm, TraitExtroversion, TraitLiveliness, TraitClinginess, TraitLaziness, TraitTimidity,
    Breed,
    AffinityLevelFmt,   // "Lv.%d · %s"
    TraitJoinFmt,       // "%s · %s" 性格摘要
    RowSummary,         // "性格摘要"
    PersonalityTitle, PersonalityHint, PersonalityReroll,
    // 插件
    DrinkWater, StandUp, MemoPrefix,
    WeatherLineFmt, WeatherHiLoFmt, WeatherNotFoundFmt, WeatherNetFail, WeatherDataBad,
    Wx0, Wx1, Wx2, Wx3, WxFog, WxDrizzle, WxRain, WxSnow, WxShowers, WxSnowShowers, WxThunder, WxUnknown,
    Tomorrow, MemoSeen, MenuOpenData,
    // 操作提示（plugins/tips）
    TipPet, TipStats, TipDrag, TipWeather, TipMemo, TipLanguage, TipHit,
    Count
};

const char* tr(Str s);
// 动作名（属性面板「状态」行）。
const char* action_name_tr(ActionKind k);

}  // namespace pet
