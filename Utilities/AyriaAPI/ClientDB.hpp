/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-09-26
    License: MIT
*/

#pragma once
#include <Stdinclude.hpp>

namespace AyriaAPI
{

    // To allow for breaking changes later.
    namespace V1
    {

        struct Account_t
        {
            std::u8string Publickey;    // Base58 encoded qDSA::Publickey_t
            int64_t Firstseen;          // Account creation in UTC.
            int64_t Lastseen;           // Last update in UTC.

            // For alternative lookup where the game uses a 32 or 64 bit identifier.
            uint64_t ShortID;           // ((WW64(Publickey) >> 32) << 32) | WW32(Publickey)

            // Helper to construct from a SELECT * query.
            static Account_t Construct(const std::u8string &Publickey, int64_t Firstseen, int64_t Lastseen, uint64_t ShortID)
            {
                if (ShortID == NULL) ShortID = ((Hash::WW64(Publickey) >> 32) << 32) | Hash::WW32(Publickey);
                return { Publickey, Firstseen, Lastseen, ShortID };
            }
        };

        struct Clientinfo_t
        {
            enum class Clientstate_t : uint8_t { NONE, AFK, DND, LFG, INV  };
            enum class Clienttype_t : uint8_t { HWID, WEB, TEMPORAL };

            // Linked to Account_t
            std::u8string Publickey;
            uint64_t ShortID;

            // Game information.
            uint32_t Region;            // Abstract geolocation, int16_t(LAT) << 16 | int16_t(Long)
            uint32_t GameID;            // Provided by Platformwrapper.
            uint32_t ModID;             // Provided by plugins.

            // Social information.
            Clientstate_t Clientstate;  // What the client is up to..
            std::u8string Username;     // If null, Username = va("%llX", ShortID)
            uint64_t AvatarID;          // LFS FileID

            // For future use.
            int32_t Reputation;         // How likable the client is..
            Clienttype_t Clienttype;    // Some services may be restricted based on type.

            // Helper to construct from a SELECT * query.
            static Clientinfo_t Construct(const std::u8string &Publickey, uint64_t ShortID, uint32_t Region, uint32_t GameID, uint32_t ModID, uint8_t Clientstate, std::u8string Username, uint64_t AvatarID, int32_t Reputation, uint8_t Clienttype)
            {
                if (Username.empty()) Username = va("%llX", ShortID);
                return { Publickey,  ShortID,  Region,  GameID,  ModID, Clientstate_t(Clientstate), Username, AvatarID, Reputation,  Clienttype_t(Clienttype) };
            }
        };

        struct Clientrelation_t
        {
            using Flags_t = union { uint8_t Raw{}; struct { uint8_t isFriend : 1, isBlocked : 1, isFollowing : 1; }; };

            std::u8string SourceID, TargetID;
            Flags_t Flags;

            // Helper to construct from a SELECT * query.
            static Clientrelation_t Construct(const std::u8string &SourceID, const std::u8string &TargetID, uint8_t Flags)
            {
                return { SourceID, TargetID, {.Raw = Flags} };
            }
        };

        struct Clientpresence_t
        {
            // Linked to Account_t
            std::u8string Publickey;

            uint32_t Category;
            std::vector<std::u8string> Keys, Values;

            // Helper to construct from a SELECT * query.
            static Clientpresence_t Construct(const std::u8string &Publickey, uint32_t Category, const std::vector<std::u8string> &Keys, const std::vector<std::u8string> &Values)
            {
                return { Publickey, Category, Keys, Values };
            }
        };

        struct Clientmessage_t
        {
            // Linked to Account_t
            std::u8string SourceID, TargetID;

            uint32_t Messagetype;       // WW32("myType")
            int64_t Sent, Received;     // Timestamps.
            std::u8string B85Message;

            // Helper to construct from a SELECT * query.
            static Clientmessage_t Construct(const std::u8string &SourceID, const std::u8string &TargetID, uint32_t Messagetype, int64_t Sent, int64_t Received, const std::u8string &B85Message)
            {
                return { SourceID, TargetID, Messagetype, Sent, Reveived, B85Message };
            }
        };

        struct Serverheader_t
        {
            using Serverflags_t = union { uint16_t Raw{}; struct { uint16_t isDedicated : 1, isSecure : 1, isPasswordprotected : 1, RESERVED1 : 1, RESERVED2 : 1; }; };
            using Ports_t = union { uint64_t Raw{}; struct { uint16_t Primaryport; uint16_t Extraports[3]; }; };

            // Linked to Account_t
            std::u8string Publickey;

            // Game-dependant.
            uint16_t Gameflags;
            Serverflags_t Serverflags;
            std::u8string Servername, Mapname;
            uint32_t Playercount, Playerlimit;

