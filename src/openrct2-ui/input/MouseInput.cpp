/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <algorithm>
#include <cmath>
#include <iterator>
#include <openrct2-ui/interface/Dropdown.h>
#include <openrct2-ui/interface/Viewport.h>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/windows/Window.h>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/Input.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/audio/audio.h>
#include <openrct2/config/Config.h>
#include <openrct2/interface/Chat.h>
#include <openrct2/interface/Cursors.h>
#include <openrct2/interface/InteractiveConsole.h>
#include <openrct2/localisation/Localisation.h>
#include <openrct2/platform/platform.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/scenario/Scenario.h>
#include <openrct2/world/Banner.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/Scenery.h>
#include <openrct2/world/Sprite.h>

struct rct_mouse_data
{
    uint32_t x;
    uint32_t y;
    uint32_t state;
};

static rct_mouse_data _mouseInputQueue[64];
static uint8_t _mouseInputQueueReadIndex = 0;
static uint8_t _mouseInputQueueWriteIndex = 0;

static uint32_t _ticksSinceDragStart;
static widget_ref _dragWidget;
static uint8_t _dragScrollIndex;
static int32_t _originalWindowWidth;
static int32_t _originalWindowHeight;

static uint8_t _currentScrollIndex;
static uint8_t _currentScrollArea;

ScreenCoordsXY gInputDragLast;

uint16_t gTooltipTimeout;
widget_ref gTooltipWidget;
ScreenCoordsXY gTooltipCursor;

static int16_t _clickRepeatTicks;

static int32_t game_get_next_input(ScreenCoordsXY& screenCoords);
static void input_widget_over(const ScreenCoordsXY& screenCoords, rct_window* w, rct_widgetindex widgetIndex);
static void input_widget_over_change_check(
    rct_windowclass windowClass, rct_windownumber windowNumber, rct_widgetindex widgetIndex);
static void input_widget_over_flatbutton_invalidate();
void process_mouse_over(const ScreenCoordsXY& screenCoords);
void process_mouse_tool(const ScreenCoordsXY& screenCoords);
void invalidate_scroll();
static rct_mouse_data* get_mouse_input();
void tile_element_right_click(int32_t type, TileElement* tileElement, const ScreenCoordsXY& screenCoords);
static void game_handle_input_mouse(const ScreenCoordsXY& screenCoords, int32_t state);
static void input_widget_left(const ScreenCoordsXY& screenCoords, rct_window* w, rct_widgetindex widgetIndex);
void input_state_widget_pressed(
    const ScreenCoordsXY& screenCoords, int32_t state, rct_widgetindex widgetIndex, rct_window* w, rct_widget* widget);
void set_cursor(CursorID cursor_id);
static void input_window_position_continue(
    rct_window* w, const ScreenCoordsXY& lastScreenCoords, const ScreenCoordsXY& newScreenCoords);
static void input_window_position_end(rct_window* w, const ScreenCoordsXY& screenCoords);
static void input_window_resize_begin(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords);
static void input_window_resize_continue(rct_window* w, const ScreenCoordsXY& screenCoords);
static void input_window_resize_end();
static void input_viewport_drag_begin(rct_window* w);
static void input_viewport_drag_continue();
static void input_viewport_drag_end();
static void input_scroll_begin(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords);
static void input_scroll_continue(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords);
static void input_scroll_end();
static void input_scroll_part_update_hthumb(rct_window* w, rct_widgetindex widgetIndex, int32_t x, int32_t scroll_id);
static void input_scroll_part_update_hleft(rct_window* w, rct_widgetindex widgetIndex, int32_t scroll_id);
static void input_scroll_part_update_hright(rct_window* w, rct_widgetindex widgetIndex, int32_t scroll_id);
static void input_scroll_part_update_vthumb(rct_window* w, rct_widgetindex widgetIndex, int32_t y, int32_t scroll_id);
static void input_scroll_part_update_vtop(rct_window* w, rct_widgetindex widgetIndex, int32_t scroll_id);
static void input_scroll_part_update_vbottom(rct_window* w, rct_widgetindex widgetIndex, int32_t scroll_id);
static void input_update_tooltip(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords);

#pragma region Mouse input

/**
 *
 *  rct2: 0x006EA627
 */
void game_handle_input()
{
    window_visit_each([](rct_window* w) { window_event_periodic_update_call(w); });

    invalidate_all_windows_after_input();

    int32_t state;
    ScreenCoordsXY screenCoords;
    while ((state = game_get_next_input(screenCoords)) != MOUSE_STATE_RELEASED)
    {
        game_handle_input_mouse(screenCoords, state & 0xFF);
    }

    if (_inputFlags & INPUT_FLAG_5)
    {
        game_handle_input_mouse(screenCoords, state);
    }
    else
    {
        int32_t screenWidth = context_get_width();
        int32_t screenHeight = context_get_height();
        screenCoords.x = std::clamp(screenCoords.x, 0, screenWidth - 1);
        screenCoords.y = std::clamp(screenCoords.y, 0, screenHeight - 1);

        game_handle_input_mouse(screenCoords, state);
        process_mouse_over(screenCoords);
        process_mouse_tool(screenCoords);
    }

    window_visit_each([](rct_window* w) { window_event_unknown_08_call(w); });
}

/**
 *
 *  rct2: 0x006E83C7
 */
static int32_t game_get_next_input(ScreenCoordsXY& screenCoords)
{
    rct_mouse_data* input = get_mouse_input();
    if (input == nullptr)
    {
        const CursorState* cursorState = context_get_cursor_state();
        screenCoords = cursorState->position;
        return 0;
    }
    else
    {
        screenCoords.x = input->x;
        screenCoords.y = input->y;
        return input->state;
    }
}

/**
 *
 *  rct2: 0x00407074
 */
static rct_mouse_data* get_mouse_input()
{
    // Check if that location has been written to yet
    if (_mouseInputQueueReadIndex == _mouseInputQueueWriteIndex)
    {
        return nullptr;
    }
    else
    {
        rct_mouse_data* result = &_mouseInputQueue[_mouseInputQueueReadIndex];
        _mouseInputQueueReadIndex = (_mouseInputQueueReadIndex + 1) % std::size(_mouseInputQueue);
        return result;
    }
}

/**
 *
 *  rct2: 0x006E957F
 */
static void input_scroll_drag_begin(const ScreenCoordsXY& screenCoords, rct_window* w, rct_widgetindex widgetIndex)
{
    _inputState = InputState::ScrollRight;
    gInputDragLast = screenCoords;
    _dragWidget.window_classification = w->classification;
    _dragWidget.window_number = w->number;
    _dragWidget.widget_index = widgetIndex;
    _ticksSinceDragStart = 0;

    _dragScrollIndex = window_get_scroll_data_index(w, widgetIndex);
    context_hide_cursor();
}

/**
 * Based on (heavily changed)
 *  rct2: 0x006E9E0E,  0x006E9ED0
 */
