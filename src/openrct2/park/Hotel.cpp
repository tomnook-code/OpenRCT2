/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Hotel.h"

#include "../Context.h"
#include "../GameState.h"
#include "../actions/GameActionResult.h"
#include "../entity/EntityList.h"
#include "../entity/EntityRegistry.h"
#include "../entity/Guest.h"
#include "../interface/WindowClasses.h"
#include "../localisation/Formatting.h"
#include "../management/Finance.h"
#include "../management/NewsItem.h"
#include "../object/LargeSceneryEntry.h"
#include "../object/ObjectEntryManager.h"
#include "../object/ObjectManager.h"
#include "../ui/WindowManager.h"
#include "../world/Footpath.h"
#include "../world/Map.h"
#include "../world/Park.h"
#include "../world/TileElementsView.h"
#include "../world/tile_element/LargeSceneryElement.h"
#include "../world/tile_element/PathElement.h"
#include "../world/tile_element/SurfaceElement.h"
#include "../world/tile_element/TileElement.h"
#include "ParkDay.h"

#include <algorithm>
#include <cstdio>

namespace OpenRCT2::Hotel
{
    // How many guests leave the hotels per game tick in the morning, so they don't appear as one blob.
    constexpr uint32_t kReleasesPerTick = 4;
    // How far (in coords z) a footpath may sit above or below the hotel base and still serve as its door.
    constexpr int32_t kDoorMaxStep = 2 * kCoordsZStep * 2;

    static uint32_t HashGuest(uint32_t id, uint32_t entryTime)
    {
        uint32_t h = id * 0x9E3779B1u ^ (entryTime * 0x85EBCA77u + 0x27D4EB2Fu);
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        return h;
    }

    // ---------------------------------------------------------------------------------------------
    // Bookings

    static auto FindBookingIt(State& st, EntityId id)
    {
        return std::lower_bound(
            st.bookings.begin(), st.bookings.end(), id, [](const Booking& b, EntityId v) { return b.guest < v; });
    }

    static Booking* FindBooking(State& st, const Guest& guest)
    {
        auto it = FindBookingIt(st, guest.id);
        if (it == st.bookings.end() || it->guest != guest.id || it->parkEntryTime != guest.parkEntryTime)
            return nullptr;
        return &*it;
    }

    static const Booking* FindBooking(const State& st, const Guest& guest)
    {
        return FindBooking(const_cast<State&>(st), guest);
    }

    static Building* FindHotel(State& st, uint8_t hotelId)
    {
        for (auto& h : st.hotels)
        {
            if (h.id == hotelId)
                return &h;
        }
        return nullptr;
    }

    const Building* GetHotel(const GameState_t& gameState, uint8_t hotelId)
    {
        return FindHotel(const_cast<State&>(gameState.hotels), hotelId);
    }

    std::string GetHotelName(uint8_t hotelId)
    {
        return "Hotel " + std::to_string(hotelId);
    }

    uint32_t GetBookedRooms(const GameState_t& gameState, uint8_t hotelId)
    {
        uint32_t n = 0;
        for (const auto& b : gameState.hotels.bookings)
        {
            if (b.hotelId == hotelId)
                n++;
        }
        return n;
    }

    uint32_t GetGuestsInside(const GameState_t& gameState, uint8_t hotelId)
    {
        uint32_t n = 0;
        for (const auto& b : gameState.hotels.bookings)
        {
            if (b.hotelId == hotelId && b.status == BookingStatus::inHotel)
                n++;
        }
        return n;
    }

    uint32_t GetTotalCapacity(const GameState_t& gameState)
    {
        uint32_t total = 0;
        for (const auto& h : gameState.hotels.hotels)
        {
            if (h.hasDoor)
                total += h.GetCapacity();
        }
        return total;
    }

    bool IsStayingGuest(const GameState_t& gameState, const Guest& guest)
    {
        if (gameState.hotels.bookings.empty())
            return false;
        const auto* b = FindBooking(gameState.hotels, guest);
        return b != nullptr && b->nightsLeft > 0;
    }

    bool IsInHotel(const GameState_t& gameState, const Guest& guest)
    {
        if (gameState.hotels.bookings.empty())
            return false;
        const auto* b = FindBooking(gameState.hotels, guest);
        return b != nullptr && b->status == BookingStatus::inHotel;
    }

