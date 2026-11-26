/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ParkDay.h"

#include "../GameState.h"
#include "../entity/EntityList.h"
#include "../entity/Guest.h"
#include "../management/Award.h"
#include "../management/NewsItem.h"
#include "../world/Park.h"
#include "../world/ParkData.h"
#include "../world/Weather.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace OpenRCT2::ParkDay
{
    // Vanilla advances the date 4 month-ticks per game tick and a month is 0x10000 month-ticks,
    // so one park day (one month) is 16384 game ticks, about 6.8 minutes at normal speed.
    // Longer days slow the calendar down to 2 or 1 month-ticks per game tick.
    constexpr int32_t kVanillaMonthTicksIncrement = 4;

    // Share of the day's arrivals in each hour after the gates open (rope drop peak, then tailing off).
    constexpr std::array<uint32_t, 10> kArrivalWeights = { 18, 20, 16, 11, 9, 8, 6, 5, 4, 3 };
    constexpr uint32_t kLateArrivalWeight = 3;

    // Day-of-week factor in tenths, Monday first.
    constexpr std::array<uint32_t, 7> kDayOfWeekFactor = { 8, 8, 8, 8, 10, 14, 14 };
    constexpr std::array<const char*, 7> kDayOfWeekNames = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

    constexpr uint32_t kMaxDailyTarget = 30000;
    constexpr uint32_t kMaxSpawnsPerTick = 8;

    bool IsEnabled(const GameState_t& gameState)
    {
        return gameState.parkDay.settings.enabled;
    }

    bool IsEnabled()
    {
        return IsEnabled(getGameState());
    }

    uint16_t GetMinuteOfDay(const GameState_t& gameState)
    {
        return static_cast<uint16_t>((static_cast<uint32_t>(gameState.date.monthTicks) * kMinutesPerDay) >> 16);
    }

    int32_t GetMonthTicksIncrement(const GameState_t& gameState)
    {
        const auto& s = gameState.parkDay.settings;
        if (!s.enabled)
            return kVanillaMonthTicksIncrement;
        switch (s.dayLength)
        {
            case 2:
                return 2;
            case 4:
                return 1;
            default:
                return kVanillaMonthTicksIncrement;
        }
    }

    uint32_t GetTicksPerDay(const GameState_t& gameState)
    {
        return kTicksPerMonth / GetMonthTicksIncrement(gameState);
    }

    const char* GetDayLengthName(uint8_t dayLength)
    {
        switch (dayLength)
        {
            case 2:
                return "13.7 min";
            case 4:
                return "27.3 min";
            default:
                return "6.8 min";
        }
    }

    uint32_t GetDayIndex(const GameState_t& gameState)
    {
        return gameState.date.monthsElapsed;
    }

    int32_t GetDayOfWeek(const GameState_t& gameState)
    {
        return static_cast<int32_t>(GetDayIndex(gameState) % 7);
    }

    const char* GetDayOfWeekName(int32_t dayOfWeek)
    {
        return kDayOfWeekNames[static_cast<size_t>(dayOfWeek) % kDayOfWeekNames.size()];
    }

    std::string FormatClock(uint16_t minuteOfDay)
    {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", (minuteOfDay / 60) % 24, minuteOfDay % 60);
        return buffer;
    }

    std::string FormatStatusLine(const GameState_t& gameState)
    {
        char buffer[64];
        std::snprintf(
            buffer, sizeof(buffer), "Day %u, %s %s", GetDayIndex(gameState) + 1, GetDayOfWeekName(GetDayOfWeek(gameState)),
            FormatClock(GetMinuteOfDay(gameState)).c_str());
        return buffer;
    }

    bool IsGateOpen(const GameState_t& gameState)
    {
        if (!IsEnabled(gameState))
            return true;
        const auto& s = gameState.parkDay.settings;
        auto minute = GetMinuteOfDay(gameState);
        return minute >= s.openMinute && minute < s.closeMinute;
    }

    bool IsAcceptingArrivals(const GameState_t& gameState)
    {
        if (!IsEnabled(gameState))
            return true;
        const auto& s = gameState.parkDay.settings;
        auto minute = GetMinuteOfDay(gameState);
        return minute >= s.openMinute && minute < s.lastEntryMinute;
    }

    bool IsNight(const GameState_t& gameState)
    {
        if (!IsEnabled(gameState))
            return false;
        const auto& s = gameState.parkDay.settings;
        auto minute = GetMinuteOfDay(gameState);
        auto nightStart = s.closeMinute + kAfterCloseCheckMinutes;
        auto nightEnd = s.openMinute >= kMorningLeadMinutes ? s.openMinute - kMorningLeadMinutes : 0;
        return minute >= nightStart || minute < nightEnd;
    }

    bool IsFastForwarding(const GameState_t& gameState)
    {
        if (!IsEnabled(gameState))
            return false;
        const auto& st = gameState.parkDay;
        return (st.settings.autoSkipNight || st.skipTonight) && IsNight(gameState);
    }

    float GetNightAmount(const GameState_t& gameState)
    {
        auto minute = GetMinuteOfDay(gameState);
        constexpr float kSunriseStart = 6 * 60;
        constexpr float kSunriseEnd = 8 * 60;
        constexpr float kSunsetStart = 19 * 60;
        constexpr float kSunsetEnd = 21 * 60;
        float m = minute;
        if (m < kSunriseStart)
            return 1.0f;
        if (m < kSunriseEnd)
            return 1.0f - (m - kSunriseStart) / (kSunriseEnd - kSunriseStart);
        if (m < kSunsetStart)
            return 0.0f;
        if (m < kSunsetEnd)
            return (m - kSunsetStart) / (kSunsetEnd - kSunsetStart);
        return 1.0f;
    }

    static uint32_t ArrivalWeight(uint32_t hourOffset)
    {
        return hourOffset < kArrivalWeights.size() ? kArrivalWeights[hourOffset] : kLateArrivalWeight;
    }

    static uint32_t ArrivalHours(const Settings& s)
    {
        if (s.lastEntryMinute <= s.openMinute)
            return 0;
        return (s.lastEntryMinute - s.openMinute + 59) / 60;
    }

    static uint32_t TotalArrivalWeight(const Settings& s)
    {
        uint32_t total = 0;
        for (uint32_t i = 0; i < ArrivalHours(s); i++)
            total += ArrivalWeight(i);
        return total;
    }

    static uint32_t CalculateDailyTarget(const GameState_t& gameState)
    {
        const auto& park = gameState.park;

        // Guests no longer pile up over many days, so more must arrive each day to fill the park.
        uint64_t target = static_cast<uint64_t>(park.suggestedGuestMaximum) * 3 / 2;

        // Park rating, same curve as the vanilla spawn probability (50..700).
        uint64_t ratingScore = 50 + std::clamp<int32_t>(park.rating - 200, 0, 650);
        target = target * ratingScore / 700;

        // Overpriced admission relative to what the rides are worth.
        auto entranceFee = Park::GetEntranceFee(park);
        if (entranceFee > park.totalRideValueForMoney)
        {
            target /= 2;
            if (entranceFee / 2 > park.totalRideValueForMoney)
                target /= 2;
        }

        // Weekends are busier.
        target = target * kDayOfWeekFactor[GetDayOfWeek(gameState)] / 10;

        // Awards: +/- 10% each.
        for (const auto& award : park.currentAwards)
        {
            if (AwardIsPositive(award.type))
                target += target / 10;
            else
                target -= target / 10;
        }

        return static_cast<uint32_t>(std::min<uint64_t>(target, kMaxDailyTarget));
    }

    static void StartNewDay(State& st, uint32_t dayIndex)
    {
        st.dayIndex = dayIndex;
        st.dailyTarget = 0;
        st.targetSet = false;
        st.arrivalsToday = 0;
        st.admissionsToday = 0;
        st.peakGuestsToday = 0;
        st.arrivalAccumulator = 0;
    }

    static void PostClosingSummary(GameState_t& gameState)
    {
        auto& st = gameState.parkDay;
        char buffer[256];
        std::snprintf(
            buffer, sizeof(buffer), "Day %u has closed. %u guests visited today, with a peak of %u in the park at once.",
            st.dayIndex + 1, st.admissionsToday, st.peakGuestsToday);
        gameState.park.newsItems.addItemToQueue(News::ItemType::graph, buffer, 0);
    }

    static void RunStragglerCheck(GameState_t& gameState)
    {
        auto& st = gameState.parkDay;
        uint32_t stragglers = 0;
        uint32_t onRides = 0;
        for (auto* guest : EntityList<Guest>())
        {
            if (guest->outsideOfPark)
                continue;
            stragglers++;
            switch (guest->state)
            {
                case PeepState::queuing:
                case PeepState::queuingFront:
                case PeepState::enteringRide:
                case PeepState::onRide:
                case PeepState::leavingRide:
                    onRides++;
                    break;
                default:
                    break;
            }
        }
        st.lastStragglers = stragglers;
        st.lastStragglersOnRides = onRides;
        if (stragglers == 0)
            return;

        char buffer[256];
        if (onRides == stragglers)
        {
            std::snprintf(
                buffer, sizeof(buffer),
                "%u guests are still in the park an hour after closing, all of them riding or queuing for rides.", stragglers);
        }
        else
        {
            std::snprintf(
                buffer, sizeof(buffer),
                "%u guests are still in the park an hour after closing (%u riding or queuing). Check for dead ends, "
                "missing paths or a hard-to-find exit.",
                stragglers, onRides);
        }
        gameState.park.newsItems.addItemToQueue(News::ItemType::peeps, buffer, 0);
    }

    uint32_t Update(GameState_t& gameState)
    {
        auto& st = gameState.parkDay;
        if (!st.settings.enabled)
            return 0;

        const auto& s = st.settings;
        auto& park = gameState.park;
        const auto dayIndex = GetDayIndex(gameState);
        const auto minute = GetMinuteOfDay(gameState);

        if (st.dayIndex != dayIndex)
        {
            StartNewDay(st, dayIndex);
            st.lastMinute = minute;
        }

        const auto prev = st.lastMinute;
        st.lastMinute = minute;
        if (prev != 0xFFFF && prev != minute)
        {
            auto crossed = [prev, minute](uint32_t m) { return prev < m && minute >= m; };
            if (crossed(s.closeMinute))
                PostClosingSummary(gameState);
            if (crossed(s.closeMinute + kAfterCloseCheckMinutes))
                RunStragglerCheck(gameState);
            if (s.openMinute >= kMorningLeadMinutes && crossed(s.openMinute - kMorningLeadMinutes))
                st.skipTonight = false;
        }

        if (IsGateOpen(gameState))
            st.peakGuestsToday = std::max(st.peakGuestsToday, park.numGuestsInPark);

        if (!IsAcceptingArrivals(gameState) || !park.flags.has(ParkFlag::parkOpen))
            return 0;

        if (!st.targetSet)
        {
            st.dailyTarget = CalculateDailyTarget(gameState);
            st.targetSet = true;
        }

        const auto totalWeight = TotalArrivalWeight(s);
        if (totalWeight == 0 || st.dailyTarget == 0)
            return 0;

        // Spread the day's target over the opening hours. Over one hour the increments add up to
        // target * weight * ticksPerDay, so dividing by totalWeight * ticksPerDay gives
        // target * weight / totalWeight arrivals in that hour.
        const auto hourOffset = static_cast<uint32_t>(minute - s.openMinute) / 60;
        uint64_t increment = static_cast<uint64_t>(st.dailyTarget) * ArrivalWeight(hourOffset) * 24;
        if (Weather::isRaining())
            increment = increment * 7 / 10;

        st.arrivalAccumulator += increment;
        const uint64_t threshold = static_cast<uint64_t>(totalWeight) * GetTicksPerDay(gameState);
        auto spawns = static_cast<uint32_t>(std::min<uint64_t>(st.arrivalAccumulator / threshold, kMaxSpawnsPerTick));
        st.arrivalAccumulator -= static_cast<uint64_t>(spawns) * threshold;
        if (st.arrivalAccumulator > threshold * kMaxSpawnsPerTick)
            st.arrivalAccumulator = threshold * kMaxSpawnsPerTick;
        st.arrivalsToday += spawns;
        return spawns;
    }

    static uint32_t HashGuest(uint32_t id, uint32_t entryTime)
    {
        uint32_t h = id * 2654435761u ^ (entryTime + 0x9E3779B9u);
        h ^= h >> 16;
        h *= 0x85EBCA6Bu;
        h ^= h >> 13;
        h *= 0xC2B2AE35u;
        h ^= h >> 16;
        return h;
    }

    bool ShouldGuestGoHome(const GameState_t& gameState, const Guest& guest)
    {
        if (!IsEnabled(gameState) || guest.outsideOfPark)
            return false;

        const auto& s = gameState.parkDay.settings;
        const auto minute = GetMinuteOfDay(gameState);
        const auto windDown = s.closeMinute > kWindDownMinutes ? s.closeMinute - kWindDownMinutes : 0;
        if (minute >= windDown || minute < s.openMinute)
            return true;

        if (guest.parkEntryTime == -1)
            return false;

        // Planned stay of 3 to 8 hours, weighted towards the middle, plus up to half an hour of jitter.
        const auto h = HashGuest(guest.id.ToUnderlying(), static_cast<uint32_t>(guest.parkEntryTime));
        const uint32_t stayMinutes = 180 + ((h % 6) + ((h >> 8) % 6)) * 30 + ((h >> 16) % 30);
        const uint32_t stayTicks = stayMinutes * GetTicksPerDay(gameState) / kMinutesPerDay;
        const uint32_t elapsed = gameState.currentTicks - static_cast<uint32_t>(guest.parkEntryTime);
        return elapsed >= stayTicks;
    }

    void OnGuestAdmitted(GameState_t& gameState)
    {
        if (IsEnabled(gameState))
            gameState.parkDay.admissionsToday++;
    }

    bool IsValidParameter(const Settings& s, Parameter parameter, uint32_t value)
    {
        switch (parameter)
        {
            case Parameter::dayLength:
                return value == 1 || value == 2 || value == 4;
            case Parameter::enabled:
            case Parameter::autoSkipNight:
            case Parameter::skipTonight:
                return value <= 1;
            case Parameter::openMinute:
                return value >= 5 * 60 && value <= 12 * 60;
            case Parameter::lastEntryMinute:
                return value > s.openMinute && value <= s.closeMinute;
            case Parameter::closeMinute:
                return value >= static_cast<uint32_t>(s.openMinute) + 120 && value <= 22 * 60 + 30;
            default:
                return false;
        }
    }

    void ApplyParameter(GameState_t& gameState, Parameter parameter, uint32_t value)
    {
        auto& st = gameState.parkDay;
        auto& s = st.settings;

        // Changing the calendar speed must keep month ticks on a multiple of 4 so that
        // week, fortnight and month starts (wages, upkeep, finance) are still hit exactly.
        gameState.date.monthTicks = static_cast<uint16_t>(gameState.date.monthTicks & ~3u);

        switch (parameter)
        {
            case Parameter::enabled:
                s.enabled = value != 0;
                StartNewDay(st, GetDayIndex(gameState));
                st.lastMinute = 0xFFFF;
                st.skipTonight = false;
                break;
            case Parameter::openMinute:
                s.openMinute = static_cast<uint16_t>(value);
                if (s.closeMinute < s.openMinute + 120)
                    s.closeMinute = s.openMinute + 120;
                if (s.lastEntryMinute <= s.openMinute)
                    s.lastEntryMinute = s.openMinute + 60;
                if (s.lastEntryMinute > s.closeMinute)
                    s.lastEntryMinute = s.closeMinute;
                break;
            case Parameter::lastEntryMinute:
                s.lastEntryMinute = static_cast<uint16_t>(value);
                break;
            case Parameter::closeMinute:
                s.closeMinute = static_cast<uint16_t>(value);
                if (s.lastEntryMinute > s.closeMinute)
                    s.lastEntryMinute = s.closeMinute;
                break;
            case Parameter::autoSkipNight:
                s.autoSkipNight = value != 0;
                break;
            case Parameter::skipTonight:
                st.skipTonight = value != 0;
                break;
            case Parameter::dayLength:
                s.dayLength = static_cast<uint8_t>(value);
                break;
            default:
                break;
        }
    }
} // namespace OpenRCT2::ParkDay
