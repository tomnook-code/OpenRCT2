/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

// Overnight hotels for the realistic park day cycle (fork feature).
//
// A hotel is a rectangle of existing large scenery buildings (Office Block by default) placed next
// to a footpath. Its capacity follows its footprint: 25 guests per tile. While rooms are free, some
// arriving guests book one to three nights. They stay until the evening wind-down, walk to the
// hotel door, pay for the night and wait inside (hidden) until the next morning, then carry on.
//
// Everything lives here so upstream merges only touch a handful of small hooks.

#include "../Identifiers.h"
#include "../core/Money.hpp"
#include "../object/ObjectTypes.h"
#include "../world/Location.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OpenRCT2
{
    struct GameState_t;
    struct Guest;
    struct Peep;
} // namespace OpenRCT2

namespace OpenRCT2::Hotel
{
    constexpr uint32_t kGuestsPerTile = 25;
    constexpr size_t kMaxHotels = 16;
    // Share of arriving guests that book a room while rooms are free.
    constexpr uint32_t kBookingChancePercent = 25;
    constexpr money64 kDefaultRoomPrice = 40.00_GBP;
    constexpr money64 kMaxRoomPrice = 500.00_GBP;
    constexpr money64 kRoomPriceStep = 5.00_GBP;

    struct Size
    {
        uint8_t width;  // tiles along x
        uint8_t length; // tiles along y
    };

    // Footprints the player can pick from, smallest first.
    constexpr std::array<Size, 7> kSizes = { {
        { 2, 2 },
        { 4, 2 },
        { 4, 4 },
        { 6, 4 },
        { 6, 6 },
        { 8, 6 },
        { 8, 8 },
    } };

    constexpr uint32_t GetSizeCapacity(uint8_t sizeIndex)
    {
        return sizeIndex < kSizes.size() ? kSizes[sizeIndex].width * kSizes[sizeIndex].length * kGuestsPerTile : 0;
    }

    // Large scenery used to build hotels, best first. All have a 1x1 or 2x2 footprint so they tile
    // every size above. All are part of the base game.
    constexpr std::array<std::string_view, 5> kBuildingObjects = {
        "rct2.scenery_large.sob",  // Office Block
        "rct2.scenery_large.sth",  // Town Hall
        "rct2.scenery_large.sah",  // House
        "rct2.scenery_large.sah2", // House
        "rct2.scenery_large.sah3", // Tenement
    };

    struct Building
    {
        uint8_t id = 0; // 1-based, stable for the life of the hotel; shown as "Hotel <id>"
        TileCoordsXY origin;
        uint8_t width = 0;
        uint8_t length = 0;
        int32_t baseZ = 0; // coords
        ObjectEntryIndex sceneryEntry = kObjectEntryIndexNull;
        uint8_t blockWidth = 1;
        uint8_t blockLength = 1;

        // Footpath tile guests walk to. Re-checked every few seconds.
        TileCoordsXYZ door;
        bool hasDoor = false;

        // Tiles still standing; capacity follows this, so a partly demolished hotel holds fewer guests.
        uint16_t tilesStanding = 0;

        // Tonight, and overall.
        uint32_t guestsTonight = 0;
        money64 revenueTonight = 0;
        uint32_t guestsLastNight = 0;
        money64 revenueLastNight = 0;
        money64 revenueTotal = 0;
        uint32_t nightsTotal = 0;

        uint32_t GetCapacity() const
        {
            return tilesStanding * kGuestsPerTile;
        }

        bool ContainsTile(const TileCoordsXY& tile) const
        {
            return tile.x >= origin.x && tile.x < origin.x + width && tile.y >= origin.y && tile.y < origin.y + length;
        }
    };

    enum class BookingStatus : uint8_t
    {
        daytime = 0, // in the park; will head to the hotel at wind-down
        inHotel = 1, // checked in for the night, hidden
    };

    struct Booking
    {
        EntityId guest = EntityId::GetNull();
        int32_t parkEntryTime = 0; // guards against the entity id being reused by another guest
        uint8_t hotelId = 0;
        uint8_t nightsLeft = 0;
        BookingStatus status = BookingStatus::daytime;
        uint32_t night = 0; // the night (day index of the evening) the guest checked in for
    };

    struct State
    {
        std::vector<Building> hotels;
        std::vector<Booking> bookings; // sorted by guest id
        money64 roomPrice = kDefaultRoomPrice;
        uint16_t lastMinute = 0xFFFF;
    };

    enum class Parameter : uint8_t
    {
        roomPrice,
        demolish, // value = hotel id; removes the record and the buildings stay as scenery
        count
    };

    // Called once per tick from Park::Update while the day cycle is on.
    void Update(GameState_t& gameState);

    // Hooks into the day cycle.
    void OnGuestAdmitted(GameState_t& gameState, Guest& guest);
    // True while a guest has a room for tonight: it ignores its planned stay, sleeps at the hotel and
    // doesn't count as a straggler.
    bool IsStayingGuest(const GameState_t& gameState, const Guest& guest);
    // True if the guest is checked in and hidden inside a hotel.
    bool IsInHotel(const GameState_t& gameState, const Guest& guest);
    // Middle of the hotel a hidden guest is sleeping in, for the guest window's viewport.
    std::optional<CoordsXYZ> GetGuestHotelView(const GameState_t& gameState, const Guest& guest);
    // Where a guest who is heading for the exit should go instead, if it has a room tonight.
    std::optional<TileCoordsXYZ> GetDestination(const GameState_t& gameState, const Peep& peep);

    // Placement helpers, shared by the place action and the UI preview.
    const Building* GetHotel(const GameState_t& gameState, uint8_t hotelId);
    std::optional<TileCoordsXYZ> FindDoor(const TileCoordsXY& origin, uint8_t width, uint8_t length, int32_t baseZ);
    int32_t GetFootprintBaseZ(const TileCoordsXY& origin, uint8_t width, uint8_t length);
    bool FootprintOverlapsHotel(const GameState_t& gameState, const TileCoordsXY& origin, uint8_t width, uint8_t length);
    // Loaded large scenery entry to build with, or null if none of kBuildingObjects is loaded.
    ObjectEntryIndex FindBuildingEntry();
    void GetBlockSize(ObjectEntryIndex entry, uint8_t& blockWidth, uint8_t& blockLength);
    void AddHotel(GameState_t& gameState, Building building);

    uint32_t GetTotalCapacity(const GameState_t& gameState);
    uint32_t GetBookedRooms(const GameState_t& gameState, uint8_t hotelId);
    uint32_t GetGuestsInside(const GameState_t& gameState, uint8_t hotelId);
    std::string GetHotelName(uint8_t hotelId);

    bool IsValidParameter(const GameState_t& gameState, Parameter parameter, uint32_t value);
    void ApplyParameter(GameState_t& gameState, Parameter parameter, uint32_t value);

    // Removes every booking and releases anyone inside, e.g. when the day cycle is switched off.
    void ReleaseEveryone(GameState_t& gameState);
} // namespace OpenRCT2::Hotel
