//
// Created by romain-p on 17/10/2021.
//
#include "ArenaReplayDatabaseConnection.h"
#include "ArenaReplay_loader.h"
#include "ArenaTeamMgr.h"
#include "Base32.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "CharacterDatabase.h"
#include "Chat.h"
#include "Config.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerGossip.h"
#include "PlayerGossipMgr.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "ChatCommand.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

std::vector<Opcodes> watchList =
{
        SMSG_NOTIFICATION,
        SMSG_AURA_UPDATE,
        SMSG_WORLD_STATE_UI_TIMER_UPDATE,
        SMSG_COMPRESSED_UPDATE_OBJECT,
        SMSG_AURA_UPDATE_ALL,
        SMSG_NAME_QUERY_RESPONSE,
        SMSG_DESTROY_OBJECT,
        MSG_MOVE_START_FORWARD,
        MSG_MOVE_SET_FACING,
        MSG_MOVE_HEARTBEAT,
        MSG_MOVE_JUMP,
        SMSG_MONSTER_MOVE,
        MSG_MOVE_FALL_LAND,
        SMSG_PERIODICAURALOG,
        SMSG_ARENA_UNIT_DESTROYED,
        MSG_MOVE_START_STRAFE_RIGHT,
        MSG_MOVE_STOP_STRAFE,
        MSG_MOVE_START_STRAFE_LEFT,
        MSG_MOVE_STOP,
        MSG_MOVE_START_BACKWARD,
        MSG_MOVE_START_TURN_LEFT,
        MSG_MOVE_STOP_TURN,
        MSG_MOVE_START_TURN_RIGHT,
        SMSG_SPELL_START,
        SMSG_SPELL_GO,
        CMSG_CAST_SPELL,
        CMSG_CANCEL_CAST,
        SMSG_CAST_FAILED,
        SMSG_SPELL_START,
        SMSG_SPELL_FAILURE,
        SMSG_SPELL_DELAYED,
        SMSG_PLAY_SPELL_IMPACT,
        SMSG_FORCE_RUN_SPEED_CHANGE,
        SMSG_ATTACKSTART,
        SMSG_POWER_UPDATE,
        SMSG_ATTACKERSTATEUPDATE,
        SMSG_SPELLDAMAGESHIELD,
        SMSG_SPELLHEALLOG,
        SMSG_SPELLENERGIZELOG,
        SMSG_SPELLNONMELEEDAMAGELOG,
        SMSG_ATTACKSTOP,
        SMSG_EMOTE,
        SMSG_AI_REACTION,
        SMSG_PET_NAME_QUERY_RESPONSE,
        SMSG_CANCEL_AUTO_REPEAT,
        SMSG_UPDATE_OBJECT,
        SMSG_FORCE_FLIGHT_SPEED_CHANGE,
        SMSG_GAMEOBJECT_QUERY_RESPONSE,
        SMSG_FORCE_SWIM_SPEED_CHANGE,
        SMSG_GAMEOBJECT_DESPAWN_ANIM,
        SMSG_CANCEL_COMBAT,
        SMSG_DISMOUNTRESULT,
        SMSG_MOUNTRESULT,
        SMSG_DISMOUNT,
        CMSG_MOUNTSPECIAL_ANIM,
        SMSG_MOUNTSPECIAL_ANIM,
        SMSG_MIRRORIMAGE_DATA,
        CMSG_MESSAGECHAT,
        SMSG_MESSAGECHAT
};

/*
CMSG_CANCEL_MOUNT_AURA,
CMSG_ALTER_APPEARANCE
SMSG_SUMMON_CANCEL
SMSG_PLAY_SOUND
SMSG_PLAY_SPELL_VISUAL
CMSG_ATTACKSWING
CMSG_ATTACKSTOP*/

struct PacketRecord { uint32 timestamp; WorldPacket packet; };
struct ActorFrame
{
    uint32 timestamp = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
};
struct ActorTrack
{
    uint64 guid = 0;
    uint8 playerClass = 0;
    uint8 race = 0;
    uint8 gender = 0;
    std::string name;
    std::vector<ActorFrame> frames;
};
struct MatchRecord
{
    BattlegroundTypeId typeId;
    uint8 arenaTypeId;
    uint32 mapId;
    std::deque<PacketRecord> packets;
    std::vector<uint64> winnerPlayerGuidList;
    std::vector<uint64> loserPlayerGuidList;
    std::vector<ActorTrack> winnerActorTracks;
    std::vector<ActorTrack> loserActorTracks;
};
struct BgPlayersGuids { std::string alliancePlayerGuids; std::string hordePlayerGuids; };
struct TeamRecorders { ObjectGuid alliance; ObjectGuid horde; };
struct ActiveReplaySession
{
    uint64 traceId = 0;
    uint32 battlegroundInstanceId = 0;
    uint32 priorBattlegroundInstanceId = 0;
    uint32 replayId = 0;
    uint32 anchorMapId = 0;
    Position anchorPosition;
    uint32 nextAnchorEnforceMs = 0;
    bool movementLocked = false;
    bool viewerWasParticipant = false;
    bool actorSpectateActive = false;
    bool actorSpectateOnWinnerTeam = true;
    uint32 actorTrackIndex = 0;
    uint32 nextActorTeleportMs = 0;
    bool hudStarted = false;
    uint64 lastHudActorGuid = 0;
    uint32 lastHudActorFlatIndex = 0;
    uint32 lastHudActorCount = 0;
    uint32 lastHudWatcherCount = 0;
    std::string lastHudWatcherPayload;
    uint32 nextHudWatcherSyncMs = 0;
    uint32 replayWarmupUntilMs = 0;
    bool replayMovementStabilized = false;
    bool viewerHidden = false;
    bool selfActorViewActive = false;
    bool teardownInProgress = false;
    bool replayComplete = false;
    uint32 replayCompleteAtMs = 0;
    uint32 packetBudgetPerUpdate = 60;
    uint64 lastAppliedActorGuid = 0;
    uint32 lastAppliedActorFlatIndex = 0;
    uint32 lastPlaybackLogMs = 0;
    std::string lastTeardownReason;
    bool teardownRequested = false;
    uint32 teardownRequestedAtMs = 0;
    uint32 teardownExecuteAtMs = 0;
    bool teardownPreferBattlegroundLeave = false;
    bool teardownHudEnded = false;
    bool replayBgEnded = false;
    uint32 cameraStallCount = 0;
    float lastCameraX = 0.0f;
    float lastCameraY = 0.0f;
    float lastCameraZ = 0.0f;
    float lastCameraO = 0.0f;
};
struct LiveActorRecorderState
{
    uint32 nextSampleMs = 0;
    std::unordered_map<uint64, ActorTrack> allianceTracks;
    std::unordered_map<uint64, ActorTrack> hordeTracks;
};
std::unordered_map<uint32, MatchRecord> records;
std::unordered_map<uint64, MatchRecord> loadedReplays;
std::unordered_map<uint32, uint32> bgReplayIds;
std::unordered_map<uint32, BgPlayersGuids> bgPlayersGuids;
std::unordered_map<uint32, TeamRecorders> bgRecorders;
std::unordered_map<uint32, LiveActorRecorderState> liveActorRecorders;
std::unordered_map<uint64, ActiveReplaySession> activeReplaySessions;
uint64 gReplayTraceCounter = 0;

namespace
{
    enum class ReplayDebugFlag
    {
        General,
        Hud,
        Actors,
        Playback,
        Teardown,
        Return
    };

    static bool ReplayDebugEnabled(ReplayDebugFlag flag)
    {
        if (!sConfigMgr->GetOption<bool>("ArenaReplay.Debug.Enable", false))
            return false;

        switch (flag)
        {
            case ReplayDebugFlag::General:
                return true;
            case ReplayDebugFlag::Hud:
                return sConfigMgr->GetOption<bool>("ArenaReplay.Debug.LogHud", true);
            case ReplayDebugFlag::Actors:
                return sConfigMgr->GetOption<bool>("ArenaReplay.Debug.LogActors", true);
            case ReplayDebugFlag::Playback:
                return sConfigMgr->GetOption<bool>("ArenaReplay.Debug.LogPlayback", false);
            case ReplayDebugFlag::Teardown:
                return sConfigMgr->GetOption<bool>("ArenaReplay.Debug.LogTeardown", true);
            case ReplayDebugFlag::Return:
                return sConfigMgr->GetOption<bool>("ArenaReplay.Debug.LogReturn", true);
        }

        return false;
    }

