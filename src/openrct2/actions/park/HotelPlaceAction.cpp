/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "HotelPlaceAction.h"

#include "../../GameState.h"
#include "../../drawing/Colour.h"
#include "../../interface/WindowClasses.h"
#include "../../management/Finance.h"
#include "../../object/LargeSceneryEntry.h"
#include "../../object/ObjectEntryManager.h"
#include "../../object/ObjectLimits.h"
#include "../../park/ParkDay.h"
#include "../../ui/WindowManager.h"
#include "../../world/Map.h"
#include "../GameActionRunner.h"
#include "../scenery/LargeSceneryPlaceAction.h"

namespace OpenRCT2::GameActions
{
    HotelPlaceAction::HotelPlaceAction(const CoordsXY& loc, uint8_t sizeIndex, ObjectEntryIndex sceneryType)
        : _loc(loc)
        , _sizeIndex(sizeIndex)
        , _sceneryType(sceneryType)
    {
    }

    void HotelPlaceAction::AcceptParameters(GameActionParameterVisitor& visitor)
    {
        visitor.Visit(_loc);
        visitor.Visit("size", _sizeIndex);
        visitor.Visit("object", _sceneryType);
    }

    uint16_t HotelPlaceAction::GetActionFlags() const
    {
        return GameAction::GetActionFlags();
    }

    void HotelPlaceAction::Serialise(DataSerialiser& stream)
    {
        GameAction::Serialise(stream);
        stream << DS_TAG(_loc) << DS_TAG(_sizeIndex) << DS_TAG(_sceneryType);
    }

    Result HotelPlaceAction::Query(GameState_t& gameState, Park::ParkData& park) const
    {
        return QueryExecute(gameState, false);
    }

    Result HotelPlaceAction::Execute(GameState_t& gameState, Park::ParkData& park) const
    {
        return QueryExecute(gameState, true);
    }

    static Result HotelError(Status status, const char* message)
    {
        Result res;
        res.error = status;
        res.errorTitle = std::string("Can't build the hotel here...");
        res.errorMessage = std::string(message);
        return res;
    }

    Result HotelPlaceAction::QueryExecute(GameState_t& gameState, bool executing) const
    {
        if (!ParkDay::IsEnabled(gameState))
            return HotelError(Status::disallowed, "Turn on the realistic day cycle first");
        if (_sizeIndex >= Hotel::kSizes.size())
            return HotelError(Status::invalidParameters, "Unknown hotel size");
        if (gameState.hotels.hotels.size() >= Hotel::kMaxHotels)
            return HotelError(Status::disallowed, "Too many hotels in the park");
        if (_sceneryType >= kMaxLargeSceneryObjects
            || ObjectEntryManager::GetObjectEntry<LargeSceneryEntry>(_sceneryType) == nullptr)
            return HotelError(Status::invalidParameters, "The hotel building scenery is not loaded");

        const auto size = Hotel::kSizes[_sizeIndex];
        uint8_t blockWidth = 1;
        uint8_t blockLength = 1;
        Hotel::GetBlockSize(_sceneryType, blockWidth, blockLength);
        if (size.width % blockWidth != 0 || size.length % blockLength != 0)
            return HotelError(Status::invalidParameters, "This building doesn't fit the hotel size");

        const auto origin = TileCoordsXY(_loc);
        for (auto corner : { origin, TileCoordsXY{ origin.x + size.width - 1, origin.y + size.length - 1 } })
        {
            if (!MapIsLocationValid(corner.toCoordsXY()) || MapIsEdge(corner.toCoordsXY()))
                return HotelError(Status::disallowed, "Off edge of map");
        }
        if (Hotel::FootprintOverlapsHotel(gameState, origin, size.width, size.length))
            return HotelError(Status::disallowed, "There is already a hotel here");

        const auto baseZ = Hotel::GetFootprintBaseZ(origin, size.width, size.length);
        if (!Hotel::FindDoor(origin, size.width, size.length, baseZ).has_value())
            return HotelError(Status::disallowed, "The hotel must be next to a footpath at about the same height");

        Result res;
        res.expenditure = ExpenditureType::landscaping;
        res.position = { origin.toCoordsXY().toTileCentre(), baseZ };
        res.position.x += (size.width - 1) * kCoordsXYHalfTile;
        res.position.y += (size.length - 1) * kCoordsXYHalfTile;

        for (int32_t by = 0; by < size.length; by += blockLength)
        {
            for (int32_t bx = 0; bx < size.width; bx += blockWidth)
            {
                auto blockLoc = TileCoordsXY{ origin.x + bx, origin.y + by }.toCoordsXY();
                auto action = LargeSceneryPlaceAction(
                    { blockLoc, baseZ, 0 }, _sceneryType, Drawing::Colour::white, Drawing::Colour::grey,
                    Drawing::Colour::white);
                action.SetFlags(GetFlags());
                auto blockRes = executing ? ExecuteNested(&action, gameState) : QueryNested(&action, gameState);
                if (blockRes.error != Status::ok)
                {
                    blockRes.errorTitle = std::string("Can't build the hotel here...");
                    return blockRes;
                }
                res.cost += blockRes.cost;
            }
        }

        if (executing)
        {
            Hotel::Building building;
            building.origin = origin;
            building.width = size.width;
            building.length = size.length;
            building.baseZ = baseZ;
            building.sceneryEntry = _sceneryType;
            building.blockWidth = blockWidth;
            building.blockLength = blockLength;
            Hotel::AddHotel(gameState, building);

            auto* windowMgr = Ui::GetWindowManager();
            windowMgr->InvalidateByClass(WindowClass::hotels);
        }
        return res;
    }
} // namespace OpenRCT2::GameActions