    std::optional<CoordsXYZ> GetGuestHotelView(const GameState_t& gameState, const Guest& guest)
    {
        if (guest.x != kLocationNull || gameState.hotels.bookings.empty())
            return std::nullopt;
        const auto* b = FindBooking(gameState.hotels, guest);
        if (b == nullptr || b->status != BookingStatus::inHotel)
            return std::nullopt;
        const auto* h = GetHotel(gameState, b->hotelId);
        if (h == nullptr)
            return std::nullopt;
        auto centre = h->origin.toCoordsXY();
        centre.x += h->width * kCoordsXYHalfTile;
        centre.y += h->length * kCoordsXYHalfTile;
        return CoordsXYZ{ centre, h->baseZ + 4 * kCoordsZStep };
    }

    std::optional<TileCoordsXYZ> GetDestination(const GameState_t& gameState, const Peep& peep)
    {
        const auto& st = gameState.hotels;
        if (st.bookings.empty())
            return std::nullopt;
        const auto* guest = peep.as<Guest>();
        if (guest == nullptr)
            return std::nullopt;
        const auto* b = FindBooking(st, *guest);
        if (b == nullptr || b->status != BookingStatus::daytime || b->nightsLeft == 0)
            return std::nullopt;
        const auto* h = GetHotel(gameState, b->hotelId);
        if (h == nullptr || !h->hasDoor)
            return std::nullopt;
        return h->door;
    }

    void OnGuestAdmitted(GameState_t& gameState, Guest& guest)
    {
        auto& st = gameState.hotels;
        if (!ParkDay::IsEnabled(gameState) || st.hotels.empty())
            return;

        const auto h = HashGuest(guest.id.ToUnderlying(), static_cast<uint32_t>(guest.parkEntryTime));
        if (h % 100 >= kBookingChancePercent)
            return;

        if (!gameState.park.flags.has(ParkFlag::noMoney) && guest.cashInPocket < st.roomPrice)
            return;

        // Pick the hotel with the most free rooms.
        Building* best = nullptr;
        uint32_t bestFree = 0;
        for (auto& hotel : st.hotels)
        {
            if (!hotel.hasDoor)
                continue;
            auto booked = GetBookedRooms(gameState, hotel.id);
            auto capacity = hotel.GetCapacity();
            if (booked < capacity && capacity - booked > bestFree)
            {
                bestFree = capacity - booked;
                best = &hotel;
            }
        }
        if (best == nullptr)
            return;

        // One night for half the bookings, two for 30%, three for 20%.
        const auto roll = (h >> 8) % 10;
        const uint8_t nights = roll < 5 ? 1 : roll < 8 ? 2 : 3;

        Booking booking;
        booking.guest = guest.id;
        booking.parkEntryTime = guest.parkEntryTime;
        booking.hotelId = best->id;
        booking.nightsLeft = nights;
        booking.status = BookingStatus::daytime;

        auto it = FindBookingIt(st, guest.id);
        if (it != st.bookings.end() && it->guest == guest.id)
            *it = booking; // stale booking of a guest that used to have this id
        else
            st.bookings.insert(it, booking);
    }

    // ---------------------------------------------------------------------------------------------
    // Buildings

    void GetBlockSize(ObjectEntryIndex entry, uint8_t& blockWidth, uint8_t& blockLength)
    {
        blockWidth = 1;
        blockLength = 1;
        const auto* sceneryEntry = ObjectEntryManager::GetObjectEntry<LargeSceneryEntry>(entry);
        if (sceneryEntry == nullptr)
            return;
        int32_t maxX = 0;
        int32_t maxY = 0;
        for (const auto& tile : sceneryEntry->tiles)
        {
            maxX = std::max<int32_t>(maxX, tile.offset.x);
            maxY = std::max<int32_t>(maxY, tile.offset.y);
        }
        blockWidth = static_cast<uint8_t>(maxX / kCoordsXYStep + 1);
        blockLength = static_cast<uint8_t>(maxY / kCoordsXYStep + 1);
    }

    ObjectEntryIndex FindBuildingEntry()
    {
        auto& objectManager = GetContext()->GetObjectManager();
        for (auto identifier : kBuildingObjects)
        {
            auto index = objectManager.GetLoadedObjectEntryIndex(identifier);
            if (index != kObjectEntryIndexNull)
                return index;
        }
        return kObjectEntryIndexNull;
    }