static void input_scroll_drag_continue(const ScreenCoordsXY& screenCoords, rct_window* w)
{
    rct_widgetindex widgetIndex = _dragWidget.widget_index;
    uint8_t scrollIndex = _dragScrollIndex;

    rct_widget* widget = &w->widgets[widgetIndex];
    rct_scroll* scroll = &w->scrolls[scrollIndex];

    ScreenCoordsXY differentialCoords = screenCoords - gInputDragLast;

    if (scroll->flags & HSCROLLBAR_VISIBLE)
    {
        int16_t size = widget->width() - 1;
        if (scroll->flags & VSCROLLBAR_VISIBLE)
            size -= 11;
        size = std::max(0, scroll->h_right - size);
        scroll->h_left = std::min<uint16_t>(std::max(0, scroll->h_left + differentialCoords.x), size);
    }

    if (scroll->flags & VSCROLLBAR_VISIBLE)
    {
        int16_t size = widget->height() - 1;
        if (scroll->flags & HSCROLLBAR_VISIBLE)
            size -= 11;
        size = std::max(0, scroll->v_bottom - size);
        scroll->v_top = std::min<uint16_t>(std::max(0, scroll->v_top + differentialCoords.y), size);
    }

    widget_scroll_update_thumbs(w, widgetIndex);
    window_invalidate_by_number(w->classification, w->number);

    ScreenCoordsXY fixedCursorPosition = { static_cast<int32_t>(std::ceil(gInputDragLast.x * gConfigGeneral.window_scale)),
                                           static_cast<int32_t>(std::ceil(gInputDragLast.y * gConfigGeneral.window_scale)) };

    context_set_cursor_position(fixedCursorPosition);
}

/**
 *
 *  rct2: 0x006E8ACB
 */
static void input_scroll_right(const ScreenCoordsXY& screenCoords, int32_t state)
{
    rct_window* w = window_find_by_number(_dragWidget.window_classification, _dragWidget.window_number);
    if (w == nullptr)
    {
        context_show_cursor();
        _inputState = InputState::Reset;
        return;
    }

    switch (state)
    {
        case MOUSE_STATE_RELEASED:
            _ticksSinceDragStart += gCurrentDeltaTime;
            if (screenCoords.x != 0 || screenCoords.y != 0)
            {
                _ticksSinceDragStart = 1000;
                input_scroll_drag_continue(screenCoords, w);
            }
            break;
        case MOUSE_STATE_RIGHT_RELEASE:
            _inputState = InputState::Reset;
            context_show_cursor();
            break;
    }
}

/**
 *
 *  rct2: 0x006E8655
 */
static void game_handle_input_mouse(const ScreenCoordsXY& screenCoords, int32_t state)
{
    rct_window* w;
    rct_widget* widget;
    rct_widgetindex widgetIndex;

    // Get window and widget under cursor position
    w = window_find_from_point(screenCoords);
    widgetIndex = w == nullptr ? -1 : window_find_widget_from_point(w, screenCoords);
    widget = widgetIndex == -1 ? nullptr : &w->widgets[widgetIndex];

    switch (_inputState)
    {
        case InputState::Reset:
            window_tooltip_reset(screenCoords);
            // fall-through
        case InputState::Normal:
            switch (state)
            {
                case MOUSE_STATE_RELEASED:
                    input_widget_over(screenCoords, w, widgetIndex);
                    break;
                case MOUSE_STATE_LEFT_PRESS:
                    input_widget_left(screenCoords, w, widgetIndex);
                    break;
                case MOUSE_STATE_RIGHT_PRESS:
                    window_close_by_class(WC_TOOLTIP);

                    if (w != nullptr)
                    {
                        w = window_bring_to_front(w);
                    }

                    if (widgetIndex != -1)
                    {
                        switch (widget->type)
                        {
                            case WWT_VIEWPORT:
                                if (!(gScreenFlags & (SCREEN_FLAGS_TRACK_MANAGER | SCREEN_FLAGS_TITLE_DEMO)))
                                {
                                    input_viewport_drag_begin(w);
                                }
                                break;
                            case WWT_SCROLL:
                                input_scroll_drag_begin(screenCoords, w, widgetIndex);
                                break;
                        }
                    }
                    break;
            }
            break;
        case InputState::WidgetPressed:
            input_state_widget_pressed(screenCoords, state, widgetIndex, w, widget);
            break;
        case InputState::PositioningWindow:
            w = window_find_by_number(_dragWidget.window_classification, _dragWidget.window_number);
            if (w == nullptr)
            {
                _inputState = InputState::Reset;
            }
            else
            {
                input_window_position_continue(w, gInputDragLast, screenCoords);
                if (state == MOUSE_STATE_LEFT_RELEASE)
                {
                    input_window_position_end(w, screenCoords);
                }
            }
            break;
        case InputState::ViewportRight:
            if (state == MOUSE_STATE_RELEASED)
            {
                input_viewport_drag_continue();
            }
            else if (state == MOUSE_STATE_RIGHT_RELEASE)
            {
                input_viewport_drag_end();
                if (_ticksSinceDragStart < 500)
                {
                    // If the user pressed the right mouse button for less than 500 ticks, interpret as right click
                    viewport_interaction_right_click(screenCoords);
                }
            }
            break;
        case InputState::DropdownActive:
            input_state_widget_pressed(screenCoords, state, widgetIndex, w, widget);
            break;
        case InputState::ViewportLeft:
            w = window_find_by_number(_dragWidget.window_classification, _dragWidget.window_number);
            if (w == nullptr)
            {
                _inputState = InputState::Reset;
                break;
            }

            switch (state)
            {
                case MOUSE_STATE_RELEASED:
                    if (w->viewport == nullptr)
                    {
                        _inputState = InputState::Reset;
                        break;
                    }

                    if (w->classification != _dragWidget.window_classification || w->number != _dragWidget.window_number
                        || !(_inputFlags & INPUT_FLAG_TOOL_ACTIVE))
                    {
                        break;
                    }

                    w = window_find_by_number(gCurrentToolWidget.window_classification, gCurrentToolWidget.window_number);
                    if (w == nullptr)
                    {
                        break;
                    }

                    window_event_tool_drag_call(w, gCurrentToolWidget.widget_index, screenCoords);
                    break;
                case MOUSE_STATE_LEFT_RELEASE:
                    _inputState = InputState::Reset;
                    if (_dragWidget.window_number == w->number)
                    {
                        if ((_inputFlags & INPUT_FLAG_TOOL_ACTIVE))
                        {
                            w = window_find_by_number(
                                gCurrentToolWidget.window_classification, gCurrentToolWidget.window_number);
                            if (w != nullptr)
                            {
                                window_event_tool_up_call(w, gCurrentToolWidget.widget_index, screenCoords);
                            }
                        }
                        else if (!(_inputFlags & INPUT_FLAG_4))
                        {
                            viewport_interaction_left_click(screenCoords);
                        }
                    }
                    break;
            }
            break;
        case InputState::ScrollLeft:
            switch (state)
            {
                case MOUSE_STATE_RELEASED:
                    input_scroll_continue(w, widgetIndex, screenCoords);
                    break;
                case MOUSE_STATE_LEFT_RELEASE:
                    input_scroll_end();
                    break;
            }
            break;
        case InputState::Resizing:
            w = window_find_by_number(_dragWidget.window_classification, _dragWidget.window_number);
            if (w == nullptr)
            {
                _inputState = InputState::Reset;
            }
            else
            {
                if (state == MOUSE_STATE_LEFT_RELEASE)
                {
                    input_window_resize_end();
                }
                if (state == MOUSE_STATE_RELEASED || state == MOUSE_STATE_LEFT_RELEASE)
                {
                    input_window_resize_continue(w, screenCoords);
                }
            }
            break;
        case InputState::ScrollRight:
            input_scroll_right(screenCoords, state);
            break;
    }
}

