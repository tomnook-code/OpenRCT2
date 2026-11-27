/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../../park/ParkDay.h"
#include "../GameAction.hpp"

namespace OpenRCT2::GameActions
{
    // Fork: changes a realistic park day cycle setting.
    class ParkDaySetAction final : public GameActionBase<GameCommand::parkDaySet>
    {
    private:
        ParkDay::Parameter _parameter{ ParkDay::Parameter::count };
        uint32_t _value{};

    public:
        ParkDaySetAction() = default;
        ParkDaySetAction(ParkDay::Parameter parameter, uint32_t value);

        void AcceptParameters(GameActionParameterVisitor&) final;

        uint16_t GetActionFlags() const override;

        void Serialise(DataSerialiser& stream) override;
        Result Query(GameState_t& gameState, Park::ParkData& park) const override;
        Result Execute(GameState_t& gameState, Park::ParkData& park) const override;
    };
} // namespace OpenRCT2::GameActions