    int32_t GetFootprintBaseZ(const TileCoordsXY& origin, uint8_t width, uint8_t length)
    {
        int32_t maxZ = 0;
        for (int32_t y = origin.y; y < origin.y + length; y++)
        {
            for (int32_t x = origin.x; x < origin.x + width; x++)
            {
                auto coords = TileCoordsXY{ x, y }.toCoordsXY();
                if (!MapIsLocationValid(coords))
                    continue;
                maxZ = std::max<int32_t>(maxZ, TileElementHeight(coords));
            }
        }
        return maxZ;
    }

    bool FootprintOverlapsHotel(const GameState_t& gameState, const TileCoordsXY& origin, uint8_t width, uint8_t length)
    {
        for (const auto& h : gameState.hotels.hotels)
        {
            if (origin.x < h.origin.x + h.width && origin.x + width > h.origin.x && origin.y < h.origin.y + h.length
                && origin.y + length > h.origin.y)
                return true;
        }
        return false;
    }

    std::optional<TileCoordsXYZ> FindDoor(const TileCoordsXY& origin, uint8_t width, uint8_t length, int32_t baseZ)
    {
        // Walk the tiles around the footprint and pick the footpath closest to the middle of a side.
        const int32_t centreX2 = origin.x * 2 + width - 1;
        const int32_t centreY2 = origin.y * 2 + length - 1;
        std::optional<TileCoordsXYZ> best;
        int32_t bestScore = std::numeric_limits<int32_t>::max();

        auto consider = [&](int32_t x, int32_t y) {
            TileCoordsXY tile{ x, y };
            if (!MapIsLocationValid(tile.toCoordsXY()))
                return;
            for (auto* path : TileElementsView<PathElement>(tile.toCoordsXY()))
            {
                if (path->isQueue())
                    continue;
                if (std::abs(path->getBaseZ() - baseZ) > kDoorMaxStep)
                    continue;
                const int32_t score = std::abs(x * 2 - centreX2) + std::abs(y * 2 - centreY2)
                    + std::abs(path->getBaseZ() - baseZ) / kCoordsZStep;
                if (score < bestScore)
                {
                    bestScore = score;
                    best = TileCoordsXYZ{ x, y, path->baseHeight };
                }
            }
        };

        for (int32_t x = origin.x; x < origin.x + width; x++)
        {
            consider(x, origin.y - 1);
            consider(x, origin.y + length);
        }
        for (int32_t y = origin.y; y < origin.y + length; y++)
        {
            consider(origin.x - 1, y);
            consider(origin.x + width, y);
        }
        return best;
    }

    static uint16_t CountTilesStanding(const Building& h)
    {
        uint16_t standing = 0;
        const auto blockTiles = static_cast<uint16_t>(h.blockWidth * h.blockLength);
        for (int32_t by = 0; by < h.length; by += h.blockLength)
        {
            for (int32_t bx = 0; bx < h.width; bx += h.blockWidth)
            {
                // A block counts while its anchor tile is still there.
                auto coords = TileCoordsXY{ h.origin.x + bx, h.origin.y + by }.toCoordsXY();
                for (auto* el : TileElementsView<LargeSceneryElement>(coords))
                {
                    if (el->getEntryIndex() == h.sceneryEntry && el->getSequenceIndex() == 0 && el->getBaseZ() == h.baseZ)
                    {
                        standing += blockTiles;
                        break;
                    }
                }
            }
        }
        return standing;
    }

    void AddHotel(GameState_t& gameState, Building building)
    {
        auto& st = gameState.hotels;
        uint8_t id = 1;
        while (FindHotel(st, id) != nullptr)
            id++;
        building.id = id;
        building.tilesStanding = CountTilesStanding(building);
        auto door = FindDoor(building.origin, building.width, building.length, building.baseZ);
        building.hasDoor = door.has_value();
        if (door)
            building.door = *door;
        st.hotels.push_back(building);
    }

    // ---------------------------------------------------------------------------------------------
    // Guests

    static void InvalidateGuest(const Guest& guest)
    {
        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->InvalidateByNumber(WindowClass::peep, guest.id);
        windowMgr->InvalidateByClass(WindowClass::guestList);
    }

    static bool IsOnTile(const Guest& guest, const TileCoordsXYZ& tile)
    {
        if (guest.x == kLocationNull)
            return false;
        auto loc = TileCoordsXY(CoordsXY{ guest.x, guest.y });
        return loc.x == tile.x && loc.y == tile.y && std::abs(guest.z - tile.z * kCoordsZStep) <= 2 * kCoordsZStep * 2;
    }

