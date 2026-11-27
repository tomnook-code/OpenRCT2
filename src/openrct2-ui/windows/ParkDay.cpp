/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Fork: settings and status window for the realistic park day cycle.
// Strings are plain English on purpose, so no language files need to change.

#include <cstdio>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/widget/CheckboxWidget.h>
#include <openrct2-ui/windows/Windows.h>
#include <openrct2/GameState.h>
#include <openrct2/actions/GameActionRunner.h>
#include <openrct2/actions/park/ParkDaySetAction.h>
#include <openrct2/drawing/Text.h>
#include <openrct2/park/ParkDay.h>
#include <openrct2/ui/WindowManager.h>
#include <string>

namespace OpenRCT2::Ui::Windows
{
    static constexpr ScreenSize kWindowSize = { 260, 230 };
    static constexpr int32_t kStepMinutes = 30;

    enum WindowParkDayWidgetIdx : WidgetIndex
    {
        WIDX_BACKGROUND,
        WIDX_TITLE,
        WIDX_CLOSE,
        WIDX_ENABLED,
        WIDX_OPEN,
        WIDX_OPEN_UP,
        WIDX_OPEN_DOWN,
        WIDX_LAST_ENTRY,
        WIDX_LAST_ENTRY_UP,
        WIDX_LAST_ENTRY_DOWN,
        WIDX_CLOSE_TIME,
        WIDX_CLOSE_TIME_UP,
        WIDX_CLOSE_TIME_DOWN,
        WIDX_DAY_LENGTH,
        WIDX_AUTO_SKIP,
        WIDX_SKIP_NIGHT,
    };

    // clang-format off
    static constexpr auto kParkDayWidgets = makeWidgets(
        makeWindowShim(kStringIdNone, kWindowSize),
        Widgets::Checkbox         ({ 10,  20}, {240, 12},                      WindowColour::secondary),
        makeSpinnerWidgets        ({150,  38}, {100, 12}, WidgetType::spinner, WindowColour::secondary), // gates open (3 widgets)
        makeSpinnerWidgets        ({150,  54}, {100, 12}, WidgetType::spinner, WindowColour::secondary), // last entry (3 widgets)
        makeSpinnerWidgets        ({150,  70}, {100, 12}, WidgetType::spinner, WindowColour::secondary), // park closes (3 widgets)
        makeWidget                ({150,  86}, {100, 12}, WidgetType::button,  WindowColour::secondary), // day length
        Widgets::Checkbox         ({ 10, 104}, {240, 12},                      WindowColour::secondary),
        makeWidget                ({ 10, 120}, {240, 14}, WidgetType::button,  WindowColour::secondary)
    );
    // clang-format on

    class ParkDayWindow final : public Window
    {
        std::string _openText;
        std::string _lastEntryText;
        std::string _closeText;
        std::string _dayLengthText;

        void setParameter(ParkDay::Parameter parameter, uint32_t value)
        {
            auto action = GameActions::ParkDaySetAction(parameter, value);
            GameActions::Execute(&action, getGameState());
        }

        void stepTime(ParkDay::Parameter parameter, int32_t current, int32_t delta)
        {
            setParameter(parameter, static_cast<uint32_t>(current + delta));
        }

    public:
        void onOpen() override
        {
            setWidgets(kParkDayWidgets);
            widgets[WIDX_TITLE].setString("Park day cycle");
            widgets[WIDX_ENABLED].setString("Realistic day cycle (gate admission, free rides)");
            widgets[WIDX_AUTO_SKIP].setString("Skip nights automatically");
            widgets[WIDX_SKIP_NIGHT].setString("Skip to tomorrow morning");
            WindowInitScrollWidgets(*this);
        }

        void onUpdate() override
        {
            invalidate();
        }

        void onMouseUp(WidgetIndex widgetIndex) override
        {
            const auto& st = getGameState().parkDay;
            switch (widgetIndex)
            {
                case WIDX_CLOSE:
                    close();
                    break;
                case WIDX_ENABLED:
                    setParameter(ParkDay::Parameter::enabled, st.settings.enabled ? 0 : 1);
                    break;
                case WIDX_AUTO_SKIP:
                    setParameter(ParkDay::Parameter::autoSkipNight, st.settings.autoSkipNight ? 0 : 1);
                    break;
                case WIDX_SKIP_NIGHT:
                    setParameter(ParkDay::Parameter::skipTonight, st.skipTonight ? 0 : 1);
                    break;
                case WIDX_DAY_LENGTH:
                {
                    setParameter(ParkDay::Parameter::dayLength, ParkDay::GetNextDayLength(st.settings.dayLength));
                    break;
                }
            }
        }

