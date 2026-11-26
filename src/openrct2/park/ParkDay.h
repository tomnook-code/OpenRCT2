/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

// Realistic park day cycle (fork feature).
//
// One game month is treated as one park day with a 24-hour clock. When enabled, guests arrive
// during opening hours following a morning-heavy curve, stay for a planned time, and all head
// home at closing. Rides are free and admission is paid at the gate.
//
// Everything lives here so upstream merges only touch a handful of small hooks.

#include <cstdint>
#include <string>

namespace OpenRCT2
{
    struct GameState_t;
    struct Guest;
} // namespace OpenRCT2

namespace OpenRCT2::ParkDay
{
    constexpr uint16_t kMinutesPerDay = 24 * 60;

    // How long before closing guests stop queuing for rides and drift out.
    constexpr uint16_t kWindDownMinutes = 30;
    // How long after closing the straggler check runs and night begins.
    constexpr uint16_t kAfterCloseCheckMinutes = 60;
    // How long before opening the night fast-forward stops.
    constexpr uint16_t kMorningLeadMinutes = 60;

    struct Settings
    {
        bool enabled = false;
        uint16_t openMinute = 9 * 60;       // 09:00
        uint16_t lastEntryMinute = 19 * 60; // 19:00
        uint16_t closeMinute = 21 * 60;     // 21:00
        bool autoSkipNight = false;
        // Real time per park day at normal speed: 1 = 6.8 min, 2 = 13.7 min, 4 = 27.3 min.
        uint8_t dayLength = 1;
    };

    struct State
    {
        Settings settings;

        // Per-day bookkeeping, saved with the park.
        uint32_t dayIndex = 0xFFFFFFFF; // day the counters below belong to
        uint32_t dailyTarget = 0;
        bool targetSet = false;
        uint32_t arrivalsToday = 0;
        uint32_t admissionsToday = 0;
        uint32_t peakGuestsToday = 0;
        uint64_t arrivalAccumulator = 0;
        uint16_t lastMinute = 0xFFFF;
        uint32_t lastStragglers = 0;
        uint32_t lastStragglersOnRides = 0; // of lastStragglers, still riding or queuing
        bool skipTonight = false;
    };

    enum class Parameter : uint8_t
    {
        enabled,
        openMinute,
        lastEntryMinute,
        closeMinute,
        autoSkipNight,
        skipTonight,
        dayLength,
        count
    };

    bool IsEnabled(const GameState_t& gameState);
    bool IsEnabled();

    // Clock
    uint16_t GetMinuteOfDay(const GameState_t& gameState);
    // How far the calendar moves per game tick (vanilla: 4), and the resulting game ticks per park day.
    int32_t GetMonthTicksIncrement(const GameState_t& gameState);
    uint32_t GetTicksPerDay(const GameState_t& gameState);
    const char* GetDayLengthName(uint8_t dayLength);
    uint32_t GetDayIndex(const GameState_t& gameState);
    int32_t GetDayOfWeek(const GameState_t& gameState); // 0 = Monday
    const char* GetDayOfWeekName(int32_t dayOfWeek);
    std::string FormatClock(uint16_t minuteOfDay);
    std::string FormatStatusLine(const GameState_t& gameState);

    // Opening hours
    bool IsGateOpen(const GameState_t& gameState);
    bool IsAcceptingArrivals(const GameState_t& gameState);
    bool IsNight(const GameState_t& gameState);

    // Called once per tick from Park::Update. Returns how many guests should spawn this tick.
    uint32_t Update(GameState_t& gameState);

    // Called from the guest leave decision. True if this guest's day is over.
    bool ShouldGuestGoHome(const GameState_t& gameState, const Guest& guest);

    // Called when a guest pays and walks through the gate.
    void OnGuestAdmitted(GameState_t& gameState);

    // Night fast-forward: true while the game loop should run many updates per frame.
    bool IsFastForwarding(const GameState_t& gameState);

    // Lighting: 0 = full day, 1 = full night.
    float GetNightAmount(const GameState_t& gameState);

    // Parameter validation and application, used by ParkDaySetAction.
    bool IsValidParameter(const Settings& settings, Parameter parameter, uint32_t value);
    void ApplyParameter(GameState_t& gameState, Parameter parameter, uint32_t value);
} // namespace OpenRCT2::ParkDay
