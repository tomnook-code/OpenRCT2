/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Fork: build and manage overnight hotels for the realistic park day cycle.
// Strings are plain English on purpose, so no language files need to change.

#include <algorithm>
#include <cstdio>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/windows/Windows.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/actions/GameActionRunner.h>
#include <openrct2/actions/park/HotelPlaceAction.h>
#include <openrct2/actions/park/HotelSetAction.h>
#include <openrct2/audio/Audio.h>
#include <openrct2/drawing/Text.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/interface/Window.h>
#include <openrct2/localisation/Formatting.h>
#include <openrct2/network/Network.h>
#include <openrct2/object/Object.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/park/Hotel.h>
#include <openrct2/park/ParkDay.h>
#include <openrct2/ui/WindowManager.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/MapSelection.h>
#include <string>

namespace OpenRCT2::Ui::Windows
{
    static constexpr ScreenSize kWindowSize = { 300, 236 };

    enum WindowHotelsWidgetIdx : WidgetIndex
    {
        WIDX_BACKGROUND,
        WIDX_TITLE,
        WIDX_CLOSE,
        WIDX_SIZE,
        WIDX_SIZE_UP,
        WIDX_SIZE_DOWN,
        WIDX_BUILD,
        WIDX_PRICE,
        WIDX_PRICE_UP,
        WIDX_PRICE_DOWN,
        WIDX_SELECTED,
        WIDX_SELECTED_UP,
        WIDX_SELECTED_DOWN,
        WIDX_LOCATE,
        WIDX_REMOVE,
    };

    // clang-format off
    static constexpr auto kHotelsWidgets = makeWidgets(
        makeWindowShim(kStringIdNone, kWindowSize),
        makeSpinnerWidgets({120,  20}, {170, 12}, WidgetType::spinner, WindowColour::secondary), // size (3 widgets)
        makeWidget        ({ 10,  36}, {280, 14}, WidgetType::button,  WindowColour::secondary), // build
        makeSpinnerWidgets({120,  70}, {170, 12}, WidgetType::spinner, WindowColour::secondary), // room price (3 widgets)
        makeSpinnerWidgets({120, 104}, {170, 12}, WidgetType::spinner, WindowColour::secondary), // selected hotel (3 widgets)
        makeWidget        ({ 10, 216}, {137, 14}, WidgetType::button,  WindowColour::secondary), // locate
        makeWidget        ({153, 216}, {137, 14}, WidgetType::button,  WindowColour::secondary)  // close hotel
    );
    // clang-format on

    class HotelsWindow final : public Window
    {
        uint8_t _sizeIndex = 2;
        size_t _selected = 0;
        std::string _sizeText;
        std::string _priceText;
        std::string _selectedText;

        static void setParameter(Hotel::Parameter parameter, uint32_t value)
        {
            auto action = GameActions::HotelSetAction(parameter, value);
            GameActions::Execute(&action, getGameState());
        }

        bool isPlacing() const
        {
            return isToolActive(*this, WIDX_BUILD);
        }

        const Hotel::Building* selectedHotel() const
        {
            const auto& hotels = getGameState().hotels.hotels;
            if (hotels.empty())
                return nullptr;
            return &hotels[std::min(_selected, hotels.size() - 1)];
        }

        // Footprint under the cursor, centred on it.
        std::optional<TileCoordsXY> getOrigin(const ScreenCoordsXY& screenCoords) const
        {
            auto info = GetMapCoordinatesFromPos(
                screenCoords,
                { ViewportInteractionItem::terrain, ViewportInteractionItem::footpath, ViewportInteractionItem::scenery,
                  ViewportInteractionItem::largeScenery });
            if (info.interactionType == ViewportInteractionItem::none)
                return std::nullopt;
            const auto size = Hotel::kSizes[_sizeIndex];
            auto tile = TileCoordsXY(info.Loc);
            return TileCoordsXY{ tile.x - (size.width - 1) / 2, tile.y - (size.length - 1) / 2 };
        }

        static ObjectEntryIndex getOrLoadBuildingEntry()
        {
            auto entry = Hotel::FindBuildingEntry();
            if (entry != kObjectEntryIndexNull)
                return entry;

            // Loading objects mid-game isn't synchronised in multiplayer, so only do it in single player.
            if (Network::GetMode() != Network::Mode::none)
                return kObjectEntryIndexNull;

            auto& objectManager = GetContext()->GetObjectManager();
            for (auto identifier : Hotel::kBuildingObjects)
            {
                if (objectManager.LoadObject(identifier) != nullptr)
                    return Hotel::FindBuildingEntry();
            }
            return kObjectEntryIndexNull;
        }

