#include "plugins/tips_plugin.h"

#include "core/i18n.h"

namespace pet::plugins {

void TipsPlugin::on_tick(const HostClock& clock) {
    if (!host_ || clock.nowSeconds < nextAt_) return;
    if (!clock.petVisible || clock.petBusy) { nextAt_ = clock.nowSeconds + 30.0; return; }

    // 长时间没摸头：优先教摸头。否则按顺序轮换其余提示。
    Str tip;
    if (clock.sinceLastPetSeconds > kNoPetSeconds) {
        tip = Str::TipPet;
    } else {
        static constexpr Str kOthers[] = {Str::TipStats, Str::TipDrag, Str::TipWeather, Str::TipMemo, Str::TipLanguage, Str::TipHit};
        tip = kOthers[rotation_ % 6];
        ++rotation_;
    }
    PetActionRequest req;
    req.action = ActionKind::CharmTilt;   // 歪个头，像在说话
    req.bubbleText = tr(tip);
    req.bubbleSeconds = 10.0f;
    if (host_->request_pet_action(req)) {
        ++shown_;
        nextAt_ = clock.nowSeconds + rng_.range(20.0f * 60.0f, 35.0f * 60.0f);
    } else {
        nextAt_ = clock.nowSeconds + 30.0;
    }
}

}  // namespace pet::plugins