#pragma region Window positioning / resizing

void input_window_position_begin(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords)
{
    _inputState = InputState::PositioningWindow;
    gInputDragLast = screenCoords - w->windowPos;
    _dragWidget.window_classification = w->classification;
    _dragWidget.window_number = w->number;
    _dragWidget.widget_index = widgetIndex;
}

static void input_window_position_continue(
    rct_window* w, const ScreenCoordsXY& lastScreenCoords, const ScreenCoordsXY& newScreenCoords)
{
    int32_t snapProximity;

    snapProximity = (w->flags & WF_NO_SNAPPING) ? 0 : gConfigGeneral.window_snap_proximity;
    window_move_and_snap(w, newScreenCoords - lastScreenCoords, snapProximity);
}

static void input_window_position_end(rct_window* w, const ScreenCoordsXY& screenCoords)
{
    _inputState = InputState::Normal;
    gTooltipTimeout = 0;
    gTooltipWidget = _dragWidget;
    window_event_moved_call(w, screenCoords);
}

static void input_window_resize_begin(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords)
{
    _inputState = InputState::Resizing;
    gInputDragLast = screenCoords;
    _dragWidget.window_classification = w->classification;
    _dragWidget.window_number = w->number;
    _dragWidget.widget_index = widgetIndex;
    _originalWindowWidth = w->width;
    _originalWindowHeight = w->height;
}

static void input_window_resize_continue(rct_window* w, const ScreenCoordsXY& screenCoords)
{
    if (screenCoords.y < static_cast<int32_t>(context_get_height()) - 2)
    {
        auto differentialCoords = screenCoords - gInputDragLast;
        int32_t targetWidth = _originalWindowWidth + differentialCoords.x - w->width;
        int32_t targetHeight = _originalWindowHeight + differentialCoords.y - w->height;

        window_resize(w, targetWidth, targetHeight);
    }
}

static void input_window_resize_end()
{
    _inputState = InputState::Normal;
    gTooltipTimeout = 0;
    gTooltipWidget = _dragWidget;
}

#pragma endregion

#pragma region Viewport dragging

static void input_viewport_drag_begin(rct_window* w)
{
    w->flags &= ~WF_SCROLLING_TO_LOCATION;
    _inputState = InputState::ViewportRight;
    _dragWidget.window_classification = w->classification;
    _dragWidget.window_number = w->number;
    _ticksSinceDragStart = 0;
    auto cursorPosition = context_get_cursor_position();
    gInputDragLast = cursorPosition;
    context_hide_cursor();

    window_unfollow_sprite(w);
    // gInputFlags |= INPUT_FLAG_5;
}

static void input_viewport_drag_continue()
{
    rct_window* w;
    rct_viewport* viewport;

    auto newDragCoords = context_get_cursor_position();
    const CursorState* cursorState = context_get_cursor_state();

    auto differentialCoords = newDragCoords - gInputDragLast;
    w = window_find_by_number(_dragWidget.window_classification, _dragWidget.window_number);

    // #3294: Window can be closed during a drag session, so just finish
    //        the session if the window no longer exists
    if (w == nullptr)
    {
        input_viewport_drag_end();
        return;
    }

    viewport = w->viewport;
    _ticksSinceDragStart += gCurrentDeltaTime;
    if (viewport == nullptr)
    {
        context_show_cursor();
        _inputState = InputState::Reset;
    }
    else if (differentialCoords.x != 0 || differentialCoords.y != 0)
    {
        if (!(w->flags & WF_NO_SCROLLING))
        {
            // User dragged a scrollable viewport

            // If the drag time is less than 500 the "drag" is usually interpreted as a right click.
            // As the user moved the mouse, don't interpret it as right click in any case.
            _ticksSinceDragStart = 1000;

            differentialCoords.x = differentialCoords.x * (viewport->zoom + 1);
            differentialCoords.y = differentialCoords.y * (viewport->zoom + 1);
            if (gConfigGeneral.invert_viewport_drag)
            {
                w->savedViewPos -= differentialCoords;
            }
            else
            {
                w->savedViewPos += differentialCoords;
            }
        }
    }

    if (cursorState->touch)
    {
        gInputDragLast = newDragCoords;
    }
    else
    {
        context_set_cursor_position(gInputDragLast);
    }
}

static void input_viewport_drag_end()
{
    _inputState = InputState::Reset;
    context_show_cursor();
}

#pragma endregion

#pragma region Scroll bars

static void input_scroll_begin(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords)
{
    rct_widget* widget;

    widget = &w->widgets[widgetIndex];

    _inputState = InputState::ScrollLeft;
    gPressedWidget.window_classification = w->classification;
    gPressedWidget.window_number = w->number;
    gPressedWidget.widget_index = widgetIndex;
    gTooltipCursor = screenCoords;

    int32_t scroll_area, scroll_id;
    ScreenCoordsXY scrollCoords;
    scroll_id = 0; // safety
    widget_scroll_get_part(w, widget, screenCoords, scrollCoords, &scroll_area, &scroll_id);

    _currentScrollArea = scroll_area;
    _currentScrollIndex = scroll_id;
    window_event_unknown_15_call(w, scroll_id, scroll_area);
    if (scroll_area == SCROLL_PART_VIEW)
    {
        window_event_scroll_mousedown_call(w, scroll_id, scrollCoords);
        return;
    }

    rct_widget* widg = &w->widgets[widgetIndex];
    rct_scroll* scroll = &w->scrolls[scroll_id];

    int32_t widget_width = widg->width() - 1;
    if (scroll->flags & VSCROLLBAR_VISIBLE)
        widget_width -= SCROLLBAR_WIDTH + 1;
    int32_t widget_content_width = std::max(scroll->h_right - widget_width, 0);

    int32_t widget_height = widg->bottom - widg->top - 1;
    if (scroll->flags & HSCROLLBAR_VISIBLE)
        widget_height -= SCROLLBAR_WIDTH + 1;
    int32_t widget_content_height = std::max(scroll->v_bottom - widget_height, 0);

    switch (scroll_area)
    {
        case SCROLL_PART_HSCROLLBAR_LEFT:
            scroll->h_left = std::max(scroll->h_left - 3, 0);
            break;
        case SCROLL_PART_HSCROLLBAR_RIGHT:
            scroll->h_left = std::min(scroll->h_left + 3, widget_content_width);
            break;
        case SCROLL_PART_HSCROLLBAR_LEFT_TROUGH:
            scroll->h_left = std::max(scroll->h_left - widget_width, 0);
            break;
        case SCROLL_PART_HSCROLLBAR_RIGHT_TROUGH:
            scroll->h_left = std::min(scroll->h_left + widget_width, widget_content_width);
            break;
        case SCROLL_PART_VSCROLLBAR_TOP:
            scroll->v_top = std::max(scroll->v_top - 3, 0);
            break;
        case SCROLL_PART_VSCROLLBAR_BOTTOM:
            scroll->v_top = std::min(scroll->v_top + 3, widget_content_height);
            break;
        case SCROLL_PART_VSCROLLBAR_TOP_TROUGH:
            scroll->v_top = std::max(scroll->v_top - widget_height, 0);
            break;
        case SCROLL_PART_VSCROLLBAR_BOTTOM_TROUGH:
            scroll->v_top = std::min(scroll->v_top + widget_height, widget_content_height);
            break;
        default:
            break;
    }
    widget_scroll_update_thumbs(w, widgetIndex);
    window_invalidate_by_number(widgetIndex, w->classification);
}

