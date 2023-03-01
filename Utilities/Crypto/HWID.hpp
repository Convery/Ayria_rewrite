/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-21
    License: MIT

    Some methods of generating identifiers for crypto.
*/

#pragma once
#include <Utilities/Utilities.hpp>
#include "SHA.hpp"

namespace HWID
{
    // BIOS provided identifiers.
    using BIOSData_t = struct { std::string UUID, MOBOSerial, Caseserial, RAMSerial; };
    inline BIOSData_t getSMBIOS()
    {
        std::u8string_view Table{};
        uint8_t Version_major{};
        BIOSData_t Result{};

        #if defined (_WIN32)
        const auto Size = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
        const auto Buffer = std::make_unique<char8_t[]>(Size);
        GetSystemFirmwareTable('RSMB', 0, Buffer.get(), Size);

        Version_major = *(uint8_t *)(Buffer.get() + 1);
        const auto Tablelength = *(uint32_t *)(Buffer.get() + 4);
        Table = std::u8string_view(Buffer.get() + 8, Tablelength);

        #else // Linux assumed.
        const auto File = FS::Readfile<char8_t>("/sys/firmware/dmi/tables/smbios_entry_point");

        // SMBIOS
        if (*(uint32_t *)File.data() == 0x5F534D5F)
        {
            Version_major = File[6];

            const auto Offset = *(uint32_t *)(File.data() + 0x18);
            const auto Tablelength = *(uint32_t *)(File.data() + 0x16);
            Table = std::u8string_view(File.data() + Offset, Tablelength);
        }

        // SMBIOS3
        if (*(uint32_t *)File.data() == 0x5F534D33)
        {
            Version_major = File[7];

            const auto Offset = *(uint64_t *)(File.data() + 0x10);
            const auto Tablelength = *(uint32_t *)(File.data() + 0x0C);
            Table = std::u8string_view(File.data() + Offset, Tablelength);
        }
        #endif

        // Sometimes 2.x is reported as "default" AKA 0.
        if (Version_major == 0 || Version_major >= 2)
        {
            // Parse the whole table.
            while (!Table.empty())
            {
                // Helper to skip trailing strings.
                const auto Structsize = [](const char8_t *Start) -> size_t
                {
                    auto End = Start;
                    End += Start[1];

                    if (!*End) End++;
                    while (*(End++)) while (*(End++)) {};
                    return End - Start;
                } (Table.data());

                auto Entry = Table.substr(0, Structsize);
                const auto Headerlength = Entry[1];
                const auto Type = Entry[0];

                // BIOS UUID
                if (Type == 1)
                {
                    Result.UUID = Encoding::toHexstringU(Entry.substr(8, 16));
                }

                // Motherboard serial.
                if (Type == 2)
                {
                    const auto Stringindex = Entry[0x07];
                    Entry.remove_prefix(Headerlength);

                    for (uint8_t i = 1; i < Stringindex; ++i)
                        Entry.remove_prefix(std::strlen((char *)Entry.data()) + 1);

                    Result.MOBOSerial = (char *)Entry.data();
                }

                // Case serial.
                if (Type == 3)
                {
                    const auto Stringindex = Entry[0x06];
                    Entry.remove_prefix(Headerlength);

                    for (uint8_t i = 1; i < Stringindex; ++i)
                        Entry.remove_prefix(std::strlen((char *)Entry.data()) + 1);

                    Result.Caseserial = (char *)Entry.data();
                }

                // RAM Serial.
                if (Type == 17)
                {
                    const auto Stringindex = Entry[0x18];
                    Entry.remove_prefix(Headerlength);

                    for (uint8_t i = 1; i < Stringindex; ++i)
                        Entry.remove_prefix(std::strlen((char *)Entry.data()) + 1);

                    const std::string Current((char *)Entry.data());
                    const auto P1 = Hash::SHA256(Result.RAMSerial);
                    auto P2 = Hash::SHA256(Current);

                    for (uint8_t i = 0; i < P1.size(); ++i)
                        P2[i] ^= P1[i];

                    Result.RAMSerial = P2;
                }

                // Skip to next entry.
                Table.remove_prefix(Structsize);
            }

            // Do some cleanup.
            const std::array<std::string, 6> Badstrings = { "NONE", "FILLED", "OEM", "O.E.M.", "00020003000400050006000700080009", "SERNUM" };
            if (std::ranges::any_of(Badstrings, [&](const auto &Item) { return Result.Caseserial.contains(Item); })) Result.Caseserial.clear();
            if (std::ranges::any_of(Badstrings, [&](const auto &Item) { return Result.MOBOSerial.contains(Item); })) Result.MOBOSerial.clear();
            if (std::ranges::any_of(Badstrings, [&](const auto &Item) { return Result.UUID.contains(Item); })) Result.UUID.clear();
        }

        // In-case we want to verify something.
        if constexpr (Build::isDebug)
        {
            Infoprint(va("UUID: %s", Result.UUID.c_str()));
            Infoprint(va("Caseserial: %s", Result.Caseserial.c_str()));
            Infoprint(va("MOBOSerial: %s", Result.MOBOSerial.c_str()));
            Infoprint(va("RAMSerial: %s", Encoding::toHexstringU(Result.RAMSerial).c_str()));
        }

        return Result;
    }

