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
                    const auto Hashed = Hash::SHA256(Current);

                    if (Result.RAMSerial.empty())
                        Result.RAMSerial.resize(Hashed.size());

                    for (uint8_t i = 0; i < Hashed.size(); ++i)
                        Result.RAMSerial[i] ^= Hashed[i];
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
        Debugprint(va("UUID: %s", Result.UUID.c_str()));
        Debugprint(va("Caseserial: %s", Result.Caseserial.c_str()));
        Debugprint(va("MOBOSerial: %s", Result.MOBOSerial.c_str()));
        Debugprint(va("RAMSerial: %s", Encoding::toHexstringU(Result.RAMSerial).c_str()));

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
        Debugprint(va("ARP: %s", Encoding::toHexstringU(Result).c_str()));

        CloseHandle(Handle);
        return Encoding::toHexstringU(Result);
    }

    // Get the primary volume's information.
    struct Diskinfo_t { std::string Serial, UUID; bool Full() const { return !Serial.empty() && !UUID.empty(); } };
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

        // In-case we want to verify something.
        Debugprint(va("Diskinfo: %s - %s", Result.UUID.c_str(), Result.Serial.c_str()));

        CloseHandle(Handle);
        return Result;

    }

    // Get the CPU model.
    struct CPUinfo_t { uint32_t Versioninfo; union { char Vendor[13]; int RAW[4]; }; };
    inline CPUinfo_t getCPUinfo()
    {
        int VendorID[4]{}; __cpuid(VendorID, 0);
        int CPUID[4]{}; __cpuid(CPUID, 1);

        return CPUinfo_t
        {
            .Versioninfo = std::bit_cast<uint32_t>(CPUID[0]),
            .RAW = { VendorID[1], VendorID[3], VendorID[2] }
        };
    }

    // TPM 1.2 requires authorization, so this will only work on TPM 2.0+
    inline std::optional<std::vector<uint8_t>> getTPMEK()
    {
        // NOTE(tcn): For Socket mode, prefix the command with { uint32_t Command = 8, uint8_t Locality = 0, uint32_t Payloadsize = sizeof(Command) }
        uint8_t Command[14] =
        {
            0x80, 0x01,             // TPM_ST_NO_SESSIONS
            0x00, 0x00, 0x00, 0x0E, // commandSize
            0x00, 0x00, 0x01, 0x73, // TPM_CC_ReadPublic
            0x40, 0x00, 0x00, 0x0B  // TPM_RH_ENDORSEMENT
        };

        // Expected response is 512 bytes.
        DWORD Responsesize = 1024;
        uint8_t Response[1024]{};

        const auto Handle = CreateFileW(L"\\??\\TPM", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (INVALID_HANDLE_VALUE == Handle) return {};

        const auto Failed = DeviceIoControl(Handle, 0x22C00CU, Command, sizeof(Command), Response, Responsesize, &Responsesize, nullptr);
        if (Failed || Responsesize < 10 || *(uint32_t *)&Response[8] != NULL) { CloseHandle(Handle); return {}; }

        // ECC key.
        if (*(uint16_t *)&Response[16] == 0x23)
        {
            const auto S1 = *(uint16_t *)&Response[112];
            const auto S2 = *(uint16_t *)&Response[146];
            std::vector<uint8_t> Result(S1 + S2);

            for (int i = 0; i < S1; ++i)
                Result[i] = Response[114 + i];

            for (int i = 0; i < S2; ++i)
                Result[i + S1] = Response[148 + i];

            CloseHandle(Handle);
            return Result;
        }
        else
        {
            const auto Size = *(uint16_t *)&Response[112];
            std::vector<uint8_t> Result(Size);

            for (int i = 0; i < Size; ++i)
                Result[i] = Response[114 + i];

            CloseHandle(Handle);
            return Result;
        }
    }

    // Modern GPUs tend to have a unique ID.
    inline std::string getNVIDIA()
    {
        D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Adapter{};
        std::wcscpy(Adapter.DeviceName, L"\\\\.\\DISPLAY1");
        D3DKMTOpenAdapterFromGdiDisplayName(&Adapter);

        // Shouldn't happen..
        if (Adapter.hAdapter == 0)
            return {};

        // WDDM's equivalent of DeviceIoControl.
        const auto Driverescape = [Adapter](void *Data, UINT Size) -> bool
        {
            D3DKMT_ESCAPE Escape{ Adapter.hAdapter, NULL, D3DKMT_ESCAPE_DRIVERPRIVATE };
            Escape.PrivateDriverDataSize = Size;
            Escape.pPrivateDriverData = Data;

            return 0 == D3DKMTEscape(&Escape);
        };

        // Not really documented. Check nvml.dll for a basic outline of UMD <-> KMD communication.
        const auto NvidiaCMD = [Driverescape](uint32_t CommandID, uint32_t Devicehandle, uint32_t Objecthandle, uint32_t Param1, uint32_t Param2, uint32_t Param3, std::span<uint32_t> InOut)
        {
            struct NVIDIA_HDR
            {
                // Common header.
                uint32_t VendorTag = 'NVDA';
                uint32_t Version = 0x10002;         // 1.2
                uint32_t Commandsize;               // Including header.
                uint32_t CallerTag = 'NV**';

                // Escape part.
                uint32_t CommandID;                 // Set << 24 | ID
                uint32_t Unknown[6];                // Not relevant for us.
                uint32_t Completed = 0;             // Result, > 0 if successful.

                // Arguments.
                uint32_t Devicehandle;
                uint32_t Objecthandle;
                union
                {
                    struct
                    {
                        uint32_t Destination;
                        uint32_t Objecttype;
                        uint32_t Resultsize;        // + 4 bytes for the result code.
                    } Type_2A;
                    struct
                    {
                        uint32_t Subcommand;
                        uint32_t Resultsize;
                    } Type_2B;
                };
            };

            // Buffer is updated by the driver call, so need to be contigious.
            const auto Commandsize = uint32_t(sizeof(NVIDIA_HDR) + (InOut.size() * sizeof(uint32_t)));
            const auto Buffer = (uint8_t *)alloca(Commandsize);
            const auto Header = (NVIDIA_HDR *)Buffer;
            std::memset(Buffer, 0, Commandsize);
            *Header = NVIDIA_HDR{};

            // If there's no object, assume it's to the device.
            if (Objecthandle == NULL) Objecthandle = Devicehandle;

            Header->CommandID = CommandID;
            Header->Commandsize = Commandsize;
            Header->Devicehandle = Devicehandle;
            Header->Objecthandle = Objecthandle;

            // Parameters are command dependant.
            if (CommandID == 0x500002A)
            {
                Header->Type_2A.Destination = Param1;
                Header->Type_2A.Objecttype = Param2;
                Header->Type_2A.Resultsize = Param3 - 4;
            }
            if (CommandID == 0x500002B)
            {
                Header->Type_2B.Subcommand = Param1;
                Header->Type_2B.Resultsize = Param2;
            }

            // NOTE(tcn): The buffer starts with input, but for our commands they are NULL.
            // std::memcpy(Buffer + sizeof(NVIDIA_HDR), InOut.data(), InOut.size() * sizeof(uint32_t));

            const auto Success = Driverescape(Buffer, Commandsize);
            if (Success) std::memcpy(InOut.data(), Buffer + sizeof(NVIDIA_HDR), InOut.size() * sizeof(uint32_t));

            return Success;
        };

        // Get a handle to the primary GPU.
        const auto Devicehandle = [NvidiaCMD]()
        {
            uint32_t Response[2]{};

            const auto Success = NvidiaCMD(0x500002A, NULL, NULL, 0, 0x41, sizeof(Response), Response);

            const auto Errorcode = Response[0];
            const auto Devicehandle = Response[1];

            if (!Success || Errorcode != NULL) return 0U;
            else return Devicehandle;
        }();

        // Invalid handle =(
        if (Devicehandle == NULL)
            return {};

        // Internal allocations and configuration.
        {
            uint32_t Response[15]{};
            NvidiaCMD(0x500002A, Devicehandle, NULL, 0xA55A0000, 0x80, sizeof(Response), Response);
        }
        {
            uint32_t Response[2]{};
            NvidiaCMD(0x500002A, Devicehandle, 0xA55A0000, 0xA55A0010, 0x2080, sizeof(Response), Response);
        }

        uint32_t Response[67]{};
        const auto Success = NvidiaCMD(0x500002B, Devicehandle, 0xA55A0010, 0x2080014A, sizeof(Response), NULL, Response);

        const auto Errorcode = Response[0];
        const auto Length = Response[2];

        if (!Success || Errorcode != NULL || Length == NULL)
            return {};

        return std::string((const char *)&Response[3], Length);
    }
    inline std::string getAMD()
    {
        /*
            NOTE(tcn):
            The consumer GPUs do not have an 'official' ID, but they do contain ASCI serials for QA/verification.
            These are available in physical memory and are architecture dependant. I docummented the general idea
            in /Research/AMD.md but you still need a driver for MMIO on Windows.
        */

        return {};
    }
    inline std::optional<std::string> getGPU()
    {
        const auto NVIDIA = getNVIDIA();
        if (!NVIDIA.empty()) return NVIDIA;

        return std::nullopt;
    }
}