static void input_scroll_continue(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords)
{
    rct_widget* widget;
    int32_t scroll_part, scroll_id;

    assert(w != nullptr);

    widget = &w->widgets[widgetIndex];
    if (w->classification != gPressedWidget.window_classification || w->number != gPressedWidget.window_number
        || widgetIndex != gPressedWidget.widget_index)
    {
        invalidate_scroll();
        return;
    }

    ScreenCoordsXY newScreenCoords;
    widget_scroll_get_part(w, widget, screenCoords, newScreenCoords, &scroll_part, &scroll_id);

    if (_currentScrollArea == SCROLL_PART_HSCROLLBAR_THUMB)
    {
        int32_t originalTooltipCursorX = gTooltipCursor.x;
        gTooltipCursor.x = screenCoords.x;
        input_scroll_part_update_hthumb(w, widgetIndex, screenCoords.x - originalTooltipCursorX, scroll_id);
        return;
    }

    if (_currentScrollArea == SCROLL_PART_VSCROLLBAR_THUMB)
    {
        int32_t originalTooltipCursorY = gTooltipCursor.y;
        gTooltipCursor.y = screenCoords.y;
        input_scroll_part_update_vthumb(w, widgetIndex, screenCoords.y - originalTooltipCursorY, scroll_id);
        return;
    }

    if (scroll_part != _currentScrollArea)
    {
        invalidate_scroll();
        return;
    }

    switch (scroll_part)
    {
        case SCROLL_PART_VIEW:
            window_event_scroll_mousedrag_call(w, scroll_id, newScreenCoords);
            break;
        case SCROLL_PART_HSCROLLBAR_LEFT:
            input_scroll_part_update_hleft(w, widgetIndex, scroll_id);
            break;
        case SCROLL_PART_HSCROLLBAR_RIGHT:
            input_scroll_part_update_hright(w, widgetIndex, scroll_id);
            break;
        case SCROLL_PART_VSCROLLBAR_TOP:
            input_scroll_part_update_vtop(w, widgetIndex, scroll_id);
            break;
        case SCROLL_PART_VSCROLLBAR_BOTTOM:
            input_scroll_part_update_vbottom(w, widgetIndex, scroll_id);
            break;
    }
}

static void input_scroll_end()
{
    _inputState = InputState::Reset;
    invalidate_scroll();
}

/**
 *
 *  rct2: 0x006E98F2
 */
static void input_scroll_part_update_hthumb(rct_window* w, rct_widgetindex widgetIndex, int32_t x, int32_t scroll_id)
{
    rct_widget* widget = &w->widgets[widgetIndex];

    if (window_find_by_number(w->classification, w->number))
    {
        int32_t newLeft;
        newLeft = w->scrolls[scroll_id].h_right;
        newLeft *= x;
        x = widget->width() - 21;
        if (w->scrolls[scroll_id].flags & VSCROLLBAR_VISIBLE)
            x -= SCROLLBAR_WIDTH + 1;
        newLeft /= x;
        x = newLeft;
        w->scrolls[scroll_id].flags |= HSCROLLBAR_THUMB_PRESSED;
        newLeft = w->scrolls[scroll_id].h_left;
        newLeft += x;
        if (newLeft < 0)
            newLeft = 0;
        x = widget->width() - 1;
        if (w->scrolls[scroll_id].flags & VSCROLLBAR_VISIBLE)
            x -= SCROLLBAR_WIDTH + 1;
        x *= -1;
        x += w->scrolls[scroll_id].h_right;
        if (x < 0)
            x = 0;
        if (newLeft > x)
            newLeft = x;
        w->scrolls[scroll_id].h_left = newLeft;
        widget_scroll_update_thumbs(w, widgetIndex);
        widget_invalidate_by_number(w->classification, w->number, widgetIndex);
    }
}

/**
 *
 *  rct2: 0x006E99A9
 */
static void input_scroll_part_update_vthumb(rct_window* w, rct_widgetindex widgetIndex, int32_t y, int32_t scroll_id)
{
    assert(w != nullptr);
    rct_widget* widget = &w->widgets[widgetIndex];

    if (window_find_by_number(w->classification, w->number))
    {
        int32_t newTop;
        newTop = w->scrolls[scroll_id].v_bottom;
        newTop *= y;
        y = widget->height() - 21;
        if (w->scrolls[scroll_id].flags & HSCROLLBAR_VISIBLE)
            y -= SCROLLBAR_WIDTH + 1;
        newTop /= y;
        y = newTop;
        w->scrolls[scroll_id].flags |= VSCROLLBAR_THUMB_PRESSED;
        newTop = w->scrolls[scroll_id].v_top;
        newTop += y;
        if (newTop < 0)
            newTop = 0;
        y = widget->height() - 1;
        if (w->scrolls[scroll_id].flags & HSCROLLBAR_VISIBLE)
            y -= SCROLLBAR_WIDTH + 1;
        y *= -1;
        y += w->scrolls[scroll_id].v_bottom;
        if (y < 0)
            y = 0;
        if (newTop > y)
            newTop = y;
        w->scrolls[scroll_id].v_top = newTop;
        widget_scroll_update_thumbs(w, widgetIndex);
        widget_invalidate_by_number(w->classification, w->number, widgetIndex);
    }
}

/**
 *
 *  rct2: 0x006E9A60
 */
