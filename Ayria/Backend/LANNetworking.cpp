/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-01-23
    License: MIT
*/

#include <Ayria.hpp>

namespace Backend::Network::LANNetworking
{
    constexpr uint32_t Broadcastaddress = Hash::FNV1_32("Ayria"sv) << 8;    // 228.58.137.0
    constexpr uint16_t Broadcastport = Hash::FNV1_32("Ayria"sv) & 0xFFFF;   // 14985

    constexpr sockaddr_in Multicast{ AF_INET, cmp::toBig(Broadcastport), {{.S_addr = cmp::toBig(Broadcastaddress)}} };
    static std::queue<Blob_t> Packetqueue{};
    static size_t Broadcastsocket{};
    static Spinlock_t Threadsafe;

    // Broadcast to the local network.
    static void Publish(const Blob_t &Packet)
    {
        // Try to put the packet onto the network.
        for (uint8_t i = 0; i < 10; ++i)
        {
            const auto Result = sendto(Broadcastsocket, (const char *)Packet.data(), (int)Packet.size(), NULL, (const sockaddr *)&Multicast, sizeof(Multicast));
            if (Result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) [[unlikely]] return;
            if (Result == static_cast<int>(Packet.size())) [[likely]] return;
        }

        // Copy and try to send it in the background.
        std::thread([](Blob_t Packet)
        {
            while(true)
            {
                const auto Result = sendto(Broadcastsocket, (const char *)Packet.data(), (int)Packet.size(), NULL, (const sockaddr *)&Multicast, sizeof(Multicast));
                if (Result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) [[unlikely]] return;
                if (Result == static_cast<int>(Packet.size())) [[likely]] return;

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }, Packet).detach();
    }

    // Every 100ms.
    static void __cdecl Poll()
    {
        // Check for data on the socket.
        FD_SET ReadFD{}; FD_SET(Broadcastsocket, &ReadFD);
        constexpr timeval Defaulttimeout{ NULL, 1 };
        const auto Count{ ReadFD.fd_count };
        auto Timeout{ Defaulttimeout };

        // If there's any delayed packets, push them.
        if (!Packetqueue.empty()) [[unlikely]]
        {
            do
            {
                Publish(Packetqueue.front());
                Packetqueue.pop();

            } while (!Packetqueue.empty());
        }

        // Check if there's any data available for us.
        if (!select(Count, &ReadFD, nullptr, nullptr, &Timeout)) [[likely]]
            return;

        // Since we have data, allocate a small buffer.
        constexpr int UDPSize = 0xFFE3;
        const auto Buffer = (uint8_t *)alloca(UDPSize);

        // Fetch all the data available.
        while (true)
        {
            // Fetch the whole packet at once, we don't care from where.
            const auto Packetsize = recvfrom(Broadcastsocket, (char *)Buffer, UDPSize, NULL, nullptr, nullptr);
            if (Packetsize < static_cast<int>(sizeof(Header_t))) [[unlikely]]
                break;

            // Check if this packet is a duplicate of ours.
            const auto Header = reinterpret_cast<const Header_t *>(Buffer);
            if (Header->Publickey == Global.Publickey) [[likely]]
                continue;

            // Set up the ranges we will care about.
            const auto Signedpart = std::span(Buffer + 96, Packetsize - 96);
            const auto Payload = std::span(Buffer + 108, Packetsize - 108);

            // Validate the integrity of the packet.
            if (!qDSA::Verify(Header->Publickey, Header->Signature, Signedpart)) [[unlikely]]
                continue;

            // Forward to DB.
            Synchronization::Storemessage(Header->Signature, Header->Publickey, Header->Messagetype, Header->Timestamp, Payload);
        }


    }

    // On startup.
    static void Initialize()
    {
        constexpr sockaddr_in Localhost{ AF_INET, cmp::toBig(Broadcastport), {{.S_addr = cmp::toBig(INADDR_ANY)}} };
        constexpr ip_mreq Request{ {{.S_addr = cmp::toBig(Broadcastaddress)}} };
        unsigned long Argument{ 1 };
        unsigned long Error{ 0 };
        WSADATA Unused;

        // We only need WS 1.1, no need for more.
        (void)WSAStartup(MAKEWORD(1, 1), &Unused);
        Broadcastsocket = socket(AF_INET, SOCK_DGRAM, 0);
        Error |= ioctlsocket(Broadcastsocket, FIONBIO, &Argument);

        // TODO(tcn): Investigate if WS2's IP_ADD_MEMBERSHIP (12) gets mapped to the WS1's original (5) internally.
        // Join the multicast group, reuse address if multiple clients are on the same PC (mainly for developers).
        Error |= setsockopt(Broadcastsocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&Request, sizeof(Request));
        Error |= setsockopt(Broadcastsocket, SOL_SOCKET, SO_REUSEADDR, (char *)&Argument, sizeof(Argument));
        Error |= bind(Broadcastsocket, (sockaddr *)&Localhost, sizeof(Localhost));

        // TODO(tcn): Proper error handling.
        if (Error) [[unlikely]]
        {
            closesocket(Broadcastsocket);
            assert(false);
            return;
        }

        // Add periodic tasks.
        Enqueuetask(Poll, 100);
    }

    // Register initialization to run on startup.
    struct Startup_t { Startup_t() { Backend::Backgroundtasks::addStartuptask(Initialize); } } Startup{};
}

namespace Backend::Network
{
    // Publish a payload to the network.
    void PublishLAN(const Blob_t &Packet, bool Delayed)
    {
        if (Delayed)
        {
            LANNetworking::Packetqueue.push(Packet);
        }
        else
        {
            LANNetworking::Publish(Packet);
        }
    }
}
