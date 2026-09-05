#include "core/i18n.h"

#include <cstring>

namespace pet {
namespace {

Lang g_lang = Lang::Zh;

struct Entry { const char* zh; const char* en; const char* it; };

// 顺序 = Str 的顺序。
constexpr Entry kTable[] = {
    {"：", ": ", ": "},
    {"我是%s", "I'm %s", "Sono %s"},
    {"我叫%s了", "Call me %s from now on", "Da ora mi chiamo %s"},
    {"右键托盘图标可以给我改名。托盘菜单里的「命名提醒」可以关掉这句话",
     "Right-click the tray icon to rename me. \"Name reminder\" in the tray menu turns this off",
     "Fai clic destro sull'icona nella barra per rinominarmi. \"Promemoria nome\" nel menu disattiva questo avviso"},
    {"好感度 +1", "Affinity +1", "Affinità +1"},
    {"好，你看到了", "OK, you saw it", "OK, l'hai visto"},
    {"好，以后开机我自己来", "OK, I'll start with Windows", "OK, partirò con Windows"},
    {"好，以后不自动来了", "OK, I won't start automatically", "OK, non partirò più da solo"},
    {"注册表写不进去，没设成", "Couldn't write the registry, not set", "Impossibile scrivere nel registro, non impostato"},
    {"时间没看懂。写法：14:30、+30、2026-09-05 09:00", "Couldn't read the time. Use 14:30, +30 or 2026-09-05 09:00",
     "Orario non riconosciuto. Usa 14:30, +30 o 2026-09-05 09:00"},
    {"内容是空的", "The note is empty", "La nota è vuota"},
    {"填城市名，中文外文都行，如「杭州」「罗马」「Torino」；重名的加上国家，如「罗马 意大利」。填了它每次启动会联网查一次天气（地名问 OpenStreetMap，天气问 Open-Meteo，都免费无需注册）；留空一个字节都不发。",
     "Type a city name in any language, e.g. Hangzhou, Rome, Torino; add the country if ambiguous, e.g. \"Rome Italy\". With a city set, the weather is fetched once per launch (place names from OpenStreetMap, weather from Open-Meteo, both free, no account). Leave it empty and nothing is ever sent.",
     "Scrivi il nome di una città in qualsiasi lingua, es. Hangzhou, Roma, Torino; aggiungi il paese se ambiguo, es. \"Roma Italia\". Con una città impostata il meteo viene letto una volta per avvio (nomi da OpenStreetMap, meteo da Open-Meteo, entrambi gratuiti, senza account). Vuoto = non viene inviato nulla."},
    // 托盘菜单
    {"显示宠物", "Show pet", "Mostra il cane"},
    {"隐藏宠物", "Hide pet", "Nascondi il cane"},
    {"属性…", "Stats…", "Statistiche…"},
    {"备忘录…", "Notes…", "Promemoria…"},
    {"改名…", "Rename…", "Rinomina…"},
    {"声音", "Sound", "Suoni"},
    {"命名提醒", "Name reminder", "Promemoria nome"},
    {"天气城市…（未设，不联网）", "Weather city… (not set, offline)", "Città meteo… (non impostata, offline)"},
    {"天气城市…（%s）", "Weather city… (%s)", "Città meteo… (%s)"},
    {"开机自启", "Start with Windows", "Avvia con Windows"},
    {"语言 / Language", "Language / 语言", "Lingua / Language"},
    {"退出", "Exit", "Esci"},
    {"性格…", "Personality…", "Carattere…"},
    // 对话框
    {"确定", "OK", "OK"},
    {"取消", "Cancel", "Annulla"},
    {"给它起个名字", "Give it a name", "Dagli un nome"},
    {"最多 16 个字。回车确定。", "Up to 16 characters. Enter to confirm.", "Massimo 16 caratteri. Invio per confermare."},
    {"天气城市", "Weather city", "Città meteo"},
    {"备忘录", "Notes", "Promemoria"},
    {"添加", "Add", "Aggiungi"},
    {"删除", "Delete", "Elimina"},
    {"关闭", "Close", "Chiudi"},
    {"内容", "Note", "Testo"},
    {"时间", "Time", "Ora"},
    {"时间写法：14:30（过了算明天）、+30（30 分钟后）、2026-09-05 09:00。回车添加。",
     "Time formats: 14:30 (tomorrow if already past), +30 (in 30 minutes), 2026-09-05 09:00. Enter to add.",
     "Formati ora: 14:30 (domani se già passato), +30 (tra 30 minuti), 2026-09-05 09:00. Invio per aggiungere."},
    {"已添加。时间写法：14:30、+30、2026-09-05 09:00", "Added. Time formats: 14:30, +30, 2026-09-05 09:00",
     "Aggiunto. Formati ora: 14:30, +30, 2026-09-05 09:00"},
    // 属性面板
    {"属性", "Stats", "Statistiche"},
    {"%s 的属性", "%s's stats", "Statistiche di %s"},
    {"再右键它、按 Esc 或点别处关闭", "Right-click it again, press Esc or click elsewhere to close",
     "Clic destro di nuovo, Esc o clic altrove per chiudere"},
    {"基本", "Basics", "Generale"},
    {"亲密度", "Affinity", "Affinità"},
    {"性格", "Personality", "Carattere"},
    {"成长", "Growth", "Crescita"},
    {"名字", "Name", "Nome"},
    {"品种", "Breed", "Razza"},
    {"今天", "Today", "Oggi"},
    {"领养日", "Adopted", "Adottato"},
    {"（相处 %lld 天）", " (%lld days together)", " (%lld giorni insieme)"},
    {"状态", "Status", "Stato"},
    {"亲密度", "Affinity", "Affinità"},
    {"阶段", "Stage", "Livello"},
    {"启动", "Launches", "Avvii"},
    {"被摸 / 被打", "Petted / hit", "Carezze / colpi"},
    {"玩球", "Ball games", "Palla"},
    {"存档", "Save file", "Salvataggio"},
    {"%llu 次", "%llu", "%llu"},
    {"%llu / %llu 次", "%llu / %llu", "%llu / %llu"},
    {"睡觉", "sleeping", "dorme"},
    {"闲着", "idle", "a riposo"},
    {"（乖着，还有 %.0f 秒）", " (behaving, %.0f s left)", " (buono, ancora %.0f s)"},
    {"陌生", "stranger", "estraneo"},
    {"初识", "just met", "appena conosciuto"},
    {"眼熟", "familiar face", "viso familiare"},
    {"认识了", "acquainted", "conoscente"},
    {"熟络", "familiar", "familiare"},
    {"亲近", "close", "affezionato"},
    {"信赖", "trusted", "fidato"},
    {"默契", "in sync", "in sintonia"},
    {"羁绊", "bonded", "legame"},
    {"形影不离", "inseparable", "inseparabile"},
    {"捣蛋", "Mischief", "Birbante"},
    {"好奇", "Curiosity", "Curiosità"},
    {"卖萌", "Charm", "Tenerezza"},
    {"外向", "Extroversion", "Estroversione"},
    {"活泼", "Liveliness", "Vivacità"},
    {"黏人", "Clinginess", "Appiccicoso"},
    {"懒散", "Laziness", "Pigrizia"},
    {"胆小", "Timidity", "Timidezza"},
    {"邪恶比格", "Evil beagle", "Beagle malefico"},
    {"Lv.%d · %s", "Lv.%d · %s", "Lv.%d · %s"},
    {"%s · %s", "%s · %s", "%s · %s"},
    {"性格摘要", "Personality summary", "Sintesi del carattere"},
    {"改性格", "Edit personality", "Modifica carattere"},
    {"拖动滑条改各项；「重新随机」按邪恶比格基线再摇一次。确定后立刻生效。",
     "Drag the sliders to change traits. \"Reroll\" draws a new set from the evil-beagle baseline. OK applies immediately.",
     "Trascina i cursori per cambiare i tratti. \"Rimescola\" ne estrae di nuovi dalla base beagle malefico. OK applica subito."},
    {"重新随机", "Reroll", "Rimescola"},
    // 插件
    {"该喝水了", "Time to drink some water", "È ora di bere un po' d'acqua"},
    {"坐太久了，起来动一动", "You've been sitting too long, get up and move", "Sei seduto da troppo, alzati e muoviti"},
    {"备忘：", "Note: ", "Promemoria: "},
    {"%s 今天%s，现在 %s", "%s today: %s, now %s", "%s oggi: %s, ora %s"},
    {"，最高 %s 最低 %s", ", high %s low %s", ", max %s min %s"},
    {"没查到「%s」这个地方，试试加上国家，比如「罗马 意大利」", "Couldn't find \"%s\", try adding the country, e.g. \"Rome Italy\"",
     "Non trovo \"%s\", prova ad aggiungere il paese, es. \"Roma Italia\""},
    {"天气没取到，网络不通或者服务没响应", "Couldn't get the weather: no network or the service didn't answer",
     "Meteo non disponibile: nessuna rete o il servizio non risponde"},
    {"天气服务回的数据我看不懂", "The weather service sent data I can't read", "Il servizio meteo ha inviato dati che non capisco"},
    {"晴", "clear", "sereno"},
    {"大致晴", "mostly clear", "poco nuvoloso"},
    {"多云", "cloudy", "nuvoloso"},
    {"阴", "overcast", "coperto"},
    {"有雾", "foggy", "nebbia"},
    {"毛毛雨", "drizzle", "pioggerella"},
    {"下雨", "rain", "pioggia"},
    {"下雪", "snow", "neve"},
    {"阵雨", "showers", "rovesci"},
    {"阵雪", "snow showers", "rovesci di neve"},
    {"雷雨", "thunderstorm", "temporale"},
    {"天气未知", "unknown weather", "meteo sconosciuto"},
    {"明天", "Tomorrow", "Domani"},
    {"[已看] ", "[seen] ", "[visto] "},
    {"打开数据目录", "Open data folder", "Apri la cartella dati"},
    // 操作提示
    {"好久没人摸我了……鼠标在我头上慢慢移动就是摸，好感度会涨", "Nobody has petted me for a while... move the mouse slowly over my head to pet me, affinity goes up",
     "È da un po' che nessuno mi accarezza... muovi il mouse piano sulla mia testa, l'affinità sale"},
    {"右键点我，能看到我的属性和好感度", "Right-click me to see my stats and affinity", "Clic destro su di me per vedere le mie statistiche e l'affinità"},
    {"按住我的身体可以把我拖到别处，松手的地方就是我的窝", "Press and hold my body to drag me anywhere; where you let go becomes my home",
     "Tieni premuto sul mio corpo per trascinarmi; dove mi lasci diventa la mia cuccia"},
    {"托盘菜单填个天气城市，我每次启动就报天气", "Set a weather city in the tray menu and I'll report the weather on every launch",
     "Imposta una città meteo nel menu della barra e ti dirò il meteo a ogni avvio"},
    {"托盘菜单有备忘录，到点我会提醒你", "The tray menu has notes; I'll remind you when they're due", "Nel menu della barra ci sono i promemoria; ti avviso quando scadono"},
    {"托盘菜单可以换语言：中文 / English / Italiano", "You can switch language in the tray menu: 中文 / English / Italiano", "Puoi cambiare lingua nel menu della barra: 中文 / English / Italiano"},
    {"在我头上快速来回就是打我……好感度会掉，但很难掉", "Sweeping fast back and forth over my head counts as hitting... affinity drops, but only a little",
     "Passare veloce avanti e indietro sulla mia testa conta come un colpo... l'affinità scende, ma poco"},
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(Str::Count), "文案表与 Str 数量不一致");

// 动作名，顺序 = ActionKind。
constexpr Entry kActions[] = {
    {"发呆", "idle", "a riposo"},
    {"闲逛", "wandering", "a spasso"},
    {"伸懒腰", "stretching", "si stira"},
    {"抖身子", "shaking", "si scrolla"},
    {"坐下", "sitting", "seduto"},
    {"卖萌·歪头", "head tilt", "testa inclinata"},
    {"卖萌·扒手", "pawing", "zampina"},
    {"卖萌·翻肚皮", "belly up", "pancia all'aria"},
    {"玩球", "playing ball", "gioca con la palla"},
    {"捣蛋·扑光标", "pouncing the cursor", "salta sul cursore"},
    {"捣蛋·打翻水碗", "flipping the bowl", "rovescia la ciotola"},
    {"提醒·尿尿", "reminder: peeing", "promemoria: pipì"},
    {"提醒·跳起踢球", "reminder: kicking the ball", "promemoria: calcia la palla"},
    {"出场", "entrance", "entrata"},
    {"被摸", "being petted", "coccolato"},
    {"挨打", "cowering", "si rannicchia"},
    {"被戳", "poked", "toccato"},
    {"睡觉", "sleeping", "dorme"},
    {"提醒·备忘", "reminder: note", "promemoria: nota"},
    {"冲屏", "charging the screen", "carica verso lo schermo"},
    {"回巢", "going home", "torna a casa"},
};
static_assert(sizeof(kActions) / sizeof(kActions[0]) == static_cast<size_t>(ActionKind::Count), "动作名表与 ActionKind 数量不一致");

const char* pick(const Entry& e) {
    switch (g_lang) {
        case Lang::En: return e.en;
        case Lang::It: return e.it;
        default:       return e.zh;
    }
}

}  // namespace

const char* lang_code(Lang l) {
    switch (l) {
        case Lang::En: return "en";
        case Lang::It: return "it";
        default:       return "zh";
    }
}

Lang lang_from_code(const char* c) {
    if (!c) return Lang::Zh;
    if (std::strncmp(c, "en", 2) == 0) return Lang::En;
    if (std::strncmp(c, "it", 2) == 0) return Lang::It;
    return Lang::Zh;
}

const char* lang_native_name(Lang l) {
    switch (l) {
        case Lang::En: return "English";
        case Lang::It: return "Italiano";
        default:       return "中文";
    }
}

void set_language(Lang l) { g_lang = l < Lang::Count ? l : Lang::Zh; }
Lang language() { return g_lang; }

const char* tr(Str s) {
    if (s >= Str::Count) return "";
    return pick(kTable[static_cast<int>(s)]);
}

const char* action_name_tr(ActionKind k) {
    if (k >= ActionKind::Count) return "";
    return pick(kActions[static_cast<int>(k)]);
}

}  // namespace pet