            // Connection-info, IPv4 or IPv6
            std::u8string IPAddress;
            Ports_t Ports;

            // Helper to construct from a SELECT * query.
            static Serverheader_t Construct(const std::u8string &Publickey, uint16_t Gameflags, uint16_t Serverflags, const std::u8string &Servername, const std::u8string &Mapname, uint32_t Playercount, uint32_t Playerlimit, const std::u8string &IPAddress, uint64_t Ports)
            {
                return { Publickey, Gameflags, Serverflags_t{.Raw = Serverflags}, Servername, Mapname, Playercount, Playerlimit, IPAddress, Ports_t{.Raw = Ports} };
            }
        };

        struct Serverdata_t
        {
            // Linked to Account_t
            std::u8string Publickey;

            // Probably game-dependant.
            std::vector<std::u8string> Infokeys, Infovalues;
            std::vector<std::u8string> Tagkeys, Tagvalues;

            // Helper to construct from a SELECT * query.
            static Serverdata_t Construct(const std::u8string &Publickey, const std::vector<std::u8string> &Infokeys, const std::vector<std::u8string> &Infovalues, const std::vector<std::u8string> &Tagkeys, const std::vector<std::u8string> &Tagvalues)
            {
                return { Publickey, Infokeys, Infovalues, Tagkeys, Tagvalues };
            }
        };

        struct Playerdata_t
        {
            // Linked to Account_t
            std::u8string Serverkey, Clientkey;

            // Probably game-dependant.
            std::vector<std::u8string> Keys, Values;

            // Helper to construct from a SELECT * query.
            static Playerdata_t Construct(const std::u8string &Serverkey, const std::u8string &Clientkey, const std::vector<std::u8string> &Keys, const std::vector<std::u8string> &Values)
            {
                return { Serverkey, Clientkey, Keys, Values };
            }
        };

        struct Guild_t
        {
            // Linked to Account_t
            std::u8string OwnerID;

            // For identification.
            uint64_t GroupID;

            // For displaying ingame.
            std::u8string Friendlyname, Grouptag;
            std::vector<std::u8string> Moderators;

            // Helper to construct from a SELECT * query.
            static Guild_t Construct(const std::u8string &OwnerID, uint64_t GroupID, const std::u8string &Friendlyname, const std::u8string &Grouptag, const std::vector<std::u8string> &Moderators)
            {
                if (GroupID == NULL) GroupID = Hash::WW64(OwnerID + u8"Guild");
                return { OwnerID, GroupID, Friendlyname, Grouptag, Moderators };
            }
        };

        struct Lobby_t
        {
            using Flags_t = union { uint8_t Raw{}; struct { uint8_t isJoinable : 1, isPublic : 1, isChatgroup : 1; }; };

            // Linked to Account_t
            std::u8string OwnerID, Serverkey;

            // For identification.
            uint64_t GroupID;

            Flags_t Flags;
            uint32_t Grouptype;
            uint32_t Maxmembers;
            std::vector<std::u8string> Moderators;

            // Helper to construct from a SELECT * query.
            static Lobby_t Construct(const std::u8string &OwnerID, const std::u8string &Serverkey, uint64_t GroupID, uint8_t Flags, uint32_t Grouptype, uint32_t Maxmembers, const std::vector<std::u8string> &Moderators)
            {
                if (GroupID == NULL) GroupID = Hash::WW64(OwnerID + u8"Lobby");
                return { OwnerID, Serverkey, GroupID, Flags_t{.Raw = Flags}, Grouptype, Maxmembers, Moderators };
            }
        };

        struct Groupinfo_t
        {
            uint64_t GroupID;

            std::vector<std::u8string> Groupkeys, Groupvalues;
            std::vector<std::u8string> Memberkeys, Membervalues;

            // Helper to construct from a SELECT * query.
            static Groupinfo_t Construct(uint64_t GroupID, const std::vector<std::u8string> &Groupdatakeys, const std::vector<std::u8string> &Groupvalues, const std::vector<std::u8string> &Memberkeys, const std::vector<std::u8string> &Membervalues)
            {
                return { GroupID, Groupkeys, Groupvalues, Memberkeys, Membervalues };
            }
        };

        struct Groupmember_t
        {
            // Linked to Account_t
            std::u8string Memberkey, Moderatorkey;

            // qDSA::Sign(Moderatorkey, Memberkey + va(u8"%llX", GroupID))
            std::u8string Signature;
            uint64_t GroupID;

            // Helper to construct from a SELECT * query.
            static Groupmember_t Construct(const std::u8string &Memberkey, const std::u8string &Moderatorkey, const std::u8string &Signature, uint64_t GroupID)
            {
                return { Memberkey, Moderatorkey, Signature, GroupID };
            }
        };

        struct Groupmessage_t
        {
            // Linked to Account_t
            std::u8string SenderID;