    // Which night it is: before the gates open it still counts as the previous evening.
    static uint32_t GetNightIndex(const GameState_t& gameState)
    {
        const auto day = ParkDay::GetDayIndex(gameState);
        if (ParkDay::GetMinuteOfDay(gameState) < gameState.parkDay.settings.openMinute && day > 0)
            return day - 1;
        return day;
    }

    static bool CheckIn(GameState_t& gameState, Guest& guest, Booking& booking, Building& hotel)
    {
        const auto price = gameState.hotels.roomPrice;
        if (!gameState.park.flags.has(ParkFlag::noMoney) && price > 0)
        {
            if (guest.cashInPocket < price)
                return false;
            guest.spendMoney(price, ExpenditureType::shopSales);
            hotel.revenueTonight += price;
            hotel.revenueTotal += price;
        }

        guest.guestHeadingToRideId = RideId::GetNull();
        guest.peepFlags.set(PeepFlag::positionFrozen);
        guest.peepFlags.set(PeepFlag::animationFrozen);
        guest.moveTo({ kLocationNull, guest.y, guest.z });

        booking.status = BookingStatus::inHotel;
        booking.night = GetNightIndex(gameState);
        hotel.guestsTonight++;
        hotel.nightsTotal++;
        InvalidateGuest(guest);
        return true;
    }

    static void RestoreAfterNight(Guest& guest)
    {
        guest.energy = kPeepMaxEnergy;
        guest.energyTarget = kPeepMaxEnergy;
        guest.hunger = std::max<uint8_t>(guest.hunger, 200);
        guest.thirst = std::max<uint8_t>(guest.thirst, 200);
        guest.toilet = 0;
        guest.nausea = 0;
        guest.nauseaTarget = 0;
        guest.happinessTarget = std::max<uint8_t>(guest.happinessTarget, 200);
        guest.angriness = 0;
    }

    // Puts a hidden guest back on the map at the hotel door (or wherever it can stand).
    static void PutBackOnMap(GameState_t& gameState, Guest& guest, const Building* hotel)
    {
        guest.peepFlags.unset(PeepFlag::positionFrozen);
        guest.peepFlags.unset(PeepFlag::animationFrozen);
        guest.peepFlags.unset(PeepFlag::leavingPark);
        guest.peepFlags.unset(PeepFlag::parkEntranceChosen);
        guest.guestIsLostCountdown = 254;
        guest.guestHeadingToRideId = RideId::GetNull();
        guest.setState(PeepState::walking);

        bool placed = false;
        if (hotel != nullptr && hotel->hasDoor)
        {
            placed = guest.place(hotel->door, true).error == GameActions::Status::ok;
        }
        if (!placed && hotel != nullptr)
        {
            // The door is gone: try the ground next to the building.
            if (auto door = FindDoor(hotel->origin, hotel->width, hotel->length, hotel->baseZ))
                placed = guest.place(*door, true).error == GameActions::Status::ok;
        }
        if (!placed)
        {
            // Nowhere to stand: the guest goes home.
            guest.remove();
            return;
        }
        InvalidateGuest(guest);
    }

    static void Release(GameState_t& gameState, Guest& guest, Booking& booking)
    {
        auto* hotel = FindHotel(gameState.hotels, booking.hotelId);
        RestoreAfterNight(guest);

        // A fresh planned stay for the day ahead.
        guest.parkEntryTime = static_cast<int32_t>(gameState.currentTicks);
        booking.parkEntryTime = guest.parkEntryTime;
        booking.status = BookingStatus::daytime;
        if (booking.nightsLeft > 0)
            booking.nightsLeft--;

        PutBackOnMap(gameState, guest, hotel);
    }

    static Guest* GetBookedGuest(GameState_t& gameState, const Booking& booking)
    {
        auto* guest = gameState.entities.tryGetEntity<Guest>(booking.guest);
        if (guest == nullptr || guest->parkEntryTime != booking.parkEntryTime)
            return nullptr;
        return guest;
    }

