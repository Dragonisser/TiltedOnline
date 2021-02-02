#include <Services/ActorService.h>
#include <World.h>
#include <Forms/ActorValueInfo.h>
#include <Games/References.h>
#include <Components.h>

#include <Events/UpdateEvent.h>
#include <Events/ReferenceSpawnedEvent.h>
#include <Events/ReferenceRemovedEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/HealthChangeEvent.h>

#include <Messages/NotifyActorValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/RequestHealthChangeBroadcast.h>

#include <misc/ActorValueOwner.h>

ActorService::ActorService(entt::dispatcher& aDispatcher, World& aWorld, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_world.on_construct<LocalComponent>().connect<&ActorService::OnLocalComponentAdded>(this);
    aDispatcher.sink<DisconnectedEvent>().connect<&ActorService::OnDisconnected>(this);
    aDispatcher.sink<ReferenceSpawnedEvent>().connect<&ActorService::OnReferenceSpawned>(this);
    aDispatcher.sink<ReferenceRemovedEvent>().connect<&ActorService::OnReferenceRemoved>(this);
    aDispatcher.sink<UpdateEvent>().connect<&ActorService::OnUpdate>(this);
    aDispatcher.sink<NotifyActorValueChanges>().connect<&ActorService::OnActorValueChanges>(this);
    aDispatcher.sink<NotifyActorMaxValueChanges>().connect<&ActorService::OnActorMaxValueChanges>(this);
    aDispatcher.sink<HealthChangeEvent>().connect<&ActorService::OnHealthChange>(this);
    aDispatcher.sink<NotifyHealthChangeBroadcast>().connect<&ActorService::OnHealthChangeBroadcast>(this);
}

ActorService::~ActorService() noexcept
{
}

void ActorService::AddToActorMap(uint32_t aId, Actor* aActor) noexcept
{
    Map<uint32_t, float> values;
    int amountOfValues;

#if TP_SKYRIM64
    amountOfValues = 164;
#elif TP_FALLOUT4
    amountOfValues = 132;
#endif

    for (int i = 0; i < amountOfValues; i++)
    {
#if TP_FALLOUT4
        if (i == 23 || i == 48 || i == 70)
            continue;
#endif
        float value = GetActorValue(aActor, i);
        values.insert({i, value});
    }

    Map<uint32_t, float> maxValues;
    // Should be a for loop here to store all values.
    float maxValue = GetActorMaxValue(aActor, ActorValueInfo::kHealth);
    maxValues.insert({ActorValueInfo::kHealth, maxValue});

    m_actorMaxValues.insert({aId, maxValues});
    m_actorValues.insert({aId, values});
}

void ActorService::OnLocalComponentAdded(entt::registry& aRegistry, const entt::entity aEntity) noexcept
{
    auto& formIdComponent = aRegistry.get<FormIdComponent>(aEntity);
    auto& localComponent = aRegistry.get<LocalComponent>(aEntity);
    auto* pForm = TESForm::GetById(formIdComponent.Id);
    auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

    if (pActor != NULL)
    {
        //std::cout << "Form: " << std::hex << formIdComponent.Id << " Local: " << std::hex << localComponent.Id << std::endl;
        AddToActorMap(localComponent.Id, pActor);
    }
}

void ActorService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    m_actorValues.clear();
}

void ActorService::OnReferenceSpawned(const ReferenceSpawnedEvent& acEvent) noexcept
{
    if (m_transport.IsConnected())
    {
        auto* localComponent = m_world.try_get<LocalComponent>(acEvent.Entity);
        if (localComponent != NULL)
        {
            auto* pForm = TESForm::GetById(acEvent.FormId);
            auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

            if (pActor != NULL)
            {
                AddToActorMap(localComponent->Id, pActor);
            }
        }
    }    
}

void ActorService::OnReferenceRemoved(const ReferenceRemovedEvent& acEvent) noexcept
{
    if (m_transport.IsConnected())
    {
        m_actorValues.erase(acEvent.FormId);
    }    
}

