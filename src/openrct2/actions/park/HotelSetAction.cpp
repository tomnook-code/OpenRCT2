/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "HotelSetAction.h"

#include "../../GameState.h"
#include "../../interface/WindowClasses.h"
#include "../../ui/WindowManager.h"

namespace OpenRCT2::GameActions
{
    HotelSetAction::HotelSetAction(Hotel::Parameter parameter, uint32_t value)
        : _parameter(parameter)
        , _value(value)
    {
    }

    void HotelSetAction::AcceptParameters(GameActionParameterVisitor& visitor)
    {
        visitor.Visit("parameter", _parameter);
        visitor.Visit("value", _value);
    }

    uint16_t HotelSetAction::GetActionFlags() const
    {
        return GameAction::GetActionFlags() | Flags::AllowWhilePaused;
    }

    void HotelSetAction::Serialise(DataSerialiser& stream)
    {
        GameAction::Serialise(stream);
        stream << DS_TAG(_parameter) << DS_TAG(_value);
    }

    Result HotelSetAction::Query(GameState_t& gameState, Park::ParkData& park) const
    {
        if (_parameter >= Hotel::Parameter::count || !Hotel::IsValidParameter(gameState, _parameter, _value))
        {
            return Result(Status::invalidParameters, STR_ERR_INVALID_PARAMETER, STR_ERR_VALUE_OUT_OF_RANGE);
        }
        return Result();
    }

    Result HotelSetAction::Execute(GameState_t& gameState, Park::ParkData& park) const
    {
        if (_parameter >= Hotel::Parameter::count || !Hotel::IsValidParameter(gameState, _parameter, _value))
        {
            return Result(Status::invalidParameters, STR_ERR_INVALID_PARAMETER, STR_ERR_VALUE_OUT_OF_RANGE);
        }

        Hotel::ApplyParameter(gameState, _parameter, _value);

        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->InvalidateByClass(WindowClass::hotels);
        return Result();
    }
} // namespace OpenRCT2::GameActions