static void input_scroll_part_update_hleft(rct_window* w, rct_widgetindex widgetIndex, int32_t scroll_id)
{
    assert(w != nullptr);
    if (window_find_by_number(w->classification, w->number))
    {
        w->scrolls[scroll_id].flags |= HSCROLLBAR_LEFT_PRESSED;
        if (w->scrolls[scroll_id].h_left >= 3)
            w->scrolls[scroll_id].h_left -= 3;
        widget_scroll_update_thumbs(w, widgetIndex);
        widget_invalidate_by_number(w->classification, w->number, widgetIndex);
    }
}

/**
 *
 *  rct2: 0x006E9ABF
 */
static void input_scroll_part_update_hright(rct_window* w, rct_widgetindex widgetIndex, int32_t scroll_id)
{
    assert(w != nullptr);
    rct_widget* widget = &w->widgets[widgetIndex];
    if (window_find_by_number(w->classification, w->number))
    {
        w->scrolls[scroll_id].flags |= HSCROLLBAR_RIGHT_PRESSED;
        w->scrolls[scroll_id].h_left += 3;
        int32_t newLeft = widget->width() - 1;
        if (w->scrolls[scroll_id].flags & VSCROLLBAR_VISIBLE)
            newLeft -= SCROLLBAR_WIDTH + 1;
        newLeft *= -1;
        newLeft += w->scrolls[scroll_id].h_right;
        if (newLeft < 0)
            newLeft = 0;
        if (w->scrolls[scroll_id].h_left > newLeft)
            w->scrolls[scroll_id].h_left = newLeft;
        widget_scroll_update_thumbs(w, widgetIndex);
        widget_invalidate_by_number(w->classification, w->number, widgetIndex);
    }
}

/**
 *
 *  rct2: 0x006E9C37
 */
static void input_scroll_part_update_vtop(rct_window* w, rct_widgetindex widgetIndex, int32_t scroll_id)
{
    assert(w != nullptr);
    if (window_find_by_number(w->classification, w->number))
    {
        w->scrolls[scroll_id].flags |= VSCROLLBAR_UP_PRESSED;
        if (w->scrolls[scroll_id].v_top >= 3)
            w->scrolls[scroll_id].v_top -= 3;
        widget_scroll_update_thumbs(w, widgetIndex);
        widget_invalidate_by_number(w->classification, w->number, widgetIndex);
    }
}

/**
 *
 *  rct2: 0x006E9C96
 */
static void input_scroll_part_update_vbottom(rct_window* w, rct_widgetindex widgetIndex, int32_t scroll_id)
{
    assert(w != nullptr);
    rct_widget* widget = &w->widgets[widgetIndex];
    if (window_find_by_number(w->classification, w->number))
    {
        w->scrolls[scroll_id].flags |= VSCROLLBAR_DOWN_PRESSED;
        w->scrolls[scroll_id].v_top += 3;
        int32_t newTop = widget->height() - 1;
        if (w->scrolls[scroll_id].flags & HSCROLLBAR_VISIBLE)
            newTop -= SCROLLBAR_WIDTH + 1;
        newTop *= -1;
        newTop += w->scrolls[scroll_id].v_bottom;
        if (newTop < 0)
            newTop = 0;
        if (w->scrolls[scroll_id].v_top > newTop)
            w->scrolls[scroll_id].v_top = newTop;
        widget_scroll_update_thumbs(w, widgetIndex);
        widget_invalidate_by_number(w->classification, w->number, widgetIndex);
    }
}

#pragma endregion

#pragma region Widgets

/**
 *
 *  rct2: 0x006E9253
 */
static void input_widget_over(const ScreenCoordsXY& screenCoords, rct_window* w, rct_widgetindex widgetIndex)
{
    rct_windowclass windowClass = WC_NULL;
    rct_windownumber windowNumber = 0;
    rct_widget* widget = nullptr;

    if (w != nullptr)
    {
        windowClass = w->classification;
        windowNumber = w->number;
        widget = &w->widgets[widgetIndex];
    }

    input_widget_over_change_check(windowClass, windowNumber, widgetIndex);

    if (w != nullptr && widgetIndex != -1 && widget->type == WWT_SCROLL)
    {
        int32_t scroll_part, scrollId;
        ScreenCoordsXY newScreenCoords;
        widget_scroll_get_part(w, widget, screenCoords, newScreenCoords, &scroll_part, &scrollId);

        if (scroll_part != SCROLL_PART_VIEW)
            window_tooltip_close();
        else
        {
            window_event_scroll_mouseover_call(w, scrollId, newScreenCoords);
            input_update_tooltip(w, widgetIndex, screenCoords);
        }
    }
    else
    {
        input_update_tooltip(w, widgetIndex, screenCoords);
    }
}

/**
 *
 *  rct2: 0x006E9269
 */
static void input_widget_over_change_check(
    rct_windowclass windowClass, rct_windownumber windowNumber, rct_widgetindex widgetIndex)
{
    // Prevents invalid widgets being clicked source of bug is elsewhere
    if (widgetIndex == -1)
        return;

    // Check if the widget that the cursor was over, has changed
    if (windowClass != gHoverWidget.window_classification || windowNumber != gHoverWidget.window_number
        || widgetIndex != gHoverWidget.widget_index)
    {
        // Invalidate last widget cursor was on if widget is a flat button
        input_widget_over_flatbutton_invalidate();

        // Set new cursor over widget
        gHoverWidget.window_classification = windowClass;
        gHoverWidget.window_number = windowNumber;
        gHoverWidget.widget_index = widgetIndex;

        // Invalidate new widget cursor is on if widget is a flat button
        if (windowClass != 255)
            input_widget_over_flatbutton_invalidate();
    }
}

/**
 * Used to invalidate flat button widgets when the mouse leaves and enters them. This should be generalised so that all widgets
 * can use this in the future.
 */
static void input_widget_over_flatbutton_invalidate()
{
    rct_window* w = window_find_by_number(gHoverWidget.window_classification, gHoverWidget.window_number);
    if (w != nullptr)
    {
        window_event_invalidate_call(w);
        if (w->widgets[gHoverWidget.widget_index].type == WWT_FLATBTN)
        {
            widget_invalidate_by_number(
                gHoverWidget.window_classification, gHoverWidget.window_number, gHoverWidget.widget_index);
        }
    }
}

/**
 *
 *  rct2: 0x006E95F9
 */
