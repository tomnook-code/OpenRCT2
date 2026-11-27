/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Fork: tests for the realistic park day cycle.

#include "TestData.h"

#include <array>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/actions/GameActionRunner.h>
#include <openrct2/actions/park/ParkDaySetAction.h>
#include <openrct2/actions/park/ParkSetEntranceFeeAction.h>
#include <openrct2/actions/park/ParkSetParameterAction.h>
#include <openrct2/actions/ride/RideSetStatusAction.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/entity/EntityList.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/park/ParkDay.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/Park.h>

using namespace OpenRCT2;

namespace
{
    class ParkDayTests : public testing::Test
    {
    };

    std::unique_ptr<IContext> StartGame(const std::string& parkPath)
    {
        gOpenRCT2Headless = true;
        gOpenRCT2NoGraphics = true;

        auto context = CreateContext();
        if (!context->Initialise())
            return {};

        auto importer = ParkImporter::CreateS6(context->GetObjectRepository());
        auto loadResult = importer->LoadSavedGame(parkPath.c_str(), false);
        context->GetObjectManager().LoadObjects(loadResult.RequiredObjects);

        MapAnimations::ClearAll();
        auto& gameState = getGameState();
        importer->Import(gameState);

        gameState.entities.resetEntitySpatialIndices();
        ResetAllSpriteQuadrantPlacements();
        Drawing::LoadPalette();
        EntityTweener::get().reset();
        MapAnimations::MarkAllTiles();
        FixInvalidVehicleSpriteSizes();

        gGameSpeed = 1;
        return context;
    }

    template<class GA, class... Args>
    GameActions::Result RunAction(Args&&... args)
    {
        GA ga(std::forward<Args>(args)...);
        return GameActions::Execute(&ga, getGameState());
    }

    void SetClock(GameState_t& gameState, uint32_t day, uint32_t minuteOfDay)
    {
        // Month ticks advance by 4 per game tick, so keep the value a multiple of 4.
        auto monthTicks = static_cast<uint16_t>(((minuteOfDay << 16) / ParkDay::kMinutesPerDay) & ~3u);
        gameState.date = Date{ day, monthTicks };
    }

    Ride* FindRide(GameState_t& gameState, ride_type_t type)
    {
        for (auto& ride : RideManager(gameState))
        {
            if (ride.type == type)
                return &ride;
        }
        return nullptr;
    }
} // namespace

TEST_F(ParkDayTests, ClockFollowsTheMonth)
{
    auto& gameState = getGameState();
    SetClock(gameState, 3, 0);
    EXPECT_EQ(ParkDay::GetMinuteOfDay(gameState), 0);
    SetClock(gameState, 3, 9 * 60);
    EXPECT_NEAR(ParkDay::GetMinuteOfDay(gameState), 9 * 60, 1);
    SetClock(gameState, 3, 21 * 60 + 30);
    EXPECT_NEAR(ParkDay::GetMinuteOfDay(gameState), 21 * 60 + 30, 1);
    EXPECT_EQ(ParkDay::GetDayIndex(gameState), 3u);
    EXPECT_EQ(ParkDay::FormatClock(9 * 60 + 5), "09:05");
    EXPECT_EQ(ParkDay::FormatClock(21 * 60), "21:00");
}

TEST_F(ParkDayTests, LightingFollowsTheClock)
{
    auto& gameState = getGameState();
    SetClock(gameState, 0, 3 * 60);
    EXPECT_FLOAT_EQ(ParkDay::GetNightAmount(gameState), 1.0f);
    SetClock(gameState, 0, 12 * 60);
    EXPECT_FLOAT_EQ(ParkDay::GetNightAmount(gameState), 0.0f);
    SetClock(gameState, 0, 23 * 60);
    EXPECT_FLOAT_EQ(ParkDay::GetNightAmount(gameState), 1.0f);
}