    // Drops bookings whose guest has left or was removed, and fixes guests someone moved by hand.
    static void PruneBookings(GameState_t& gameState)
    {
        auto& st = gameState.hotels;
        std::erase_if(st.bookings, [&](Booking& b) {
            auto* guest = GetBookedGuest(gameState, b);
            if (guest == nullptr || guest->outsideOfPark)
                return true;
            if (b.status == BookingStatus::inHotel && guest->x != kLocationNull)
            {
                // Picked up and dropped back on the map by the player: treat it as checked out.
                guest->peepFlags.unset(PeepFlag::positionFrozen);
                guest->peepFlags.unset(PeepFlag::animationFrozen);
                b.status = BookingStatus::daytime;
                if (b.nightsLeft > 0)
                    b.nightsLeft--;
            }
            if (b.status == BookingStatus::daytime && b.nightsLeft == 0)
                return true;
            if (FindHotel(st, b.hotelId) == nullptr)
                return b.status == BookingStatus::daytime;
            return false;
        });
    }

    static void RefreshBuildings(GameState_t& gameState)
    {
        auto& st = gameState.hotels;
        for (auto& h : st.hotels)
        {
            h.tilesStanding = CountTilesStanding(h);
            bool doorOk = false;
            if (h.hasDoor)
            {
                for (auto* path : TileElementsView<PathElement>(h.door.toCoordsXY()))
                {
                    if (path->baseHeight == h.door.z && !path->isQueue())
                    {
                        doorOk = true;
                        break;
                    }
                }
            }
            if (!doorOk)
            {
                auto door = FindDoor(h.origin, h.width, h.length, h.baseZ);
                h.hasDoor = door.has_value();
                if (door)
                    h.door = *door;
            }
        }

        // A hotel with nothing left standing is gone. Anyone inside is put back outside.
        std::vector<uint8_t> demolished;
        for (const auto& h : st.hotels)
        {
            if (h.tilesStanding == 0)
                demolished.push_back(h.id);
        }
        for (auto id : demolished)
            ApplyParameter(gameState, Parameter::demolish, id);
    }

    static void PostNightlyReport(GameState_t& gameState)
    {
        auto& st = gameState.hotels;
        uint32_t guests = 0;
        money64 revenue = 0;
        uint32_t capacity = 0;
        for (const auto& h : st.hotels)
        {
            guests += h.guestsTonight;
            revenue += h.revenueTonight;
            capacity += h.GetCapacity();
        }
        if (st.hotels.empty())
            return;

        char buffer[256];
        if (gameState.park.flags.has(ParkFlag::noMoney))
        {
            std::snprintf(buffer, sizeof(buffer), "%u guests stayed overnight in your hotels (%u rooms).", guests, capacity);
        }
        else
        {
            std::snprintf(
                buffer, sizeof(buffer), "%u guests stayed overnight in your hotels (%u rooms), paying %s in total.", guests,
                capacity, FormatString("{CURRENCY2DP}", revenue).c_str());
        }
        gameState.park.newsItems.addItemToQueue(News::ItemType::money, buffer, 0);
    }

    static void StartNight(GameState_t& gameState)
    {
        for (auto& h : gameState.hotels.hotels)
        {
            h.guestsLastNight = h.guestsTonight;
            h.revenueLastNight = h.revenueTonight;
            h.guestsTonight = 0;
            h.revenueTonight = 0;
        }
    }

    // Booked guests that never made it to the hotel go home instead.
    static void CancelMissedCheckIns(GameState_t& gameState)
    {
        auto& st = gameState.hotels;
        std::erase_if(st.bookings, [](const Booking& b) { return b.status == BookingStatus::daytime; });
    }

    void ReleaseEveryone(GameState_t& gameState)
    {
        auto& st = gameState.hotels;
        auto bookings = std::move(st.bookings);
        st.bookings.clear();
        for (auto& b : bookings)
        {
            if (b.status != BookingStatus::inHotel)
                continue;
            if (auto* guest = GetBookedGuest(gameState, b))
            {
                RestoreAfterNight(*guest);
                PutBackOnMap(gameState, *guest, FindHotel(st, b.hotelId));
            }
        }
    }