static void input_widget_left(const ScreenCoordsXY& screenCoords, rct_window* w, rct_widgetindex widgetIndex)
{
    rct_windowclass windowClass = WC_NULL;
    rct_windownumber windowNumber = 0;
    rct_widget* widget;

    if (w != nullptr)
    {
        windowClass = w->classification;
        windowNumber = w->number;
    }

    window_close_by_class(WC_ERROR);
    window_close_by_class(WC_TOOLTIP);

    // Window might have changed position in the list, therefore find it again
    w = window_find_by_number(windowClass, windowNumber);
    if (w == nullptr)
        return;

    w = window_bring_to_front(w);
    if (widgetIndex == -1)
        return;

    if (windowClass != gCurrentTextBox.window.classification || windowNumber != gCurrentTextBox.window.number
        || widgetIndex != gCurrentTextBox.widget_index)
    {
        window_cancel_textbox();
    }

    widget = &w->widgets[widgetIndex];

    switch (widget->type)
    {
        case WWT_FRAME:
        case WWT_RESIZE:
            if (window_can_resize(w)
                && (screenCoords.x >= w->windowPos.x + w->width - 19 && screenCoords.y >= w->windowPos.y + w->height - 19))
                input_window_resize_begin(w, widgetIndex, screenCoords);
            break;
        case WWT_VIEWPORT:
            _inputState = InputState::ViewportLeft;
            gInputDragLast = screenCoords;
            _dragWidget.window_classification = windowClass;
            _dragWidget.window_number = windowNumber;
            if (_inputFlags & INPUT_FLAG_TOOL_ACTIVE)
            {
                w = window_find_by_number(gCurrentToolWidget.window_classification, gCurrentToolWidget.window_number);
                if (w != nullptr)
                {
                    window_event_tool_down_call(w, gCurrentToolWidget.widget_index, screenCoords);
                    _inputFlags |= INPUT_FLAG_4;
                }
            }
            break;
        case WWT_CAPTION:
            input_window_position_begin(w, widgetIndex, screenCoords);
            break;
        case WWT_SCROLL:
            input_scroll_begin(w, widgetIndex, screenCoords);
            break;
        default:
            if (widget_is_enabled(w, widgetIndex) && !widget_is_disabled(w, widgetIndex))
            {
                OpenRCT2::Audio::Play(OpenRCT2::Audio::SoundId::Click1, 0, w->windowPos.x + widget->midX());

                // Set new cursor down widget
                gPressedWidget.window_classification = windowClass;
                gPressedWidget.window_number = windowNumber;
                gPressedWidget.widget_index = widgetIndex;
                _inputFlags |= INPUT_FLAG_WIDGET_PRESSED;
                _inputState = InputState::WidgetPressed;
                _clickRepeatTicks = 1;

                widget_invalidate_by_number(windowClass, windowNumber, widgetIndex);
                window_event_mouse_down_call(w, widgetIndex);
            }
            break;
    }
}

#pragma endregion

/**
 *
 *  rct2: 0x006ED833
 */
void process_mouse_over(const ScreenCoordsXY& screenCoords)
{
    rct_window* window;

    CursorID cursorId = CursorID::Arrow;
    auto ft = Formatter();
    ft.Add<rct_string_id>(STR_NONE);
    SetMapTooltip(ft);
    window = window_find_from_point(screenCoords);

    if (window != nullptr)
    {
        rct_widgetindex widgetId = window_find_widget_from_point(window, screenCoords);
        if (widgetId != -1)
        {
            switch (window->widgets[widgetId].type)
            {
                case WWT_VIEWPORT:
                    if (!(_inputFlags & INPUT_FLAG_TOOL_ACTIVE))
                    {
                        if (viewport_interaction_left_over(screenCoords))
                        {
                            set_cursor(CursorID::HandPoint);
                            return;
                        }
                        break;
                    }
                    cursorId = static_cast<CursorID>(gCurrentToolId);
                    break;

                case WWT_FRAME:
                case WWT_RESIZE:
                    if (!(window->flags & WF_RESIZABLE))
                        break;

                    if (window->min_width == window->max_width && window->min_height == window->max_height)
                        break;

                    if (screenCoords.x < window->windowPos.x + window->width - 0x13)
                        break;

                    if (screenCoords.y < window->windowPos.y + window->height - 0x13)
                        break;

                    cursorId = CursorID::DiagonalArrows;
                    break;

                case WWT_SCROLL:
                {
                    int32_t output_scroll_area, scroll_id;
                    ScreenCoordsXY scrollCoords;
                    widget_scroll_get_part(
                        window, &window->widgets[widgetId], screenCoords, scrollCoords, &output_scroll_area, &scroll_id);
                    if (output_scroll_area != SCROLL_PART_VIEW)
                    {
                        cursorId = CursorID::Arrow;
                        break;
                    }
                    // Same as default but with scroll_x/y
                    cursorId = window_event_cursor_call(window, widgetId, scrollCoords);
                    if (cursorId == CursorID::Undefined)
                        cursorId = CursorID::Arrow;
                    break;
                }
                default:
                    cursorId = window_event_cursor_call(window, widgetId, screenCoords);
                    if (cursorId == CursorID::Undefined)
                        cursorId = CursorID::Arrow;
                    break;
            }
        }
    }

    viewport_interaction_right_over(screenCoords);
    set_cursor(cursorId);
}

/**
 *
 *  rct2: 0x006ED801
 */
void process_mouse_tool(const ScreenCoordsXY& screenCoords)
{
    if (_inputFlags & INPUT_FLAG_TOOL_ACTIVE)
    {
        rct_window* w = window_find_by_number(gCurrentToolWidget.window_classification, gCurrentToolWidget.window_number);

        if (!w)
            tool_cancel();
        else
            window_event_tool_update_call(w, gCurrentToolWidget.widget_index, screenCoords);
    }
}

/**
 *
 *  rct2: 0x006E8DA7
 */