        void startPlacing()
        {
            if (isPlacing())
            {
                ToolCancel();
                return;
            }
            if (!ParkDay::IsEnabled())
            {
                ErrorOpen("Can't build a hotel...", "Turn on the realistic day cycle first");
                return;
            }
            if (getOrLoadBuildingEntry() == kObjectEntryIndexNull)
            {
                ErrorOpen(
                    "Can't build a hotel...",
                    Network::GetMode() == Network::Mode::none ? "Office Block, Town Hall and House scenery are not installed"
                                                              : "Add Office Block (Urban Theming) to the park's scenery first");
                return;
            }
            ToolSet(*this, WIDX_BUILD, Tool::upArrow);
            ShowGridlines();
            invalidate();
        }

    public:
        void onOpen() override
        {
            setWidgets(kHotelsWidgets);
            widgets[WIDX_TITLE].setString("Hotels");
            widgets[WIDX_LOCATE].setString("Show on map");
            widgets[WIDX_REMOVE].setString("Close this hotel");
            WindowInitScrollWidgets(*this);
        }

        void onClose() override
        {
            if (isPlacing())
                ToolCancel();
        }

        void onUpdate() override
        {
            invalidate();
        }

        void onMouseUp(WidgetIndex widgetIndex) override
        {
            switch (widgetIndex)
            {
                case WIDX_CLOSE:
                    close();
                    break;
                case WIDX_BUILD:
                    startPlacing();
                    break;
                case WIDX_LOCATE:
                    if (const auto* h = selectedHotel())
                    {
                        auto* mainWindow = WindowGetMain();
                        if (mainWindow != nullptr)
                        {
                            auto centre = TileCoordsXY{ h->origin.x + h->width / 2, h->origin.y + h->length / 2 };
                            WindowScrollToLocation(*mainWindow, { centre.toCoordsXY(), h->baseZ });
                        }
                    }
                    break;
                case WIDX_REMOVE:
                    if (const auto* h = selectedHotel())
                        setParameter(Hotel::Parameter::demolish, h->id);
                    break;
            }
        }

        void onMouseDown(WidgetIndex widgetIndex) override
        {
            const auto& st = getGameState().hotels;
            const auto count = st.hotels.size();
            switch (widgetIndex)
            {
                case WIDX_SIZE_UP:
                    _sizeIndex = static_cast<uint8_t>(std::min<size_t>(_sizeIndex + 1, Hotel::kSizes.size() - 1));
                    break;
                case WIDX_SIZE_DOWN:
                    _sizeIndex = static_cast<uint8_t>(_sizeIndex > 0 ? _sizeIndex - 1 : 0);
                    break;
                case WIDX_PRICE_UP:
                    setParameter(
                        Hotel::Parameter::roomPrice,
                        static_cast<uint32_t>(std::min(st.roomPrice + Hotel::kRoomPriceStep, Hotel::kMaxRoomPrice)));
                    break;
                case WIDX_PRICE_DOWN:
                    setParameter(
                        Hotel::Parameter::roomPrice,
                        static_cast<uint32_t>(std::max<money64>(st.roomPrice - Hotel::kRoomPriceStep, 0)));
                    break;
                case WIDX_SELECTED_UP:
                    if (count > 0)
                        _selected = (std::min(_selected, count - 1) + 1) % count;
                    break;
                case WIDX_SELECTED_DOWN:
                    if (count > 0)
                        _selected = (std::min(_selected, count - 1) + count - 1) % count;
                    break;
            }
            invalidate();
        }

        void onToolUpdate(WidgetIndex widgetIndex, const ScreenCoordsXY& screenCoords) override
        {
            gMapSelectFlags.unset(MapSelectFlag::enable);
            auto origin = getOrigin(screenCoords);
            if (!origin)
                return;
            const auto size = Hotel::kSizes[_sizeIndex];
            gMapSelectFlags.set(MapSelectFlag::enable);
            gMapSelectType = MapSelectType::full;
            gMapSelectPositionA = origin->toCoordsXY();
            gMapSelectPositionB = TileCoordsXY{ origin->x + size.width - 1, origin->y + size.length - 1 }.toCoordsXY();
        }

        void onToolDown(WidgetIndex widgetIndex, const ScreenCoordsXY& screenCoords) override
        {
            auto origin = getOrigin(screenCoords);
            if (!origin)
                return;
            auto entry = getOrLoadBuildingEntry();
            if (entry == kObjectEntryIndexNull)
                return;

            auto action = GameActions::HotelPlaceAction(origin->toCoordsXY(), _sizeIndex, entry);
            auto result = GameActions::Execute(&action, getGameState());
            if (result.error == GameActions::Status::ok)
            {
                Audio::Play3D(Audio::SoundId::placeItem, result.position);
                _selected = getGameState().hotels.hotels.size() - 1;
                ToolCancel();
            }
        }

        void onToolAbort(WidgetIndex widgetIndex) override
        {
            HideGridlines();
            gMapSelectFlags.unset(MapSelectFlag::enable);
            invalidate();
        }