    static bool ReplayDebugVerbose()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.Debug.Verbose", false);
    }

    static uint64 NextReplayTraceId()
    {
        ++gReplayTraceCounter;
        if (gReplayTraceCounter == 0)
            ++gReplayTraceCounter;
        return gReplayTraceCounter;
    }

    static std::string ReplayPlayerTag(Player* player)
    {
        if (!player)
            return "viewer=<null> viewerGuid=0";

        std::ostringstream ss;
        ss << "viewer=" << player->GetName() << " viewerGuid=" << player->GetGUID().GetCounter();
        return ss.str();
    }

    static void ReplayLog(ReplayDebugFlag flag, ActiveReplaySession const* session, Player* player, char const* phase, std::string const& details)
    {
        if (!ReplayDebugEnabled(flag))
            return;

        uint64 traceId = session ? session->traceId : 0;
        uint32 replayId = session ? session->replayId : 0;
        LOG_INFO("server.loading", "[RTG][REPLAY][{}] trace={} replay={} {} {}", phase, traceId, replayId, ReplayPlayerTag(player), details);
    }

    static bool EvaluateReplayHudAllowance(Player* player, std::string* reason = nullptr)
    {
        auto fail = [reason](char const* why) -> bool
        {
            if (reason)
                *reason = why;
            return false;
        };

        if (!player || !player->GetSession())
            return fail("missing_player_or_session");

        auto it = activeReplaySessions.find(player->GetGUID().GetCounter());
        if (it == activeReplaySessions.end())
            return fail("no_active_session");

        ActiveReplaySession const& session = it->second;
        if (session.teardownInProgress)
            return fail("teardown_in_progress");

        Battleground* bg = player->GetBattleground();
        if (!bg)
            return fail("no_battleground");

        if (!bg->isArena())
            return fail("not_arena");

        if (session.battlegroundInstanceId == 0)
            return fail("session_bg_zero");

        if (bg->GetInstanceID() != session.battlegroundInstanceId)
            return fail("session_bg_mismatch");

        auto replayIt = bgReplayIds.find(bg->GetInstanceID());
        if (replayIt == bgReplayIds.end())
            return fail("bg_not_registered_as_replay");

        if (replayIt->second != player->GetGUID().GetCounter())
            return fail("replay_owner_mismatch");

        if (loadedReplays.find(player->GetGUID().GetCounter()) == loadedReplays.end())
            return fail("replay_not_loaded");

        if (reason)
            *reason = "allowed";
        return true;
    }
    static bool IsGuidInBgPlayers(Battleground* bg, ObjectGuid guid)
    {
        if (!bg || !guid)
            return false;

        for (auto const& it : bg->GetPlayers())
            if (it.second && it.second->GetGUID() == guid)
                return true;

        return false;
    }

    static ObjectGuid GetOrAssignRecorderGuid(Battleground* bg, Player* player)
    {
        if (!bg || !player)
            return ObjectGuid();

        auto& rec = bgRecorders[bg->GetInstanceID()];
        TeamId team = player->GetBgTeamId();

        ObjectGuid& slot = (team == TEAM_ALLIANCE) ? rec.alliance : rec.horde;

        // If no recorder yet, assign deterministically (first player we see for that team).
        if (!slot)
            slot = player->GetGUID();

        // If recorder left, re-assign to current player (keeps capture alive even if someone disconnects).
        if (slot && !IsGuidInBgPlayers(bg, slot))
            slot = player->GetGUID();

        return slot;
    }

    static std::string Trim(std::string value)
    {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    static void AppendGuidCsv(std::string& dest, uint64 guid)
    {
        if (!dest.empty())
            dest += ",";

        dest += std::to_string(guid);
    }

    static std::vector<uint64> ParseGuidCsv(std::string const& input)
    {
        std::vector<uint64> guids;
        std::stringstream ss(input);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            token = Trim(token);
            if (token.empty())
                continue;

            try
            {
                guids.push_back(std::stoull(token));
            }
            catch (...)
            {
            }
        }
        return guids;
    }

    static bool VectorContainsGuid(std::vector<uint64> const& guids, uint64 guid)
    {
        return std::find(guids.begin(), guids.end(), guid) != guids.end();
    }

    static std::string EscapeSqlString(std::string value)
    {
        CharacterDatabase.EscapeString(value);
        return value;
    }

    static std::vector<ActorTrack>* SelectTracks(MatchRecord& match, bool winnerSide)
    {
        return winnerSide ? &match.winnerActorTracks : &match.loserActorTracks;
    }

    static std::vector<ActorTrack> const* SelectTracks(MatchRecord const& match, bool winnerSide)
    {
        return winnerSide ? &match.winnerActorTracks : &match.loserActorTracks;
    }

    static std::string SerializeActorTracks(std::vector<ActorTrack> const& tracks)
    {
        std::ostringstream out;
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            ActorTrack const& track = tracks[i];
            if (i)
                out << "||";

            out << track.guid << ';' << uint32(track.playerClass) << ';' << uint32(track.race) << ';' << uint32(track.gender) << ';' << track.name << ';';
            for (size_t j = 0; j < track.frames.size(); ++j)
            {
                ActorFrame const& frame = track.frames[j];
                if (j)
                    out << '|';
                out << frame.timestamp << ',' << frame.x << ',' << frame.y << ',' << frame.z << ',' << frame.o;
            }
        }
        return out.str();
    }

    static std::vector<ActorTrack> DeserializeActorTracks(std::string const& encoded)
    {
        std::vector<ActorTrack> tracks;
        if (encoded.empty())
            return tracks;

        size_t start = 0;
        while (start < encoded.size())
        {
            size_t end = encoded.find("||", start);
            std::string entry = encoded.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = (end == std::string::npos) ? encoded.size() : end + 2;
            if (entry.empty())
                continue;

            std::vector<std::string> parts;
            size_t pstart = 0;
            while (parts.size() < 5)
            {
                size_t pend = entry.find(';', pstart);
                if (pend == std::string::npos)
                    break;
                parts.push_back(entry.substr(pstart, pend - pstart));
                pstart = pend + 1;
            }
            if (parts.size() < 5)
                continue;

            ActorTrack track;
            try
            {
                track.guid = std::stoull(parts[0]);
                track.playerClass = uint8(std::stoul(parts[1]));
                track.race = uint8(std::stoul(parts[2]));
                track.gender = uint8(std::stoul(parts[3]));
            }
            catch (...)
            {
                continue;
            }
            track.name = parts[4];
            std::string framesPart = pstart <= entry.size() ? entry.substr(pstart) : std::string();
            std::stringstream fss(framesPart);
            std::string frameToken;
            while (std::getline(fss, frameToken, '|'))
            {
                if (frameToken.empty())
                    continue;
                std::stringstream tss(frameToken);
                std::string seg;
                std::vector<std::string> vals;
                while (std::getline(tss, seg, ','))
                    vals.push_back(seg);
                if (vals.size() != 5)
                    continue;
                try
                {
                    ActorFrame frame;
                    frame.timestamp = uint32(std::stoul(vals[0]));
                    frame.x = std::stof(vals[1]);
                    frame.y = std::stof(vals[2]);
                    frame.z = std::stof(vals[3]);
                    frame.o = std::stof(vals[4]);
                    track.frames.push_back(frame);
                }
                catch (...)
                {
                }
            }
            if (!track.frames.empty())
                tracks.push_back(std::move(track));
        }
        return tracks;
    }

    static void CaptureActorSnapshot(Battleground* bg)
    {
        if (!bg || !sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.Enable", true))
            return;

        LiveActorRecorderState& state = liveActorRecorders[bg->GetInstanceID()];
        uint32 nowMs = bg->GetStartTime();
        if (state.nextSampleMs > nowMs)
            return;

        state.nextSampleMs = nowMs + sConfigMgr->GetOption<uint32>("ArenaReplay.ActorSpectate.SampleMs", 250);

        for (auto const& pair : bg->GetPlayers())
        {
            Player* actor = pair.second;
            if (!actor || actor->IsSpectator())
                continue;

            std::unordered_map<uint64, ActorTrack>& bucket = actor->GetBgTeamId() == TEAM_ALLIANCE ? state.allianceTracks : state.hordeTracks;
            ActorTrack& track = bucket[actor->GetGUID().GetRawValue()];
            if (track.guid == 0)
            {
                track.guid = actor->GetGUID().GetRawValue();
                track.playerClass = actor->getClass();
                track.race = actor->getRace();
                track.gender = actor->getGender();
                track.name = actor->GetName();
            }

            ActorFrame frame;
            frame.timestamp = nowMs;
            frame.x = actor->GetPositionX();
            frame.y = actor->GetPositionY();
            frame.z = actor->GetPositionZ();
            frame.o = actor->GetOrientation();
            track.frames.push_back(frame);
        }
    }

    static void FinalizeActorSnapshots(Battleground* bg, MatchRecord& match, TeamId winnerTeamId)
    {
        auto it = liveActorRecorders.find(bg->GetInstanceID());
        if (it == liveActorRecorders.end())
            return;

        auto flush = [](std::unordered_map<uint64, ActorTrack> const& src)
        {
            std::vector<ActorTrack> out;
            out.reserve(src.size());
            for (auto const& kv : src)
                if (!kv.second.frames.empty())
                    out.push_back(kv.second);
            std::sort(out.begin(), out.end(), [](ActorTrack const& a, ActorTrack const& b){ return a.guid < b.guid; });
            return out;
        };

        std::vector<ActorTrack> alliance = flush(it->second.allianceTracks);
        std::vector<ActorTrack> horde = flush(it->second.hordeTracks);
        if (winnerTeamId == TEAM_ALLIANCE)
        {
            match.winnerActorTracks = std::move(alliance);
            match.loserActorTracks = std::move(horde);
        }
        else
        {
            match.winnerActorTracks = std::move(horde);
            match.loserActorTracks = std::move(alliance);
        }

        liveActorRecorders.erase(it);
    }

    static char const* GetReplayHudPrefix()
    {
        return "[RTG_REPLAY] ";
    }

    static std::string GetClassToken(uint8 classId)
    {
        switch (classId)
        {
            case CLASS_WARRIOR: return "WARRIOR";
            case CLASS_PALADIN: return "PALADIN";
            case CLASS_HUNTER: return "HUNTER";
            case CLASS_ROGUE: return "ROGUE";
            case CLASS_PRIEST: return "PRIEST";
            case CLASS_DEATH_KNIGHT: return "DEATHKNIGHT";
            case CLASS_SHAMAN: return "SHAMAN";
            case CLASS_MAGE: return "MAGE";
            case CLASS_WARLOCK: return "WARLOCK";
            case CLASS_DRUID: return "DRUID";
            default: return "UNKNOWN";
        }
    }

    static std::string GetClassIconPath(uint8 classId)
    {
        switch (classId)
        {
            case CLASS_WARRIOR: return "Interface\\Icons\\Ability_Warrior_DefensiveStance";
            case CLASS_PALADIN: return "Interface\\Icons\\Spell_Holy_HolyBolt";
            case CLASS_HUNTER: return "Interface\\Icons\\INV_Weapon_Bow_07";
            case CLASS_ROGUE: return "Interface\\Icons\\Ability_BackStab";
            case CLASS_PRIEST: return "Interface\\Icons\\Spell_Holy_PowerWordShield";
            case CLASS_DEATH_KNIGHT: return "Interface\\Icons\\Spell_Deathknight_ClassIcon";
            case CLASS_SHAMAN: return "Interface\\Icons\\Spell_Nature_BloodLust";
            case CLASS_MAGE: return "Interface\\Icons\\Spell_Frost_FrostBolt02";
            case CLASS_WARLOCK: return "Interface\\Icons\\Spell_Shadow_DeathCoil";
            case CLASS_DRUID: return "Interface\\Icons\\Ability_Druid_CatForm";
            default: return "Interface\\Icons\\INV_Misc_QuestionMark";
        }
    }

    static bool IsReplayPacketOpcodeAllowedForPlayback(uint16 opcode)
    {
        switch (opcode)
        {
            case CMSG_CAST_SPELL:
            case CMSG_CANCEL_CAST:
            case CMSG_MESSAGECHAT:
            case CMSG_MOUNTSPECIAL_ANIM:
                return false;
            default:
                return true;
        }
    }

    static bool IsReplayHudAllowed(Player* player)
    {
        return EvaluateReplayHudAllowance(player, nullptr);
    }

    static void SendReplayHudMessage(Player* player, std::string const& body)
    {
        if (!player || !player->GetSession())
            return;

        std::string reason;
        if (body != "END" && !EvaluateReplayHudAllowance(player, &reason))
        {
            if (ReplayDebugEnabled(ReplayDebugFlag::Hud) && ReplayDebugVerbose())
            {
                auto sessionIt = activeReplaySessions.find(player->GetGUID().GetCounter());
                ActiveReplaySession const* session = sessionIt != activeReplaySessions.end() ? &sessionIt->second : nullptr;
                Battleground* bg = player->GetBattleground();
                std::ostringstream ss;
                ss << "msg=" << body << " allowed=0 reason=" << reason
                   << " playerBg=" << (bg ? bg->GetInstanceID() : 0)
                   << " sessionBg=" << (session ? session->battlegroundInstanceId : 0);
                ReplayLog(ReplayDebugFlag::Hud, session, player, "HUD", ss.str());
            }
            return;
        }

        if (ReplayDebugEnabled(ReplayDebugFlag::Hud) && ReplayDebugVerbose())
        {
            auto sessionIt = activeReplaySessions.find(player->GetGUID().GetCounter());
            ActiveReplaySession const* session = sessionIt != activeReplaySessions.end() ? &sessionIt->second : nullptr;
            ReplayLog(ReplayDebugFlag::Hud, session, player, "HUD", std::string("msg=") + body + " allowed=1");
        }

        std::string text = std::string(GetReplayHudPrefix()) + body;
        ChatHandler(player->GetSession()).SendSysMessage(text.c_str());
    }

    struct ReplayActorSelectionRef
    {
        bool winnerSide = true;
        uint32 trackIndex = 0;
    };

    static bool IsReplayActorTrackPlayable(ActorTrack const& track)
    {
        if (track.guid == 0 || track.frames.empty())
            return false;

        uint32 validFrames = 0;
        for (ActorFrame const& frame : track.frames)
        {
            if (!std::isfinite(frame.x) || !std::isfinite(frame.y) || !std::isfinite(frame.z) || !std::isfinite(frame.o))
                continue;

            ++validFrames;
            if (validFrames >= 2)
                return true;
        }

        return validFrames > 0;
    }

    static void SanitizeActorTracks(std::vector<ActorTrack>& tracks)
    {
        for (ActorTrack& track : tracks)
        {
            std::vector<ActorFrame> sanitized;
            sanitized.reserve(track.frames.size());
            uint32 lastTimestamp = 0;
            bool haveLastTimestamp = false;
            for (ActorFrame const& frame : track.frames)
            {
                if (!std::isfinite(frame.x) || !std::isfinite(frame.y) || !std::isfinite(frame.z) || !std::isfinite(frame.o))
                    continue;

                if (haveLastTimestamp && frame.timestamp < lastTimestamp)
                    continue;

                sanitized.push_back(frame);
                lastTimestamp = frame.timestamp;
                haveLastTimestamp = true;
            }

            std::sort(sanitized.begin(), sanitized.end(), [](ActorFrame const& a, ActorFrame const& b)
            {
                return a.timestamp < b.timestamp;
            });
            track.frames.swap(sanitized);
            if (track.name.empty())
                track.name = "Unknown";
        }

        std::unordered_map<uint64, size_t> bestTrackByGuid;
        std::vector<ActorTrack> deduped;
        deduped.reserve(tracks.size());
        for (ActorTrack& track : tracks)
        {
            if (!IsReplayActorTrackPlayable(track))
                continue;

            auto it = bestTrackByGuid.find(track.guid);
            if (it == bestTrackByGuid.end())
            {
                bestTrackByGuid[track.guid] = deduped.size();
                deduped.push_back(std::move(track));
                continue;
            }

            ActorTrack& existing = deduped[it->second];
            if (track.frames.size() > existing.frames.size())
                existing = std::move(track);
        }

        tracks.swap(deduped);
    }

    static std::vector<ReplayActorSelectionRef> BuildPlayableReplayActorSelections(MatchRecord const& match)
    {
        std::vector<ReplayActorSelectionRef> refs;
        std::unordered_set<uint64> seenGuids;

        for (uint32 i = 0; i < match.winnerActorTracks.size(); ++i)
        {
            ActorTrack const& track = match.winnerActorTracks[i];
            if (!IsReplayActorTrackPlayable(track) || seenGuids.find(track.guid) != seenGuids.end())
                continue;
            seenGuids.insert(track.guid);
            refs.push_back({ true, i });
        }

        for (uint32 i = 0; i < match.loserActorTracks.size(); ++i)
        {
            ActorTrack const& track = match.loserActorTracks[i];
            if (!IsReplayActorTrackPlayable(track) || seenGuids.find(track.guid) != seenGuids.end())
                continue;
            seenGuids.insert(track.guid);
            refs.push_back({ false, i });
        }

        return refs;
    }

    static uint32 GetReplayActorTotalCount(MatchRecord const& match)
    {
        return uint32(BuildPlayableReplayActorSelections(match).size());
    }

    static bool NormalizeReplayActorSelection(MatchRecord const& match, ActiveReplaySession& session)
    {
        std::vector<ReplayActorSelectionRef> playable = BuildPlayableReplayActorSelections(match);
        if (playable.empty())
            return false;

        for (ReplayActorSelectionRef const& ref : playable)
        {
            if (ref.winnerSide == session.actorSpectateOnWinnerTeam && ref.trackIndex == session.actorTrackIndex)
                return true;
        }

        session.actorSpectateOnWinnerTeam = playable.front().winnerSide;
        session.actorTrackIndex = playable.front().trackIndex;
        return true;
    }

    static ActorTrack const* GetSelectedReplayActorTrack(MatchRecord const& match, ActiveReplaySession& session, uint32* flatIndex = nullptr)
    {
        std::vector<ReplayActorSelectionRef> playable = BuildPlayableReplayActorSelections(match);
        if (playable.empty())
            return nullptr;

        if (!NormalizeReplayActorSelection(match, session))
            return nullptr;

        for (uint32 i = 0; i < playable.size(); ++i)
        {
            ReplayActorSelectionRef const& ref = playable[i];
            if (ref.winnerSide == session.actorSpectateOnWinnerTeam && ref.trackIndex == session.actorTrackIndex)
            {
                if (flatIndex)
                    *flatIndex = i + 1;

                auto const* tracks = SelectTracks(match, ref.winnerSide);
                return tracks ? &(*tracks)[ref.trackIndex] : nullptr;
            }
        }

        session.actorSpectateOnWinnerTeam = playable.front().winnerSide;
        session.actorTrackIndex = playable.front().trackIndex;
        if (flatIndex)
            *flatIndex = 1;

        auto const* tracks = SelectTracks(match, session.actorSpectateOnWinnerTeam);
        return tracks ? &(*tracks)[session.actorTrackIndex] : nullptr;
    }

    static bool StepReplayActorSelection(MatchRecord const& match, ActiveReplaySession& session, int32 delta)
    {
        std::vector<ReplayActorSelectionRef> playable = BuildPlayableReplayActorSelections(match);
        uint32 total = uint32(playable.size());
        if (total == 0)
            return false;

        uint32 currentFlatIndex = 0;
        bool found = false;
        for (uint32 i = 0; i < playable.size(); ++i)
        {
            ReplayActorSelectionRef const& ref = playable[i];
            if (ref.winnerSide == session.actorSpectateOnWinnerTeam && ref.trackIndex == session.actorTrackIndex)
            {
                currentFlatIndex = i;
                found = true;
                break;
            }
        }

        if (!found)
            currentFlatIndex = 0;

        int32 idx = int32(currentFlatIndex) + delta;
        while (idx < 0)
            idx += int32(total);
        idx %= int32(total);

        ReplayActorSelectionRef const& next = playable[uint32(idx)];
        session.actorSpectateOnWinnerTeam = next.winnerSide;
        session.actorTrackIndex = next.trackIndex;
        session.nextActorTeleportMs = 0;
        return true;
    }

    static ActorFrame const* GetActorFrameAtOrBeforeTime(ActorTrack const& track, uint32 nowMs)
    {
        if (track.frames.empty())
            return nullptr;

        ActorFrame const* frame = &track.frames.front();
        for (ActorFrame const& candidate : track.frames)
        {
            if (candidate.timestamp > nowMs)
                break;
            frame = &candidate;
        }

        return frame;
    }

    static void SendReplayHudPov(Player* replayer, MatchRecord const& match, ActiveReplaySession& session, bool force)
    {
        uint32 flatIndex = 1;
        ActorTrack const* track = GetSelectedReplayActorTrack(match, session, &flatIndex);
        uint32 total = GetReplayActorTotalCount(match);
        if (!track)
            return;

        if (!force && session.hudStarted && session.lastHudActorGuid == track->guid && session.lastHudActorFlatIndex == flatIndex && session.lastHudActorCount == total)
            return;

        std::ostringstream body;
        if (!session.hudStarted)
            body << "START|";
        else
            body << "POV|";

        body << track->guid << '|' << track->name << '|' << GetClassToken(track->playerClass) << '|' << flatIndex << '|' << total;
        SendReplayHudMessage(replayer, body.str());
        if (ReplayDebugEnabled(ReplayDebugFlag::Hud))
        {
            std::ostringstream ss;
            ss << "force=" << (force ? 1 : 0) << " actorGuid=" << track->guid << " actorName=" << track->name
               << " actorClass=" << GetClassToken(track->playerClass) << " actorIndex=" << flatIndex << '/' << total;
            ReplayLog(ReplayDebugFlag::Hud, &session, replayer, "POV", ss.str());
        }
        session.hudStarted = true;
        session.lastHudActorGuid = track->guid;
        session.lastHudActorFlatIndex = flatIndex;
        session.lastHudActorCount = total;
    }

    static void SendReplayHudWatchers(Battleground* bg, Player* replayer, MatchRecord const& /*match*/, ActiveReplaySession& session, bool force)
    {
        if (!bg || !replayer || !IsReplayHudAllowed(replayer))
            return;

        uint32 nowMs = bg->GetStartTime();
        if (!force && session.nextHudWatcherSyncMs > nowMs)
            return;
        session.nextHudWatcherSyncMs = nowMs + 1000;

        std::ostringstream payload;
        std::vector<std::string> entries;
        uint32 count = 0;
        for (auto const& pair : bg->GetPlayers())
        {
            Player* viewer = pair.second;
            if (!viewer || viewer == replayer)
                continue;

            auto viewerSessionIt = activeReplaySessions.find(viewer->GetGUID().GetCounter());
            if (viewerSessionIt == activeReplaySessions.end() || viewerSessionIt->second.teardownInProgress)
                continue;

            ActiveReplaySession const& viewerSession = viewerSessionIt->second;
            if (viewerSession.battlegroundInstanceId != session.battlegroundInstanceId || viewerSession.replayId != session.replayId)
                continue;

            ++count;
            std::ostringstream e;
            e << viewer->GetGUID().GetCounter() << ',' << viewer->GetName() << ',' << GetClassToken(viewer->getClass()) << ',' << GetClassIconPath(viewer->getClass());
            entries.push_back(e.str());
        }

        payload << "WATCHERS|" << count << '|';
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (i)
                payload << ';';
            payload << entries[i];
        }

        std::string payloadText = payload.str();
        if (!force && session.hudStarted && session.lastHudWatcherCount == count && session.lastHudWatcherPayload == payloadText)
            return;

        SendReplayHudMessage(replayer, payloadText);
        if (ReplayDebugEnabled(ReplayDebugFlag::Hud) && (force || ReplayDebugVerbose()))
        {
            std::ostringstream ss;
            ss << "force=" << (force ? 1 : 0) << " watcherCount=" << count;
            ReplayLog(ReplayDebugFlag::Hud, &session, replayer, "WATCHERS", ss.str());
        }
        session.lastHudWatcherCount = count;
        session.lastHudWatcherPayload = payloadText;
    }

    static void SendReplayHudEnd(Player* player)
    {
        SendReplayHudMessage(player, "END");
    }

    static void ReturnReplayViewerToAnchor(Player* player, ActiveReplaySession const& session)
    {
        if (!player || session.anchorMapId == 0)
            return;

        player->TeleportTo(session.anchorMapId,
            session.anchorPosition.GetPositionX(),
            session.anchorPosition.GetPositionY(),
            session.anchorPosition.GetPositionZ(),
            session.anchorPosition.GetOrientation());
    }

    static void ResetActorReplayView(Player* replayer, ActiveReplaySession& session);

    static void RestoreReplayViewerState(Player* player, ActiveReplaySession const& session)
    {
        if (!player)
            return;

        if (session.movementLocked)
            player->SetClientControl(player, true);

        player->StopMoving();
        player->SetCanFly(false);
        player->SetDisableGravity(false);
        player->SetHover(false);
        player->SetClientControl(player, true);
        player->StopMoving();

        if (session.viewerHidden || session.selfActorViewActive || !player->IsVisible())
            player->SetVisible(true);
    }

    static bool ShouldPreferBattlegroundLeave(ActiveReplaySession const& session)
    {
        return session.priorBattlegroundInstanceId != 0 && session.priorBattlegroundInstanceId != session.battlegroundInstanceId;
    }

    static void RequestReplayTeardown(Player* player, Battleground* bg, char const* reason = "unknown", uint32 delayMs = 0)
    {
        if (!player)
            return;

        auto it = activeReplaySessions.find(player->GetGUID().GetCounter());
        if (it == activeReplaySessions.end())
        {
            loadedReplays.erase(player->GetGUID().GetCounter());
            return;
        }

        ActiveReplaySession& session = it->second;
        uint32 nowMs = bg ? bg->GetStartTime() : 0;
        if (!session.teardownRequested)
        {
            session.teardownRequested = true;
            session.teardownRequestedAtMs = nowMs;
            session.teardownExecuteAtMs = nowMs + delayMs;
            session.teardownInProgress = true;
            session.lastTeardownReason = reason ? reason : "unknown";
            session.teardownPreferBattlegroundLeave = ShouldPreferBattlegroundLeave(session);

            if (!session.teardownHudEnded)
            {
                SendReplayHudEnd(player);
                session.teardownHudEnded = true;
            }

            if (ReplayDebugEnabled(ReplayDebugFlag::Teardown))
            {
                std::ostringstream ss;
                ss << "reason=" << session.lastTeardownReason
                   << " delayMs=" << delayMs
                   << " nowMs=" << nowMs
                   << " executeAtMs=" << session.teardownExecuteAtMs
                   << " preferLeave=" << (session.teardownPreferBattlegroundLeave ? 1 : 0)
                   << " playerMap=" << player->GetMapId()
                   << " playerBg=" << player->GetBattlegroundId()
                   << " sessionBg=" << session.battlegroundInstanceId
                   << " replayBgEnded=" << (session.replayBgEnded ? 1 : 0)
                   << " anchorMap=" << session.anchorMapId;
                ReplayLog(ReplayDebugFlag::Teardown, &session, player, "EXIT_REQUEST", ss.str());
            }
        }
        else if (delayMs > 0)
        {
            uint32 wanted = nowMs + delayMs;
            if (session.teardownExecuteAtMs == 0 || wanted < session.teardownExecuteAtMs)
                session.teardownExecuteAtMs = wanted;
        }
    }

    static void PerformReplayTeardown(Player* player, Battleground* bg)
    {
        if (!player)
            return;

        uint64 viewerKey = player->GetGUID().GetCounter();
        auto it = activeReplaySessions.find(viewerKey);
        if (it == activeReplaySessions.end())
        {
            loadedReplays.erase(viewerKey);
            return;
        }

        ActiveReplaySession session = it->second;
        if (!session.teardownRequested)
            RequestReplayTeardown(player, bg, "implicit_teardown", 0);

        if (ReplayDebugEnabled(ReplayDebugFlag::Teardown))
        {
            std::ostringstream ss;
            ss << "reason=" << session.lastTeardownReason
               << " requestedAtMs=" << session.teardownRequestedAtMs
               << " executeAtMs=" << session.teardownExecuteAtMs
               << " playerMap=" << player->GetMapId()
               << " playerBg=" << player->GetBattlegroundId()
               << " sessionBg=" << session.battlegroundInstanceId
               << " replayBgEnded=" << (session.replayBgEnded ? 1 : 0);
            ReplayLog(ReplayDebugFlag::Teardown, &session, player, "EXIT_BEGIN", ss.str());
        }

        ResetActorReplayView(player, session);
        RestoreReplayViewerState(player, session);
        loadedReplays.erase(viewerKey);

        std::string action = "return_to_anchor";
        bool canLeaveBattleground = session.teardownPreferBattlegroundLeave && bg && !session.replayBgEnded && player->GetBattlegroundId() == bg->GetInstanceID();
        if (canLeaveBattleground)
        {
            action = "leave_battleground";
            player->LeaveBattleground(bg);
        }
        else if (session.anchorMapId != 0)
            ReturnReplayViewerToAnchor(player, session);
        else
            action = "restore_only";

        RestoreReplayViewerState(player, session);

        if (ReplayDebugEnabled(ReplayDebugFlag::Teardown))
        {
            std::ostringstream ss;
            ss << "reason=" << session.lastTeardownReason
               << " action=" << action
               << " map=" << player->GetMapId()
               << " playerBg=" << player->GetBattlegroundId();
            ReplayLog(ReplayDebugFlag::Teardown, &session, player, "EXIT_PATH", ss.str());
        }

        if (ReplayDebugEnabled(ReplayDebugFlag::Return))
        {
            std::ostringstream ss;
            ss << "reason=" << session.lastTeardownReason
               << " map=" << player->GetMapId()
               << " x=" << player->GetPositionX()
               << " y=" << player->GetPositionY()
               << " z=" << player->GetPositionZ()
               << " o=" << player->GetOrientation()
               << " inBg=" << (player->GetBattleground() ? 1 : 0)
               << " hidden=" << (player->IsVisible() ? 0 : 1)
               << " canFly=" << (player->CanFly() ? 1 : 0);
            ReplayLog(ReplayDebugFlag::Return, &session, player, "RETURN_STATE", ss.str());
        }

        activeReplaySessions.erase(viewerKey);
    }

	static void ReleaseReplayViewerControl(Player* player)
    {
        if (!player)
            return;

        auto it = activeReplaySessions.find(player->GetGUID().GetCounter());
        if (it != activeReplaySessions.end())
            RestoreReplayViewerState(player, it->second);
        else
        {
            player->StopMoving();
            player->SetCanFly(false);
            player->SetDisableGravity(false);
            player->SetHover(false);
            player->SetClientControl(player, true);
            player->SetVisible(true);
        }

        activeReplaySessions.erase(player->GetGUID().GetCounter());
    }

    static void ExitReplayAndReturnToAnchor(Player* player, Battleground* bg, char const* reason = "unknown")
    {
        RequestReplayTeardown(player, bg, reason, 0);
        PerformReplayTeardown(player, bg);
    }

    static void ResetActorReplayView(Player* replayer, ActiveReplaySession& session)
    {
        if (!replayer)
            return;

        session.actorSpectateActive = false;
        session.selfActorViewActive = false;
        session.nextActorTeleportMs = 0;
    }

    static ActorFrame GetInterpolatedActorFrame(ActorTrack const& track, uint32 nowMs, bool& ok)
    {
        ok = false;
        ActorFrame out;

        if (track.frames.empty())
            return out;

        if (nowMs <= track.frames.front().timestamp)
        {
            ok = true;
            return track.frames.front();
        }

        for (size_t i = 1; i < track.frames.size(); ++i)
        {
            ActorFrame const& prev = track.frames[i - 1];
            ActorFrame const& next = track.frames[i];
            if (nowMs > next.timestamp)
                continue;

            out = prev;
            uint32 span = next.timestamp - prev.timestamp;
            if (span > 0)
            {
                float t = float(nowMs - prev.timestamp) / float(span);
                out.x = prev.x + ((next.x - prev.x) * t);
                out.y = prev.y + ((next.y - prev.y) * t);
                out.z = prev.z + ((next.z - prev.z) * t);
                out.o = next.o;
            }

            ok = true;
            return out;
        }

        ok = true;
        return track.frames.back();
    }

    static bool ApplyActorReplayView(Player* replayer, MatchRecord& match, ActiveReplaySession& session, uint32 nowMs, bool forceImmediate = false)
    {
        if (!replayer || !sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.Enable", true) || session.teardownInProgress)
            return false;

        if (!forceImmediate && session.replayWarmupUntilMs > nowMs)
            return false;

        uint32 flatIndex = 1;
        ActorTrack const* track = GetSelectedReplayActorTrack(match, session, &flatIndex);
        if (!track)
        {
            ResetActorReplayView(replayer, session);
            return false;
        }

        if (track->frames.empty())
        {
            ResetActorReplayView(replayer, session);
            return false;
        }

        if (!session.replayMovementStabilized)
        {
            replayer->SetCanFly(true);
            replayer->SetDisableGravity(true);
            replayer->SetHover(true);
            session.replayMovementStabilized = true;
        }

        bool selfActorTrack = session.viewerWasParticipant && track->guid == replayer->GetGUID().GetRawValue();
        if (selfActorTrack)
        {
            if (session.viewerHidden || !replayer->IsVisible())
            {
                replayer->SetVisible(true);
                session.viewerHidden = false;
            }
            session.selfActorViewActive = true;
        }
        else
        {
            if (!session.viewerHidden || replayer->IsVisible())
            {
                replayer->SetVisible(false);
                session.viewerHidden = true;
            }
            session.selfActorViewActive = false;
        }

        bool actorChanged = (session.lastAppliedActorGuid != track->guid || session.lastAppliedActorFlatIndex != flatIndex);
        if (actorChanged)
            forceImmediate = true;

        if (!forceImmediate && session.nextActorTeleportMs > nowMs)
            return true;
        session.nextActorTeleportMs = nowMs + sConfigMgr->GetOption<uint32>("ArenaReplay.ActorSpectate.TeleportMs", 125);

        bool haveFrame = false;
        ActorFrame frame = GetInterpolatedActorFrame(*track, nowMs, haveFrame);
        if (!haveFrame)
        {
            ResetActorReplayView(replayer, session);
            return false;
        }

        float followDistance = std::max(0.0f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.FollowDistance", 2.25f));
        float followHeight = sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.FollowHeight", 1.75f);
        float snapDistance = std::max(0.5f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.SnapDistance", 1.10f));
        float orientationLerpPct = std::min(1.0f, std::max(0.05f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.OrientationLerpPct", 0.45f)));

        float targetX = frame.x - std::cos(frame.o) * followDistance;
        float targetY = frame.y - std::sin(frame.o) * followDistance;
        float targetZ = frame.z + followHeight;
        float dx = replayer->GetPositionX() - targetX;
        float dy = replayer->GetPositionY() - targetY;
        float dz = replayer->GetPositionZ() - targetZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        float targetO = frame.o;
        bool stalled = (distSq < 0.01f && std::abs(session.lastCameraX - targetX) < 0.01f && std::abs(session.lastCameraY - targetY) < 0.01f && std::abs(session.lastCameraZ - targetZ) < 0.01f);
        if (stalled)
            ++session.cameraStallCount;
        else
            session.cameraStallCount = 0;

        if (forceImmediate || distSq > (snapDistance * snapDistance) || session.cameraStallCount >= 8)
            replayer->NearTeleportTo(targetX, targetY, targetZ, targetO);
        else
        {
            float currentO = replayer->GetOrientation();
            float diffO = targetO - currentO;
            while (diffO > float(M_PI)) diffO -= float(2.0 * M_PI);
            while (diffO < float(-M_PI)) diffO += float(2.0 * M_PI);
            if (std::abs(diffO) > 0.02f)
                replayer->SetFacingTo(currentO + diffO * orientationLerpPct);
        }

        session.lastCameraX = targetX;
        session.lastCameraY = targetY;
        session.lastCameraZ = targetZ;
        session.lastCameraO = targetO;
        session.actorSpectateActive = true;
        session.lastAppliedActorGuid = track->guid;
        session.lastAppliedActorFlatIndex = flatIndex;
        if (ReplayDebugEnabled(ReplayDebugFlag::Actors) && (forceImmediate || ReplayDebugVerbose()))
        {
            std::ostringstream ss;
            ss << "force=" << (forceImmediate ? 1 : 0)
               << " actorGuid=" << track->guid
               << " actorName=" << track->name
               << " actorIndex=" << flatIndex << '/' << GetReplayActorTotalCount(match)
               << " selfActorView=" << (selfActorTrack ? 1 : 0)
               << " x=" << targetX << " y=" << targetY << " z=" << targetZ << " o=" << frame.o;
            ReplayLog(ReplayDebugFlag::Actors, &session, replayer, "APPLY", ss.str());
        }
        return true;
    }

    static void LockReplayViewerControl(Player* player, uint32 replayId, uint32 replayBgInstanceId)
    {
        if (!player)
            return;

        ActiveReplaySession& session = activeReplaySessions[player->GetGUID().GetCounter()];
        session.traceId = NextReplayTraceId();
        session.priorBattlegroundInstanceId = player->GetBattlegroundId();
        session.battlegroundInstanceId = replayBgInstanceId;
        session.replayId = replayId;
        session.anchorMapId = player->GetMapId();
        session.anchorPosition.Relocate(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        session.nextAnchorEnforceMs = 0;
        session.nextActorTeleportMs = 0;
        session.nextHudWatcherSyncMs = 0;
        session.hudStarted = false;
        session.lastHudActorGuid = 0;
        session.lastHudActorFlatIndex = 0;
        session.lastHudActorCount = 0;
        session.lastHudWatcherCount = 0;
        session.lastHudWatcherPayload.clear();
        session.replayWarmupUntilMs = 1500;
        session.replayMovementStabilized = false;
        session.viewerHidden = false;
        session.selfActorViewActive = false;
        session.teardownInProgress = false;
        session.replayComplete = false;
        session.replayCompleteAtMs = 0;
        session.packetBudgetPerUpdate = std::max<uint32>(15u, sConfigMgr->GetOption<uint32>("ArenaReplay.Playback.PacketBudgetPerUpdate", 40));
        session.lastAppliedActorGuid = 0;
        session.lastAppliedActorFlatIndex = 0;
        session.lastPlaybackLogMs = 0;
        session.lastTeardownReason.clear();
        session.teardownRequested = false;
        session.teardownRequestedAtMs = 0;
        session.teardownExecuteAtMs = 0;
        session.teardownPreferBattlegroundLeave = ShouldPreferBattlegroundLeave(session);
        session.teardownHudEnded = false;
        session.replayBgEnded = false;
        session.cameraStallCount = 0;
        session.lastCameraX = player->GetPositionX();
        session.lastCameraY = player->GetPositionY();
        session.lastCameraZ = player->GetPositionZ();
        session.lastCameraO = player->GetOrientation();

        if (ReplayDebugEnabled(ReplayDebugFlag::General))
        {
            std::ostringstream ss;
            ss << "priorBg=" << session.priorBattlegroundInstanceId
               << " replayBg=" << session.battlegroundInstanceId
               << " anchorMap=" << session.anchorMapId
               << " anchorX=" << session.anchorPosition.GetPositionX()
               << " anchorY=" << session.anchorPosition.GetPositionY()
               << " anchorZ=" << session.anchorPosition.GetPositionZ();
            ReplayLog(ReplayDebugFlag::General, &session, player, "LOCK", ss.str());
        }

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.LockMovement", true))
        {
            player->SetClientControl(player, false);
            session.movementLocked = true;
        }

        player->SetVisible(false);
        session.viewerHidden = true;
    }
}

class ArenaReplayServerScript : public ServerScript
{
public:
    ArenaReplayServerScript() : ServerScript("ArenaReplayServerScript", {
        SERVERHOOK_CAN_PACKET_SEND
    }) {}

    bool CanPacketSend(WorldSession* session, WorldPacket& packet) override
    {
        if (session == nullptr || session->GetPlayer() == nullptr)
            return true;

        Battleground* bg = session->GetPlayer()->GetBattleground();

        if (!bg)
            return true;

        const bool isReplay = bgReplayIds.find(bg->GetInstanceID()) != bgReplayIds.end();

        // ignore packet when no bg or casual games
        if (isReplay)
          return true;

        // ignore packets until arena started
        if (bg->GetStatus() != BattlegroundStatus::STATUS_IN_PROGRESS)
            return true;

        // Deterministic recorder selection:
        // Choose exactly one "recorder" per team per BG instance and keep it stable.
        // If the recorder leaves, we re-assign to the next player we observe from that team.
        ObjectGuid recorder = GetOrAssignRecorderGuid(bg, session->GetPlayer());
        if (recorder && session->GetPlayer()->GetGUID() != recorder)
            return true;

        // ignore packets not in watch list
        if (std::find(watchList.begin(), watchList.end(), packet.GetOpcode()) == watchList.end())
            return true;

        if (records.find(bg->GetInstanceID()) == records.end())
            records[bg->GetInstanceID()].packets.clear();
        MatchRecord& record = records[bg->GetInstanceID()];

        uint32 timestamp = bg->GetStartTime();
        record.typeId = bg->GetBgTypeID();
        record.arenaTypeId = bg->GetArenaType();
        record.mapId = bg->GetMapId();
        // push back packet inside queue of matchId 0
        record.packets.push_back({ timestamp, /* copy */ WorldPacket(packet) });
        return true;
    }
};

class ArenaReplayArenaScript : public ArenaScript {
public:
  ArenaReplayArenaScript() : ArenaScript("ArenaReplayArenaScript", {
      ARENAHOOK_ON_BEFORE_CHECK_WIN_CONDITION
  }) {}

  bool OnBeforeArenaCheckWinConditions(Battleground *const bg) override {
    const bool isReplay = bgReplayIds.find(bg->GetInstanceID()) != bgReplayIds.end();

    // if isReplay then return false to exit from check condition
    return !isReplay;
  }
};

class ArenaReplayBGScript : public BGScript
{
public:
    ArenaReplayBGScript() : BGScript("ArenaReplayBGScript", {
        ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_UPDATE,
        ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_ADD_PLAYER,
        ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_END
    }) {}

    void OnBattlegroundUpdate(Battleground* bg, uint32 /* diff */) override
    {
        const bool isReplay = bgReplayIds.find(bg->GetInstanceID()) != bgReplayIds.end();
        if (!isReplay)
        {
            if (bg && bg->GetStatus() == BattlegroundStatus::STATUS_IN_PROGRESS)
                CaptureActorSnapshot(bg);
            return;
        }

        if (!bg->isArena() && !sConfigMgr->GetOption<bool>("ArenaReplay.SaveBattlegrounds", true))
            return;

        if (!bg->isRated() && !sConfigMgr->GetOption<bool>("ArenaReplay.SaveUnratedArenas", true))
            return;

        uint64 replayOwnerKey = bgReplayIds.at(bg->GetInstanceID());

        int32 startDelayTime = bg->GetStartDelayTime();
        if (startDelayTime > 1000) // reduces StartTime only when watching Replay
        {
            bg->SetStartDelayTime(1000);
            bg->SetStartTime(bg->GetStartTime() + (startDelayTime - 1000));
        }

        if (bg->GetStatus() != BattlegroundStatus::STATUS_IN_PROGRESS)
            return;

        // retrieve arena replay data
        auto it = loadedReplays.find(replayOwnerKey);
        if (it == loadedReplays.end())
            return;

        MatchRecord& match = it->second;

        if (bg->GetPlayers().empty())
        {
            loadedReplays.erase(it);
            return;
        }

        Player* replayer = bg->GetPlayers().begin()->second;
        auto sessionIt = activeReplaySessions.find(replayer->GetGUID().GetCounter());
        if (sessionIt == activeReplaySessions.end())
            return;

        ActiveReplaySession& session = sessionIt->second;

        // send replay data to spectator first so actors exist client-side before camera positioning,
        // but cap burst size to avoid hitching and client overload on older replays.
        uint32 packetsSentThisUpdate = 0;
        uint32 packetBudgetPerUpdate = std::max<uint32>(15u, session.packetBudgetPerUpdate);
        while (!match.packets.empty() && match.packets.front().timestamp <= bg->GetStartTime() && packetsSentThisUpdate < packetBudgetPerUpdate)
        {
            WorldPacket* myPacket = &match.packets.front().packet;
            if (IsReplayPacketOpcodeAllowedForPlayback(myPacket->GetOpcode()))
            {
                replayer->GetSession()->SendPacket(myPacket);
                ++packetsSentThisUpdate;
            }
            match.packets.pop_front();
        }

        if (match.packets.empty())
        {
            if (!session.replayComplete)
            {
                session.replayComplete = true;
                session.replayCompleteAtMs = bg->GetStartTime() + 1500;
                RequestReplayTeardown(replayer, bg, "packet_stream_complete", 1500);
            }
        }
        else
        {
            session.replayComplete = false;
            session.replayCompleteAtMs = 0;
        }

        if (sessionIt != activeReplaySessions.end())
        {
            if (session.replayWarmupUntilMs == 1500)
                session.replayWarmupUntilMs = bg->GetStartTime() + 1000;
            if (ReplayDebugEnabled(ReplayDebugFlag::Playback) && (session.lastPlaybackLogMs == 0 || (bg->GetStartTime() >= session.lastPlaybackLogMs + 1000) || session.replayComplete))
            {
                session.lastPlaybackLogMs = bg->GetStartTime();
                std::ostringstream ss;
                ss << "remainingPackets=" << match.packets.size()
                   << " packetsSent=" << packetsSentThisUpdate
                   << " replayTimeMs=" << bg->GetStartTime()
                   << " replayComplete=" << (session.replayComplete ? 1 : 0)
                   << " actorGuid=" << session.lastAppliedActorGuid
                   << " actorIndex=" << session.lastAppliedActorFlatIndex;
                ReplayLog(ReplayDebugFlag::Playback, &session, replayer, "PLAYBACK", ss.str());
            }
            if (session.teardownRequested)
            {
                if (bg->GetStartTime() >= session.teardownExecuteAtMs)
                {
                    PerformReplayTeardown(replayer, bg);
                    return;
                }

                if (ReplayDebugEnabled(ReplayDebugFlag::Teardown) && ReplayDebugVerbose())
                {
                    std::ostringstream ss;
                    ss << "reason=" << session.lastTeardownReason
                       << " nowMs=" << bg->GetStartTime()
                       << " executeAtMs=" << session.teardownExecuteAtMs;
                    ReplayLog(ReplayDebugFlag::Teardown, &session, replayer, "EXIT_PENDING", ss.str());
                }
                return;
            }
            if (session.teardownInProgress)
                return;
            bool actorViewApplied = ApplyActorReplayView(replayer, match, session, bg->GetStartTime());
            SendReplayHudPov(replayer, match, session, false);
            SendReplayHudWatchers(bg, replayer, match, session, false);
            if (!actorViewApplied && session.nextAnchorEnforceMs <= bg->GetStartTime())
            {
                session.nextAnchorEnforceMs = bg->GetStartTime() + 500;

                if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.AnchorViewer", true) && replayer->GetMapId() == bg->GetMapId())
                {
                    if (!session.replayMovementStabilized)
                    {
                        replayer->SetCanFly(true);
                        replayer->SetDisableGravity(true);
                        replayer->SetHover(true);
                        session.replayMovementStabilized = true;
                    }

                    float anchorX = replayer->GetPositionX();
                    float anchorY = replayer->GetPositionY();
                    float anchorZ = replayer->GetPositionZ();
                    float anchorO = replayer->GetOrientation();

                    if (ActorTrack const* track = GetSelectedReplayActorTrack(match, session))
                    {
                        bool haveFrame = false;
                        ActorFrame frame = GetInterpolatedActorFrame(*track, bg->GetStartTime(), haveFrame);
                        if (haveFrame)
                        {
                            float followDistance = std::max(0.0f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.FollowDistance", 2.25f));
                            float followHeight = sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.FollowHeight", 1.75f);
                            anchorX = frame.x - std::cos(frame.o) * followDistance;
                            anchorY = frame.y - std::sin(frame.o) * followDistance;
                            anchorZ = frame.z + followHeight;
                            anchorO = frame.o;
                        }
                    }

                    float dx = replayer->GetPositionX() - anchorX;
                    float dy = replayer->GetPositionY() - anchorY;
                    float dz = replayer->GetPositionZ() - anchorZ;
                    float distSq = dx * dx + dy * dy + dz * dz;
                    if (distSq > 4.0f)
                        replayer->NearTeleportTo(anchorX, anchorY, anchorZ, anchorO);
                }
            }
        }
    }

    void OnBattlegroundAddPlayer(Battleground* bg, Player* player) override
    {
        if (!player)
            return;

        if (bg && bgReplayIds.find(bg->GetInstanceID()) != bgReplayIds.end())
            return;

        if (player->IsSpectator())
            return;

        // Ensure recorder slots are assigned deterministically as players join.
        // (Recording still starts only once the arena is in progress.)
        GetOrAssignRecorderGuid(bg, player);

        if (!bg->isArena() && !sConfigMgr->GetOption<bool>("ArenaReplay.SaveBattlegrounds", true))
            return;

        if (!bg->isRated() && !sConfigMgr->GetOption<bool>("ArenaReplay.SaveUnratedArenas", true))
            return;

        if (bgPlayersGuids.find(bg->GetInstanceID()) == bgPlayersGuids.end())
        {
            BgPlayersGuids playerguids;
            bgPlayersGuids[bg->GetInstanceID()] = playerguids;
        }

        TeamId bgTeamId = player->GetBgTeamId();

        if (bgTeamId == TEAM_ALLIANCE)
            AppendGuidCsv(bgPlayersGuids[bg->GetInstanceID()].alliancePlayerGuids, player->GetGUID().GetRawValue());
        else
            AppendGuidCsv(bgPlayersGuids[bg->GetInstanceID()].hordePlayerGuids, player->GetGUID().GetRawValue());
    }

    void OnBattlegroundEnd(Battleground *bg, TeamId winnerTeamId ) override {

        if (!bg->isArena() && !sConfigMgr->GetOption<bool>("ArenaReplay.SaveBattlegrounds", true))
            return;

        if (!bg->isRated() && !sConfigMgr->GetOption<bool>("ArenaReplay.SaveUnratedArenas", true))
            return;

        const bool isReplay = bgReplayIds.find(bg->GetInstanceID()) != bgReplayIds.end();

        // only saves if arena lasted at least X secs (StartDelayTime is included - 60s StartDelayTime + X StartTime)
        uint32 ValidArenaDuration = sConfigMgr->GetOption<uint32>("ArenaReplay.ValidArenaDuration", 75) * IN_MILLISECONDS;
        bool ValidArena = (bg->GetStartTime()) >= ValidArenaDuration || sConfigMgr->GetOption<uint32>("ArenaReplay.ValidArenaDuration", 75) == 0;

        // save replay when a bg ends
        if (!isReplay && ValidArena)
        {
           saveReplay(bg, winnerTeamId);
           return;
        }

        if (isReplay && !bg->GetPlayers().empty())
        {
            Player* viewer = bg->GetPlayers().begin()->second;
            auto sessionIt = viewer ? activeReplaySessions.find(viewer->GetGUID().GetCounter()) : activeReplaySessions.end();
            if (viewer && sessionIt != activeReplaySessions.end())
            {
                sessionIt->second.replayBgEnded = true;
                RequestReplayTeardown(viewer, bg, "battleground_end", 0);
                PerformReplayTeardown(viewer, bg);
            }
        }

        bgReplayIds.erase(bg->GetInstanceID());
        bgPlayersGuids.erase(bg->GetInstanceID());
        bgRecorders.erase(bg->GetInstanceID());
        liveActorRecorders.erase(bg->GetInstanceID());
    }

    void saveReplay(Battleground* bg, TeamId winnerTeamId)
    {
        // retrieve replay data
        auto it = records.find(bg->GetInstanceID());
        if (it == records.end())
            return;

        MatchRecord& match = it->second;
        FinalizeActorSnapshots(bg, match, winnerTeamId);

        /** serialize arena replay data **/
        ArenaReplayByteBuffer buffer;
        uint32 headerSize;
        uint32 timestamp;
        for (auto it : match.packets)
        {
            headerSize = it.packet.size(); //header 4Bytes packet size
            timestamp = it.timestamp;

            buffer << headerSize; // 4 bytes
            buffer << timestamp; // 4 bytes
            buffer << it.packet.GetOpcode(); // 2 bytes
            if (headerSize > 0)
                buffer.append(it.packet.contents(), it.packet.size()); // headerSize bytes
        }

        uint32 teamWinnerRating = 0;
        uint32 teamLoserRating = 0;
        uint32 teamWinnerMMR = 0;
        uint32 teamLoserMMR = 0;
        std::string teamWinnerName;
        std::string teamLoserName;
        std::string winnerGuids;
        std::string loserGuids;

        if (winnerTeamId == TEAM_ALLIANCE)
        {
            winnerGuids = bgPlayersGuids[bg->GetInstanceID()].alliancePlayerGuids;
            loserGuids = bgPlayersGuids[bg->GetInstanceID()].hordePlayerGuids;
        }
        else
        {
            loserGuids = bgPlayersGuids[bg->GetInstanceID()].alliancePlayerGuids;
            winnerGuids = bgPlayersGuids[bg->GetInstanceID()].hordePlayerGuids;
        }

        std::vector<Player*> notifyPlayers;
        for (const auto& playerPair : bg->GetPlayers())
        {
            Player* player = playerPair.second;
            if (!player || player->IsSpectator())
                continue;

            notifyPlayers.push_back(player);
            TeamId bgTeamId = player->GetBgTeamId();
            ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(bg->GetArenaTeamIdForTeam(bgTeamId));
            uint32 arenaTeamId = bg->GetArenaTeamIdForTeam(bgTeamId);
            TeamId teamId = static_cast<TeamId>(arenaTeamId);
            uint32 teamMMR = bg->GetArenaMatchmakerRating(teamId);

            if (bgTeamId == winnerTeamId)
            {
                getTeamInformation(bg, team, teamWinnerName, teamWinnerRating);
                teamWinnerMMR = teamMMR;
            }
            else
            {
                getTeamInformation(bg, team, teamLoserName, teamLoserRating);
                teamLoserMMR = teamMMR;
            }
        }

        const uint8 ARENA_TYPE_3V3_SOLO_QUEUE = sConfigMgr->GetOption<uint8>("ArenaReplay.3v3soloQ.ArenaType", 4);
        if (bg->isArena() && (!bg->isRated() || bg->GetArenaType() == ARENA_TYPE_3V3_SOLO_QUEUE))
        {
            teamWinnerName = GetTeamName(winnerGuids);
            teamLoserName = GetTeamName(loserGuids);
        }
        else if (!bg->isArena())
        {
            teamWinnerName = "Battleground";
            teamLoserName = "Battleground";
        }

        // // if loser has a negative value. the uint variable could return this (wrong) value
        // if (teamLoserMMR >= 4294967286)
        //     teamLoserMMR=0;

        // if (teamWinnerMMR >= 4294967286)
        //     teamWinnerMMR=0;

        // temporary code until the issue is not properly fixed
        teamLoserMMR=0;
        teamWinnerMMR=0;

        match.winnerPlayerGuidList = ParseGuidCsv(winnerGuids);
        match.loserPlayerGuidList = ParseGuidCsv(loserGuids);

        std::string encodedContents = Acore::Encoding::Base32::Encode(buffer.contentsAsVector());
        std::string encodedWinnerTracks = SerializeActorTracks(match.winnerActorTracks);
        std::string encodedLoserTracks = SerializeActorTracks(match.loserActorTracks);

        CharacterDatabase.Execute("INSERT INTO `character_arena_replays` "
            "(`arenaTypeId`, `typeId`, `contentSize`, `contents`, `mapId`, `winnerTeamName`, `winnerTeamRating`, `winnerTeamMMR`, "
            "`loserTeamName`, `loserTeamRating`, `loserTeamMMR`, `winnerPlayerGuids`, `loserPlayerGuids`, `winnerActorTrack`, `loserActorTrack`) "
            "VALUES ({}, {}, {}, '{}', {}, '{}', {}, {}, '{}', {}, {}, '{}', '{}', '{}', '{}')",
            uint32(match.arenaTypeId),
            uint32(match.typeId),
            buffer.size(),
            EscapeSqlString(encodedContents),
            bg->GetMapId(),
            EscapeSqlString(teamWinnerName),
            teamWinnerRating,
            teamWinnerMMR,
            EscapeSqlString(teamLoserName),
            teamLoserRating,
            teamLoserMMR,
            EscapeSqlString(winnerGuids),
            EscapeSqlString(loserGuids),
            EscapeSqlString(encodedWinnerTracks),
            EscapeSqlString(encodedLoserTracks)
        );

        QueryResult insertedIdResult = CharacterDatabase.Query("SELECT LAST_INSERT_ID()");
        uint32 insertedReplayId = 0;
        if (insertedIdResult)
            insertedReplayId = insertedIdResult->Fetch()[0].Get<uint32>();

        for (Player* notifyPlayer : notifyPlayers)
            if (notifyPlayer)
                ChatHandler(notifyPlayer->GetSession()).PSendSysMessage("Replay saved. Match ID: {}", insertedReplayId);

        records.erase(it);
    }

private:
    void getTeamInformation(Battleground *bg, ArenaTeam* team, std::string &teamName, uint32 &teamRating) {
        if (bg->isRated() && team)
        {
            if (team->GetId() < 0xFFF00000)
            {
                teamName = team->GetName();
                teamRating = team->GetRating();
            }
        }
    }

    std::string GetTeamName(std::string listPlayerGuids) {
        std::string teamName;
        for (uint64 _guid : ParseGuidCsv(listPlayerGuids))
        {
            CharacterCacheEntry const* playerData = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid(_guid));
            if (playerData)
                teamName += playerData->Name + " ";
        }

        // truncate last character if space
        if (!teamName.empty() && teamName.substr(teamName.size()-1, teamName.size()) == " ") {
            teamName.pop_back();
        }

        return teamName;
    }
};


static constexpr uint32 REPLAY_GOSSIP_SENDER_SELECT = GOSSIP_SENDER_MAIN;
static constexpr uint32 REPLAY_GOSSIP_SENDER_CODE   = 1001;

enum ReplayGossips
{
    REPLAY_LATEST_2V2 = 1,
    REPLAY_LATEST_3V3 = 2,
    REPLAY_LATEST_5V5 = 3,
    REPLAY_LATEST_3V3SOLO = 4,
    REPLAY_LATEST_1V1 = 5,
    REPLAY_MATCH_ID = 6,
    REPLAY_LIST_BY_PLAYERNAME = 7,
    MY_FAVORITE_MATCHES = 8,
    REPLAY_TOP_2V2_ALLTIME = 9,
    REPLAY_TOP_3V3_ALLTIME = 10,
    REPLAY_TOP_5V5_ALLTIME = 11,
    REPLAY_TOP_3V3SOLO_ALLTIME = 12,
    REPLAY_TOP_1V1_ALLTIME = 13,
    REPLAY_MOST_WATCHED_ALLTIME = 14,
    REPLAY_MY_RECENT_MATCHES = 15,
    REPLAY_RECENTLY_WATCHED = 16
};

class ReplayGossip : public CreatureScript
{
public:

    ReplayGossip() : CreatureScript("ReplayGossip") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sConfigMgr->GetOption<bool>("ArenaReplay.Enable", true))
        {
            ChatHandler(player->GetSession()).SendSysMessage("Arena Replay disabled!");
            return true;
        }

        const bool isArena1v1Enabled = sConfigMgr->GetOption<bool>("ArenaReplay.1v1.Enable", false);
        const bool isArena3v3soloQEnabled = sConfigMgr->GetOption<bool>("ArenaReplay.3v3soloQ.Enable", false);

        if (isArena1v1Enabled)
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 1v1 games of the last 30 days", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_LATEST_1V1);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 2v2 games of the last 30 days", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_LATEST_2V2);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 3v3 games of the last 30 days", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_LATEST_3V3);

        if (isArena3v3soloQEnabled)
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 3v3 Solo games of the last 30 days", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_LATEST_3V3SOLO);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 5v5 games of the last 30 days", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_LATEST_5V5);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Replay a Match ID", REPLAY_GOSSIP_SENDER_CODE, REPLAY_MATCH_ID, "", 0, true);             // maybe add command .replay 'replayID' aswell
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Replay list by player name", REPLAY_GOSSIP_SENDER_CODE, REPLAY_LIST_BY_PLAYERNAME, "", 0, true); // to do: show a list, showing games with type, teamname and teamrating
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, "My recent matches", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_MY_RECENT_MATCHES);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, "Recently watched", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_RECENTLY_WATCHED);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, "My favorite matches", REPLAY_GOSSIP_SENDER_SELECT, MY_FAVORITE_MATCHES);                   // To do: somehow show teamName/TeamRating/Classes (it's a different db table)

        if (isArena1v1Enabled)
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 1v1 games of all time", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_TOP_1V1_ALLTIME);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 2v2 games of all time", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_TOP_2V2_ALLTIME);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 3v3 games of all time", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_TOP_3V3_ALLTIME);

        if (isArena3v3soloQEnabled)
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 3v3 Solo games of all time", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_TOP_3V3SOLO_ALLTIME);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay top 5v5 games of all time", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_TOP_5V5_ALLTIME);


        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Replay most watched games of all time", REPLAY_GOSSIP_SENDER_SELECT, REPLAY_MOST_WATCHED_ALLTIME);  // To Do: show arena type + watchedTimes, maybe hide team name
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature ? creature->GetGUID() : player->GetGUID());

        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /* sender */, uint32 action) override
    {
        const uint8 ARENA_TYPE_1v1 = sConfigMgr->GetOption<uint8>("ArenaReplay.1v1.ArenaType", 1);
        const uint8 ARENA_TYPE_3V3_SOLO_QUEUE = sConfigMgr->GetOption<uint8>("ArenaReplay.3v3soloQ.ArenaType", 4);

        player->PlayerTalkClass->ClearMenus();
        switch (action)
        {
            case REPLAY_LATEST_2V2:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysLast30Days(player, creature, ARENA_TYPE_2v2);
                break;
            case REPLAY_LATEST_3V3:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysLast30Days(player, creature, ARENA_TYPE_3v3);
                break;
            case REPLAY_LATEST_5V5:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysLast30Days(player, creature, ARENA_TYPE_5v5);
                break;
            case REPLAY_LATEST_3V3SOLO:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysLast30Days(player, creature, ARENA_TYPE_3V3_SOLO_QUEUE);
                break;
            case REPLAY_LATEST_1V1:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysLast30Days(player, creature, ARENA_TYPE_1v1);
                break;
            case REPLAY_TOP_2V2_ALLTIME:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysAllTime(player, creature, ARENA_TYPE_2v2);
                break;
            case REPLAY_TOP_3V3_ALLTIME:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysAllTime(player, creature, ARENA_TYPE_3v3);
                break;
            case REPLAY_TOP_5V5_ALLTIME:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysAllTime(player, creature, ARENA_TYPE_5v5);
                break;
            case REPLAY_TOP_3V3SOLO_ALLTIME:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysAllTime(player, creature, ARENA_TYPE_3V3_SOLO_QUEUE);
                break;
            case REPLAY_TOP_1V1_ALLTIME:
                player->PlayerTalkClass->SendCloseGossip();
                ShowReplaysAllTime(player, creature, ARENA_TYPE_1v1);
                break;
            case REPLAY_MOST_WATCHED_ALLTIME:
                player->PlayerTalkClass->SendCloseGossip();
                ShowMostWatchedReplays(player, creature);
                break;
            case MY_FAVORITE_MATCHES:
                player->PlayerTalkClass->SendCloseGossip();
                ShowSavedReplays(player, creature);
                break;
            case REPLAY_MY_RECENT_MATCHES:
                player->PlayerTalkClass->SendCloseGossip();
                ShowMyRecentMatches(player, creature);
                break;
            case REPLAY_RECENTLY_WATCHED:
                player->PlayerTalkClass->SendCloseGossip();
                ShowRecentlyWatched(player, creature);
                break;
            case GOSSIP_ACTION_INFO_DEF: // "Back"
                OnGossipHello(player, creature);
                break;

            default:
                if (action >= GOSSIP_ACTION_INFO_DEF + 30) // Replay selected arenas (intid >= 30)
                    return replayArenaMatch(player, action - (GOSSIP_ACTION_INFO_DEF + 30));
        }

        return true;
    }

    bool OnGossipSelectCode(Player* player, Creature* /* creature */, uint32 /* sender */, uint32 action, const char* code) override
    {
        if (!code)
        {
            CloseGossipMenuFor(player);
            return false;
        }

        // Forbidden: ', %, and , (' causes crash when using 'Replay list by player name')
        std::string inputCode = std::string(code);
        if (inputCode.find('\'') != std::string::npos ||
            inputCode.find('%') != std::string::npos ||
            inputCode.find(',') != std::string::npos ||
            inputCode.length() > 50 ||
            inputCode.empty())
        {
            ChatHandler(player->GetSession()).PSendSysMessage("Invalid input.");
            CloseGossipMenuFor(player);
            return false;
        }

        switch (action)
        {
            case REPLAY_MATCH_ID:
            {
                uint32 replayId;
                try
                {
                    replayId = std::stoi(code);
                }
                catch (...)
                {
                    ChatHandler(player->GetSession()).PSendSysMessage("Invalid Match ID.");
                    CloseGossipMenuFor(player);
                    return false;
                }

                return replayArenaMatch(player, replayId);
            }
            case REPLAY_LIST_BY_PLAYERNAME:
            {
                CharacterCacheEntry const* playerData = sCharacterCache->GetCharacterCacheByName(std::string(code));
                if (!playerData)
                {
                    ChatHandler(player->GetSession()).PSendSysMessage("No player found with the name: {}", std::string(code));
                    CloseGossipMenuFor(player);
                    return false;
                }

                std::string playerGuidStr = std::to_string(playerData->Guid.GetRawValue());

                QueryResult result = CharacterDatabase.Query(
                    "SELECT id, winnerTeamName, winnerTeamRating, winnerPlayerGuids, loserTeamName, loserTeamRating, loserPlayerGuids "
                    "FROM character_arena_replays "
                    "WHERE FIND_IN_SET('{}', REPLACE(winnerPlayerGuids, ' ', '')) OR FIND_IN_SET('{}', REPLACE(loserPlayerGuids, ' ', '')) "
                    "ORDER BY id DESC LIMIT {}",
                    playerGuidStr, playerGuidStr, GetReplayBrowseLimit());
                if (!result)
                {
                    ChatHandler(player->GetSession()).PSendSysMessage("No replays found for player: {}", std::string(code));
                    CloseGossipMenuFor(player);
                    return false;
                }

                std::vector<ReplayInfo> infos;
                do
                {
                    Field* fields = result->Fetch();
                    if (!fields)
                        break;

                    infos.push_back(BuildReplayInfo(fields));
                } while (result->NextRow());

                ShowReplays(player, nullptr, infos);
                return true;
            }
            case MY_FAVORITE_MATCHES:
            {
                try
                {
                    uint32 NumberTyped = std::stoi(code);
                    FavoriteMatchId(player->GetGUID().GetCounter(), NumberTyped);
                    return true;
                }
                catch (...)
                {
                    ChatHandler(player->GetSession()).PSendSysMessage("Invalid Match ID.");
                    CloseGossipMenuFor(player);
                    return false;
                }
            }
        }

        return false;
    }

