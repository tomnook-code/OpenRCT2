/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Fork: tests for overnight hotels.

#include "TestData.h"

#include <cstdio>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/ParkImporter.h>
#include <openrct2/actions/GameActionRunner.h>
#include <openrct2/actions/footpath/FootpathPlaceAction.h>
#include <openrct2/actions/park/HotelPlaceAction.h>
#include <openrct2/actions/park/HotelSetAction.h>
#include <openrct2/actions/park/ParkDaySetAction.h>
#include <openrct2/actions/park/ParkSetEntranceFeeAction.h>
#include <openrct2/actions/park/ParkSetParameterAction.h>
#include <openrct2/actions/ride/RideSetStatusAction.h>
#include <openrct2/actions/scenery/LargeSceneryRemoveAction.h>
#include <openrct2/core/MemoryStream.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/entity/EntityList.h>
#include <openrct2/entity/EntityTweener.h>
#include <openrct2/entity/Guest.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/park/Hotel.h>
#include <openrct2/park/ParkDay.h>
#include <openrct2/park/ParkFile.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapAnimation.h>
#include <openrct2/world/Park.h>
#include <openrct2/world/TileElementsView.h>
#include <openrct2/world/tile_element/LargeSceneryElement.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <vector>

using namespace OpenRCT2;

namespace
{
    class HotelTests : public testing::Test
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
        auto monthTicks = static_cast<uint16_t>(((minuteOfDay << 16) / ParkDay::kMinutesPerDay) & ~3u);
        gameState.date = Date{ day, monthTicks };
    }

    // Finds the buildable spot closest to the park entrance, so its door is on the main path network.
    std::optional<TileCoordsXY> FindHotelSpot(GameState_t& gameState, uint8_t sizeIndex, ObjectEntryIndex entry)
    {
        const auto size = Hotel::kSizes[sizeIndex];
        if (gameState.park.entrances.empty())
            return std::nullopt;
        const auto gate = TileCoordsXY(gameState.park.entrances[0]);
        std::optional<TileCoordsXY> best;
        int32_t bestDist = std::numeric_limits<int32_t>::max();
        for (int32_t y = 1; y < gameState.mapSize.y - size.length - 1; y++)
        {
            for (int32_t x = 1; x < gameState.mapSize.x - size.width - 1; x++)
            {
                const int32_t dist = std::abs(x - gate.x) + std::abs(y - gate.y);
                if (dist >= bestDist || dist < 4)
                    continue;
                GameActions::HotelPlaceAction action({ TileCoordsXY{ x, y }.toCoordsXY() }, sizeIndex, entry);
                if (GameActions::Query(&action, gameState).error == GameActions::Status::ok)
                {
                    best = TileCoordsXY{ x, y };
                    bestDist = dist;
                }
            }
        }
        return best;
    }

    void RunUntil(GameState_t& gameState, uint32_t day, uint32_t minute)
    {
        while (ParkDay::GetDayIndex(gameState) < day
               || (ParkDay::GetDayIndex(gameState) == day && ParkDay::GetMinuteOfDay(gameState) < minute))
        {
            gameStateUpdateLogic();
        }
    }
} // namespace

TEST_F(HotelTests, CapacityFollowsFootprint)
{
    EXPECT_EQ(Hotel::GetSizeCapacity(0), 100u);  // 2x2
    EXPECT_EQ(Hotel::GetSizeCapacity(2), 400u);  // 4x4
    EXPECT_EQ(Hotel::GetSizeCapacity(6), 1600u); // 8x8
    for (size_t i = 1; i < Hotel::kSizes.size(); i++)
        EXPECT_GT(Hotel::GetSizeCapacity(static_cast<uint8_t>(i)), Hotel::GetSizeCapacity(static_cast<uint8_t>(i - 1)));
}