        void onPrepareDraw() override
        {
            const auto& gameState = getGameState();
            const auto& st = gameState.hotels;
            const bool dayCycle = ParkDay::IsEnabled(gameState);

            const auto size = Hotel::kSizes[_sizeIndex];
            char buffer[64];
            std::snprintf(
                buffer, sizeof(buffer), "%ux%u tiles, %u guests", size.width, size.length, Hotel::GetSizeCapacity(_sizeIndex));
            _sizeText = buffer;
            widgets[WIDX_SIZE].setString(_sizeText.c_str());

            widgets[WIDX_BUILD].setString(isPlacing() ? "Click on the map next to a footpath (Esc to stop)" : "Build hotel");
            setWidgetPressed(WIDX_BUILD, isPlacing());
            setWidgetDisabled(WIDX_BUILD, !dayCycle);

            if (gameState.park.flags.has(ParkFlag::noMoney))
                _priceText = "Free";
            else
                _priceText = FormatString("{CURRENCY2DP}", st.roomPrice);
            widgets[WIDX_PRICE].setString(_priceText.c_str());

            const auto* h = selectedHotel();
            _selectedText = h != nullptr ? Hotel::GetHotelName(h->id) : "No hotels yet";
            widgets[WIDX_SELECTED].setString(_selectedText.c_str());
            for (auto idx : { WIDX_SELECTED, WIDX_SELECTED_UP, WIDX_SELECTED_DOWN, WIDX_LOCATE, WIDX_REMOVE })
                setWidgetDisabled(idx, h == nullptr);
        }

        void onDraw(Drawing::RenderTarget& rt) override
        {
            drawWidgets(rt);

            const auto& gameState = getGameState();
            const auto& st = gameState.hotels;
            const auto textColour = colours[1];
            const bool noMoney = gameState.park.flags.has(ParkFlag::noMoney);

            auto label = [&](int32_t y, const std::string& text) {
                drawText(rt, windowPos + ScreenCoordsXY{ 10, y }, text.c_str(), { textColour });
            };
            label(widgets[WIDX_SIZE].top + 1, "New hotel size");
            label(widgets[WIDX_PRICE].top + 1, "Room price per night");
            label(widgets[WIDX_SELECTED].top + 1, "Hotel");

            char buffer[256];
            if (!ParkDay::IsEnabled(gameState))
            {
                label(widgets[WIDX_BUILD].bottom + 4, "Hotels need the realistic day cycle (Park day window).");
            }
            else
            {
                std::snprintf(
                    buffer, sizeof(buffer), "Rooms in all hotels: %u, booked: %u", Hotel::GetTotalCapacity(gameState),
                    static_cast<uint32_t>(st.bookings.size()));
                label(widgets[WIDX_BUILD].bottom + 4, buffer);
            }

            const auto* h = selectedHotel();
            int32_t y = widgets[WIDX_SELECTED].bottom + 6;
            constexpr int32_t kLineHeight = 12;
            if (h == nullptr)
            {
                label(y, "About 1 in 4 arriving guests books a room while");
                label(y + kLineHeight, "rooms are free, for 1 to 3 nights. Capacity is");
                label(y + kLineHeight * 2, "25 guests per tile of the building.");
                return;
            }

            std::snprintf(
                buffer, sizeof(buffer), "%ux%u tiles, %u rooms (%u tiles standing)", h->width, h->length, h->GetCapacity(),
                h->tilesStanding);
            label(y, buffer);
            y += kLineHeight;

            std::snprintf(
                buffer, sizeof(buffer), "Booked: %u   Inside now: %u", Hotel::GetBookedRooms(gameState, h->id),
                Hotel::GetGuestsInside(gameState, h->id));
            label(y, buffer);
            y += kLineHeight;

            if (noMoney)
                std::snprintf(
                    buffer, sizeof(buffer), "Tonight: %u guests   Last night: %u", h->guestsTonight, h->guestsLastNight);
            else
                std::snprintf(
                    buffer, sizeof(buffer), "Tonight: %u guests (%s)   Last night: %u", h->guestsTonight,
                    FormatString("{CURRENCY2DP}", h->revenueTonight).c_str(), h->guestsLastNight);
            label(y, buffer);
            y += kLineHeight;

            if (!noMoney)
            {
                std::snprintf(
                    buffer, sizeof(buffer), "Earned so far: %s over %u guest nights",
                    FormatString("{CURRENCY2DP}", h->revenueTotal).c_str(), h->nightsTotal);
                label(y, buffer);
                y += kLineHeight;
            }

            if (!h->hasDoor)
                label(y, "No footpath next to the building: guests can't get in!");
            else
            {
                std::snprintf(buffer, sizeof(buffer), "Guests use the door at %d, %d", h->door.x, h->door.y);
                label(y, buffer);
            }
        }
    };

    WindowBase* HotelsOpen()
    {
        auto* windowMgr = GetWindowManager();
        return windowMgr->FocusOrCreate<HotelsWindow>(WindowClass::hotels, kWindowSize, WindowFlag::centreScreen);
    }
} // namespace OpenRCT2::Ui::Windows