        void onMouseDown(WidgetIndex widgetIndex) override
        {
            const auto& s = getGameState().parkDay.settings;
            switch (widgetIndex)
            {
                case WIDX_OPEN_UP:
                    stepTime(ParkDay::Parameter::openMinute, s.openMinute, kStepMinutes);
                    break;
                case WIDX_OPEN_DOWN:
                    stepTime(ParkDay::Parameter::openMinute, s.openMinute, -kStepMinutes);
                    break;
                case WIDX_LAST_ENTRY_UP:
                    stepTime(ParkDay::Parameter::lastEntryMinute, s.lastEntryMinute, kStepMinutes);
                    break;
                case WIDX_LAST_ENTRY_DOWN:
                    stepTime(ParkDay::Parameter::lastEntryMinute, s.lastEntryMinute, -kStepMinutes);
                    break;
                case WIDX_CLOSE_TIME_UP:
                    stepTime(ParkDay::Parameter::closeMinute, s.closeMinute, kStepMinutes);
                    break;
                case WIDX_CLOSE_TIME_DOWN:
                    stepTime(ParkDay::Parameter::closeMinute, s.closeMinute, -kStepMinutes);
                    break;
            }
        }

        void onPrepareDraw() override
        {
            const auto& gameState = getGameState();
            const auto& st = gameState.parkDay;
            const bool enabled = st.settings.enabled;

            setCheckboxValue(WIDX_ENABLED, enabled);
            setCheckboxValue(WIDX_AUTO_SKIP, st.settings.autoSkipNight);

            _openText = ParkDay::FormatClock(st.settings.openMinute);
            _lastEntryText = ParkDay::FormatClock(st.settings.lastEntryMinute);
            _closeText = ParkDay::FormatClock(st.settings.closeMinute);
            widgets[WIDX_OPEN].setString(_openText.c_str());
            widgets[WIDX_LAST_ENTRY].setString(_lastEntryText.c_str());
            widgets[WIDX_CLOSE_TIME].setString(_closeText.c_str());
            _dayLengthText = ParkDay::GetDayLengthName(st.settings.dayLength);
            widgets[WIDX_DAY_LENGTH].setString(_dayLengthText.c_str());

            widgets[WIDX_SKIP_NIGHT].setString(st.skipTonight ? "Cancel skipping tonight" : "Skip to tomorrow morning");

            for (auto idx : { WIDX_OPEN, WIDX_OPEN_UP, WIDX_OPEN_DOWN, WIDX_LAST_ENTRY, WIDX_LAST_ENTRY_UP,
                              WIDX_LAST_ENTRY_DOWN, WIDX_CLOSE_TIME, WIDX_CLOSE_TIME_UP, WIDX_CLOSE_TIME_DOWN, WIDX_DAY_LENGTH,
                              WIDX_AUTO_SKIP, WIDX_SKIP_NIGHT })
            {
                setWidgetDisabled(idx, !enabled);
            }
        }

        void onDraw(Drawing::RenderTarget& rt) override
        {
            drawWidgets(rt);

            const auto& gameState = getGameState();
            const auto& st = gameState.parkDay;
            const auto textColour = colours[1];

            auto label = [&](int32_t y, const char* text) {
                drawText(rt, windowPos + ScreenCoordsXY{ 10, y }, text, { textColour });
            };
            label(widgets[WIDX_OPEN].top + 1, "Gates open");
            label(widgets[WIDX_LAST_ENTRY].top + 1, "Last entry");
            label(widgets[WIDX_CLOSE_TIME].top + 1, "Park closes");
            label(widgets[WIDX_DAY_LENGTH].top + 1, "Day length at 1x speed");

            int32_t y = widgets[WIDX_SKIP_NIGHT].bottom + 8;
            constexpr int32_t kLineHeight = 12;
            char buffer[256];

            if (!st.settings.enabled)
            {
                label(y, "Off: the park behaves like normal OpenRCT2.");
                return;
            }

            const char* gateState = ParkDay::IsNight(gameState) ? "night" : ParkDay::IsGateOpen(gameState) ? "open" : "closed";
            std::snprintf(buffer, sizeof(buffer), "Now: %s (%s)", ParkDay::FormatStatusLine(gameState).c_str(), gateState);
            label(y, buffer);
            y += kLineHeight;

            if (st.targetSet)
                std::snprintf(buffer, sizeof(buffer), "Expected today: %u guests", st.dailyTarget);
            else
                std::snprintf(buffer, sizeof(buffer), "Expected today: set when the gates open");
            label(y, buffer);
            y += kLineHeight;

            std::snprintf(buffer, sizeof(buffer), "Through the gates today: %u", st.admissionsToday);
            label(y, buffer);
            y += kLineHeight;

            std::snprintf(
                buffer, sizeof(buffer), "In the park now: %u (peak %u)", gameState.park.numGuestsInPark, st.peakGuestsToday);
            label(y, buffer);
            y += kLineHeight;

            std::snprintf(
                buffer, sizeof(buffer), "Still inside an hour after closing: %u (%u on rides)", st.lastStragglers,
                st.lastStragglersOnRides);
            label(y, buffer);
            y += kLineHeight;

            if (ParkDay::IsFastForwarding(gameState))
            {
                label(y, "Skipping the night...");
            }
        }
    };

    WindowBase* ParkDayOpen()
    {
        auto* windowMgr = GetWindowManager();
        return windowMgr->FocusOrCreate<ParkDayWindow>(WindowClass::parkDay, kWindowSize, WindowFlag::centreScreen);
    }
} // namespace OpenRCT2::Ui::Windows