void ActorService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    m_timeSinceDiff += acEvent.Delta;
    if (m_timeSinceDiff >= 1)
    {
        m_timeSinceDiff = 0;

        auto view = m_world.view<FormIdComponent, LocalComponent>();

        for (auto& value : m_actorValues)
        {
            for (auto entity : view)
            {
                auto& localComponent = view.get<LocalComponent>(entity);
                if (localComponent.Id == value.first)
                {
                    auto& formIdComponent = view.get<FormIdComponent>(entity);
                    auto* pForm = TESForm::GetById(formIdComponent.Id);
                    auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

                    if (pActor != NULL)
                    {
                        RequestActorValueChanges requestChanges;
                        requestChanges.m_Id = value.first;

                        int amountOfValues;
#if TP_SKYRIM64
                        amountOfValues = 164;
#elif TP_FALLOUT4
                        amountOfValues = 132;
#endif

                        for (int i = 0; i < amountOfValues; i++)
                        {
#if TP_FALLOUT4
                            if (i == 23 || i == 48 || i == 70)
                                continue;
#endif
                            float oldValue = value.second[i];
                            float newValue = GetActorValue(pActor, i);
                            if (newValue != oldValue)
                            {
                                requestChanges.m_values.insert({i, newValue});
                                value.second[i] = newValue;
                            }
                        }

                        if (requestChanges.m_values.size() > 0)
                        {
                            m_transport.Send(requestChanges);
                        }
                    }
                }
            }
        }        

        auto view2 = m_world.view<FormIdComponent, LocalComponent>();

        for (auto& maxValue : m_actorMaxValues)
        {
            for (auto entity : view2)
            {
                auto& localComponent = view2.get<LocalComponent>(entity);
                if (localComponent.Id == maxValue.first)
                {
                    auto& formIdComponent = view2.get<FormIdComponent>(entity);
                    auto* pForm = TESForm::GetById(formIdComponent.Id);
                    auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

                    if (pActor != NULL)
                    {
                        RequestActorMaxValueChanges requestChanges;
                        requestChanges.m_Id = maxValue.first;

                        // Again, there should probably be a loop here
                        float oldValue = maxValue.second[ActorValueInfo::kHealth];
                        float newValue = GetActorValue(pActor, ActorValueInfo::kHealth);
                        if (newValue != oldValue)
                        {
                            requestChanges.m_values.insert({ActorValueInfo::kHealth, newValue});
                            maxValue.second[ActorValueInfo::kHealth] = newValue;
                        }

                        if (requestChanges.m_values.size() > 0)
                        {
                            m_transport.Send(requestChanges);
                        }
                    }
                }
            }
        }        
    }    
}

void ActorService::OnHealthChange(const HealthChangeEvent& acEvent) noexcept
{
    if (m_transport.IsConnected())
    {
        auto view = m_world.view<FormIdComponent>();

        for (auto entity : view)
        {
            auto& formIdComponent = view.get(entity);
            if (formIdComponent.Id == acEvent.Hittee->formID)
            {
                const auto localComponent = m_world.try_get<LocalComponent>(entity);

                if (localComponent)
                {
                    RequestHealthChangeBroadcast requestHealthChange;
                    requestHealthChange.m_Id = localComponent->Id;
                    requestHealthChange.m_DeltaHealth = acEvent.DeltaHealth;

                    m_transport.Send(requestHealthChange);
                }
                else
                {
                    const auto remoteComponent = m_world.try_get<RemoteComponent>(entity);

                    RequestHealthChangeBroadcast requestHealthChange;
                    requestHealthChange.m_Id = remoteComponent->Id;
                    requestHealthChange.m_DeltaHealth = acEvent.DeltaHealth;

                    m_transport.Send(requestHealthChange);
                }
            }
        }
    }
}

void ActorService::OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acEvent) noexcept
{
    auto view = m_world.view<FormIdComponent>();

    for (auto entity : view)
    {
        uint32_t componentId;
        const auto localComponent = m_world.try_get<LocalComponent>(entity);
        const auto remoteComponent = m_world.try_get<RemoteComponent>(entity);
        if (localComponent)
            componentId = localComponent->Id;
        else
            componentId = remoteComponent->Id;

        if (componentId == acEvent.m_Id)
        {
            auto& formIdComponent = view.get(entity);
            auto* pForm = TESForm::GetById(formIdComponent.Id);
            auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

            if (pActor != NULL)
            {
                float newHealth = GetActorValue(pActor, ActorValueInfo::kHealth) + acEvent.m_DeltaHealth;
                ForceActorValue(pActor, 2, ActorValueInfo::kHealth, newHealth);
            }
        }
    }
}