TEST_F(ParkDayTests, SettingsAreValidated)
{
    ParkDay::Settings s;
    EXPECT_TRUE(ParkDay::IsValidParameter(s, ParkDay::Parameter::openMinute, 8 * 60));
    EXPECT_FALSE(ParkDay::IsValidParameter(s, ParkDay::Parameter::openMinute, 2 * 60));
    EXPECT_FALSE(ParkDay::IsValidParameter(s, ParkDay::Parameter::lastEntryMinute, 22 * 60)); // after closing
    EXPECT_FALSE(ParkDay::IsValidParameter(s, ParkDay::Parameter::closeMinute, 10 * 60));     // too soon after opening
    EXPECT_FALSE(ParkDay::IsValidParameter(s, ParkDay::Parameter::closeMinute, 23 * 60 + 30));
    EXPECT_TRUE(ParkDay::IsValidParameter(s, ParkDay::Parameter::closeMinute, 22 * 60));
}

TEST_F(ParkDayTests, FullDayArrivalsAndClosing)
{
    auto context = StartGame(TestData::GetParkPath("small_park_with_ferris_wheel.sv6"));
    ASSERT_NE(context.get(), nullptr);
    auto& gameState = getGameState();
    auto& park = gameState.park;

    RunAction<GameActions::ParkSetParameterAction>(GameActions::ParkParameter::open);
    auto* ferrisWheel = FindRide(gameState, RIDE_TYPE_FERRIS_WHEEL);
    ASSERT_NE(ferrisWheel, nullptr);
    RunAction<GameActions::RideSetStatusAction>(ferrisWheel->id, RideStatus::open);

    // Off by default.
    EXPECT_FALSE(ParkDay::IsEnabled(gameState));
    auto res = RunAction<GameActions::ParkDaySetAction>(ParkDay::Parameter::enabled, 1u);
    ASSERT_EQ(res.error, GameActions::Status::ok);
    gameStateUpdateLogic(); // actions run from outside the update loop are queued for the next tick
    ASSERT_TRUE(ParkDay::IsEnabled(gameState));

    // Admission is paid at the gate and rides are free.
    EXPECT_FALSE(Park::RidePricesUnlocked(park));
    EXPECT_TRUE(Park::EntranceFeeUnlocked(park));
    EXPECT_EQ(RideGetPrice(*ferrisWheel), 0);
    RunAction<GameActions::ParkSetEntranceFeeAction>(0);

    // A well-rated park on a Saturday (day index 12) so there is a real crowd.
    gameState.cheats.forcedParkRating = 999;

    // Make sure the park has a guest cap and rating to work with.
    SetClock(gameState, 12, 6 * 60);
    for (int i = 0; i < 600; i++)
        gameStateUpdateLogic();
    ASSERT_GT(park.suggestedGuestMaximum, 0u);

    // Simulate from 07:00 until just before midnight.
    SetClock(gameState, 12, 7 * 60);
    gameState.parkDay.lastMinute = 0xFFFF;
    std::array<uint64_t, 24> admissionsByHour{};
    uint32_t peakInPark = 0;
    uint32_t inParkAt2130 = 0;
    bool sampled2130 = false;
    auto lastAdmissions = park.totalAdmissions;
    while (ParkDay::GetDayIndex(gameState) == 12 && ParkDay::GetMinuteOfDay(gameState) < 23 * 60 + 50)
    {
        gameStateUpdateLogic();
        auto minute = ParkDay::GetMinuteOfDay(gameState);
        admissionsByHour[minute / 60] += park.totalAdmissions - lastAdmissions;
        lastAdmissions = park.totalAdmissions;
        peakInPark = std::max(peakInPark, park.numGuestsInPark);
        if (!sampled2130 && minute >= 21 * 60 + 30)
        {
            inParkAt2130 = park.numGuestsInPark;
            sampled2130 = true;
        }
    }

    uint64_t morning = 0, afternoon = 0, total = 0;
    for (int h = 0; h < 24; h++)
    {
        total += admissionsByHour[h];
        if (h >= 9 && h < 12)
            morning += admissionsByHour[h];
        if (h >= 15 && h < 19)
            afternoon += admissionsByHour[h];
    }
    std::printf(
        "cap=%u target=%u admissions=%llu morning=%llu afternoon=%llu peak=%u at21:30=%u stragglers=%u\n",
        park.suggestedGuestMaximum, gameState.parkDay.dailyTarget, static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(morning), static_cast<unsigned long long>(afternoon), peakInPark, inParkAt2130,
        gameState.parkDay.lastStragglers);

    // Gates closed before opening and after closing.
    for (int h = 0; h < 9; h++)
        EXPECT_EQ(admissionsByHour[h], 0u) << "admitted at hour " << h;
    for (int h = 21; h < 24; h++)
        EXPECT_EQ(admissionsByHour[h], 0u) << "admitted at hour " << h;

    // Guests came, mostly in the morning.
    EXPECT_GT(total, 20u);
    EXPECT_GT(morning, afternoon);

    // Everyone heads home at closing: by the 22:00 check the park is nearly empty.
    EXPECT_GT(peakInPark, 0u);
    // Guests still riding or queuing at 22:00 are expected (queues close, riders finish); walkers should be gone.
    const auto walkingStragglers = gameState.parkDay.lastStragglers - gameState.parkDay.lastStragglersOnRides;
    EXPECT_LE(walkingStragglers * 10, peakInPark);
    EXPECT_EQ(gameState.parkDay.admissionsToday, total);
}