private:
    static uint32 GetReplayBrowseLimit()
    {
        return std::max<uint32>(5, sConfigMgr->GetOption<uint32>("ArenaReplay.Library.BrowseLimit", 20));
    }

    static uint32 GetRecentMatchesDays()
    {
        return std::max<uint32>(1, sConfigMgr->GetOption<uint32>("ArenaReplay.Library.RecentMatchesDays", 30));
    }

    static bool IsRecentlyWatchedEnabled()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.Library.RecentlyWatched.Enable", true);
    }

    std::string GetClassIconById(uint8 id)
    {
        switch (id)
        {
            case CLASS_WARRIOR:
                return "|TInterface\\icons\\inv_sword_27";
            case CLASS_PALADIN:
                return "|TInterface\\icons\\inv_hammer_01";
            case CLASS_HUNTER:
                return "|TInterface\\icons\\inv_weapon_bow_07";
            case CLASS_ROGUE:
                return "|TInterface\\icons\\inv_throwingknife_04";
            case CLASS_PRIEST:
                return "|TInterface\\icons\\inv_staff_30";
            case CLASS_DEATH_KNIGHT:
                return "|TInterface\\icons\\spell_deathknight_classicon";
            case CLASS_SHAMAN:
                return "|TInterface\\icons\\inv_jewelry_talisman_04";
            case CLASS_MAGE:
                return "|TInterface\\icons\\inv_staff_13";
            case CLASS_WARLOCK:
                return "|TInterface\\icons\\spell_nature_drowsy";
            case CLASS_DRUID:
                return "|TInterface\\icons\\inv_misc_monsterclaw_04";
            default:
                return "";
        }
    }

    std::string GetRaceIconById(uint8 id, uint8 gender) {
        const std::string gender_icon = gender == GENDER_MALE ? "male" : "female";
        switch (id) {
            case RACE_HUMAN:
                return "|TInterface/ICONS/achievement_character_human_" + gender_icon;
            case RACE_ORC:
                return "|TInterface/ICONS/achievement_character_orc_" + gender_icon;
            case RACE_DWARF:
                return "|TInterface/ICONS/achievement_character_dwarf_" + gender_icon;
            case RACE_NIGHTELF:
                return "|TInterface/ICONS/achievement_character_nightelf_" + gender_icon;
            case RACE_UNDEAD_PLAYER:
                return "|TInterface/ICONS/achievement_character_undead_" + gender_icon;
            case RACE_TAUREN:
                return "|TInterface/ICONS/achievement_character_tauren_" + gender_icon;
            case RACE_GNOME:
                return "|TInterface/ICONS/achievement_character_gnome_" + gender_icon;
            case RACE_TROLL:
                return "|TInterface/ICONS/achievement_character_troll_" + gender_icon;
            case RACE_BLOODELF:
                return "|TInterface/ICONS/achievement_character_bloodelf_" + gender_icon;
            case RACE_DRAENEI:
                return "|TInterface/ICONS/achievement_character_draenei_" + gender_icon;
            default:
                return "";
        }
    }

    std::string GetPlayersIconTexts(std::string playerGuids) {
        std::string iconsTextTeam;
        for (uint64 _guid : ParseGuidCsv(playerGuids))
        {
            CharacterCacheEntry const* playerData = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid(_guid));
            if (playerData)
            {
                iconsTextTeam += GetClassIconById(playerData->Class) + ":14:14:05:00|t|r";
                iconsTextTeam += GetRaceIconById(playerData->Race, playerData->Sex) + ":14:14:05:00|t|r ";
            }
        }

        if (!iconsTextTeam.empty() && iconsTextTeam.back() == '\n')
            iconsTextTeam.pop_back();

        return iconsTextTeam;
    }

    struct ReplayInfo
    {
        uint32 matchId;

        std::string winnerPlayerGuids;
        std::string winnerTeamName;
        uint32 winnerTeamRating;

        std::string loserPlayerGuids;
        std::string loserTeamName;
        uint32 loserTeamRating;

        //uint32 winnerMMR;
        //uint32 loserMMR;
    };

    static ReplayInfo BuildReplayInfo(Field* fields)
    {
        ReplayInfo info;
        info.matchId = fields[0].Get<uint32>();
        info.winnerTeamName = fields[1].Get<std::string>();
        info.winnerTeamRating = fields[2].Get<uint32>();
        info.winnerPlayerGuids = fields[3].Get<std::string>();
        info.loserTeamName = fields[4].Get<std::string>();
        info.loserTeamRating = fields[5].Get<uint32>();
        info.loserPlayerGuids = fields[6].Get<std::string>();
        return info;
    }

    std::string GetGossipText(ReplayInfo info) {
        std::string iconsTextTeam1 = GetPlayersIconTexts(info.winnerPlayerGuids);
        std::string iconsTextTeam2 = GetPlayersIconTexts(info.loserPlayerGuids);

        std::string coloredWinnerTeamName = "|cff33691E" + info.winnerTeamName + "|r";
        std::string LoserTeamName = info.loserTeamName;

        std::string gossipText = ("[" + std::to_string(info.matchId) + "] (" +
            std::to_string(info.winnerTeamRating) + ")" +
            iconsTextTeam1 + "" +
            " '" + coloredWinnerTeamName + "'" +
            "\n vs (" + std::to_string(info.loserTeamRating) + ")" +
            iconsTextTeam2 + "" +
            " '" + LoserTeamName + "'");

        return gossipText;
    }

    void ShowMyRecentMatches(Player* player, Creature* creature)
    {
        auto matchInfos = loadRecentMatchesByCharacter(player->GetGUID().GetCounter());
        ShowReplays(player, creature, matchInfos);
    }

    std::vector<ReplayInfo> loadRecentMatchesByCharacter(uint32 characterId)
    {
        std::vector<ReplayInfo> records;

        std::time_t now = std::time(nullptr);
        std::tm* tmNow = std::localtime(&now);
        tmNow->tm_mday -= int(GetRecentMatchesDays());
        std::mktime(tmNow);

        std::stringstream ss;
        ss << std::put_time(tmNow, "%Y-%m-%d %H:%M:%S");
        std::string recentCutoff = ss.str();
        std::string playerGuidStr = std::to_string(uint64(characterId));

        QueryResult result = CharacterDatabase.Query(
            "SELECT id, winnerTeamName, winnerTeamRating, winnerPlayerGuids, loserTeamName, loserTeamRating, loserPlayerGuids "
            "FROM character_arena_replays "
            "WHERE timestamp >= '{}' AND (FIND_IN_SET('{}', REPLACE(winnerPlayerGuids, ' ', '')) OR FIND_IN_SET('{}', REPLACE(loserPlayerGuids, ' ', ''))) "
            "ORDER BY id DESC LIMIT {}",
            recentCutoff, playerGuidStr, playerGuidStr, GetReplayBrowseLimit());

        if (!result)
            return records;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                return records;

            records.push_back(BuildReplayInfo(fields));
        } while (result->NextRow());

        return records;
    }

    void ShowRecentlyWatched(Player* player, Creature* creature)
    {
        auto matchInfos = loadRecentlyWatchedReplays(player->GetGUID().GetCounter());
        ShowReplays(player, creature, matchInfos);
    }

    std::vector<ReplayInfo> loadRecentlyWatchedReplays(uint32 characterId)
    {
        std::vector<ReplayInfo> records;
        if (!IsRecentlyWatchedEnabled())
            return records;

        QueryResult result = CharacterDatabase.Query(
            "SELECT r.id, r.winnerTeamName, r.winnerTeamRating, r.winnerPlayerGuids, r.loserTeamName, r.loserTeamRating, r.loserPlayerGuids "
            "FROM character_recently_watched_replays rw "
            "JOIN character_arena_replays r ON r.id = rw.replay_id "
            "WHERE rw.character_id = {} "
            "ORDER BY rw.last_watched DESC LIMIT {}",
            characterId, GetReplayBrowseLimit());

        if (!result)
            return records;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                return records;

            records.push_back(BuildReplayInfo(fields));
        } while (result->NextRow());

        return records;
    }

    void ShowReplaysAllTime(Player* player, Creature* creature, uint8 arenaTypeId)
    {
        auto matchInfos = loadReplaysAllTimeByArenaType(arenaTypeId);
        ShowReplays(player, creature, matchInfos);
    }

    std::vector<ReplayInfo> loadReplaysAllTimeByArenaType(uint8 arenaTypeId)
    {
        std::vector<ReplayInfo> records;
        QueryResult result = CharacterDatabase.Query("SELECT id, winnerTeamName, winnerTeamRating, winnerPlayerGuids, loserTeamName, loserTeamRating, loserPlayerGuids FROM character_arena_replays WHERE arenaTypeId = {} ORDER BY winnerTeamRating DESC LIMIT {}", arenaTypeId, GetReplayBrowseLimit());

        if (!result)
            return records;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                return records;

            records.push_back(BuildReplayInfo(fields));
        } while (result->NextRow());

        return records;
    }

    void ShowReplaysLast30Days(Player* player, Creature* creature, uint8 arenaTypeId)
    {
        auto matchInfos = loadReplaysLast30Days(arenaTypeId);
        ShowReplays(player, creature, matchInfos);
    }

    std::vector<ReplayInfo> loadReplaysLast30Days(uint8 arenaTypeId)
    {
        std::vector<ReplayInfo> records;

        std::time_t now = std::time(nullptr);
        std::tm* tmNow = std::localtime(&now);
        tmNow->tm_mday -= 30;
        std::mktime(tmNow);

        std::stringstream ss;
        ss << std::put_time(tmNow, "%Y-%m-%d %H:%M:%S");
        std::string thirtyDaysAgo = ss.str();

		// Only show games that are 30 days old
        QueryResult result = CharacterDatabase.Query(
            "SELECT id, winnerTeamName, winnerTeamRating, winnerPlayerGuids, loserTeamName, loserTeamRating, loserPlayerGuids, timestamp "
            "FROM character_arena_replays "
            "WHERE arenaTypeId = {} AND timestamp >= '{}' "
            "ORDER BY id DESC LIMIT {}", arenaTypeId, thirtyDaysAgo.c_str(), GetReplayBrowseLimit());

        if (!result)
            return records;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                return records;

            records.push_back(BuildReplayInfo(fields));
        } while (result->NextRow());

        return records;
    }

    void ShowMostWatchedReplays(Player* player, Creature* creature)
    {
        auto matchInfos = loadMostWatchedReplays();
        ShowReplays(player, creature, matchInfos);
    }

    void ShowReplays(Player* player, Creature* creature, std::vector<ReplayInfo> matchInfos) {
        if (matchInfos.empty())
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, "No replays found.", REPLAY_GOSSIP_SENDER_SELECT, GOSSIP_ACTION_INFO_DEF);
        else
        {
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, "[Replay ID] (Team Rating) 'Team Name'\n----------------------------------------------", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF); // Back to Main Menu
            for (const auto& info : matchInfos)
            {
                const std::string gossipText = GetGossipText(info);
                const uint32 actionOffset = GOSSIP_ACTION_INFO_DEF + 30;
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, gossipText, REPLAY_GOSSIP_SENDER_SELECT, actionOffset + info.matchId);
            }
        }

        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Back", REPLAY_GOSSIP_SENDER_SELECT, GOSSIP_ACTION_INFO_DEF);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature ? creature->GetGUID() : player->GetGUID());
    }

    std::vector<ReplayInfo> loadMostWatchedReplays()
    {
        std::vector<ReplayInfo> records;
        QueryResult result = CharacterDatabase.Query(
            "SELECT id, winnerTeamName, winnerTeamRating, winnerPlayerGuids, loserTeamName, loserTeamRating, loserPlayerGuids "
            "FROM character_arena_replays "
            "ORDER BY timesWatched DESC, winnerTeamRating DESC "
            "LIMIT {}", GetReplayBrowseLimit());

        if (!result)
            return records;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                return records;

            records.push_back(BuildReplayInfo(fields));
        } while (result->NextRow());

        return records;
    }

    void ShowSavedReplays(Player* player, Creature* creature, bool firstPage = true)
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Favorite a Match ID", REPLAY_GOSSIP_SENDER_CODE, MY_FAVORITE_MATCHES, "", 0, true);

        std::string sortOrder = (firstPage) ? "ASC" : "DESC";
        QueryResult result = CharacterDatabase.Query(
            "SELECT r.id, r.winnerTeamName, r.winnerTeamRating, r.winnerPlayerGuids, r.loserTeamName, r.loserTeamRating, r.loserPlayerGuids "
            "FROM character_saved_replays s "
            "JOIN character_arena_replays r ON r.id = s.replay_id "
            "WHERE s.character_id = {} ORDER BY s.id {} LIMIT {}",
            player->GetGUID().GetCounter(), sortOrder, GetReplayBrowseLimit());
        if (!result)
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, "No saved replays found.", REPLAY_GOSSIP_SENDER_SELECT, GOSSIP_ACTION_INFO_DEF);
        else
        {
            std::vector<ReplayInfo> infos;
            do
            {
                Field* fields = result->Fetch();
                if (!fields)
                    break;

                infos.push_back(BuildReplayInfo(fields));
            } while (result->NextRow());

            ShowReplays(player, creature, infos);
            return;
        }
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Back", REPLAY_GOSSIP_SENDER_SELECT, GOSSIP_ACTION_INFO_DEF);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature ? creature->GetGUID() : player->GetGUID());
    }

    void RecordReplayWatch(uint32 characterId, uint32 replayId)
    {
        if (!IsRecentlyWatchedEnabled())
            return;

        CharacterDatabase.Execute(
            "INSERT INTO character_recently_watched_replays (character_id, replay_id, last_watched, watch_count) "
            "VALUES ({}, {}, CURRENT_TIMESTAMP, 1) "
            "ON DUPLICATE KEY UPDATE last_watched = CURRENT_TIMESTAMP, watch_count = watch_count + 1",
            characterId, replayId);
    }

    void FavoriteMatchId(uint64 playerGuid, uint32 code)
    {
        // Need to check if the match exists in character_arena_replays, then insert in character_saved_replays
        QueryResult result = CharacterDatabase.Query("SELECT id FROM character_arena_replays WHERE id = " + std::to_string(code));
        if (result)
        {
            std::string query = "INSERT IGNORE INTO character_saved_replays (character_id, replay_id) VALUES (" + std::to_string(playerGuid) + ", " + std::to_string(code) + ")";
            CharacterDatabase.Execute(query.c_str());

            if (Player* player = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(playerGuid)))
            {
                ChatHandler(player->GetSession()).PSendSysMessage("Replay match ID {} saved.", code);
                CloseGossipMenuFor(player);
            }
        }
        else
        {
            if (Player* player = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(playerGuid)))
            {
                ChatHandler(player->GetSession()).PSendSysMessage("Replay match ID {} does not exist.", code);
                CloseGossipMenuFor(player);
            }
        }
    }

    bool replayArenaMatch(Player* player, uint32 replayId)
    {
        auto handler = ChatHandler(player->GetSession());

        if (player->InBattlegroundQueue())
        {
            handler.PSendSysMessage("Can't be queued for arena or bg.");
            return false;
        }

        if (!loadReplayDataForPlayer(player, replayId))
        {
            CloseGossipMenuFor(player);
            return false;
        }

        auto loadedIt = loadedReplays.find(player->GetGUID().GetCounter());
        if (loadedIt == loadedReplays.end())
        {
            handler.PSendSysMessage("Replay data could not be loaded.");
            return false;
        }

        MatchRecord const& record = loadedIt->second;
        bool viewerWasParticipant = VectorContainsGuid(record.winnerPlayerGuidList, player->GetGUID().GetRawValue()) ||
                                    VectorContainsGuid(record.loserPlayerGuidList, player->GetGUID().GetRawValue());

        bool hasActorTracks = !record.winnerActorTracks.empty() || !record.loserActorTracks.empty();
        if (viewerWasParticipant && sConfigMgr->GetOption<bool>("ArenaReplay.BlockWatchingOwnReplay", true) && !hasActorTracks)
        {
            loadedReplays.erase(loadedIt);
            handler.PSendSysMessage("Watching your own recorded match is blocked when no actor tracks are available.");
            return false;
        }

        // Use the spectator's real level bracket, not max level.
        // Replays are spectator-only, but the battleground instance still needs a valid bracket/template.
        Battleground* bg = sBattlegroundMgr->CreateNewBattleground(record.typeId, GetBattlegroundBracketByLevel(record.mapId, player->GetLevel()), record.arenaTypeId, false);
        if (!bg)
        {
            handler.PSendSysMessage("Couldn't create arena map!");
            handler.SetSentErrorMessage(true);
            return false;
        }

        bgReplayIds[bg->GetInstanceID()] = player->GetGUID().GetCounter();
        player->SetPendingSpectatorForBG(bg->GetInstanceID());
        bg->StartBattleground();

        BattlegroundTypeId bgTypeId = bg->GetBgTypeID();

        TeamId teamId = player->GetTeamId() == TEAM_ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE;

        uint32 queueSlot = 0;
        WorldPacket data;

        // TEAM_NEUTRAL can leave the replay instance without a valid team start location on some maps,
        // which can cascade into bad teleports/homebinds. Keep the player on their real faction team
        // while still marking them as spectator via SetPendingSpectatorForBG().
        LockReplayViewerControl(player, replayId, bg->GetInstanceID());
        player->SetBattlegroundId(bg->GetInstanceID(), bgTypeId, queueSlot, true, false, teamId);
        player->SetEntryPoint();
        sBattlegroundMgr->SendToBattleground(player, bg->GetInstanceID(), bgTypeId);
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_IN_PROGRESS, 0, bg->GetStartTime(), bg->GetArenaType(), teamId);
        player->GetSession()->SendPacket(&data);
        ActiveReplaySession& replaySession = activeReplaySessions[player->GetGUID().GetCounter()];
        replaySession.viewerWasParticipant = viewerWasParticipant;
        if (ReplayDebugEnabled(ReplayDebugFlag::General))
        {
            std::ostringstream ss;
            ss << "viewerWasParticipant=" << (viewerWasParticipant ? 1 : 0)
               << " actualPlayerBg=" << player->GetBattlegroundId()
               << " sessionBg=" << replaySession.battlegroundInstanceId
               << " bgType=" << uint32(bgTypeId)
               << " arenaType=" << uint32(bg->GetArenaType());
            ReplayLog(ReplayDebugFlag::General, &replaySession, player, "ENTER_BG", ss.str());
        }
        replaySession.actorSpectateOnWinnerTeam = sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.StartOnWinnerTeam", true);
        replaySession.actorTrackIndex = 0;
        replaySession.nextActorTeleportMs = 0;
        replaySession.replayWarmupUntilMs = 1500;

        if (viewerWasParticipant && sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.StartOnSelfWhenParticipant", true))
        {
            auto const* winnerTracks = SelectTracks(record, true);
            auto const* loserTracks = SelectTracks(record, false);
            auto findSelf = [player](std::vector<ActorTrack> const* tracks) -> int32
            {
                if (!tracks)
                    return -1;
                for (size_t i = 0; i < tracks->size(); ++i)
                    if ((*tracks)[i].guid == player->GetGUID().GetRawValue() && IsReplayActorTrackPlayable((*tracks)[i]))
                        return int32(i);
                return -1;
            };

            int32 selfIndex = findSelf(winnerTracks);
            if (selfIndex >= 0)
            {
                replaySession.actorSpectateOnWinnerTeam = true;
                replaySession.actorTrackIndex = uint32(selfIndex);
            }
            else
            {
                selfIndex = findSelf(loserTracks);
                if (selfIndex >= 0)
                {
                    replaySession.actorSpectateOnWinnerTeam = false;
                    replaySession.actorTrackIndex = uint32(selfIndex);
                }
            }
        }

        handler.PSendSysMessage("Replay ID {} begins.", replayId);

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.LockMovement", true))
            handler.PSendSysMessage("Spectator-only mode active: movement is locked during replay playback.");

        return true;
    }

    bool loadReplayDataForPlayer(Player* p, uint32 matchId)
    {
        QueryResult result = CharacterDatabase.Query("SELECT id, arenaTypeId, typeId, contentSize, contents, mapId, timesWatched, winnerPlayerGuids, loserPlayerGuids, winnerActorTrack, loserActorTrack FROM character_arena_replays WHERE id = {}", matchId);
        if (!result)
        {
            ChatHandler(p->GetSession()).PSendSysMessage("Replay data not found.");
            CloseGossipMenuFor(p);
            return false;
        }

        Field* fields = result->Fetch();
        if (!fields)
        {
            ChatHandler(p->GetSession()).PSendSysMessage("Replay data not found.");
            CloseGossipMenuFor(p);
            return false;
        }

        CharacterDatabase.Execute("UPDATE character_arena_replays SET timesWatched = timesWatched + 1 WHERE id = {}", matchId);

        MatchRecord record;
        deserializeMatchData(record, fields);

        if (record.packets.empty())
        {
            ChatHandler(p->GetSession()).PSendSysMessage("Replay data is incomplete or unsafe for playback.");
            CloseGossipMenuFor(p);
            return false;
        }

        loadedReplays[p->GetGUID().GetCounter()] = std::move(record);
        RecordReplayWatch(p->GetGUID().GetCounter(), matchId);
        return true;
    }

    void deserializeMatchData(MatchRecord& record, Field* fields)
    {
        record.arenaTypeId = uint8(fields[1].Get<uint32>());
        record.typeId = BattlegroundTypeId(fields[2].Get<uint32>());
        record.mapId = uint32(fields[5].Get<uint32>());
        record.winnerPlayerGuidList = ParseGuidCsv(fields[7].Get<std::string>());
        record.loserPlayerGuidList = ParseGuidCsv(fields[8].Get<std::string>());
        record.winnerActorTracks = DeserializeActorTracks(fields[9].Get<std::string>());
        record.loserActorTracks = DeserializeActorTracks(fields[10].Get<std::string>());
        SanitizeActorTracks(record.winnerActorTracks);
        SanitizeActorTracks(record.loserActorTracks);

        auto decoded = Acore::Encoding::Base32::Decode(fields[4].Get<std::string>());
        if (!decoded || decoded->empty())
            return;

        ByteBuffer buffer;
        buffer.append(decoded->data(), decoded->size());

        /** deserialize replay binary data **/
        while (buffer.rpos() + 10 <= buffer.size())
        {
            uint32 packetSize = 0;
            uint32 packetTimestamp = 0;
            uint16 opcode = 0;
            buffer >> packetSize;
            buffer >> packetTimestamp;
            buffer >> opcode;

            if (packetSize > (buffer.size() - buffer.rpos()))
                break;

            if (packetSize > 524288)
            {
                std::vector<uint8> ignored(packetSize, 0);
                buffer.read(ignored.data(), packetSize);
                continue;
            }

            WorldPacket packet(opcode, packetSize);

            if (packetSize > 0)
            {
                std::vector<uint8> tmp(packetSize, 0);
                buffer.read(tmp.data(), packetSize);
                packet.append(tmp.data(), packetSize);
            }

            if (!IsReplayPacketOpcodeAllowedForPlayback(packet.GetOpcode()))
                continue;

            record.packets.push_back({ packetTimestamp, packet });
        }
    }
};