void input_state_widget_pressed(
    const ScreenCoordsXY& screenCoords, int32_t state, rct_widgetindex widgetIndex, rct_window* w, rct_widget* widget)
{
    rct_windowclass cursor_w_class;
    rct_windownumber cursor_w_number;
    cursor_w_class = gPressedWidget.window_classification;
    cursor_w_number = gPressedWidget.window_number;
    rct_widgetindex cursor_widgetIndex = gPressedWidget.widget_index;

    rct_window* cursor_w = window_find_by_number(cursor_w_class, cursor_w_number);
    if (cursor_w == nullptr)
    {
        _inputState = InputState::Reset;
        return;
    }

    switch (state)
    {
        case MOUSE_STATE_RELEASED:
            if (!w || cursor_w_class != w->classification || cursor_w_number != w->number || widgetIndex != cursor_widgetIndex)
                break;

            if (w->disabled_widgets & (1ULL << widgetIndex))
                break;

            if (_clickRepeatTicks != 0)
            {
                _clickRepeatTicks++;

                // Handle click repeat
                if (_clickRepeatTicks >= 16 && (_clickRepeatTicks & 3) == 0)
                {
                    if (w->hold_down_widgets & (1ULL << widgetIndex))
                    {
                        window_event_mouse_down_call(w, widgetIndex);
                    }
                }
            }

            if (_inputFlags & INPUT_FLAG_WIDGET_PRESSED)
            {
                if (_inputState == InputState::DropdownActive)
                {
                    gDropdownHighlightedIndex = gDropdownDefaultIndex;
                    window_invalidate_by_class(WC_DROPDOWN);
                }
                return;
            }

            _inputFlags |= INPUT_FLAG_WIDGET_PRESSED;
            widget_invalidate_by_number(cursor_w_class, cursor_w_number, widgetIndex);
            return;
        case MOUSE_STATE_LEFT_RELEASE:
        case MOUSE_STATE_RIGHT_PRESS:
            if (_inputState == InputState::DropdownActive)
            {
                if (w)
                {
                    auto wClass = w->classification;
                    auto wNumber = w->number;
                    int32_t dropdown_index = 0;
                    bool dropdownCleanup = false;

                    if (w->classification == WC_DROPDOWN)
                    {
                        dropdown_index = dropdown_index_from_point(screenCoords, w);
                        dropdownCleanup = dropdown_index == -1
                            || (dropdown_index < DROPDOWN_ITEMS_MAX_SIZE && dropdown_is_disabled(dropdown_index))
                            || gDropdownItemsFormat[dropdown_index] == DROPDOWN_SEPARATOR;
                        w = nullptr; // To be closed right next
                    }
                    else
                    {
                        if (cursor_w_class != w->classification || cursor_w_number != w->number
                            || widgetIndex != cursor_widgetIndex)
                        {
                            dropdownCleanup = true;
                        }
                        else
                        {
                            dropdown_index = -1;
                            if (_inputFlags & INPUT_FLAG_DROPDOWN_STAY_OPEN)
                            {
                                if (!(_inputFlags & INPUT_FLAG_DROPDOWN_MOUSE_UP))
                                {
                                    _inputFlags |= INPUT_FLAG_DROPDOWN_MOUSE_UP;
                                    return;
                                }
                            }
                        }
                    }

                    window_close_by_class(WC_DROPDOWN);

                    if (dropdownCleanup)
                    {
                        // Update w as it will be invalid after closing the dropdown window
                        w = window_find_by_number(wClass, wNumber);
                    }
                    else
                    {
                        cursor_w = window_find_by_number(cursor_w_class, cursor_w_number);
                        if (_inputFlags & INPUT_FLAG_WIDGET_PRESSED)
                        {
                            _inputFlags &= ~INPUT_FLAG_WIDGET_PRESSED;
                            widget_invalidate_by_number(cursor_w_class, cursor_w_number, cursor_widgetIndex);
                        }

                        _inputState = InputState::Normal;
                        gTooltipTimeout = 0;
                        gTooltipWidget.widget_index = cursor_widgetIndex;
                        gTooltipWidget.window_classification = cursor_w_class;
                        gTooltipWidget.window_number = cursor_w_number;

                        if (dropdown_index == -1)
                        {
                            if (!dropdown_is_disabled(gDropdownDefaultIndex))
                            {
                                dropdown_index = gDropdownDefaultIndex;
                            }
                        }
                        window_event_dropdown_call(cursor_w, cursor_widgetIndex, dropdown_index);
                    }
                }
            }

            _inputState = InputState::Normal;

            if (state == MOUSE_STATE_RIGHT_PRESS)
            {
                return;
            }

            gTooltipTimeout = 0;
            gTooltipWidget.widget_index = cursor_widgetIndex;

            if (!w)
                break;

            if (!widget)
                break;

            {
                int32_t mid_point_x = widget->midX() + w->windowPos.x;
                OpenRCT2::Audio::Play(OpenRCT2::Audio::SoundId::Click2, 0, mid_point_x);
            }
            if (cursor_w_class != w->classification || cursor_w_number != w->number || widgetIndex != cursor_widgetIndex)
                break;

            if (w->disabled_widgets & (1ULL << widgetIndex))
                break;

            widget_invalidate_by_number(cursor_w_class, cursor_w_number, widgetIndex);
            window_event_mouse_up_call(w, widgetIndex);
            return;

        default:
            return;
    }

    _clickRepeatTicks = 0;
    if (_inputState != InputState::DropdownActive)
    {
        // Hold down widget and drag outside of area??
        if (_inputFlags & INPUT_FLAG_WIDGET_PRESSED)
        {
            _inputFlags &= ~INPUT_FLAG_WIDGET_PRESSED;
            widget_invalidate_by_number(cursor_w_class, cursor_w_number, cursor_widgetIndex);
        }
        return;
    }

    gDropdownHighlightedIndex = -1;
    window_invalidate_by_class(WC_DROPDOWN);
    if (w == nullptr)
    {
        return;
    }

    if (w->classification == WC_DROPDOWN)
    {
        int32_t dropdown_index = dropdown_index_from_point(screenCoords, w);
        if (dropdown_index == -1)
        {
            return;
        }

        if (gDropdownIsColour && gDropdownLastColourHover != dropdown_index)
        {
            gDropdownLastColourHover = dropdown_index;
            window_tooltip_close();

            static constexpr const rct_string_id colourTooltips[] = {
                STR_COLOUR_BLACK_TIP,
                STR_COLOUR_GREY_TIP,
                STR_COLOUR_WHITE_TIP,
                STR_COLOUR_DARK_PURPLE_TIP,
                STR_COLOUR_LIGHT_PURPLE_TIP,
                STR_COLOUR_BRIGHT_PURPLE_TIP,
                STR_COLOUR_DARK_BLUE_TIP,
                STR_COLOUR_LIGHT_BLUE_TIP,
                STR_COLOUR_ICY_BLUE_TIP,
                STR_COLOUR_TEAL_TIP,
                STR_COLOUR_AQUAMARINE_TIP,
                STR_COLOUR_SATURATED_GREEN_TIP,
                STR_COLOUR_DARK_GREEN_TIP,
                STR_COLOUR_MOSS_GREEN_TIP,
                STR_COLOUR_BRIGHT_GREEN_TIP,
                STR_COLOUR_OLIVE_GREEN_TIP,
                STR_COLOUR_DARK_OLIVE_GREEN_TIP,
                STR_COLOUR_BRIGHT_YELLOW_TIP,
                STR_COLOUR_YELLOW_TIP,
                STR_COLOUR_DARK_YELLOW_TIP,
                STR_COLOUR_LIGHT_ORANGE_TIP,
                STR_COLOUR_DARK_ORANGE_TIP,
                STR_COLOUR_LIGHT_BROWN_TIP,
                STR_COLOUR_SATURATED_BROWN_TIP,
                STR_COLOUR_DARK_BROWN_TIP,
                STR_COLOUR_SALMON_PINK_TIP,
                STR_COLOUR_BORDEAUX_RED_TIP,
                STR_COLOUR_SATURATED_RED_TIP,
                STR_COLOUR_BRIGHT_RED_TIP,
                STR_COLOUR_DARK_PINK_TIP,
                STR_COLOUR_BRIGHT_PINK_TIP,
                STR_COLOUR_LIGHT_PINK_TIP,
            };
            window_tooltip_show(OpenRCT2String{ colourTooltips[dropdown_index], {} }, screenCoords);
        }

        if (dropdown_index < DROPDOWN_ITEMS_MAX_SIZE && dropdown_is_disabled(dropdown_index))
        {
            return;
        }

        if (gDropdownItemsFormat[dropdown_index] == DROPDOWN_SEPARATOR)
        {
            return;
        }

        gDropdownHighlightedIndex = dropdown_index;
        window_invalidate_by_class(WC_DROPDOWN);
    }
    else
    {
        gDropdownLastColourHover = -1;
        window_tooltip_close();
    }
}