            uint64_t GroupID;
            uint32_t Messagetype;       // WW32("myType")
            int64_t Sent;               // Timestamp, matches syncpacket.
            std::u8string B85Message;

            // Helper to construct from a SELECT * query.
            static Groupmessage_t Construct(const std::u8string &SenderID, uint64_t GroupID, uint32_t Messagetype, int64_t Sent, const std::u8string &B85Message)
            {
                return { SenderID, GroupID, Messagetype, Sent, B85Message };
            }
        };

        struct Fileheader_t
        {
            uint64_t FileID;
            std::u8string OwnerID;

            uint32_t Filesize;
            uint32_t Checksum;

            // Helper to construct from a SELECT * query.
            static Fileheader_t Construct(uint64_t FileID, const std::u8string &OwnerID, uint32_t Filesize, uint32_t Checksum)
            {
                return { FileID, OwnerID, Filesize, Checksum };
            }
            static uint64_t CreateID(std::u8string_view OwnerID, std::u8string_view Filepath)
            {
                return uint64_t(Hash::WW32(OwnerID) << 32) | Hash::WW32(Filepath);
            }
        };

        struct Filemetadata_t
        {
            using Visibility_t = enum : uint8_t { LFS_PUBLIC, LFS_FRIENDS, LFS_PRIVATE };

            // Primary file, optional metadata file, optional thumbnail file.
            uint64_t FileID, MetadataID, PreviewID;

            Visibility_t Visibility;
            uint32_t Category;

            int64_t Creationdate;
            int64_t Modifieddate;

            std::u8string Title, Filename, Description;
            std::vector<std::u8string> Tagkeys, Tagvalues;

            // Helper to construct from a SELECT * query.
            static Filemetadata_t Construct(uint64_t FileID, uint64_t MetadataID, uint64_t PreviewID, uint8_t Visibility, uint32_t Category, int64_t Creationdate, int64_t Modifieddate, const std::u8string &Title, const std::u8string &Filename, const std::u8string &Description, const std::vector<std::u8string> &Tagkeys, const std::vector<std::u8string> &Tagvalues)
            {
                return { FileID, MetadataID, PreviewID, Visibility_t(Visibility), Category, Creationdate, Modifieddate, Title, Filename, Description, Tagkeys, Tagvalues };
            }
        };

        struct Filedata_t
        {
            uint64_t FileID;
            Blob_t Compressedfile;

            // Helper to construct from a SELECT * query.
            static Filedata_t Construct(uint64_t FileID, const Blob_t &Compressedfile)
            {
                return { FileID, Compressedfile };
            }
        };

        // 112 bytes + payload on line.
        struct Syncpacket_t
        {
            cmp::Array_t<uint8_t, 32> Publickey;
            cmp::Array_t<uint8_t, 64> Signature;

            // VVV Singned data VVV
            int64_t Timestamp;
            uint8_t Reserved;
            uint8_t Version;
            uint8_t Flags;
            uint8_t Type;

            Bytebuffer_t Data;

            // Helper to construct from a SELECT * query.
            static Syncpacket_t Construct(const std::u8string &Publickey, const std::u8string &Signature,
                int64_t Timestamp, uint8_t Reserved, uint8_t Version, uint8_t Flags, uint8_t Type, const Blob_t &Data)
            {
                return { Base58::Decode(Publickey), Base58::Decode(Signature), Timestamp, Reserved, Version, Flags, Type, Bytebuffer_t(Data) };
            }
        };

        void CreateSYNC()
        {
            constexpr auto Syncpacket =
                "CREATE TABLE IF NOT EXISTS Syncpacket ("
                "Publickey TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Signature TEXT, "

                "Timestamp INTEGER, "
                "Reserved INTEGER, "
                "Version INTEGER, "
                "Flags INTEGER, "
                "Type INTEGER, "

                "Data BLOB,"
                "UNIQUE (Publickey, Signature) );";
        }

        void CreateLFS()
        {
            constexpr auto Fileheader =
                "CREATE TABLE IF NOT EXISTS Fileheader ("
                "OwnerID TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "FileID INTEGER PRIMARY KEY, "
                "Filesize INTEGER, "
                "Checksum INTEGER );";

            constexpr auto Filemetadata =
                "CREATE TABLE IF NOT EXISTS Filemetadata ("
                "FileID INTEGER PRIMARY KEY REFERENCES Fileheader(FileID) ON DELETE CASCADE, "
                "MetadataID INTEGER DEFAULT 0, "
                "PreviewID INTEGER DEFAULT 0, "

                "Visibility INTEGER, "
                "Category INTEGER, "
                "Creationdate INTEGER, "
                "Modifieddate INTEGER, "

                "Title TEXT, "
                "Filename TEXT, "
                "Description TEXT, "

                "Tagkeys BLOB, "
                "Tagvalues BLOB );";

            constexpr auto Filedata =
                "CREATE TABLE IF NOT EXISTS Filedata ("
                "FileID INTEGER PRIMARY KEY REFERENCES Fileheader(FileID) ON DELETE CASCADE, "
                "Compressedfile BLOB );";

        }