class ArenaReplayPlayerScript : public PlayerScript
{
public:
    ArenaReplayPlayerScript() : PlayerScript("ArenaReplayPlayerScript", {
        PLAYERHOOK_ON_LOGOUT
    }) {}

    void OnPlayerLogout(Player* player) override
    {
                auto it = activeReplaySessions.find(player->GetGUID().GetCounter());
        if (it != activeReplaySessions.end())
        {
            if (ReplayDebugEnabled(ReplayDebugFlag::Teardown))
                ReplayLog(ReplayDebugFlag::Teardown, &it->second, player, "LOGOUT", "reason=player_logout");
            it->second.replayBgEnded = true;
            RequestReplayTeardown(player, player->GetBattleground(), "player_logout", 0);
        }
        ReleaseReplayViewerControl(player);
        loadedReplays.erase(player->GetGUID().GetCounter());
    }
};

class ConfigLoaderArenaReplay : public WorldScript
{
public:
    ConfigLoaderArenaReplay() : WorldScript("config_loader_arena_replay", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD
    }) {}
    virtual void OnAfterConfigLoad(bool /*Reload*/) override
    {
        DeleteOldReplays();
    }

private:
    void DeleteOldReplays()
    {
        // delete all the replays older than X days
        const auto days = sConfigMgr->GetOption<uint32>("ArenaReplay.DeleteReplaysAfterDays", 30);
        if (days > 0)
        {
            std::string addition = "";

            const bool deleteSavedReplays = sConfigMgr->GetOption<bool>("ArenaReplay.DeleteSavedReplays", false);

            if (!deleteSavedReplays)
                addition = "AND `id` NOT IN (SELECT `replay_id` FROM `character_saved_replays`)";

            const auto query = "DELETE FROM `character_arena_replays` WHERE `timestamp` < (NOW() - INTERVAL " + std::to_string(days) + " DAY) " + addition;
            CharacterDatabase.Execute(query);

            if (deleteSavedReplays)
                CharacterDatabase.Execute("DELETE FROM `character_saved_replays` WHERE `replay_id` NOT IN (SELECT `id` FROM `character_arena_replays`)");
        }

        CharacterDatabase.Execute("DELETE FROM `character_recently_watched_replays` WHERE `replay_id` NOT IN (SELECT `id` FROM `character_arena_replays`)");

        const auto recentWatchDays = sConfigMgr->GetOption<uint32>("ArenaReplay.Library.RecentlyWatched.RetentionDays", 90);
        if (recentWatchDays > 0)
            CharacterDatabase.Execute("DELETE FROM `character_recently_watched_replays` WHERE `last_watched` < (NOW() - INTERVAL {} DAY)", recentWatchDays);
    }
};