static void input_update_tooltip(rct_window* w, rct_widgetindex widgetIndex, const ScreenCoordsXY& screenCoords)
{
    if (gTooltipWidget.window_classification == 255)
    {
        if (gTooltipCursor == screenCoords)
        {
            _tooltipNotShownTicks++;
            if (_tooltipNotShownTicks > 50)
            {
                gTooltipTimeout = 0;
                window_tooltip_open(w, widgetIndex, screenCoords);
            }
        }

        gTooltipTimeout = 0;
        gTooltipCursor = screenCoords;
    }
    else
    {
        reset_tooltip_not_shown();

        if (w == nullptr || gTooltipWidget.window_classification != w->classification
            || gTooltipWidget.window_number != w->number || gTooltipWidget.widget_index != widgetIndex)
        {
            window_tooltip_close();
        }

        gTooltipTimeout += gCurrentDeltaTime;
        if (gTooltipTimeout >= 8000)
        {
            window_close_by_class(WC_TOOLTIP);
        }
    }
}

#pragma endregion

#pragma region Keyboard input

/**
 *
 *  rct2: 0x00406CD2
 */
int32_t get_next_key()
{
    uint8_t* keysPressed = const_cast<uint8_t*>(context_get_keys_pressed());
    for (int32_t i = 0; i < 221; i++)
    {
        if (keysPressed[i])
        {
            keysPressed[i] = 0;
            return i;
        }
    }

    return 0;
}

#pragma endregion

/**
 *
 *  rct2: 0x006ED990
 */
void set_cursor(CursorID cursor_id)
{
    assert(cursor_id != CursorID::Undefined);
    if (_inputState == InputState::Resizing)
    {
        cursor_id = CursorID::DiagonalArrows;
    }
    context_setcurrentcursor(cursor_id);
}

/**
 *
 *  rct2: 0x006E876D
 */
void invalidate_scroll()
{
    rct_window* w = window_find_by_number(gPressedWidget.window_classification, gPressedWidget.window_number);
    if (w != nullptr)
    {
        // Reset to basic scroll
        w->scrolls[_currentScrollIndex].flags &= 0xFF11;
        window_invalidate_by_number(gPressedWidget.window_classification, gPressedWidget.window_number);
    }
}

/**
 * rct2: 0x00406C96
 */
void store_mouse_input(int32_t state, const ScreenCoordsXY& screenCoords)
{
    uint32_t writeIndex = _mouseInputQueueWriteIndex;
    uint32_t nextWriteIndex = (writeIndex + 1) % std::size(_mouseInputQueue);

    // Check if the queue is full
    if (nextWriteIndex != _mouseInputQueueReadIndex)
    {
        rct_mouse_data* item = &_mouseInputQueue[writeIndex];
        item->x = screenCoords.x;
        item->y = screenCoords.y;
        item->state = state;

        _mouseInputQueueWriteIndex = nextWriteIndex;
    }
}

void game_handle_edge_scroll()
{
    rct_window* mainWindow;
    int32_t scrollX, scrollY;

    mainWindow = window_get_main();
    if (mainWindow == nullptr)
        return;
    if ((mainWindow->flags & WF_NO_SCROLLING) || (gScreenFlags & (SCREEN_FLAGS_TRACK_MANAGER | SCREEN_FLAGS_TITLE_DEMO)))
        return;
    if (mainWindow->viewport == nullptr)
        return;
    if (!context_has_focus())
        return;

    scrollX = 0;
    scrollY = 0;

    // Scroll left / right
    const CursorState* cursorState = context_get_cursor_state();
    if (cursorState->position.x == 0)
        scrollX = -1;
    else if (cursorState->position.x >= context_get_width() - 1)
        scrollX = 1;

    // Scroll up / down
    if (cursorState->position.y == 0)
        scrollY = -1;
    else if (cursorState->position.y >= context_get_height() - 1)
        scrollY = 1;

    input_scroll_viewport(ScreenCoordsXY(scrollX, scrollY));
}

bool input_test_place_object_modifier(PLACE_OBJECT_MODIFIER modifier)
{
    return gInputPlaceObjectModifier & modifier;
}

void input_scroll_viewport(const ScreenCoordsXY& scrollScreenCoords)
{
    rct_window* mainWindow = window_get_main();
    rct_viewport* viewport = mainWindow->viewport;

    const int32_t speed = gConfigGeneral.edge_scrolling_speed;

    int32_t multiplier = speed * viewport->zoom;
    int32_t dx = scrollScreenCoords.x * multiplier;
    int32_t dy = scrollScreenCoords.y * multiplier;

    if (scrollScreenCoords.x != 0)
    {
        // Speed up scrolling horizontally when at the edge of the map
        // so that the speed is consistent with vertical edge scrolling.
        int32_t x = mainWindow->savedViewPos.x + viewport->view_width / 2 + dx;
        int32_t y = mainWindow->savedViewPos.y + viewport->view_height / 2;
        int32_t y_dy = mainWindow->savedViewPos.y + viewport->view_height / 2 + dy;

        auto mapCoord = viewport_coord_to_map_coord({ x, y }, 0);
        auto mapCoord_dy = viewport_coord_to_map_coord({ x, y_dy }, 0);

        // Check if we're crossing the boundary
        // Clamp to the map minimum value
        int32_t at_map_edge = 0;
        int32_t at_map_edge_dy = 0;
        if (mapCoord.x < MAP_MINIMUM_X_Y || mapCoord.y < MAP_MINIMUM_X_Y)
        {
            at_map_edge = 1;
        }
        if (mapCoord_dy.x < MAP_MINIMUM_X_Y || mapCoord_dy.y < MAP_MINIMUM_X_Y)
        {
            at_map_edge_dy = 1;
        }

        // Clamp to the map maximum value (scenario specific)
        if (mapCoord.x > gMapSizeMinus2 || mapCoord.y > gMapSizeMinus2)
        {
            at_map_edge = 1;
        }
        if (mapCoord_dy.x > gMapSizeMinus2 || mapCoord_dy.y > gMapSizeMinus2)
        {
            at_map_edge_dy = 1;
        }

        // If we crossed the boundary, multiply the distance by 2
        if (at_map_edge && at_map_edge_dy)
        {
            dx *= 2;
        }

        mainWindow->savedViewPos.x += dx;
        _inputFlags |= INPUT_FLAG_VIEWPORT_SCROLLING;
    }
    if (scrollScreenCoords.y != 0)
    {
        mainWindow->savedViewPos.y += dy;
        _inputFlags |= INPUT_FLAG_VIEWPORT_SCROLLING;
    }
}
