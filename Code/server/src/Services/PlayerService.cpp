#include "Events/CharacterCellChangeEvent.h"

#include <Services/PlayerService.h>
#include <Services/CharacterService.h>
#include <Components.h>
#include <GameServer.h>

#include <Messages/EnterCellRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/NotifyChatMessageBroadcast.h>

#include <regex>;

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_cellEnterConnection(aDispatcher.sink<PacketEvent<EnterCellRequest>>().connect<&PlayerService::HandleCellEnter>(this)),
      m_chatMessageConnection(aDispatcher.sink<PacketEvent<SendChatMessageRequest>>().connect<&PlayerService::HandleChatMessage>(this))
{
}

void PlayerService::HandleCellEnter(const PacketEvent<EnterCellRequest>& acMessage) const noexcept
{
    auto playerView = m_world.view<PlayerComponent>();

    const auto itor = std::find_if(std::begin(playerView), std::end(playerView),
   [playerView, connectionId = acMessage.ConnectionId](auto entity)
    {
        return playerView.get(entity).ConnectionId == connectionId;
    });

    if(itor == std::end(playerView))
    {
        spdlog::error("Connection {:x} is not associated with a player.", acMessage.ConnectionId);
        return;
    }

    auto& message = acMessage.Packet;

    m_world.emplace_or_replace<CellIdComponent>(*itor, message.CellId);

    auto& playerComponent = playerView.get(*itor);

    if (playerComponent.Character)
    {
        if (auto pCellIdComponent = m_world.try_get<CellIdComponent>(*playerComponent.Character); pCellIdComponent)
        {
            m_world.GetDispatcher().trigger(CharacterCellChangeEvent{*itor, *playerComponent.Character, pCellIdComponent->Cell, message.CellId});

            pCellIdComponent->Cell = message.CellId;
        }
        else
            m_world.emplace<CellIdComponent>(*playerComponent.Character, message.CellId);
    }

    auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();
    for (auto character : characterView)
    {
        const auto& ownedComponent = characterView.get<OwnerComponent>(character);

        // Don't send self managed
        if (ownedComponent.ConnectionId == acMessage.ConnectionId)
            continue;

        if (message.CellId != characterView.get<CellIdComponent>(character).Cell)
            continue;

        CharacterSpawnRequest spawnMessage;
        CharacterService::Serialize(m_world, character, &spawnMessage);

        GameServer::Get()->Send(acMessage.ConnectionId, spawnMessage);
    }
}

void PlayerService::HandleChatMessage(const PacketEvent<SendChatMessageRequest>& acMessage) const noexcept
{
    auto playerView = m_world.view<PlayerComponent>();

    const auto itor = std::find_if(std::begin(playerView), std::end(playerView),
                                   [playerView, connectionId = acMessage.ConnectionId](auto entity) {
                                       return playerView.get(entity).ConnectionId == connectionId;
                                   });

    if (itor == std::end(playerView))
    {
        spdlog::error("Connection {:x} is not associated with a player.", acMessage.ConnectionId);
        return;
    }

    auto& playerComponent = playerView.get(*itor);

    NotifyChatMessageBroadcast notifyMessage;
    notifyMessage.PlayerName = playerComponent.Username;

    std::regex escapeHtml{"<[^>]+>\s+(?=<)|<[^>]+>"};
    notifyMessage.ChatMessage = std::regex_replace(acMessage.Packet.ChatMessage, escapeHtml, "");


    auto view = m_world.view<PlayerComponent>();
    for (auto entity : view)
    {
        spdlog::debug("Sending message from Server to client: " + notifyMessage.ChatMessage + " - " + notifyMessage.PlayerName);
        auto& player = view.get<PlayerComponent>(entity);
        GameServer::Get()->Send(player.ConnectionId, notifyMessage);
    }
}
