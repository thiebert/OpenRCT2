/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <algorithm>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/windows/Window.h>
#include <openrct2/Context.h>
#include <openrct2/Input.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/localisation/Localisation.h>

// clang-format off
enum {
    WIDX_BACKGROUND
};

static rct_widget window_tooltip_widgets[] = {
    MakeWidget({0, 0}, {200, 32}, WWT_IMGBTN, WindowColour::Primary),
    { WIDGETS_END },
};

static void window_tooltip_update(rct_window *w);
static void window_tooltip_paint(rct_window *w, rct_drawpixelinfo *dpi);

static rct_window_event_list window_tooltip_events([](auto& events)
{
    events.update = &window_tooltip_update;
    events.paint = &window_tooltip_paint;
});
// clang-format on

static utf8 _tooltipText[sizeof(gCommonStringFormatBuffer)];
static int16_t _tooltipNumLines;

void window_tooltip_reset(const ScreenCoordsXY& screenCoords)
{
    gTooltipCursor = screenCoords;
    gTooltipTimeout = 0;
    gTooltipWidget.window_classification = 255;
    input_set_state(InputState::Normal);
    input_set_flag(INPUT_FLAG_4, false);
}

// Returns the width of the new tooltip text
static int32_t FormatTextForTooltip(const OpenRCT2String& message)
{
    format_string(_tooltipText, sizeof(_tooltipText), message.str, message.args.Data());
    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;

    auto textWidth = gfx_get_string_width_new_lined(_tooltipText);
    textWidth = std::min(textWidth, 196);

    gCurrentFontSpriteBase = FONT_SPRITE_BASE_MEDIUM;

    int32_t numLines, fontSpriteBase;
    textWidth = gfx_wrap_string(_tooltipText, textWidth + 1, &numLines, &fontSpriteBase);
    _tooltipNumLines = numLines;
    return textWidth;
}

void window_tooltip_show(const OpenRCT2String& message, ScreenCoordsXY screenCoords)
{
    auto* w = window_find_by_class(WC_ERROR);
    if (w != nullptr)
        return;

    int32_t textWidth = FormatTextForTooltip(message);
    int32_t width = textWidth + 3;
    int32_t height = ((_tooltipNumLines + 1) * font_get_line_height(FONT_SPRITE_BASE_MEDIUM)) + 4;
    window_tooltip_widgets[WIDX_BACKGROUND].right = width;
    window_tooltip_widgets[WIDX_BACKGROUND].bottom = height;

    int32_t screenWidth = context_get_width();
    int32_t screenHeight = context_get_height();
    screenCoords.x = std::clamp(screenCoords.x - (width / 2), 0, screenWidth - width);

    // TODO The cursor size will be relative to the window DPI.
    //      The amount to offset the y should be adjusted.

    int32_t max_y = screenHeight - height;
    screenCoords.y += 26; // Normally, we'd display the tooltip 26 lower
    if (screenCoords.y > max_y)
        // If y is too large, the tooltip could be forced below the cursor if we'd just clamped y,
        // so we'll subtract a bit more
        screenCoords.y -= height + 40;
    screenCoords.y = std::clamp(screenCoords.y, 22, max_y);

    w = window_create(screenCoords, width, height, &window_tooltip_events, WC_TOOLTIP, WF_TRANSPARENT | WF_STICK_TO_FRONT);
    w->widgets = window_tooltip_widgets;

    reset_tooltip_not_shown();
}

/**
 *
 *  rct2: 0x006EA10D
 */
void window_tooltip_open(rct_window* widgetWindow, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCords)
{
    rct_widget* widget;

    if (widgetWindow == nullptr || widgetIndex == -1)
        return;

    widget = &widgetWindow->widgets[widgetIndex];
    window_event_invalidate_call(widgetWindow);

    rct_string_id stringId = widget->tooltip;

    if (stringId == STR_NONE)
        return;

    gTooltipWidget.window_classification = widgetWindow->classification;
    gTooltipWidget.window_number = widgetWindow->number;
    gTooltipWidget.widget_index = widgetIndex;
    auto result = window_event_tooltip_call(widgetWindow, widgetIndex, stringId);
    if (result.str == STR_NONE)
        return;

    if (widget->flags & WIDGET_FLAGS::TOOLTIP_IS_STRING)
    {
        result.str = STR_STRING_TOOLTIP;
        result.args = Formatter();
        result.args.Add<const char*>(widget->sztooltip);
    }
    window_tooltip_show(result, screenCords);
}

/**
 *
 *  rct2: 0x006E98C6
 */
void window_tooltip_close()
{
    window_close_by_class(WC_TOOLTIP);
    gTooltipTimeout = 0;
    gTooltipWidget.window_classification = 255;
}

/**
 *
 *  rct2: 0x006EA580
 */
static void window_tooltip_update(rct_window* w)
{
    reset_tooltip_not_shown();
}

/**
 *
 *  rct2: 0x006EA41D
 */
static void window_tooltip_paint(rct_window* w, rct_drawpixelinfo* dpi)
{
    int32_t left = w->windowPos.x;
    int32_t top = w->windowPos.y;
    int32_t right = w->windowPos.x + w->width - 1;
    int32_t bottom = w->windowPos.y + w->height - 1;

    // Background
    gfx_filter_rect(dpi, { { left + 1, top + 1 }, { right - 1, bottom - 1 } }, PALETTE_45);
    gfx_filter_rect(dpi, { { left + 1, top + 1 }, { right - 1, bottom - 1 } }, PALETTE_GLASS_LIGHT_ORANGE);

    // Sides
    gfx_filter_rect(dpi, { { left + 0, top + 2 }, { left + 0, bottom - 2 } }, PALETTE_DARKEN_3);
    gfx_filter_rect(dpi, { { right + 0, top + 2 }, { right + 0, bottom - 2 } }, PALETTE_DARKEN_3);
    gfx_filter_rect(dpi, { { left + 2, bottom + 0 }, { right - 2, bottom + 0 } }, PALETTE_DARKEN_3);
    gfx_filter_rect(dpi, { { left + 2, top + 0 }, { right - 2, top + 0 } }, PALETTE_DARKEN_3);

    // Corners
    gfx_filter_pixel(dpi, { left + 1, top + 1 }, PALETTE_DARKEN_3);
    gfx_filter_pixel(dpi, { right - 1, top + 1 }, PALETTE_DARKEN_3);
    gfx_filter_pixel(dpi, { left + 1, bottom - 1 }, PALETTE_DARKEN_3);
    gfx_filter_pixel(dpi, { right - 1, bottom - 1 }, PALETTE_DARKEN_3);

    // Text
    left = w->windowPos.x + ((w->width + 1) / 2) - 1;
    top = w->windowPos.y + 1;
    draw_string_centred_raw(dpi, { left, top }, _tooltipNumLines, _tooltipText);
}