    void Update(GameState_t& gameState)
    {
        auto& st = gameState.hotels;
        if (st.hotels.empty() && st.bookings.empty())
            return;

        if (!ParkDay::IsEnabled(gameState))
        {
            if (!st.bookings.empty())
                ReleaseEveryone(gameState);
            return;
        }

        const auto& s = gameState.parkDay.settings;
        const auto minute = ParkDay::GetMinuteOfDay(gameState);
        const auto ticks = gameState.currentTicks;

        if ((ticks & 63) == 0)
        {
            RefreshBuildings(gameState);
            PruneBookings(gameState);
        }

        const uint32_t windDown = s.closeMinute > ParkDay::kWindDownMinutes ? s.closeMinute - ParkDay::kWindDownMinutes : 0;
        // Guests have all night to reach the hotel; whoever hasn't by an hour before opening goes home.
        const uint32_t nightEnd = s.openMinute > ParkDay::kMorningLeadMinutes ? s.openMinute - ParkDay::kMorningLeadMinutes : 0;

        const auto prev = st.lastMinute;
        st.lastMinute = minute;
        if (prev != 0xFFFF && prev != minute)
        {
            auto crossed = [prev, minute](uint32_t m) {
                m %= ParkDay::kMinutesPerDay;
                return prev < minute ? (prev < m && minute >= m) : (prev < m || minute >= m);
            };
            if (crossed(nightEnd))
            {
                CancelMissedCheckIns(gameState);
                PostNightlyReport(gameState);
                StartNight(gameState);
            }
        }

        // Booked guests heading "home" walk to the hotel and check in when they reach the door. That is
        // normally after the evening wind-down, but a tired guest may go back to its room earlier.
        if ((ticks & 1) == 0)
        {
            std::vector<EntityId> refused;
            for (auto& b : st.bookings)
            {
                if (b.status != BookingStatus::daytime || b.nightsLeft == 0)
                    continue;
                auto* hotel = FindHotel(st, b.hotelId);
                if (hotel == nullptr || !hotel->hasDoor)
                    continue;
                auto* guest = GetBookedGuest(gameState, b);
                if (guest == nullptr || guest->state != PeepState::walking || !guest->peepFlags.has(PeepFlag::leavingPark)
                    || !IsOnTile(*guest, hotel->door))
                    continue;
                if (!CheckIn(gameState, *guest, b, *hotel))
                    refused.push_back(b.guest);
            }
            // Guests who can't pay go home.
            for (auto id : refused)
            {
                auto it = FindBookingIt(st, id);
                if (it != st.bookings.end() && it->guest == id)
                {
                    if (auto* guest = GetBookedGuest(gameState, *it))
                        guest->peepFlags.unset(PeepFlag::parkEntranceChosen);
                    st.bookings.erase(it);
                }
            }
        }

        // Morning: guests who slept here come out a few at a time once the gates open.
        if (minute >= s.openMinute && minute < windDown)
        {
            const auto tonight = GetNightIndex(gameState);
            uint32_t released = 0;
            for (auto& b : st.bookings)
            {
                if (released >= kReleasesPerTick)
                    break;
                if (b.status != BookingStatus::inHotel || b.night >= tonight)
                    continue;
                if (auto* guest = GetBookedGuest(gameState, b))
                {
                    Release(gameState, *guest, b);
                    released++;
                }
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Settings

    bool IsValidParameter(const GameState_t& gameState, Parameter parameter, uint32_t value)
    {
        switch (parameter)
        {
            case Parameter::roomPrice:
                return value <= kMaxRoomPrice;
            case Parameter::demolish:
                return value <= 0xFF && GetHotel(gameState, static_cast<uint8_t>(value)) != nullptr;
            default:
                return false;
        }
    }

    void ApplyParameter(GameState_t& gameState, Parameter parameter, uint32_t value)
    {
        auto& st = gameState.hotels;
        switch (parameter)
        {
            case Parameter::roomPrice:
                st.roomPrice = value;
                break;
            case Parameter::demolish:
            {
                const auto id = static_cast<uint8_t>(value);
                auto* hotel = FindHotel(st, id);
                if (hotel == nullptr)
                    break;
                // Put everyone inside back outside before the record goes.
                for (auto& b : st.bookings)
                {
                    if (b.hotelId != id || b.status != BookingStatus::inHotel)
                        continue;
                    if (auto* guest = GetBookedGuest(gameState, b))
                    {
                        RestoreAfterNight(*guest);
                        PutBackOnMap(gameState, *guest, hotel);
                    }
                }
                std::erase_if(st.bookings, [id](const Booking& b) { return b.hotelId == id; });
                std::erase_if(st.hotels, [id](const Building& h) { return h.id == id; });
                break;
            }
            default:
                break;
        }
    }
} // namespace OpenRCT2::Hotel
