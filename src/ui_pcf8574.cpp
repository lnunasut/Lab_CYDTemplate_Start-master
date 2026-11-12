#include <stdint.h>

#include "app_config.h"
#include "board_config.h"
#include "hal.h"
#include "ui.h"

namespace {

constexpr uint8_t LED_COUNT = 4;
constexpr uint8_t INPUT_COUNT = 4;
constexpr uint8_t INPUT_MASK = 0xF0;

lv_obj_t *gLinkLabel = nullptr;
lv_obj_t *gLedButton[LED_COUNT]{};
lv_obj_t *gLedState[LED_COUNT]{};
lv_obj_t *gLedDot[LED_COUNT]{};
lv_obj_t *gInputTile[INPUT_COUNT]{};
lv_obj_t *gInputState[INPUT_COUNT]{};
lv_obj_t *gInputDot[INPUT_COUNT]{};
uint8_t gStableInputs = INPUT_MASK;
uint8_t gCandidateInputs = INPUT_MASK;
uint8_t gCandidateCount = 0;
uint32_t gLastPoll = 0;

void pulseExec(void *obj, int32_t value)
{
    lv_obj_set_size(static_cast<lv_obj_t *>(obj), value, value);
    lv_obj_center(static_cast<lv_obj_t *>(obj));
}

void setPulse(lv_obj_t *dot, bool active, lv_color_t color)
{
    lv_anim_del(dot, pulseExec);
    lv_obj_set_style_bg_color(dot, color, 0);
    if (!active) {
        lv_obj_set_size(dot, 14, 14);
        lv_obj_center(dot);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, pulseExec);
    lv_anim_set_values(&a, 14, 22);
    lv_anim_set_time(&a, 650);
    lv_anim_set_playback_time(&a, 650);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void refreshLed(uint8_t index)
{
    const bool on = (hal::pcf8574OutputMask() & (1U << index)) != 0;
    lv_label_set_text(gLedState[index], on ? "ติด / OUTPUT LOW" : "ดับ / OUTPUT HIGH");
    lv_obj_set_style_text_color(gLedState[index], on ? lv_palette_main(LV_PALETTE_LIGHT_GREEN) : lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(lv_obj_get_child(gLedButton[index], 0), on ? "ปิด LED" : "เปิด LED");
    setPulse(gLedDot[index], on, on ? lv_palette_main(LV_PALETTE_LIGHT_GREEN) : lv_palette_main(LV_PALETTE_GREY));
}

void onLed(lv_event_t *e)
{
    const uint8_t index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    const uint8_t mask = hal::pcf8574OutputMask() ^ (1U << index);
    if (hal::pcf8574WriteOutputs(mask)) {
        refreshLed(index);
        hal::soundTone((mask & (1U << index)) ? 1568 : 880, 65);
        Serial.printf("[PCF8574] LED%u=%s output=0x%X\n", index, (mask & (1U << index)) ? "ON" : "OFF", mask);
    } else {
        lv_label_set_text(gLinkLabel, "อุปกรณ์ไม่ตอบสนอง: ตรวจไฟเลี้ยง, SDA/SCL และ address 0x20");
        hal::soundPlay(hal::Melody::Error);
    }
}

void onAllOff(lv_event_t *)
{
    if (!hal::pcf8574WriteOutputs(0)) {
        lv_label_set_text(gLinkLabel, "อุปกรณ์ไม่ตอบสนอง: ตรวจไฟเลี้ยง, SDA/SCL และ address 0x20");
        return;
    }
    for (uint8_t i = 0; i < LED_COUNT; ++i) refreshLed(i);
    hal::soundTone(784, 90);
}

void onAllOn(lv_event_t *)
{
    if (!hal::pcf8574WriteOutputs(0x0F)) {
        lv_label_set_text(gLinkLabel, "อุปกรณ์ไม่ตอบสนอง: ตรวจไฟเลี้ยง, SDA/SCL และ address 0x20");
        return;
    }
    for (uint8_t i = 0; i < LED_COUNT; ++i) refreshLed(i);
    hal::soundPlay(hal::Melody::Success);
}

void updateInputs(uint8_t pins)
{
    const uint8_t current = pins & INPUT_MASK;
    if (current == gStableInputs) {
        gCandidateInputs = current;
        gCandidateCount = 0;
        return;
    }
    if (current != gCandidateInputs) {
        gCandidateInputs = current;
        gCandidateCount = 1;
        return;
    }
    if (++gCandidateCount < 2) return;

    const uint8_t changed = current ^ gStableInputs;
    gStableInputs = current;
    gCandidateCount = 0;
    for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
        if ((changed & (1U << (i + 4))) == 0) continue;
        const bool pressed = (current & (1U << (i + 4))) == 0;  // Active Low
        lv_label_set_text(gInputState[i], pressed ? "กดอยู่" : "ปล่อยแล้ว");
        lv_obj_set_style_text_color(gInputState[i], pressed ? lv_palette_main(LV_PALETTE_AMBER) : lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
        lv_obj_set_style_bg_color(gInputTile[i], pressed ? lv_color_hex(0x382A17) : lv_color_hex(0x17242D), 0);
        setPulse(gInputDot[i], pressed, pressed ? lv_palette_main(LV_PALETTE_AMBER) : lv_palette_main(LV_PALETTE_GREY));
        hal::soundTone(pressed ? 1319 : 659, 45);
        Serial.printf("[PCF8574] P%u=%s (active-low)\n", i + 4, pressed ? "PRESSED" : "RELEASED");
    }
}

void tick(lv_timer_t *)
{
    const uint32_t now = millis();
    if (now - gLastPoll < 50) return;
    gLastPoll = now;

    uint8_t pins = INPUT_MASK;
    if (!hal::pcf8574Read(pins)) {
        lv_label_set_text(gLinkLabel, "ออฟไลน์ | ไม่พบ PCF8574 ที่ 0x20");
        lv_obj_set_style_text_color(gLinkLabel, lv_palette_main(LV_PALETTE_RED), 0);
        return;
    }
    lv_label_set_text_fmt(gLinkLabel, "ออนไลน์ | 0x%02X | SDA %u / SCL %u", cyd::PCF8574_ADDRESS, cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL);
    lv_obj_set_style_text_color(gLinkLabel, lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
    updateInputs(pins);
}

lv_obj_t *makeDot(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

}  // namespace

void ui_pcf8574_tab(lv_obj_t *tab)
{
    lv_obj_t *card = ui_card(tab, "PCF8574 / I2C EXPANDER");
    gLinkLabel = ui_label(card, "กำลังเชื่อมต่อ...", nullptr, lv_palette_main(LV_PALETTE_GREY));
    ui_label(card, "Address 0x20 | A0-A2 = GND | SDA 27 / SCL 22", nullptr, lv_palette_lighten(LV_PALETTE_GREY, 1));

    card = ui_card(tab, "เอาต์พุต / LED แบบ ISINK");
    lv_obj_t *row = ui_row(card);
    ui_btn(row, LV_SYMBOL_POWER " เปิดทั้งหมด", onAllOn);
    ui_btn(row, LV_SYMBOL_STOP " ปิดทั้งหมด", onAllOff);
    row = ui_row(card);
    for (uint8_t i = 0; i < LED_COUNT; ++i) {
        lv_obj_t *tile = lv_obj_create(row);
        lv_obj_set_size(tile, LV_PCT(47), 78);
        lv_obj_set_style_pad_all(tile, 6, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(tile, 2, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        char title[20];
        snprintf(title, sizeof(title), "LED %u / P%u", i, i);
        lv_obj_t *titleLabel = ui_label(tile, title, nullptr, lv_color_white());
        lv_obj_set_width(titleLabel, LV_PCT(100));
        gLedDot[i] = makeDot(tile, lv_palette_main(LV_PALETTE_GREY));
        gLedState[i] = ui_label(tile, "ดับ / OUTPUT HIGH", nullptr, lv_palette_main(LV_PALETTE_GREY));
        gLedButton[i] = lv_btn_create(tile);
        lv_obj_set_width(gLedButton[i], LV_PCT(100));
        lv_obj_t *buttonLabel = lv_label_create(gLedButton[i]);
        lv_label_set_text(buttonLabel, "เปิด LED");
        lv_obj_center(buttonLabel);
        lv_obj_add_event_cb(gLedButton[i], onLed, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        setPulse(gLedDot[i], false, lv_palette_main(LV_PALETTE_GREY));
    }

    card = ui_card(tab, "อินพุต / SWITCH ACTIVE LOW");
    ui_label(card, "Pull-up 10k | กดสวิตช์แล้วขาอ่าน LOW", nullptr, lv_palette_lighten(LV_PALETTE_GREY, 1));
    row = ui_row(card);
    for (uint8_t i = 0; i < INPUT_COUNT; ++i) {
        lv_obj_t *tile = lv_obj_create(row);
        lv_obj_set_size(tile, LV_PCT(47), 55);
        lv_obj_set_style_pad_all(tile, 6, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x17242D), 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        gInputTile[i] = tile;
        char name[16];
        snprintf(name, sizeof(name), "SW%u  P%u", i, i + 4);
        ui_label(tile, name, nullptr, lv_color_white());
        gInputState[i] = ui_label(tile, "ปล่อยแล้ว", nullptr, lv_palette_main(LV_PALETTE_LIGHT_GREEN));
        gInputDot[i] = makeDot(tile, lv_palette_main(LV_PALETTE_GREY));
        setPulse(gInputDot[i], false, lv_palette_main(LV_PALETTE_GREY));
    }
    lv_timer_create(tick, 50, nullptr);
    tick(nullptr);
}
