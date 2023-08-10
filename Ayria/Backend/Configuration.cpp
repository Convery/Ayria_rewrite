/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-02
    License: MIT
*/

#include <Ayria.hpp>

namespace Backend::Config
{
    static std::string Configpath = "./Ayria/Config.json";

    // Save the configuration to disk.
    static void Saveconfig()
    {
        JSON::Object_t Object{};

        // Need to cast the bits to bool as otherwise they'd be uint16.
        Object[u8"enableExternalconsole"] = (bool)Global.Configuration.enableExternalconsole;
        Object[u8"enableIATHooking"] = (bool)Global.Configuration.enableIATHooking;
        Object[u8"enableFileshare"] = (bool)Global.Configuration.enableFileshare;
        Object[u8"noNetworking"] = (bool)Global.Configuration.noNetworking;
        Object[u8"pruneDB"] = (bool)Global.Configuration.pruneDB;
        Object[u8"Username"] = *Global.Username;

        FS::Writefile(Configpath, JSON::Dump(Object));
    }

    // Fills Globalstate_t
    void Load()
    {
        // Load the last configuration from disk.
        const auto Config = JSON::Parse(FS::Readfile<char8_t>(Configpath));
        Global.Configuration.enableExternalconsole = Config.value<bool>("enableExternalconsole");
        Global.Configuration.enableIATHooking = Config.value<bool>("enableIATHooking");
        Global.Configuration.enableFileshare = Config.value<bool>("enableFileshare");
        Global.Configuration.noNetworking = Config.value<bool>("noNetworking");
        Global.Configuration.pruneDB = Config.value<bool>("pruneDB", true);
        *Global.Username = Config.value(u8"Username", u8"AYRIA"s);

        // Select a source for crypto..
        if (std::strstr(GetCommandLineA(), "--randID"))
        {
            setPublickey_RNG();
        }
        else
        {
            setPublickey_HWID();
        }

        // Notify the user about the current settings.
        Infoprint("Loaded account:");
        Infoprint(va("ShortID: 0x%08X", Global.getShortID()));
        Infoprint(va("LongID: %s", Global.getLongID().c_str()));
        Infoprint(va("Username: %s", *Global.Username));

        // If there was no config, force-save one for the user instantly.
        (void)std::atexit([]() { if (Global.Configuration.modifiedConfig) Saveconfig(); });
        if (Config.empty()) Saveconfig();

        // Might as well create a console if requested.
        if (Global.Configuration.enableExternalconsole)
            Frontend::CreateWinconsole().detach();
    }

    // Helper to set the publickey.
    void setPublickey(std::u8string_view CredentialA, std::u8string_view CredentialB)
    {
        // TODO(tcn): Should probably have a more complex algo..
        const auto Combined = Hash::SHA256(CredentialA) + Hash::SHA256(CredentialB);
        std::array<uint8_t, 64> Temporary = Hash::SHA512(Combined);

        // Slow things down a bit.
        for (size_t i = 0; i < 1'000; ++i)
            Temporary = Hash::SHA512(Temporary + cmp::getBytes(i));

        const auto Seed = Hash::SHA512(Hash::SHA256(Temporary) + Hash::SHA256(Combined));
        std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Seed);
    }
    void setPublickey_HWID()
    {
        // TPM is not commonly available, but most stable.
        if (const auto TPMEK = HWID::getTPMEK())
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(*TPMEK));
            return;
        }

        // BIOS shouldn't update too often..
        const auto BIOS = HWID::getSMBIOS();
        if (!BIOS.Caseserial.empty())   // Laptops only.
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(BIOS.Caseserial));
            return;
        }
        if (!BIOS.MOBOSerial.empty())   // Questionable uniqueness.
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(BIOS.MOBOSerial));
            return;
        }
        if (!BIOS.RAMSerial.empty()) // Primarily ECC memory.
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(BIOS.RAMSerial));
            return;
        }
        if (!BIOS.UUID.empty())         // OEM only.
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(BIOS.UUID));
            return;
        }

        // Diskinfo should be relatively stable.
        const auto Diskinfo = HWID::getDiskinfo();
        if (!Diskinfo.UUID.empty())
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(Diskinfo.UUID));
            return;
        }
        if (!Diskinfo.Serial.empty())
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(Diskinfo.Serial));
            return;
        }

        // Unlikely that two computers on the same network has the same CPU.
        if (const auto MAC = HWID::getRouterMAC(); !MAC.empty())
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA256(MAC) + Hash::SHA256(HWID::getCPUinfo().RAW));
            return;
        }

        // Ask Windows for a UEFI-stored ID.
        std::array<uint8_t, 4096> Windows{};
        if (const auto Length = GetFirmwareEnvironmentVariableW(L"OfflineUniqueIDRandomSeed", L"{eaec226f-c9a3-477a-a826-ddc716cdc0e3}", Windows.data(), (DWORD)Windows.size()))
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(std::span(Windows.data(), Length)));
            return;
        }

        // Can't really do much else..
        if (const auto Seed = FS::Readfile<uint8_t>("./Ayria/Cryptoseed"); !Seed.empty())
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(Seed));
        }
        else
        {
            const std::array<uint64_t, 4> Source{ RNG::Next(), RNG::Next(), RNG::Next(), RNG::Next() };
            FS::Writefile("./Ayria/Cryptoseed", Source);

            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(Source));
        }
    }
    void setPublickey_RNG()
    {
        const std::array<uint64_t, 4> Source{ RNG::Next(), RNG::Next(), RNG::Next(), RNG::Next() };
        std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(Source));
    }
}