TEST_F(ParkDayTests, NightSkipOnlyAtNight)
{
    auto& gameState = getGameState();
    gameState.parkDay = {};
    gameState.parkDay.settings.enabled = true;
    gameState.parkDay.skipTonight = true;

    SetClock(gameState, 0, 14 * 60);
    EXPECT_FALSE(ParkDay::IsFastForwarding(gameState));
    SetClock(gameState, 0, 23 * 60);
    EXPECT_TRUE(ParkDay::IsFastForwarding(gameState));
    SetClock(gameState, 0, 3 * 60);
    EXPECT_TRUE(ParkDay::IsFastForwarding(gameState));
    SetClock(gameState, 0, 8 * 60 + 30);
    EXPECT_FALSE(ParkDay::IsFastForwarding(gameState));

    gameState.parkDay.skipTonight = false;
    SetClock(gameState, 0, 23 * 60);
    EXPECT_FALSE(ParkDay::IsFastForwarding(gameState));
    gameState.parkDay.settings.autoSkipNight = true;
    EXPECT_TRUE(ParkDay::IsFastForwarding(gameState));

    gameState.parkDay = {};
}

TEST_F(ParkDayTests, LongerDaysSlowTheCalendarButKeepWeekStarts)
{
    auto& gameState = getGameState();
    gameState.parkDay = {};
    gameState.parkDay.settings.enabled = true;
    SetClock(gameState, 0, 0);
    gameState.date.monthTicks = 6; // misaligned on purpose
    ParkDay::ApplyParameter(gameState, ParkDay::Parameter::dayLength, 4);
    EXPECT_EQ(gameState.date.monthTicks % 4, 0);
    EXPECT_EQ(ParkDay::GetMonthTicksIncrement(gameState), 1);
    EXPECT_EQ(ParkDay::GetTicksPerDay(gameState), 65536u);

    // Over one full park day the calendar must still hit 4 week starts and 1 month start.
    SetClock(gameState, 0, 0);
    gameState.date.monthTicks = 4;
    int weekStarts = 0;
    int monthStarts = 0;
    for (uint32_t i = 0; i < ParkDay::GetTicksPerDay(gameState); i++)
    {
        DateUpdate(gameState);
        weekStarts += gameState.date.IsWeekStart() ? 1 : 0;
        monthStarts += gameState.date.IsMonthStart() ? 1 : 0;
    }
    EXPECT_EQ(weekStarts, 4);
    EXPECT_EQ(monthStarts, 1);

    gameState.parkDay = {};
    EXPECT_EQ(ParkDay::GetMonthTicksIncrement(gameState), 4);
}
