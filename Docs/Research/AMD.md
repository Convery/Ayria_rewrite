# AMD related research
The public documentation for AMDs software, at least the subset available to the public, has always been lacking.
At times, even their own developers (e.g. ROCm) are wishing for features that already exist for OEM partners.
So here's an overview of a weeks worth of research into AMD's drivers and SDKs, and where to find specifics for your
usecase. 

As a sidenote if you wish to further research this: do not rely on naming being consistent. 
AMD's RTTI, comments, and SDKs have a lot of typos and different names are used for the same functionallity, often
for no obvious reason (e.g. versioning).

## Consumer GPU UUID
AMD only provides an official UUID for their enterprise GPUs, and seemingly only for Vega10+. 
The [Linux source code](https://elixir.bootlin.com/linux/latest/source/drivers/gpu/drm/amd/pm/powerplay/hwmgr/vega10_hwmgr.c#L509)
shows how to fetch the official one on Vega10.
All architectures do however provide between 32 and 128 bits of identification for ASIC verification/SLT/QA.
These IDs are part of the EFuse configuration and can either be read directly (MM) or queried via SMU.
Some older cards even initialize the VBIOS with a serial-number (search for a `SN` tag) on startup.

The best source for architecture-specific information are `atitool` (Linux, archived on Github) 
and `agt.exe` (Windows, included with MSI Live Update 6).

For this research I aquired a cheap R9 380, which builds on the Tonga architecture and provides 128-bits formated as such:
```cpp
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

Keep in mind that SMU/MMIO requires access to physical memory and thus you'll need a driver on Windows. 
AMD does not provide anything in their (official) drivers for doing this so you need your own.
If curious, here's an outline of how to fetch the UUID on Windows:

<details>
    <summary>Windows Tonga Example</summary>

```cpp
// uint32_t ReadPhysical(void *Address);
// void WritePhysical(void *Address, uint32_t Data);

#pragma pack(push, 1)
struct PCIAlloc_t { uint64_t Baseaddress; uint16_t Segment; 
                    uint8_t FirstBus, LastBus; uint32_t RESERVED; };
#pragma pack (pop)

// Find the PCIe configuration-space.
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

// Scan for an AMD device, you should also check class-type.
struct BDF_t { uint8_t Bus, Device, Function; };
std::optional<BDF_t> findAMD(const PCIAlloc_t &Allocation)
{
    constexpr uint32 VendorID = 0x1002;   // ATI, AMD still uses it.

    for (int Bus = Allocation.FirstBus; Bus <= Allocation.LastBus; Bus++)
    {
        // Might also want to check function.
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

// Parse the PCI BARs to get the registers (both for reading and SMU).
uint64_t getRegisterbase(const PCIAlloc_t &Allocation, const BDF_t &BDF)
{
    const auto Address = Allocation.Baseaddress + (BDF.Bus << 20) | (BDF.Device << 15) | (BDF.Function << 12);
    std::array<uint32_t, 6> BAR{};

    // PCI BaseAddressRegister
    for (int i = 0; i < 6; ++i)
        BAR[i] = ReadPhysical((void *)(Address + 0x10 + i * sizeof(uint32_t)));

    // AMD offers 3 addresses, showing how to get all for posterity.
    uint64_t Reg = 0, Mem = 0, IO = 0;

    // Bit 2 is set to indicate 64-bit addresses.
    if (!(BAR[5] & 4)) Reg = BAR[5] & 0xFFFFFFF0;
    else Reg = (BAR[5] & 0xFFFFFFF0) + (uint64_t(BAR[3]) << 32U);

    if (!(BAR[0] & 4)) Reg = BAR[0] & 0xFFFFFFF0;
    else Reg = (BAR[0] & 0xFFFFFFF0) + (uint64_t(BAR[1]) << 32U);

    // The leftover BAR.
    if (BAR[0] & 4) IO = BAR[4] & 0xFF00;
    else IO = BAR[1] & 0xFF00;

    return Reg;
}

// Extracted so to cover other register-values.
uint32_t ReadMM(uint64_t Registerbase, int32_t Register)
{
    return PhysicalRead(Registerbase + (4 * Register));
}
void WriteMM(uint64_t Registerbase, int32_t Register, uint32_t Value)
{
    PhysicalWrite(Registerbase + (4 * Register), Value);
}

// Tonga specific.
uint32_t Tonga_ReadSMU(uint64_t Registerbase, size_t First_bit, size_t Last_bit)
{
    // For Bonaire, Hawaii, Tonga
    constexpr uint32_t SMU_IN = 130;
    constexpr uint32_t SMU_OUT = 131;

    const auto Offset1 = 4 * (First_bit / 32) - 0x3FF00000;
    const auto Offset2 = 4 * (Last_bit / 32) - 0x3FF00000;

    WriteMM(Registerbase, SMU_IN, Offset1);
    const auto Part1 = ReadMM(Registerbase, SMU_OUT);

    WriteMM(Registerbase, SMU_IN, Offset2);
    const auto Part2 = ReadMM(Registerbase, SMU_OUT);

    // Assemble the bits you want into a DWORD.
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

As shown in the Linux code, Vega10 uses SMU to get the official ID. The unofficial ID is directly read via MM.
Both are 64-bit, so if you have one of those cards, please compare the IDs. Unofficial ID:
<details>
    <summary>Vega10 version</summary>

```cpp
void getVega10(uint64_t Registerbase)
{
    const auto Efuse = 0x17400;

    auto A = ReadMM(Registerbase, Efuse + 1);
    auto B = ReadMM(Registerbase, Efuse + 2);
    uint64_t ASIC = A | (B << 32U);

    uint8_t WaferID  = (ASIC >> 17) & 0x1F;
    uint8_t Die_posX  = (ASIC >> 11) & 0x3F;
    uint8_t Die_posY  = (ASIC >> 5) & 0x3F;
    uint8_t Fab  = (ASIC >> 0) & 0x1F;

    char Lot[7];
    ASIC >>= 22;
    ASIC &= 0xFFFFFFFF;

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

```
</details>

### ADL/ADL2/ADLX - Public SDK
AMD has over the years released quite a few SDKs for interacting with the GPU. They generally just
add wrappers around the original ADL library (`atiadlxx.dll`/`atiadlxy.dll`) and result in a call to
`ADL(2)_Send` or `ADL(2)_SendX2`. 

<details>
    <summary>ADL Send-structs</summary>

```cpp
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
</details>

The `_Send` functions then forwards the message to the displaydriver via `ExtEscape` (XP) or `D3DKMTEscape` (Vista+).
Keeping with AMDs habbit of wrapping older APIs/protocols the data sent should begin with the (`CWWDE/CWDDE`) header
from the ATI days.

<details>
  <summary>CWDDE example</summary>

```cpp
struct CWDDECMD_t
{
    uint32_t Totalsize;     // Including the header.
    uint32_t EscapeID;      // Command ID of sorts.
    uint32_t AdapterIndex;
    uint32_t Result;        // Used in composed commands.
};

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
      uint32_t ulPadding[2];
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

Further information about the escape IDs, and the structures can be found in the C# Dll's `atidemgy.dll` and `ADL.Foundation.dll`.

### WDDM - Windows Display Driver Model
With Vista, communication with the display-driver is done via a `D3DKMTEscape` call rather than `ExtEscape`. They are
pretty similar, just that WDDM lets the vendor define a header and the buffer is used for both input and output. 
Which means that for AMD we get yet another layer of wrappers. 
So if you don't want to include ADL as a dependency, an example of how to format the message `D3DKMTEscape` is provided.

```cpp
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

// Used for all escapes.
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
  <summary>WDDM example</summary>

```cpp
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
