/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ParkDaySetAction.h"

#include "../../GameState.h"
#include "../../interface/WindowClasses.h"
#include "../../ui/WindowManager.h"

namespace OpenRCT2::GameActions
{
    ParkDaySetAction::ParkDaySetAction(ParkDay::Parameter parameter, uint32_t value)
        : _parameter(parameter)
        , _value(value)
    {
    }

    void ParkDaySetAction::AcceptParameters(GameActionParameterVisitor& visitor)
    {
        visitor.Visit("parameter", _parameter);
        visitor.Visit("value", _value);
    }

    uint16_t ParkDaySetAction::GetActionFlags() const
    {
        return GameAction::GetActionFlags() | Flags::AllowWhilePaused;
    }

    void ParkDaySetAction::Serialise(DataSerialiser& stream)
    {
        GameAction::Serialise(stream);
        stream << DS_TAG(_parameter) << DS_TAG(_value);
    }

    Result ParkDaySetAction::Query(GameState_t& gameState, Park::ParkData& park) const
    {
        if (_parameter >= ParkDay::Parameter::count
            || !ParkDay::IsValidParameter(gameState.parkDay.settings, _parameter, _value))
        {
            return Result(Status::invalidParameters, STR_ERR_INVALID_PARAMETER, STR_ERR_VALUE_OUT_OF_RANGE);
        }
        return Result();
    }

    Result ParkDaySetAction::Execute(GameState_t& gameState, Park::ParkData& park) const
    {
        if (_parameter >= ParkDay::Parameter::count
            || !ParkDay::IsValidParameter(gameState.parkDay.settings, _parameter, _value))
        {
            return Result(Status::invalidParameters, STR_ERR_INVALID_PARAMETER, STR_ERR_VALUE_OUT_OF_RANGE);
        }

        ParkDay::ApplyParameter(gameState, _parameter, _value);

        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->InvalidateByClass(WindowClass::parkDay);
        windowMgr->InvalidateByClass(WindowClass::dateInfoPanel);
        windowMgr->InvalidateByClass(WindowClass::ride);
        windowMgr->InvalidateByClass(WindowClass::parkInformation);
        return Result();
    }
} // namespace OpenRCT2::GameActions