        void Createclient()
        {
            constexpr auto Account =
                "CREATE TABLE IF NOT EXISTS Account ("
                "Publickey TEXT PRIMARY KEY, "
                "Firstseen INTEGER, "
                "Lastseen INTEGER, "
                "ShortID INTEGER );";

            constexpr auto Clientinfo =
                "CREATE TABLE IF NOT EXISTS Clientinfo ("
                "Publickey TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "ShortID INTEGER REFERENCES Account(ShortID), "

                "Region INTEGER, "
                "GameID INTEGER, "
                "ModID INTEGER, "

                "Clientstate INTEGER, "
                "Username TEXT, "
                "AvatarID INTEGER, "

                "Reputation INTEGER, "
                "Clienttype INTEGER );";

            constexpr auto Clientrelation =
                "CREATE TABLE IF NOT EXISTS Clientrelation ("
                "SourceID TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "TargetID TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Flags INTEGER, "
                "PRIMARY KEY (SourceID, TargetID) );";

            constexpr auto Clientpresence =
                "CREATE TABLE IF NOT EXISTS Clientpresence ("
                "Publickey TEXT PRIMARY KEY REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Category INTEGER, "
                "Keys BLOB, Values BLOB );";

            constexpr auto Clientmessage =
                "SourceID TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "TargetID TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Messagetype INTEGER, "
                "Sent INTEGER, Received INTEGER, "
                "B85Message TEXT );";
        }

        void Createserver()
        {
            constexpr auto Serverheader =
                "CREATE TABLE IF NOT EXISTS Serverheader ("
                "Publickey TEXT PRIMARY KEY REFERENCES Account(Publickey) ON DELETE CASCADE, "

                "Gameflags INTEGER, "
                "Serverflags INTEGER, "
                "Servername TEXT, Mapname TEXT, "
                "Playercount INTEGER, Playerlimit INTEGER, "

                "IPAddress TEXT, Ports INTEGER );";

            constexpr auto Serverdata =
                "CREATE TABLE IF NOT EXISTS Serverdata ("
                "Publickey TEXT PRIMARY KEY REFERENCES Serverheader(Publickey) ON DELETE CASCADE, "

                "Infokeys BLOB, Infovalues BLOB, "
                "Tagkeys BLOB, Tagvalues BLOB );";

            constexpr auto Playerdata =
                "CREATE TABLE IF NOT EXISTS Playerdata ("
                "Serverkey TEXT REFERENCES Serverheader(Publickey) ON DELETE CASCADE, "
                "Clientkey TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Keys BLOB, Values BLOB, "
                "PRIMARY KEY (Serverkey, Clientkey) );";
        }

        void Creategroups()
        {
            constexpr auto Guild =
                "CREATE TABLE IF NOT EXISTS Guild ("
                "Ownerkey TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "GroupID INTEGER PRIMARY KEY, "
                "Friendlyname TEXT, "
                "Grouptag TEXT, "
                "Moderators BLOB );";

            constexpr auto Lobby =
                "CREATE TABLE IF NOT EXISTS Lobby ("
                "Ownerkey TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Serverkey TEXT REFERENCES Serverheader(Publickey) ON DELETE CASCADE, "
                "GroupID INTEGER PRIMARY KEY, "
                "Flags INTEGER, Grouptype INTEGER, Maxmembers INTEGER, "
                "Moderators BLOB );";

            constexpr auto Groupinfo =
                "CREATE TABLE IF NOT EXISTS Groupinfo ("
                "GroupID INTEGER PRIMARY KEY, "
                "Groupkeys BLOB, Groupvalues BLOB, "
                "Memberkeys BLOB, Membervalues BLOB );";

            constexpr auto Groupmember =
                "CREATE TABLE IF NOT EXISTS Groupmember ("
                "Memberkey TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Moderatorkey TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Signature TEXT, "
                "GroupID INTEGER );";
        }

        void Createmessaging()
        {
            constexpr auto Groupmessage =
                "CREATE TABLE IF NOT EXISTS Groupmessage ("
                "SenderID TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "GroupID INTEGER, Messagetype INTEGER, Sent INTEGER,"
                "B85Message TEXT );";

            constexpr auto Clientmessage =
                "CREATE TABLE IF NOT EXISTS Clientmessage ("
                "SenderID TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "TargetID TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
                "Messagetype INTEGER, Sent INTEGER,"
                "B85Message TEXT );";
        }
    }

}