void ActorService::OnActorValueChanges(const NotifyActorValueChanges& acEvent) noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();

    for (auto entity : view)
    {
        auto& remoteComponent = view.get<RemoteComponent>(entity);
        if (remoteComponent.Id == acEvent.m_Id)
        {
            auto& formIdComponent = view.get<FormIdComponent>(entity);
            auto* pForm = TESForm::GetById(formIdComponent.Id);
            auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

            if (pActor != NULL)
            {
                for (auto& value : acEvent.m_values)
                {
                    std::cout << "Form ID: " << std::hex << formIdComponent.Id << " Remote ID: " << std::hex << acEvent.m_Id << std::endl;
                    std::cout << "Key: " << std::dec << value.first << " Value: " << value.second << std::endl;

                    if (value.first == ActorValueInfo::kHealth)
                        continue;
#if TP_SKYRIM64
                    if (value.first == ActorValueInfo::kStamina || value.first == ActorValueInfo::kMagicka)
                    {
                        ForceActorValue(pActor, 2, value.first, value.second);
                    }
#endif
                    else
                    {
                        SetActorValue(pActor, value.first, value.second);
                    }                    
                }
            }
        }
    }
}

void ActorService::OnActorMaxValueChanges(const NotifyActorMaxValueChanges& acEvent) noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();

    for (auto entity : view)
    {
        auto& remoteComponent = view.get<RemoteComponent>(entity);
        if (remoteComponent.Id == acEvent.m_Id)
        {
            auto& formIdComponent = view.get<FormIdComponent>(entity);
            auto* pForm = TESForm::GetById(formIdComponent.Id);
            auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

            if (pActor != NULL)
            {
                for (auto& value : acEvent.m_values)
                {
                    std::cout << "Max values update." << std::endl;
                    std::cout << "Form ID: " << std::hex << formIdComponent.Id << " Remote ID: " << std::hex << acEvent.m_Id << std::endl;
                    std::cout << "Key: " << std::dec << value.first << " Value: " << value.second << std::endl;

                    if (value.first == ActorValueInfo::kHealth)
                    {
                        ForceActorValue(pActor, 0, value.first, value.second);
                    }
                }
            }
        }
    }
}

void ActorService::ForceActorValue(Actor* apActor, uint32_t aMode, uint32_t aId, float aValue) noexcept
{
    float current = GetActorValue(apActor, aId);
#if TP_FALLOUT4
    ActorValueInfo* pActorValueInfo = apActor->GetActorValueInfo(aId);
    apActor->actorValueOwner.ForceCurrent(aMode, pActorValueInfo, aValue - current);
#elif TP_SKYRIM64
    apActor->actorValueOwner.ForceCurrent(aMode, aId, aValue - current);
#endif
}

void ActorService::SetActorValue(Actor* apActor, uint32_t aId, float aValue) noexcept
{
#if TP_FALLOUT4
    ActorValueInfo* pActorValueInfo = apActor->GetActorValueInfo(aId);
    apActor->actorValueOwner.SetValue(pActorValueInfo, aValue);
#elif TP_SKYRIM64
    apActor->actorValueOwner.SetValue(aId, aValue);
#endif
}

float ActorService::GetActorValue(Actor* apActor, uint32_t aId) noexcept
{
#if TP_FALLOUT4
    ActorValueInfo* pActorValueInfo = apActor->GetActorValueInfo(aId);
    return apActor->actorValueOwner.GetValue(pActorValueInfo);
#elif TP_SKYRIM64
    return apActor->actorValueOwner.GetValue(aId);
#endif
}

float ActorService::GetActorMaxValue(Actor* apActor, uint32_t aId) noexcept
{
#if TP_FALLOUT4
    ActorValueInfo* pActorValueInfo = apActor->GetActorValueInfo(aId);
    return apActor->actorValueOwner.GetMaxValue(pActorValueInfo);
#elif TP_SKYRIM64
    return apActor->actorValueOwner.GetMaxValue(aId);
#endif
}