class PlayerGossip_ArenaReplayService final : public PlayerGossip
{
public:
    enum Senders
    {
        ROOT = 1000,
        LEGACY_SELECT = GOSSIP_SENDER_MAIN,
        LEGACY_CODE = 1001
    };

    PlayerGossip_ArenaReplayService() : PlayerGossip(91012)
    {
        RegisterAction(ROOT, OpenRoot);
        RegisterAction(LEGACY_SELECT, DispatchSelect);
        RegisterExtendedAction(LEGACY_CODE, DispatchSelectCode);
    }

    static void OpenRoot(Player* player, int32, int32, std::any)
    {
        ReplayGossip script;
        script.OnGossipHello(player, nullptr);
    }

    static void DispatchSelect(Player* player, int32 sender, int32 action, std::any)
    {
        ReplayGossip script;
        script.OnGossipSelect(player, nullptr, uint32(sender), uint32(action));
    }

    static void DispatchSelectCode(Player* player, int32 sender, int32 action, std::string code, std::any)
    {
        ReplayGossip script;
        script.OnGossipSelectCode(player, nullptr, uint32(sender), uint32(action), code.c_str());
    }
};

namespace RTG::Services::ArenaReplay
{
    bool Open(Player* player)
    {
        if (!player)
            return false;

        player->PlayerTalkClass->ClearMenus();
        CloseGossipMenuFor(player);

        sPlayerGossipMgr->ShowGossipMenu(player, 91012, PlayerGossip_ArenaReplayService::ROOT, 0);
        return true;
    }
}