TEST_F(HotelTests, GuestsStayOvernightAndComeBackOut)
{
    auto context = StartGame(TestData::GetParkPath("bpb.sv6"));
    ASSERT_NE(context.get(), nullptr);
    auto& gameState = getGameState();
    auto& park = gameState.park;

    RunAction<GameActions::ParkSetParameterAction>(GameActions::ParkParameter::open);
    ASSERT_EQ(RunAction<GameActions::ParkDaySetAction>(ParkDay::Parameter::enabled, 1u).error, GameActions::Status::ok);
    // The shortest day keeps the simulation quick; hotel timing only depends on the clock.
    ASSERT_EQ(RunAction<GameActions::ParkDaySetAction>(ParkDay::Parameter::dayLength, 1u).error, GameActions::Status::ok);
    gameStateUpdateLogic();
    ASSERT_TRUE(ParkDay::IsEnabled(gameState));
    RunAction<GameActions::ParkSetEntranceFeeAction>(0);
    gameState.cheats.forcedParkRating = 999;
    gameState.cheats.sandboxMode = true;
    park.cash = 1000000.00_GBP;

    // The building object isn't part of this park, so load it like the Hotels window does.
    auto& objectManager = context->GetObjectManager();
    ASSERT_NE(objectManager.LoadObject(Hotel::kBuildingObjects[0]), nullptr);
    auto entry = Hotel::FindBuildingEntry();
    ASSERT_NE(entry, kObjectEntryIndexNull);

    // Before the day cycle is on, or away from a footpath, building fails.
    {
        GameActions::HotelPlaceAction action({ TileCoordsXY{ 2, 2 }.toCoordsXY() }, 0, entry);
        auto res = GameActions::Query(&action, gameState);
        EXPECT_NE(res.error, GameActions::Status::ok);
    }

    auto spot = FindHotelSpot(gameState, 0, entry);
    ASSERT_TRUE(spot.has_value()) << "no room next to a footpath for a 2x2 hotel";
    {
        GameActions::HotelPlaceAction action({ spot->toCoordsXY() }, 0, entry);
        auto res = GameActions::Execute(&action, gameState);
        ASSERT_EQ(res.error, GameActions::Status::ok);
    }
    gameStateUpdateLogic();
    ASSERT_EQ(gameState.hotels.hotels.size(), 1u);
    const auto hotelId = gameState.hotels.hotels[0].id;
    {
        const auto& h = gameState.hotels.hotels[0];
        EXPECT_EQ(h.width, 2);
        EXPECT_EQ(h.length, 2);
        EXPECT_EQ(h.GetCapacity(), 100u);
        EXPECT_TRUE(h.hasDoor);
        // The building is real scenery on the map.
        bool found = false;
        for (auto* el : TileElementsView<LargeSceneryElement>(h.origin.toCoordsXY()))
            found = found || el->getEntryIndex() == entry;
        EXPECT_TRUE(found);
    }

    // Can't build on top of it.
    {
        GameActions::HotelPlaceAction action({ spot->toCoordsXY() }, 0, entry);
        EXPECT_NE(GameActions::Query(&action, gameState).error, GameActions::Status::ok);
    }

    // Run a Saturday from 07:00 to the 22:00 check.
    SetClock(gameState, 12, 6 * 60);
    for (int i = 0; i < 600; i++)
        gameStateUpdateLogic();
    SetClock(gameState, 12, 7 * 60);
    gameState.parkDay.lastMinute = 0xFFFF;
    gameState.hotels.lastMinute = 0xFFFF;

    uint32_t maxBookings = 0;
    while (ParkDay::GetMinuteOfDay(gameState) < 20 * 60 + 30)
    {
        gameStateUpdateLogic();
        maxBookings = std::max<uint32_t>(maxBookings, static_cast<uint32_t>(gameState.hotels.bookings.size()));
    }
    const auto admissions = gameState.parkDay.admissionsToday;
    EXPECT_GT(maxBookings, 0u);
    EXPECT_LE(maxBookings, 100u);
    // Roughly a quarter of arrivals book (hash based), never more than the rooms.
    EXPECT_LE(maxBookings, admissions);

    RunUntil(gameState, 12, 21 * 60 + 30);
    {
        const auto* hh = Hotel::GetHotel(gameState, hotelId);
        uint32_t leaving = 0, walking = 0, near = 0, nullPos = 0;
        int64_t distSum = 0;
        for (const auto& b : gameState.hotels.bookings)
        {
            auto* g = gameState.entities.tryGetEntity<Guest>(b.guest);
            if (g == nullptr)
                continue;
            if (g->peepFlags.has(PeepFlag::leavingPark))
                leaving++;
            if (g->state == PeepState::walking)
                walking++;
            if (g->x == kLocationNull)
            {
                nullPos++;
                continue;
            }
            auto t = TileCoordsXY(CoordsXY{ g->x, g->y });
            auto d = std::abs(t.x - hh->door.x) + std::abs(t.y - hh->door.y);
            distSum += d;
            if (d <= 2)
                near++;
        }
        std::printf(
            "21:30 bookings=%zu leaving=%u walking=%u near=%u null=%u avgDist=%lld door=%d,%d,%d gate=%d,%d\n",
            gameState.hotels.bookings.size(), leaving, walking, near, nullPos,
            static_cast<long long>(
                gameState.hotels.bookings.empty() ? 0 : distSum / static_cast<int64_t>(gameState.hotels.bookings.size())),
            hh->door.x, hh->door.y, hh->door.z, TileCoordsXY(park.entrances[0]).x, TileCoordsXY(park.entrances[0]).y);
    }
    // Guests have all night to reach the hotel.
    RunUntil(gameState, 13, 7 * 60 + 50);
    const auto& hotel = *Hotel::GetHotel(gameState, hotelId);
    const auto inside = Hotel::GetGuestsInside(gameState, hotelId);
    std::printf(
        "07:50 next day: admissions=%u maxBookings=%u checkedIn=%u inside=%u stragglers=%u inPark=%u\n", admissions,
        maxBookings, hotel.guestsTonight, inside, gameState.parkDay.lastStragglers, park.numGuestsInPark);
    EXPECT_GT(hotel.guestsTonight, 0u);
    EXPECT_EQ(inside, hotel.guestsTonight);
    EXPECT_GE(inside * 2, maxBookings) << "most guests with a room should make it to the hotel";

    // Guests inside are hidden and frozen, and are not reported as stragglers.
    uint32_t hidden = 0;
    std::vector<EntityId> sleepers;
    for (auto* guest : EntityList<Guest>())
    {
        if (Hotel::IsInHotel(gameState, *guest))
        {
            sleepers.push_back(guest->id);
            EXPECT_EQ(guest->x, kLocationNull);
            EXPECT_TRUE(guest->peepFlags.has(PeepFlag::positionFrozen));
            hidden++;
        }
    }
    EXPECT_EQ(hidden, inside);

    // Booked guests that never arrived are sent home at 08:00, and the nightly report rolls over.
    RunUntil(gameState, 13, 8 * 60 + 5);
    for (const auto& b : gameState.hotels.bookings)
        EXPECT_EQ(b.status, Hotel::BookingStatus::inHotel);
    EXPECT_EQ(Hotel::GetHotel(gameState, hotelId)->guestsLastNight, inside);
    EXPECT_EQ(Hotel::GetHotel(gameState, hotelId)->guestsTonight, 0u);

    // Each night costs the room price.
    if (!park.flags.has(ParkFlag::noMoney))
    {
        EXPECT_EQ(hotel.revenueTonight, static_cast<money64>(hotel.guestsTonight) * gameState.hotels.roomPrice);
    }

    // Save and load keeps hotels and bookings.
    {
        MemoryStream stream;
        auto exporter = std::make_unique<ParkFileExporter>();
        exporter->ExportObjectsList = objectManager.GetPackableObjects();
        exporter->Export(gameState, stream, kParkFileSaveCompressionLevel);

        const auto savedHotels = gameState.hotels.hotels.size();
        const auto savedBookings = gameState.hotels.bookings.size();
        stream.SetPosition(0);
        auto importer = ParkImporter::CreateParkFile(context->GetObjectRepository());
        auto loadResult = importer->LoadFromStream(&stream, false);
        objectManager.LoadObjects(loadResult.RequiredObjects);
        MapAnimations::ClearAll();
        importer->Import(gameState);
        ResetAllSpriteQuadrantPlacements();
        EntityTweener::get().reset();
        MapAnimations::MarkAllTiles();
        FixInvalidVehicleSpriteSizes();
        EXPECT_EQ(gameState.hotels.hotels.size(), savedHotels);
        EXPECT_EQ(gameState.hotels.bookings.size(), savedBookings);
        EXPECT_EQ(Hotel::GetGuestsInside(gameState, hotelId), inside);
    }

    // Next morning: everyone who slept there comes back out (a tired or broke guest may already have
    // gone back to its room for its next night, which is fine).
    RunUntil(gameState, 13, 10 * 60);
    for (const auto& b : gameState.hotels.bookings)
    {
        if (b.status == Hotel::BookingStatus::inHotel)
        {
            EXPECT_EQ(b.night, 13u) << "a guest from last night is still inside";
        }
    }
    uint32_t backOut = 0;
    uint32_t onMap = 0;
    for (auto id : sleepers)
    {
        auto* guest = gameState.entities.tryGetEntity<Guest>(id);
        if (guest == nullptr || guest->outsideOfPark)
            continue; // already went home after their last night
        backOut++;
        if (Hotel::IsInHotel(gameState, *guest))
            continue;
        EXPECT_FALSE(guest->peepFlags.has(PeepFlag::positionFrozen));
        if (guest->x != kLocationNull)
            onMap++; // the rest are on a ride already
    }
    std::printf(
        "hotel guests still in the park at 10:00 next day: %u of %zu (%u walking about), bookings left %zu\n", backOut,
        sleepers.size(), onMap, gameState.hotels.bookings.size());
    EXPECT_GE(backOut * 2, static_cast<uint32_t>(sleepers.size()));
    EXPECT_GT(onMap, 0u);

    // Demolishing the building closes the hotel.
    const auto h = *Hotel::GetHotel(gameState, hotelId);
    auto removeRes = RunAction<GameActions::LargeSceneryRemoveAction>(CoordsXYZD{ h.origin.toCoordsXY(), h.baseZ, 0 }, 0);
    EXPECT_EQ(removeRes.error, GameActions::Status::ok);
    for (int i = 0; i < 200; i++)
        gameStateUpdateLogic();
    EXPECT_EQ(Hotel::GetHotel(gameState, hotelId), nullptr);
}
