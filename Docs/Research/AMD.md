## AMD related research
AMDs software division has always been lacking, so there's barely any doccumented information relating to their SDKs.
Even their own developers (e.g. ROCm) seem to ask for features that already exist, and are used by some OEMs.
So here's some research after spending a week on poking at their drivers and SDKs. You may notice inconsistencies with
naming, spelling, etc., these are intentional to reflect what you'll find in the SDKs and binaries.


### ADL/ADL2/ADLX - SDK
AMDs primary SDKs for interacting with the devices. They are mostly abstractions on top of eachother and result in a
`ADL(2)_Send` or `ADL(2)_SendX2` call. These are then transformed and passed to the driver via WDDMs `D3DKMTEscape`.
The best source of information on the commands and structures are the C# Dll's `atidemgy.dll` and `ADL.Foundation.dll`.

```c++
int ADL_Send(ADLChannelPacket *Packet);
int ADL_SendX2(ADLChannelPacketX2 *Packet);
int ADL2_Send(void *Context, ADLChannelPacket *Packet);
int ADL2_SendX2(void *Context, ADLChannelPacketX2 *Packet);

struct ADLChannelPacket
{
    int32_t AdapterIndex;
    int32_t InputSize;
    void *InputData;
    int32_t OutputSize;
    void *OutputData;
    int32_t CWDDECode;
    uint32_t DeviceHandle;
};
struct ADLChannelPacketX2
{
    int32_t AdapterIndex;
    int32_t iBus;
    int32_t iDevice;
    int32_t iFunction;
    int32_t InputSize;
    void *InputData;
    int32_t OutputSize;
    void *OutputData;
    int32_t CWDDECode;
    uint32_t DeviceHandle;
};
```

### CWWDE/CWDDE - Legacy protocol.
As mentioned above, AMD/ATI likes to build on existing tools. So the header for ADL input should be a CWDDE header.
```C++
struct CWDDECMD_t
{
    uint32_t Totalsize;     // Including the header.
    uint32_t EscapeID;      // Command ID of sorts.
    uint32_t AdapterIndex;
    uint32_t Result;        // Used in composed commands.
};
```
<details>
  <summary>Example usage</summary>

```c++
int32_t getMemoryclock()
{
    struct CIASICID_t
    {
      uint32_t ulSize;
      uint32_t ulFlags;
      uint32_t ulChipID;
      uint32_t ulFamily;
      uint32_t ulEmulatedRevision;
      uint32_t ulVramInstalled;
      uint32_t ulVramType;
      uint32_t ulVramBitWidth;
      uint16_t usXClock;
      uint16_t usMClock;
      uint32_t ulSubsystemID;
      uint32_t ulVramInvisible;
      uint32_t ulGfxEngineID;
      uint16_t usXClockMhz;
      uint16_t usMClockMhz;
      uint16_t usCpPm4UcodeFeatureVersion;
      uint16_t usPadding;
      int ulPadding[2];
    } Output{ sizeof(CIASICID_t) };
    
    constexpr uint32_t CWDDECI_GETASICID = 4194563U;
    CWWDECMD_t Input{ sizeof(CWWDECMD_t), CWDDECI_GETASICID }

    constexpr auto ADL_PASSTHROUGH = 1;
    ADLChannelPacket Packet { 1, sizeof(Input), &Input, sizeof(Output), &Output, ADL_PASSTHROUGH, 0 };
    
    if (ADL_Send(&Packet) != 0) return -1;
    return Output.usMClockMhz;    
}
```
</details>


### WDDM - Windows Display Driver Model
Since Vista, communication with the driver is done via a `D3DKMTEscape` call rather than `ExtEscape`. They are
pretty similar, just that WDDM lets the vendor define a header. Which means that for AMD we get yet another layer
of wrappers. 

```c++
struct WDDM_t
{
    uint32_t Vendortag = 2;                 // "The second vendor"
    uint32_t Interfaceversion = 0x10002;    // WDDM 1.2
    uint32_t Unknown[16];                   // Irrelevant to us.
    uint32_t Totalsize;
};

// BDF selection (see ADLChannelPacketX2).
struct CSEL_t
{
    uint32_t iBus;
    uint32_t iDevice;
    uint32_t iFunction;
    uint32_t Magic = 'CSEL';                // NULL to disable CSEL usage.
};

// Used for escapes.
struct ATI_t
{
    uint32_t Headersize = sizeof(ATI_t);
    uint32_t Driverversion = 0x10000;       // 1.0 - minumum
    uint32_t Category;
    uint32_t Caller = 5;                    // Usermode, not fully explored.

    CSEL_t CSEL;                            // Device-selection.
    uint32_t Unknown[24];                   // Irrelevant to us.
};

```

<details>
  <summary>WDDM usage</summary>

