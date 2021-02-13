#include <Services/OverlayClient.h>
#include <OverlayRenderHandler.hpp>

#include <Services/TransportService.h>
#include <encoding/include/Messages/SendChatMessageRequest.h>

OverlayClient::OverlayClient(TransportService& aTransport, TiltedPhoques::OverlayRenderHandler* apHandler)
    : TiltedPhoques::OverlayClient(apHandler), m_transport(aTransport)
{
}

OverlayClient::~OverlayClient() noexcept
{
}

bool OverlayClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                             CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
    if (message->GetName() == "ui-event")
    {
        auto pArguments = message->GetArgumentList();

        auto eventName = pArguments->GetString(0).ToString();
        auto eventArgs = pArguments->GetList(1);

#ifndef PUBLIC_BUILD
        LOG(INFO) << "event=ui_event name=" << eventName;
#endif

        if (eventName == "connect")
        {
            std::string baseIp = eventArgs->GetString(0);
            uint16_t port = eventArgs->GetInt(1) ? eventArgs->GetInt(1) : 10578;
            m_transport.Connect(baseIp + ":" + std::to_string(port));
        }
        if (eventName == "disconnect")
        {
            m_transport.Close();
        }
        if (eventName == "sendMessage")
        {
            SendChatMessageRequest messageRequest;
            messageRequest.ChatMessage = eventArgs->GetString(0).ToString(); 
            spdlog::info("Received Message from UI and will send it away: " + eventArgs->GetString(0).ToString());
            m_transport.Send(messageRequest);
        }

        return true;
    }

    return false;
}