    // Get the MAC address of the router.
    inline std::string getRouterMAC()
    {
        struct Common_t
        {
            const void *Reserved[2];
            const void *ModuleID;

            size_t Index;
            DWORD Store;

            DWORD Reserved2;

            const void *Keystruct;
            size_t Keysize;
        };
        struct Single_t : Common_t
        {
            size_t Structtype;
            const void *Parameter;
            DWORD Parametersize;
            DWORD Offset;
        };
        struct Multiple_t  : Common_t
        {
            void *RWData;
            size_t RWSize;

            void *Dynamicdata;
            size_t Dynamicsize;

            void *Staticdata;
            size_t Staticsize;
        };

        // Normally provided from iphlpapi.lib
        constexpr auto NPI_MS_NDIS_MODULEID = std::to_array<uint8_t>({
            0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // Length, Type
            0x11, 0x4A, 0x00, 0xEB, 0x1A, 0x9B, 0xD4, 0x11, // GUID 1,2,3
            0x91, 0x23, 0x00, 0x50, 0x04, 0x77, 0x59, 0xBC  // GUID 4
            });
        constexpr auto NPI_MS_IPV4_MODULEID = std::to_array<uint8_t>({
            0x18, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // Length, Type
            0x00, 0x4A, 0x00, 0xEB, 0x1A, 0x9B, 0xD4, 0x11, // GUID 1,2,3
            0x91, 0x23, 0x00, 0x50, 0x04, 0x77, 0x59, 0xBC  // GUID 4
            });

        // Nsi is proxied to the driver.
        const auto Handle = CreateFileW(L"\\\\.\\Nsi", GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (Handle == INVALID_HANDLE_VALUE) return {};

        // Simplify IOCTL usage.
        const auto GetParameter = [Handle]<size_t N>(const void *ModuleID, size_t Index, const void *Parameter, DWORD Size, DWORD Offset)
        {
            std::array<uint8_t, N> Result{};

            auto Param = Single_t{ 0, 0, ModuleID, Index, 1, 0, Parameter, Size, N / 4, Result.data(), N, Offset };
            DeviceIoControl(Handle, 0x120007, &Param, sizeof(Param), &Param, N, NULL, NULL);

            return Result;
        };
        const auto GetParameters = [Handle](const void *ModuleID, size_t Index, const void *Parameter, DWORD Size)
        {
            std::array<uint32_t, 4> Dynamicdata{};
            std::array<uint8_t, 4> Staticdata{};
            std::array<uint32_t, 8> RWData{};

            auto Param = Multiple_t{ 0, 0, ModuleID, Index, 1, 0, Parameter, Size, RWData.data(), 32, Dynamicdata.data(), 16, &Staticdata, 4 };
            DeviceIoControl(Handle, 0x12000F, &Param, sizeof(Param), &Param, sizeof(Param), NULL, NULL);

            return std::make_tuple(Dynamicdata, Staticdata, RWData);
        };

        // Find the best interface.
        std::array<uint64_t, 4> LUID{ 0x0101A8C0ULL << 32 };
        const auto Interface = GetParameter.operator()<4>(NPI_MS_IPV4_MODULEID.data(), 0, LUID.data(), 32, 88);

        // Resolve to LUID.
        const auto Resolved = GetParameter.operator()<8>(NPI_MS_NDIS_MODULEID.data(), 2, &Interface, 4, 0);
        LUID = { std::bit_cast<uint64_t>(Resolved), std::bit_cast<uint64_t>(Resolved), 0x0101A8C0};

        // Query for fun data..
        const auto [Dynamicdata, Staticdata, RWData] = GetParameters(NPI_MS_IPV4_MODULEID.data(), 20, LUID.data(), 24);

        // Only need 6 bytes of the result.
        const auto Result = cmp::Array_t<uint8_t, 6>{ cmp::getBytes(RWData) };

        // Notify the developer.
        if constexpr (Build::isDebug) if (!Result.empty())
            Infoprint(va("ARP: %s", Encoding::toHexstringU(Result).c_str()));

        CloseHandle(Handle);
        return Encoding::toHexstringU(Result);
    }

    // Get the primary volume's information.
    struct Diskinfo_t { std::string Serial, UUID; bool Full() { return !Serial.empty() && !UUID.empty(); } };
    inline Diskinfo_t getDiskinfo()
    {
        // Different types of disks, not going to bother detecting the actual type.
        const auto Legacy_NVME = [](HANDLE Devicehandle, Diskinfo_t &Result)
        {
            std::array<uint32_t, 1062> Namespace{ 28, 'Nvme', 'Mini', 60, 0xE0002000, 0, 4220, 0, 0, 0, 0, 0, 0, 6, 0, 0 };
            std::array<uint32_t, 1062> Serial{ 28, 'Nvme', 'Mini', 60, 0xE0002000, 0, 4220, 0, 0, 0, 0, 0, 0, 6, 0, 1 };
            Namespace[33] = 2; Namespace[37] = 4248;
            Serial[33] = 2; Serial[37] = 4248;

            if (DeviceIoControl(Devicehandle, 0x4D008, &Namespace, sizeof(Namespace), &Namespace, sizeof(Namespace), NULL, NULL))
            {
                Result.UUID = Encoding::toHexstringU(Blob_t((uint8_t *)&Namespace[38 + 30], 8));
            }

            if (DeviceIoControl(Devicehandle, 0x4D008, &Serial, sizeof(Serial), &Serial, sizeof(Serial), NULL, NULL))
            {
                Serial[38 + 6] = 0;
                Result.Serial = std::string((char *)&Serial[38 + 1]);
            }
        };
        const auto Modern_NVME = [](HANDLE Devicehandle, Diskinfo_t &Result)
        {
            std::array<uint32_t, 1036> Namespace{ 50, 0, 3, 1, 0, 1, 40, 4096 };
            std::array<uint32_t, 1036> Serial{ 49, 0, 3, 1, 1, 0, 40, 4096 };

            if (DeviceIoControl(Devicehandle, 0x2D1400, &Namespace, sizeof(Namespace), &Namespace, sizeof(Namespace), NULL, NULL))
            {
                Result.UUID = Encoding::toHexstringU(Blob_t((uint8_t *)&Namespace[12 + 30], 8));
            }

            if (DeviceIoControl(Devicehandle, 0x2D1400, &Serial, sizeof(Serial), &Serial, sizeof(Serial), NULL, NULL))
            {
                Serial[12 + 6] = 0;
                Result.Serial = std::string((char *)&Serial[12 + 1]);
            }
        };
        const auto SMART = [](HANDLE Devicehandle, Diskinfo_t &Result)
        {
            std::array<uint8_t, 548> Query{ 0, 02, 0, 0, 0, 1, 1, 0, 0, 0xA0, 0xEC };

            if (DeviceIoControl(Devicehandle, 0x0007C088, &Query, sizeof(Query), &Query, sizeof(Query), NULL, NULL))
            {
                const auto Buffer = std::span((char *)&Query[36], 20);
                for (int i = 0; i < 10; ++i) *(uint16_t *)&Buffer[i * 2] = cmp::fromBig(*(uint16_t *)&Buffer[i * 2]);
                Result.Serial = std::string(Buffer.begin(), Buffer.end());
            }
        };
        const auto SCSI = [](HANDLE Devicehandle, Diskinfo_t &Result)
        {
            std::array<uint8_t, sizeof(SCSI_PASS_THROUGH_DIRECT) + 4 + 64> Query{};
            auto Header = (SCSI_PASS_THROUGH_DIRECT *)Query.data();
            std::array<uint8_t, 252> Buffer{};

            Header->Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
            Header->SenseInfoOffset = Header->Length + 4;
            Header->SenseInfoLength = 64;
            Header->TimeOutValue = 60;
            Header->CdbLength = 6;
            Header->Cdb[0] = 0x12;
            Header->Cdb[1] = 0x01;
            Header->Cdb[2] = 0x80;
            Header->Cdb[3] = 0x02;

            Header->DataIn = 1;
            Header->DataBuffer = Buffer.data();
            Header->DataTransferLength = (ULONG)Buffer.size();

            if (DeviceIoControl(Devicehandle, 0x04D014, Query.data(), (DWORD)Query.size(), Query.data(), (DWORD)Query.size(), NULL, NULL))
            {
                const auto Length = Buffer[3];
                Result.Serial = std::string((char *)&Buffer[4], Length);
            }
        };
        const auto ATA = [](HANDLE Devicehandle, Diskinfo_t &Result)
        {
            // Connection type.
            const auto ATA = [](HANDLE Devicehandle, std::array<uint8_t, 512> *Result)
            {
                std::array<uint8_t, sizeof(ATA_PASS_THROUGH_EX) + 4 + 512> Buffer{};
                *(ATA_PASS_THROUGH_EX *)&Buffer[0] = ATA_PASS_THROUGH_EX{sizeof(ATA_PASS_THROUGH_EX), ATA_FLAGS_DATA_IN, 0, 0, 0, 0, 512, 60, 0, sizeof(ATA_PASS_THROUGH_EX) + 4 };
                Buffer[sizeof(ATA_PASS_THROUGH_EX) + 4] = 0xCF;

                if (DeviceIoControl(Devicehandle, 0x04D02C, Buffer.data(), (DWORD)Buffer.size(), Buffer.data(), (DWORD)Buffer.size(), NULL, NULL))
                {
                    std::memcpy(Result->data(), &Buffer[sizeof(ATA_PASS_THROUGH_EX) + 4], 512);
                    return true;
                }

                return false;
            };
            const auto IDE = [](HANDLE Devicehandle, const IDEREGS &Registers, std::array<uint8_t, 512> *Result)
            {
                std::array<uint8_t, 12 + 512> Buffer{};
                *(IDEREGS *)&Buffer[0] = Registers;
                *(ULONG *)&Buffer[8] = 512;
                *(BYTE *)&Buffer[12] = 0xCF;

                if (DeviceIoControl(Devicehandle, 0x04D028, Buffer.data(), (DWORD)Buffer.size(), Buffer.data(), (DWORD)Buffer.size(), NULL, NULL))
                {
                    std::memcpy(Result->data(), &Buffer[12], 512);
                    return true;
                }

                return false;
            };
            const auto SCSI_miniport = [](HANDLE Devicehandle, const IDEREGS &Registers, std::array<uint8_t, 512> *Result)
            {
                std::array<uint8_t, sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDINPARAMS) + 512> Buffer{};
                *(SRB_IO_CONTROL *)&Buffer[0] = SRB_IO_CONTROL{ sizeof(SRB_IO_CONTROL), { 'S', 'C', 'S', 'I', 'D', 'I', 'S', 'K' }, 60, 0x1B0501, 0, sizeof(SENDCMDINPARAMS) + 511 };
                *(SENDCMDINPARAMS *)&Buffer[sizeof(SRB_IO_CONTROL)] = SENDCMDINPARAMS{ 512, Registers };

                if (DeviceIoControl(Devicehandle, 0x04D008, Buffer.data(), (DWORD)Buffer.size(), Buffer.data(), (DWORD)Buffer.size(), NULL, NULL))
                {
                    std::memcpy(Result->data(), ((SENDCMDOUTPARAMS *)&Buffer[sizeof(SRB_IO_CONTROL)])->bBuffer, std::min(DWORD(512), ((SENDCMDOUTPARAMS *)&Buffer[sizeof(SRB_IO_CONTROL)])->cBufferSize));
                    return true;
                }

                return false;
            };

            constexpr IDEREGS PIDENTIFY{ 0, 1, 0, 0, 0, 0, 0xA1 };
            constexpr IDEREGS IDENTIFY{ 0, 1, 0, 0, 0, 0, 0xEC };
            std::array<uint8_t, 512> Diskinfo{};

            if (SCSI_miniport(Devicehandle, IDENTIFY, &Diskinfo) ||
                IDE(Devicehandle, PIDENTIFY, &Diskinfo) ||
                IDE(Devicehandle, IDENTIFY, &Diskinfo) ||
                ATA(Devicehandle, &Diskinfo))
            {
                Diskinfo[40] = 0;
                for (int i = 0; i < 10; ++i) *(uint16_t *)&Diskinfo[20 + i * 2] = cmp::fromBig(*(uint16_t *)&Diskinfo[20 + i * 2]);
                Result.Serial = std::string((char *)&Diskinfo[20]);

                const auto Words = (uint16_t *)&Diskinfo[174];
                if ((Words[0] & 0xC100) == 0x4100)
                {
                    for (int i = 0; i < 4; ++i) Words[21 + i] = cmp::fromBig(Words[21 + i]);
                    Result.UUID = Encoding::toHexstringU(Blob_t((uint8_t *)&Words[21], 8));
                }
            }
        };

        auto Handle = CreateFileW(L"\\\\.\\C:", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (Handle == INVALID_HANDLE_VALUE) Handle = CreateFileW(L"\\\\.\\C:", GENERIC_READ | GENERIC_WRITE, NULL, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (Handle == INVALID_HANDLE_VALUE) { Errorprint("System is borked."); assert(false); return {}; }

        Diskinfo_t Result{};

        // Try everything until something works.
        if (!Result.Full()) Modern_NVME(Handle, Result);
        if (!Result.Full()) Legacy_NVME(Handle, Result);
        if (!Result.Full()) ATA(Handle, Result);
        if (!Result.Full()) SCSI(Handle, Result);
        if (!Result.Full()) SMART(Handle, Result);

        Debugprint(va("Diskinfo: %s - %s", Result.UUID.c_str(), Result.Serial.c_str()));
        CloseHandle(Handle);
        return Result;

    }
}