```c++
constexpr bool Skipheader(uint32_t EscapeID)
{
    const auto TopW = (EscapeID & 0xFFFF0000) >> 16;
    return (TopW == 0x100) || (TopW == 0x300) || (TopW == 0x600);
}
constexpr uint32_t Filter(uint32_t EscapeID)
{
    const auto TopW = (EscapeID & 0xFFFF0000) >> 16;
    const auto TopB = (EscapeID & 0xFF000000) >> 24;

    if (TopW > 0x14 && !(EscapeID & 0x30000))
    {
        if (TopW == 0x15 || TopW == 0x20)
            return 0x2000000;

        if (TopW == 0x40)
            return 0x3000000;
    }

    if (TopB == 1 || TopB == 3 || TopB == 6)
        return EscapeID;

    return 0x3000000;
}

constexpr bool WinXP = false;
std::vector<uint8_t> D3DCall(const CWDDECMD_t *Command, std::span<const std::byte> Input, std::span<const std::byte> Output, const CSEL_t *BDF = nullptr)
{
    constexpr auto Protocolsize = WinXP ? 0x88 : 0xD4;
    const auto noHeader = Skipheader(Command->EscapeID);
    const auto Totalsize = Protocolsize + Input.size() + Output.size() - (sizeof(CWDDECMD_t) * noHeader);

    std::vector<uint8_t> Result(Totalsize);
    auto Buffer = Result.data();

    // WDDM header.
    if (!WinXP)
    {
        const auto WDDM = (WDDM_t *)Buffer;
        Buffer += sizeof(WDDM_t);
        *WDDM = WDDM_t{};

        WDDM->Totalsize = Totalsize;
    }

    // ATI header.
    {
        const auto ATI = (ATI_t *)Buffer;
        Buffer += sizeof(ATI_t);
        *ATI = ATI_t{};

        // No magic if disabled.
        if (!BDF) ATI->CSEL.Magic = NULL;
        else ATI->CSEL = *BDF;

        // Similar to Nvidias module ID.
        ATI->Category = Filter(Escape);
    }

    // Total input size.
    *(uint32_t *)Buffer = uint32_t(Input.size() + (sizeof(CWDDECMD_t) * !noHeader));

    // CWDDE header.
    if (!noHeader)
    {
        const auto CWDDE = (CWDDECMD_t *)Buffer;
        Buffer += sizeof(CWDDECMD_t);
        *CWDDE = *Command;
    }

    // Extra input.
    std::memcpy(Buffer, Input.data(), Input.size()); 
    Buffer += Input.size();
    
    // Output data.
    *(uint32_t *)Buffer = uint32_t(Output.size()); Buffer += sizeof(uint32_t);
    std::memcpy(Buffer, Output.data(), Output.size());

    return Result;
}
```
</details>

### Consumer UUID
AMD only provides an official UUID for their enterprise GPUs, and only for Vega10+. There is however another ID used
for QA that specifies information about the ASIC. The problem: these IDs are only available via MMIO and I've not found
any official AMD drivers that implement this feature. The information itself is stored in the EFuse configuration which
is either initialized on startup or queried via SMU depending on the GPU architecture. 
The best source for information are `atitool` (Linux, archived on Github) and `agt.exe` (Windows, included with MSI Live Update 6).

Depending on the architecture, EFuse provides up to 128-bits of information. The encoding is architecture dependant,
so I'll just include an example for Tonga as I found a cheap R9 380. Other architectures may have less bits and 
some seem to write a Serialnumber into their VBIOS (search for a `SN` tag).

