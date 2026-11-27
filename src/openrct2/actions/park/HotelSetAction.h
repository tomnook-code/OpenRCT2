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
    // Fork: changes a hotel setting (room price) or closes a hotel.
    class HotelSetAction final : public GameActionBase<GameCommand::hotelSet>
    {
    private:
        Hotel::Parameter _parameter{ Hotel::Parameter::count };
        uint32_t _value{};

    public:
        HotelSetAction() = default;
        HotelSetAction(Hotel::Parameter parameter, uint32_t value);

        void AcceptParameters(GameActionParameterVisitor&) final;

        uint16_t GetActionFlags() const override;

        void Serialise(DataSerialiser& stream) override;
        Result Query(GameState_t& gameState, Park::ParkData& park) const override;
        Result Execute(GameState_t& gameState, Park::ParkData& park) const override;
    };
} // namespace OpenRCT2::GameActions