class ArenaReplayCommandScript : public CommandScript
{
public:
    ArenaReplayCommandScript() : CommandScript("ArenaReplayCommandScript") { }

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
	{
		static Acore::ChatCommands::ChatCommandTable replaySubTable =
		{
			{ "prev", HandleReplayPrevCommand, SEC_PLAYER, Acore::ChatCommands::Console::No },
			{ "next", HandleReplayNextCommand, SEC_PLAYER, Acore::ChatCommands::Console::No },
			{ "open", HandleReplayOpenCommand, SEC_PLAYER, Acore::ChatCommands::Console::No },
		};

		static Acore::ChatCommands::ChatCommandTable commandTable =
		{
			{ "rtgreplay", replaySubTable }
		};

		return commandTable;
	}

    static bool HandleReplayPrevCommand(ChatHandler* handler)
    {
        return HandleReplayStep(handler, -1);
    }

    static bool HandleReplayNextCommand(ChatHandler* handler)
    {
        return HandleReplayStep(handler, 1);
    }

    static bool HandleReplayOpenCommand(ChatHandler* handler)
    {
        if (!handler)
            return false;
        Player* player = handler->GetPlayer();
        return RTG::Services::ArenaReplay::Open(player);
    }

    static bool HandleReplayStep(ChatHandler* handler, int32 delta)
    {
        if (!handler)
            return false;

        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        auto sessionIt = activeReplaySessions.find(player->GetGUID().GetCounter());
        auto replayIt = loadedReplays.find(player->GetGUID().GetCounter());
        if (sessionIt == activeReplaySessions.end() || replayIt == loadedReplays.end())
        {
            handler->PSendSysMessage("You are not currently watching a replay.");
            return false;
        }

        if (!StepReplayActorSelection(replayIt->second, sessionIt->second, delta))
        {
            handler->PSendSysMessage("No replay actors are available.");
            return false;
        }

        Battleground* bg = player->GetBattleground();
        if (bg)
            ApplyActorReplayView(player, replayIt->second, sessionIt->second, bg->GetStartTime(), true);

        if (ReplayDebugEnabled(ReplayDebugFlag::Actors))
        {
            uint32 flatIndex = 0;
            if (ActorTrack const* track = GetSelectedReplayActorTrack(replayIt->second, sessionIt->second, &flatIndex))
            {
                std::ostringstream ss;
                ss << "delta=" << delta << " actorGuid=" << track->guid << " actorName=" << track->name
                   << " actorIndex=" << flatIndex << '/' << GetReplayActorTotalCount(replayIt->second);
                ReplayLog(ReplayDebugFlag::Actors, &sessionIt->second, player, "STEP", ss.str());
            }
        }

        SendReplayHudPov(player, replayIt->second, sessionIt->second, true);
        sessionIt->second.nextHudWatcherSyncMs = 0;
        SendReplayHudWatchers(player->GetBattleground(), player, replayIt->second, sessionIt->second, true);
        return true;
    }
};

void AddArenaReplayScripts()
{
    new ConfigLoaderArenaReplay();
    new ArenaReplayServerScript();
    new ArenaReplayBGScript();
    new ArenaReplayArenaScript();
    new ArenaReplayPlayerScript();
    new ReplayGossip();
    new PlayerGossip_ArenaReplayService();
    new ArenaReplayCommandScript();
}