For more information about SMU IO, check the [Linux source](https://elixir.bootlin.com/linux/latest/source/drivers/gpu/drm/amd/pm/powerplay/smumgr/smumgr.c)

```c++
// EFuse bits 368-417 
constexpr uint64_t Tonga_ASIC = 0x00;

// EFuse bits 506-548
constexpr uint64_t Tonga_ASICEX = 0x00;

// Most architectures have at least some of the same properties.
struct Tonga_UUID
{
    char Lot[7];
    uint8_t WaferID;
    uint8_t Die_posX;
    uint8_t Die_posY;
    uint8_t Foundry;
    uint8_t Fab;
    uint8_t Year;
    uint8_t Week;

    Tonga_UUID(uint64_t ASIC, uint64_t ASICEX)
    {
        WaferID = (ASIC >> 32) & 0x1F;
        Die_posX = (ASIC >> 37) & 0xFF;
        Die_posY = (ASICEX >> 0) & 0xFF;
        Foundry = (ASICEX >> 9) & 0x1F;
        Fab = (ASICEX >> 14) & 0x3F;
        Year = (ASICEX >> 20) & 0x0F;
        Week = (ASICEX >> 24) & 0x3F;

        int Index = 0;
        while (ASIC)
        {
            const auto Rem = ASIC % 0x25;
            ASIC /= 0x25;

            if (Rem == 0) Lot[Index++] = ' ';
            else if (Rem > 0x1A) Lot[Index++] = char(Rem + 0x15);
            else Lot[Index++] = char(Rem + 0x40);
        }
        Lot[Index] = 0;
    }
};
```

APU architectures seem to have a similar method using SMN registers. Supported by `Starship`, `Fireflight`, and `Raven`.

<details>
    <summary>More complete Windows example</summary>

```c++
// uint32_t ReadPhysical(void *Address);
// void WritePhysical(void *Address, uint32_t Data);

#pragma pack(push, 1)
struct PCIAlloc_t { uint64_t Baseaddress; uint16_t Segment; uint8_t FirstBus, LastBus; uint32_t RESERVED; };
#pragma pack (pop)

std::vector<PCIAlloc_t> getAllocations()
{
    // Get the MCFG table from ACPI.
    const auto Size = GetSystemFirmwareTable('ACPI', 'GFCM', nullptr, 0);
    if (Size == 0) return {};

    const auto Buffer = static_cast<uint8_t *>(alloca(Size));
    GetSystemFirmwareTable('ACPI', 'GFCM', Buffer, Size);

    const auto Allocations = (Size - 44) / sizeof(PCIAlloc_t);
    std::vector<PCIAlloc_t> Result(Allocations);

    for (size_t i = 0; i < Allocations; ++i)
        Result[i] = *(Allocation_t *)(Buffer + 44 + sizeof(PCIAlloc_t) * i);

    return Result;
}

struct BDF_t { uint8_t Bus, Device, Function; };
std::optional<BDF_t> findAMD(const PCIAlloc_t &Allocation)
{
    constexpr uint32 VendorID = 0x1002;   // ATI, AMD still uses it.

    for (int Bus = Allocation.FirstBus; Bus <= Allocation.LastBus; Bus++)
    {
        for (int Device = 0; Device < 32; ++Device)
        {
            const auto Address = Allocation.Baseaddress + (Bus << 20) | (Device << 15);
            const auto Vendordev = ReadPhysical((void *)Address);
            if ((Vendordev & 0xFFFF) == VendorID)
                return BDF_t{ Bus, Device, 0 };
        }
    }

    return {};
}

uint64_t getRegisterbase(const PCIAlloc_t &Allocation, const BDF_t &BDF)
{
    const auto Address = Allocation.Baseaddress + (BDF.Bus << 20) | (BDF.Device << 15) | (BDF.Function << 12);
    std::array<uint32_t, 6> BAR{};
    
    // PCI BaseAddressRegister
    for (int i = 0; i < 6; ++i)
        BAR[i] = ReadPhysical((void *)(Address + 0x10 + i * sizeof(uint32_t)));

    // AMD offers 3 addresses, showing how to get all for the future.
    uint64_t Reg = 0, Mem = 0, IO = 0;

    // Bit 4 is set to indicate 64-bit addresses.
    if (!(BAR[5] & 4)) Reg = BAR[5] & 0xFFFFFFF0;
    else Reg = (BAR[5] & 0xFFFFFFF0) + (uint64_t(BAR[3]) << 32U);

    if (!(BAR[0] & 4)) Reg = BAR[0] & 0xFFFFFFF0;
    else Reg = (BAR[0] & 0xFFFFFFF0) + (uint64_t(BAR[1]) << 32U);

    // The leftover BAR.
    if (BAR[0] & 4) IO = BAR[4] & 0xFF00;
    else IO = BAR[1] & 0xFF00;

    return Reg;
}

uint32_t ReadSMU(uint64_t Registerbase, size_t First_bit, size_t Last_bit)
{
    const auto A = std::floor(First_bit * 0.03125);
    const auto B = std::floor(Last_bit * 0.03125) - A;

    // For Bonaire, Hawaii, Tonga
    constexpr uint32_t SMU_IN = 130;
    constexpr uint32_t SMU_OUT = 131;

    for (int i = 0; i < B; ++i)
    {
        const auto Offset = 4 * (i + A) - 0x3FF00000;  

        PhysicalWrite(Registerbase + (4 * SMU_IN), Offset);
        auto Part = PhysicalRead(Registerbase + (4 * SMU_OUT));
    }

    // Assemble the DWORD..
}

Tonga_UUID getTonga(uint64_t Registerbase)
{
    auto A = ReadSMU(Registerbase, 368, 399);
    auto B = ReadSMU(Registerbase, 400, 417);
    uint64_t ASIC = A | ((B & 0x3FFFF) << 32U);

    auto C = ReadSMU(Registerbase, 506, 537);
    auto D = ReadSMU(Registerbase, 538, 548);
    uint64_t ASICEX = C | ((B & 0x3FF) << 32U);

    return Tonga_UUID(ASIC, ASICEX);
}
```
</details>

