/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../../park/Hotel.h"
#include "../GameAction.hpp"

namespace OpenRCT2::GameActions
{
    // Fork: builds an overnight hotel out of large scenery and registers it.
    class HotelPlaceAction final : public GameActionBase<GameCommand::hotelPlace>
    {
    private:
        CoordsXY _loc; // north-west corner tile of the footprint
        uint8_t _sizeIndex{};
        ObjectEntryIndex _sceneryType{ kObjectEntryIndexNull };

    public:
        HotelPlaceAction() = default;
        HotelPlaceAction(const CoordsXY& loc, uint8_t sizeIndex, ObjectEntryIndex sceneryType);

        void AcceptParameters(GameActionParameterVisitor&) final;

        uint16_t GetActionFlags() const override;

        void Serialise(DataSerialiser& stream) override;
        Result Query(GameState_t& gameState, Park::ParkData& park) const override;
        Result Execute(GameState_t& gameState, Park::ParkData& park) const override;

    private:
        Result QueryExecute(GameState_t& gameState, bool executing) const;
    };
} // namespace OpenRCT2::GameActions
