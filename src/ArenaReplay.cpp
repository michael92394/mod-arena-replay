//
// Created by romain-p on 17/10/2021.
//
#include "ArenaReplayDatabaseConnection.h"
#include "ArenaReplay_loader.h"
#include "ArenaTeamMgr.h"
#include "Base32.h"
#include "Battleground.h"
#include "CharacterDatabase.h"
#include "Chat.h"
#include "Config.h"
#include "GameTime.h"
#include "Creature.h"
#include "GameObject.h"
#include "Item.h"
#include "Map.h"
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
#include <vector>

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
struct ReplayActorAppearanceSnapshot
{
    uint64 guid = 0;
    bool winnerSide = false;
    uint8 playerClass = 0;
    uint8 race = 0;
    uint8 gender = 0;
    std::string name;
    uint32 displayId = 0;
    uint32 nativeDisplayId = 0;
    uint32 mainhandDisplayId = 0;
    uint32 offhandDisplayId = 0;
    uint32 rangedDisplayId = 0;
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
    std::unordered_map<uint64, ReplayActorAppearanceSnapshot> actorAppearanceSnapshots;
};
struct BgPlayersGuids { std::string alliancePlayerGuids; std::string hordePlayerGuids; };
struct TeamRecorders { ObjectGuid alliance; ObjectGuid horde; };
struct ReplayCloneBinding
{
    uint64 actorGuid = 0;
    bool winnerSide = false;
    ObjectGuid cloneGuid;
};
enum ReplayDynamicObjectRole
{
    RTG_REPLAY_OBJECT_GATE,
    RTG_REPLAY_OBJECT_BUFF,
    RTG_REPLAY_OBJECT_DS_WATER_COLLISION,
    RTG_REPLAY_OBJECT_DS_WATER_VISUAL,
    RTG_REPLAY_OBJECT_RV_ELEVATOR,
    RTG_REPLAY_OBJECT_RV_FIRE,
    RTG_REPLAY_OBJECT_RV_FIRE_DOOR,
    RTG_REPLAY_OBJECT_RV_GEAR,
    RTG_REPLAY_OBJECT_RV_PULLEY,
    RTG_REPLAY_OBJECT_RV_PILLAR,
};
struct ReplayDynamicObjectBinding
{
    ObjectGuid guid;
    uint32 entry = 0;
    uint32 nativeMap = 0;
    uint32 replayMap = 0;
    uint32 role = 0;
    bool required = false;
    bool active = false;
    bool spawned = false;
    bool cleaned = false;
};
struct ActiveReplaySession
{
    uint64 traceId = 0;
    uint32 battlegroundInstanceId = 0;
    uint32 priorBattlegroundInstanceId = 0;
    uint32 priorPhaseMask = 1;
    uint32 replayPhaseMask = 0;
    bool replayPhaseApplied = false;
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
    bool cloneSceneBuilt = false;
    uint32 nextCloneSyncMs = 0;
    std::vector<ReplayCloneBinding> cloneBindings;
    ObjectGuid cameraAnchorGuid;
    bool viewpointBound = false;
    bool spectatorShellActive = false;
    uint32 nativeMapId = 0;
    uint32 replayMapId = 0;
    Position replaySpawnPosition;
    bool sandboxTeleportIssued = false;
    bool awaitingReplayMapAttach = false;
    bool replayMapAttached = false;
    uint32 replayAttachDeadlineMs = 0;
    uint32 nextAttachLogMs = 0;
    uint32 replayPlaybackMs = 0;
    uint32 replayLastServerMs = 0;
    std::vector<ReplayDynamicObjectBinding> dynamicObjects;
    bool dynamicObjectsSpawned = false;
    bool dynamicObjectsInitialized = false;
    uint32 nextDynamicObjectLogMs = 0;
    uint32 rvState = 0;
    uint32 rvNextEventMs = 0;
    uint32 rvPillarToggleState = 0;
    bool rvInitialUpper = false;
    float rvFirstActorZ = 0.0f;
    uint32 dsWaterState = 0;
    uint32 dsNextEventMs = 0;
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
    static void ResetActorReplayView(Player* replayer, ActiveReplaySession& session);
    static ActorFrame GetInterpolatedActorFrame(ActorTrack const& track, uint32 nowMs, bool& ok);
    static ReplayActorAppearanceSnapshot const* FindReplayActorAppearanceSnapshot(MatchRecord const& match, uint64 actorGuid);
    static Creature* EnsureReplayCameraAnchor(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void BindReplayViewpoint(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void ClearReplayViewpoint(Player* viewer, ActiveReplaySession& session);
    static bool IsReplayViewerMapAttached(Player* viewer, Battleground* bg, ActiveReplaySession const& session);
    static bool GetReplayBootstrapFrame(MatchRecord const& match, ActorFrame& frame, uint64& actorGuid, bool& winnerSide);
    static bool ReplayDynamicObjectsDebugEnabled();
    static uint32 ResolveReplayMapId(uint32 nativeMapId);
    static uint32 AllocateReplayPrivatePhaseMask(uint64 viewerKey);
    static bool PrepareReplayMapSandbox(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void RestoreReplayViewerPhase(Player* player, ActiveReplaySession const& session);
    static bool InitializeReplayDynamicObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void UpdateReplayDynamicObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void DespawnReplayDynamicObjects(Player* viewer, ActiveReplaySession& session);
    static uint32 GetReplayNowMs();
    static void ProcessReplaySandboxSessions(uint32 diff);

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

        if (loadedReplays.find(player->GetGUID().GetCounter()) == loadedReplays.end())
            return fail("replay_not_loaded");

        if (session.battlegroundInstanceId == 0)
        {
            if (session.replayMapId != 0 && player->GetMapId() != session.replayMapId)
                return fail("not_on_replay_map");

            if (session.replayPhaseMask != 0 && player->GetPhaseMask() != session.replayPhaseMask)
                return fail("phase_mismatch");

            if (reason)
                *reason = "allowed";
            return true;
        }

        Battleground* bg = player->GetBattleground();
        if (!bg)
            return fail("no_battleground");

        if (!bg->isArena())
            return fail("not_arena");

        if (bg->GetInstanceID() != session.battlegroundInstanceId)
            return fail("session_bg_mismatch");

        auto replayIt = bgReplayIds.find(bg->GetInstanceID());
        if (replayIt == bgReplayIds.end())
            return fail("bg_not_registered_as_replay");

        if (replayIt->second != player->GetGUID().GetCounter())
            return fail("replay_owner_mismatch");

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
                   << " sessionBg=" << (session ? session->battlegroundInstanceId : 0)
                   << " playerPhase=" << player->GetPhaseMask()
                   << " replayPhase=" << (session ? session->replayPhaseMask : 0);
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
        if (!replayer || !IsReplayHudAllowed(replayer))
            return;

        uint32 nowMs = bg ? bg->GetStartTime() : session.replayPlaybackMs;
        if (!force && session.nextHudWatcherSyncMs > nowMs)
            return;
        session.nextHudWatcherSyncMs = nowMs + 1000;

        std::ostringstream payload;
        std::vector<std::string> entries;
        uint32 count = 0;
        auto considerViewer = [&](Player* viewer, ActiveReplaySession const& viewerSession)
        {
            if (!viewer || viewer == replayer || viewerSession.teardownInProgress)
                return;

            if (viewerSession.replayId != session.replayId)
                return;

            ++count;
            std::ostringstream e;
            e << viewer->GetGUID().GetCounter() << ',' << viewer->GetName() << ',' << GetClassToken(viewer->getClass()) << ',' << GetClassIconPath(viewer->getClass());
            entries.push_back(e.str());
        };

        if (bg)
        {
            for (auto const& pair : bg->GetPlayers())
            {
                Player* viewer = pair.second;
                auto viewerSessionIt = viewer ? activeReplaySessions.find(viewer->GetGUID().GetCounter()) : activeReplaySessions.end();
                if (viewerSessionIt == activeReplaySessions.end())
                    continue;
                ActiveReplaySession const& viewerSession = viewerSessionIt->second;
                if (viewerSession.battlegroundInstanceId != session.battlegroundInstanceId)
                    continue;
                considerViewer(viewer, viewerSession);
            }
        }
        else
        {
            for (auto const& pair : activeReplaySessions)
            {
                if (pair.first == replayer->GetGUID().GetCounter())
                    continue;
                Player* viewer = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(pair.first));
                if (!viewer)
                    continue;
                considerViewer(viewer, pair.second);
            }
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

    static uint32 GetReplayNowMs()
    {
        return uint32(GameTime::GetGameTimeMS().count());
    }

    static bool GetReplayBootstrapFrame(MatchRecord const& match, ActorFrame& frame, uint64& actorGuid, bool& winnerSide)
    {
        auto consider = [&](std::vector<ActorTrack> const& tracks, bool side) -> bool
        {
            for (ActorTrack const& track : tracks)
            {
                if (!IsReplayActorTrackPlayable(track) || track.frames.empty())
                    continue;

                frame = track.frames.front();
                actorGuid = track.guid;
                winnerSide = side;
                return true;
            }
            return false;
        };

        return consider(match.winnerActorTracks, true) || consider(match.loserActorTracks, false);
    }

    static uint32 GetReplayPrivatePhaseMinBit()
    {
        return std::min<uint32>(30u, std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("ArenaReplay.Sandbox.PrivatePhaseMinBit", 1u)));
    }

    static uint32 GetReplayPrivatePhaseMaxBit()
    {
        uint32 minBit = GetReplayPrivatePhaseMinBit();
        uint32 maxBit = std::min<uint32>(30u, std::max<uint32>(minBit, sConfigMgr->GetOption<uint32>("ArenaReplay.Sandbox.PrivatePhaseMaxBit", 30u)));
        return maxBit;
    }

    static uint32 AllocateReplayPrivatePhaseMask(uint64 viewerKey)
    {
        std::unordered_set<uint32> usedPhaseMasks;
        for (auto const& pair : activeReplaySessions)
        {
            if (pair.first == viewerKey)
                continue;

            if (pair.second.replayPhaseMask != 0)
                usedPhaseMasks.insert(pair.second.replayPhaseMask);
        }

        uint32 minBit = GetReplayPrivatePhaseMinBit();
        uint32 maxBit = GetReplayPrivatePhaseMaxBit();
        for (uint32 bit = minBit; bit <= maxBit; ++bit)
        {
            uint32 mask = (1u << bit);
            if (usedPhaseMasks.find(mask) == usedPhaseMasks.end())
                return mask;
        }

        return 0;
    }

    static bool PrepareReplayMapSandbox(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!viewer || match.mapId == 0)
            return false;

        session.nativeMapId = match.mapId;
        session.battlegroundInstanceId = 0;
        session.replayMapId = ResolveReplayMapId(match.mapId);
        if (session.replayMapId == 0)
        {
            LOG_ERROR("server.loading", "[RTG][REPLAY][MAP_RESOLVE_FAIL] replay={} viewerGuid={} nativeMap={} replayMap=0 phase={} result=unresolved",
                session.replayId, viewer->GetGUID().GetCounter(), session.nativeMapId, session.replayPhaseMask);
            if (viewer->GetSession())
                ChatHandler(viewer->GetSession()).PSendSysMessage("Replay map {} is not configured for copied-instance playback.", match.mapId);
            return false;
        }

        if (session.replayPhaseMask == 0)
            session.replayPhaseMask = AllocateReplayPrivatePhaseMask(viewer->GetGUID().GetCounter());

        if (session.replayPhaseMask == 0)
            return false;

        viewer->SetPhaseMask(session.replayPhaseMask, true);
        session.replayPhaseApplied = true;
        LOG_INFO("server.loading", "[RTG][REPLAY][MAP_RESOLVE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} result=ok",
            session.replayId, viewer->GetGUID().GetCounter(), session.nativeMapId, session.replayMapId, session.replayPhaseMask);
        return true;
    }

    static void RestoreReplayViewerPhase(Player* player, ActiveReplaySession const& session)
    {
        if (!player || !session.replayPhaseApplied)
            return;

        player->SetPhaseMask(session.priorPhaseMask ? session.priorPhaseMask : 1u, true);
    }

    static bool IsReplayViewerMapAttached(Player* viewer, Battleground* bg, ActiveReplaySession const& session)
    {
        if (!viewer || !viewer->GetMap())
            return false;

        uint32 expectedMapId = session.replayMapId ? session.replayMapId : (bg ? bg->GetMapId() : 0);
        if (!expectedMapId || viewer->GetMapId() != expectedMapId)
            return false;

        if (session.replayPhaseMask != 0 && viewer->GetPhaseMask() != session.replayPhaseMask)
            return false;

        return viewer->GetBattlegroundId() == 0 && !viewer->InBattleground();
    }

    static Creature* FindReplayCameraAnchor(Player* viewer, ActiveReplaySession const& session)
    {
        if (!viewer || !viewer->GetMap() || !session.cameraAnchorGuid)
            return nullptr;

        return viewer->GetMap()->GetCreature(session.cameraAnchorGuid);
    }

    static ActorTrack const* FindReplayActorTrackByGuid(MatchRecord const& match, uint64 actorGuid)
    {
        for (ActorTrack const& track : match.winnerActorTracks)
            if (track.guid == actorGuid && IsReplayActorTrackPlayable(track))
                return &track;
        for (ActorTrack const& track : match.loserActorTracks)
            if (track.guid == actorGuid && IsReplayActorTrackPlayable(track))
                return &track;
        return nullptr;
    }

    static uint32 GetPlayerEquippedItemDisplayId(Player* player, uint8 slot)
    {
        if (!player)
            return 0;

        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || !item->GetTemplate())
            return 0;

        return item->GetTemplate()->DisplayInfoID;
    }

    static ReplayActorAppearanceSnapshot BuildReplayActorAppearanceSnapshot(Player* player, bool winnerSide)
    {
        ReplayActorAppearanceSnapshot snapshot;
        if (!player)
            return snapshot;

        snapshot.guid = player->GetGUID().GetRawValue();
        snapshot.winnerSide = winnerSide;
        snapshot.playerClass = player->getClass();
        snapshot.race = player->getRace();
        snapshot.gender = player->getGender();
        snapshot.name = player->GetName();
        snapshot.displayId = player->GetDisplayId();
        snapshot.nativeDisplayId = player->GetNativeDisplayId();
        snapshot.mainhandDisplayId = GetPlayerEquippedItemDisplayId(player, EQUIPMENT_SLOT_MAINHAND);
        snapshot.offhandDisplayId = GetPlayerEquippedItemDisplayId(player, EQUIPMENT_SLOT_OFFHAND);
        snapshot.rangedDisplayId = GetPlayerEquippedItemDisplayId(player, EQUIPMENT_SLOT_RANGED);
        return snapshot;
    }

    static void CaptureReplayActorAppearanceSnapshots(Battleground* bg, MatchRecord& match, TeamId winnerTeamId)
    {
        match.actorAppearanceSnapshots.clear();

        if (!bg)
            return;

        for (auto const& playerPair : bg->GetPlayers())
        {
            Player* player = playerPair.second;
            if (!player || player->IsSpectator())
                continue;

            bool winnerSide = player->GetBgTeamId() == winnerTeamId;
            ReplayActorAppearanceSnapshot snapshot = BuildReplayActorAppearanceSnapshot(player, winnerSide);
            if (!snapshot.guid)
                continue;

            match.actorAppearanceSnapshots[snapshot.guid] = std::move(snapshot);
        }
    }

    static void PersistReplayActorAppearanceSnapshots(uint32 replayId, MatchRecord const& match)
    {
        if (!replayId)
            return;

        CharacterDatabase.Execute("DELETE FROM `character_arena_replay_actor_snapshot` WHERE `replay_id` = {}", replayId);

        for (auto const& pair : match.actorAppearanceSnapshots)
        {
            ReplayActorAppearanceSnapshot const& snapshot = pair.second;
            CharacterDatabase.Execute(
                "INSERT INTO `character_arena_replay_actor_snapshot` "
                "(`replay_id`, `actor_guid`, `winner_side`, `actor_name`, `player_class`, `race`, `gender`, `display_id`, `native_display_id`, `mainhand_display_id`, `offhand_display_id`, `ranged_display_id`) "
                "VALUES ({}, {}, {}, '{}', {}, {}, {}, {}, {}, {}, {}, {})",
                replayId,
                snapshot.guid,
                snapshot.winnerSide ? 1 : 0,
                EscapeSqlString(snapshot.name),
                uint32(snapshot.playerClass),
                uint32(snapshot.race),
                uint32(snapshot.gender),
                snapshot.displayId,
                snapshot.nativeDisplayId,
                snapshot.mainhandDisplayId,
                snapshot.offhandDisplayId,
                snapshot.rangedDisplayId
            );
        }
    }

    static void LoadReplayActorAppearanceSnapshots(MatchRecord& record, uint32 replayId)
    {
        record.actorAppearanceSnapshots.clear();

        if (!replayId)
            return;

        QueryResult result = CharacterDatabase.Query(
            "SELECT `actor_guid`, `winner_side`, `actor_name`, `player_class`, `race`, `gender`, `display_id`, `native_display_id`, `mainhand_display_id`, `offhand_display_id`, `ranged_display_id` "
            "FROM `character_arena_replay_actor_snapshot` WHERE `replay_id` = {}",
            replayId);
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                continue;

            ReplayActorAppearanceSnapshot snapshot;
            snapshot.guid = fields[0].Get<uint64>();
            snapshot.winnerSide = fields[1].Get<uint8>() != 0;
            snapshot.name = fields[2].Get<std::string>();
            snapshot.playerClass = fields[3].Get<uint8>();
            snapshot.race = fields[4].Get<uint8>();
            snapshot.gender = fields[5].Get<uint8>();
            snapshot.displayId = fields[6].Get<uint32>();
            snapshot.nativeDisplayId = fields[7].Get<uint32>();
            snapshot.mainhandDisplayId = fields[8].Get<uint32>();
            snapshot.offhandDisplayId = fields[9].Get<uint32>();
            snapshot.rangedDisplayId = fields[10].Get<uint32>();
            if (snapshot.guid)
                record.actorAppearanceSnapshots[snapshot.guid] = std::move(snapshot);
        }
        while (result->NextRow());
    }

    static ReplayActorAppearanceSnapshot const* FindReplayActorAppearanceSnapshot(MatchRecord const& match, uint64 actorGuid)
    {
        auto it = match.actorAppearanceSnapshots.find(actorGuid);
        if (it == match.actorAppearanceSnapshots.end())
            return nullptr;
        return &it->second;
    }

    static void ApplyReplayActorAppearanceToClone(Creature* clone, ReplayActorAppearanceSnapshot const* snapshot)
    {
        if (!clone || !snapshot)
            return;

        if (snapshot->nativeDisplayId)
            clone->SetNativeDisplayId(snapshot->nativeDisplayId);

        uint32 effectiveDisplayId = snapshot->displayId ? snapshot->displayId : snapshot->nativeDisplayId;
        if (effectiveDisplayId)
            clone->SetDisplayId(effectiveDisplayId);

        clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, snapshot->mainhandDisplayId);
        clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1, snapshot->offhandDisplayId);
        clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2, snapshot->rangedDisplayId);
    }

    static Creature* FindReplayClone(Player* viewer, ActiveReplaySession const& session, uint64 actorGuid)
    {
        if (!viewer || !viewer->GetMap())
            return nullptr;

        for (ReplayCloneBinding const& binding : session.cloneBindings)
            if (binding.actorGuid == actorGuid)
                return viewer->GetMap()->GetCreature(binding.cloneGuid);

        return nullptr;
    }

    static uint32 GetReplayCloneEntry()
    {
        return sConfigMgr->GetOption<uint32>("ArenaReplay.CloneScene.CloneEntry", 98501u);
    }

    static uint32 GetReplayCameraAnchorEntry()
    {
        return sConfigMgr->GetOption<uint32>("ArenaReplay.CloneScene.CameraAnchorEntry", 18793u);
    }

    static Creature* SummonReplaySceneUnit(Player* viewer, uint32 entry, float x, float y, float z, float o, bool hideNameplate = false)
    {
        if (!viewer)
            return nullptr;

        Creature* unit = viewer->SummonCreature(entry, x, y, z, o, TEMPSUMMON_MANUAL_DESPAWN, 0, nullptr, true);
        if (!unit)
            return nullptr;

        unit->SetPhaseMask(viewer->GetPhaseMask(), true);
        unit->SetReactState(REACT_PASSIVE);
        unit->SetFaction(35);
        unit->SetCanFly(true);
        unit->SetDisableGravity(true);
        unit->SetHover(true);
        unit->SetUInt32Value(UNIT_FIELD_FLAGS, unit->GetUInt32Value(UNIT_FIELD_FLAGS) | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_PACIFIED);
        if (hideNameplate)
            unit->SetVisible(false);
        return unit;
    }

    static Creature* EnsureReplayCameraAnchor(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!viewer || !viewer->GetMap())
            return nullptr;

        if (Creature* existing = FindReplayCameraAnchor(viewer, session))
            return existing;

        float x = viewer->GetPositionX();
        float y = viewer->GetPositionY();
        float z = viewer->GetPositionZ();
        float o = viewer->GetOrientation();

        uint32 flatIndex = 1;
        if (ActorTrack const* track = GetSelectedReplayActorTrack(const_cast<MatchRecord&>(match), session, &flatIndex))
        {
            bool haveFrame = false;
            ActorFrame frame = GetInterpolatedActorFrame(*track, 0, haveFrame);
            if (haveFrame)
            {
                x = frame.x;
                y = frame.y;
                z = frame.z + sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.FollowHeight", 1.75f);
                o = frame.o;
            }
        }

        Creature* anchor = SummonReplaySceneUnit(viewer, GetReplayCameraAnchorEntry(), x, y, z, o, true);
        if (!anchor)
            return nullptr;

        session.cameraAnchorGuid = anchor->GetGUID();
        return anchor;
    }

    static void BindReplayViewpoint(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!viewer || !sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.UseViewpoint", true))
            return;

        Creature* anchor = EnsureReplayCameraAnchor(viewer, match, session);
        if (!anchor)
            return;

        if (!session.viewpointBound)
        {
            viewer->SetViewpoint(anchor, true);
            session.viewpointBound = true;
        }

        if (ReplayDebugEnabled(ReplayDebugFlag::General) || ReplayDynamicObjectsDebugEnabled())
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][VIEWPOINT] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} bound={} anchorGuid={} result=ok",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                session.replayPhaseMask,
                session.viewpointBound ? 1 : 0,
                anchor->GetGUID().GetCounter());
        }
    }

    static void ClearReplayViewpoint(Player* viewer, ActiveReplaySession& session)
    {
        if (!viewer || !session.viewpointBound)
            return;

        if (Creature* anchor = FindReplayCameraAnchor(viewer, session))
            viewer->SetViewpoint(anchor, false);

        session.viewpointBound = false;
    }

    static void DespawnReplayActorClones(Player* viewer, ActiveReplaySession& session)
    {
        ClearReplayViewpoint(viewer, session);

        if (viewer && viewer->GetMap())
        {
            if (Creature* anchor = FindReplayCameraAnchor(viewer, session))
                anchor->DespawnOrUnsummon(Milliseconds(0));

            for (ReplayCloneBinding const& binding : session.cloneBindings)
                if (Creature* clone = viewer->GetMap()->GetCreature(binding.cloneGuid))
                    clone->DespawnOrUnsummon(Milliseconds(0));
        }

        session.cameraAnchorGuid.Clear();
        session.cloneBindings.clear();
        session.cloneSceneBuilt = false;
        session.nextCloneSyncMs = 0;
    }

    static bool BuildReplayActorCloneScene(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!viewer || !viewer->GetMap())
            return false;

        if (!sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.Enable", true))
            return false;

        if (session.cloneSceneBuilt)
            return !session.cloneBindings.empty();

        std::vector<ReplayActorSelectionRef> playable = BuildPlayableReplayActorSelections(match);
        for (ReplayActorSelectionRef const& ref : playable)
        {
            std::vector<ActorTrack> const* tracks = ref.winnerSide ? &match.winnerActorTracks : &match.loserActorTracks;
            if (!tracks || ref.trackIndex >= tracks->size())
                continue;

            ActorTrack const& track = (*tracks)[ref.trackIndex];
            if (!IsReplayActorTrackPlayable(track) || track.frames.empty())
                continue;

            ActorFrame const& frame = track.frames.front();
            Creature* clone = SummonReplaySceneUnit(viewer, GetReplayCloneEntry(), frame.x, frame.y, frame.z, frame.o, false);
            if (!clone)
                continue;

            ApplyReplayActorAppearanceToClone(clone, FindReplayActorAppearanceSnapshot(match, track.guid));
            session.cloneBindings.push_back({ track.guid, ref.winnerSide, clone->GetGUID() });
        }

        session.cloneSceneBuilt = true;
        return !session.cloneBindings.empty();
    }

    static void SyncReplayActorClones(Player* viewer, MatchRecord const& match, ActiveReplaySession& session, uint32 nowMs, bool forceImmediate = false)
    {
        if (!viewer || !viewer->GetMap() || session.teardownInProgress)
            return;

        if (!BuildReplayActorCloneScene(viewer, match, session))
            return;

        uint32 syncMs = std::max<uint32>(25u, sConfigMgr->GetOption<uint32>("ArenaReplay.CloneScene.SyncMs", 50));
        if (!forceImmediate && session.nextCloneSyncMs > nowMs)
            return;

        session.nextCloneSyncMs = nowMs + syncMs;

        for (ReplayCloneBinding const& binding : session.cloneBindings)
        {
            Creature* clone = viewer->GetMap()->GetCreature(binding.cloneGuid);
            ActorTrack const* track = FindReplayActorTrackByGuid(match, binding.actorGuid);
            if (!clone || !track)
                continue;

            bool haveFrame = false;
            ActorFrame frame = GetInterpolatedActorFrame(*track, nowMs, haveFrame);
            if (!haveFrame)
                continue;

            clone->NearTeleportTo(frame.x, frame.y, frame.z, frame.o);
        }
    }

    struct ReplayDynamicObjectTemplate
    {
        uint32 nativeMap = 0;
        uint32 replayMap = 0;
        uint32 entry = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float o = 0.0f;
        float rot0 = 0.0f;
        float rot1 = 0.0f;
        float rot2 = 0.0f;
        float rot3 = 0.0f;
        ReplayDynamicObjectRole role = RTG_REPLAY_OBJECT_GATE;
        bool required = false;
        bool invertState = false;
    };

    static ReplayDynamicObjectTemplate const ReplayDynamicObjectTemplates[] =
    {
        {559, 725, 183978, 4031.854f, 2966.833f, 12.0462f, -2.648788f, 0.0f, 0.0f, 0.9697962f, -0.2439165f, RTG_REPLAY_OBJECT_GATE, true, false},
        {559, 725, 183980, 4081.179f, 2874.970f, 12.00171f, 0.4928045f, 0.0f, 0.0f, 0.2439165f, 0.9697962f, RTG_REPLAY_OBJECT_GATE, true, false},
        {559, 725, 184663, 4009.189941f, 2895.250000f, 13.052700f, -1.448624f, 0.0f, 0.0f, 0.6626201f, -0.7489557f, RTG_REPLAY_OBJECT_BUFF, false, false},
        {559, 725, 184664, 4103.330078f, 2946.350098f, 13.051300f, -0.06981307f, 0.0f, 0.0f, 0.03489945f, -0.9993908f, RTG_REPLAY_OBJECT_BUFF, false, false},

        {562, 726, 183971, 6287.277f, 282.1877f, 3.810925f, -2.260201f, 0.0f, 0.0f, 0.9044551f, -0.4265689f, RTG_REPLAY_OBJECT_GATE, true, false},
        {562, 726, 183973, 6189.546f, 241.7099f, 3.101481f, 0.8813917f, 0.0f, 0.0f, 0.4265689f, 0.9044551f, RTG_REPLAY_OBJECT_GATE, true, false},
        {562, 726, 184663, 6249.042f, 275.3239f, 11.22033f, -1.448624f, 0.0f, 0.0f, 0.6626201f, -0.7489557f, RTG_REPLAY_OBJECT_BUFF, false, false},
        {562, 726, 184664, 6228.260f, 249.566f, 11.21812f, -0.06981307f, 0.0f, 0.0f, 0.03489945f, -0.9993908f, RTG_REPLAY_OBJECT_BUFF, false, false},

        {572, 727, 185918, 1293.561f, 1601.938f, 31.60557f, -1.457349f, 0.0f, 0.0f, -0.6658813f, 0.7460576f, RTG_REPLAY_OBJECT_GATE, true, false},
        {572, 727, 185917, 1278.648f, 1730.557f, 31.60557f, 1.684245f, 0.0f, 0.0f, 0.7460582f, 0.6658807f, RTG_REPLAY_OBJECT_GATE, true, false},
        {572, 727, 184663, 1328.719971f, 1632.719971f, 36.730400f, -1.448624f, 0.0f, 0.0f, 0.6626201f, -0.7489557f, RTG_REPLAY_OBJECT_BUFF, false, false},
        {572, 727, 184664, 1243.300049f, 1699.170044f, 34.872601f, -0.06981307f, 0.0f, 0.0f, 0.03489945f, -0.9993908f, RTG_REPLAY_OBJECT_BUFF, false, false},

        {617, 728, 192642, 1350.950f, 817.200f, 20.8096f, 3.150000f, 0.0f, 0.0f, 0.996270f, 0.0862864f, RTG_REPLAY_OBJECT_GATE, true, false},
        {617, 728, 192643, 1232.650f, 764.913f, 20.0729f, 6.300000f, 0.0f, 0.0f, 0.0310211f, -0.999519f, RTG_REPLAY_OBJECT_GATE, true, false},
        {617, 728, 194395, 1291.560f, 790.837f, 7.100f, 3.142380f, 0.0f, 0.0f, 0.694215f, -0.719768f, RTG_REPLAY_OBJECT_DS_WATER_COLLISION, false, false},
        {617, 728, 191877, 1291.560f, 790.837f, 7.100f, 3.142380f, 0.0f, 0.0f, 0.694215f, -0.719768f, RTG_REPLAY_OBJECT_DS_WATER_VISUAL, false, false},
        {617, 728, 184663, 1291.700f, 813.424f, 7.11472f, 4.64562f, 0.0f, 0.0f, 0.730314f, -0.683111f, RTG_REPLAY_OBJECT_BUFF, false, false},
        {617, 728, 184664, 1291.700f, 768.911f, 7.11472f, 1.55194f, 0.0f, 0.0f, 0.700409f, 0.713742f, RTG_REPLAY_OBJECT_BUFF, false, false},

        {618, 729, 194582, 763.536377f, -294.535767f, 0.505383f, 3.141593f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_ELEVATOR, true, false},
        {618, 729, 194586, 763.506348f, -273.873352f, 0.505383f, 0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_ELEVATOR, true, false},
        {618, 729, 184663, 735.551819f, -284.794678f, 28.276682f, 0.034906f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_BUFF, false, false},
        {618, 729, 184664, 791.224487f, -284.794464f, 28.276682f, 2.600535f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_BUFF, false, false},
        {618, 729, 192704, 743.543457f, -283.799469f, 28.286655f, 3.141593f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_FIRE, true, false},
        {618, 729, 192705, 782.971802f, -283.799469f, 28.286655f, 3.141593f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_FIRE, true, false},
        {618, 729, 192388, 743.711060f, -284.099609f, 27.542587f, 3.141593f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_FIRE_DOOR, true, false},
        {618, 729, 192387, 783.221252f, -284.133362f, 27.535686f, 0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_FIRE_DOOR, true, false},
        {618, 729, 192393, 763.664551f, -261.872986f, 26.686588f, 0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_GEAR, true, false},
        {618, 729, 192394, 763.578979f, -306.146149f, 26.665222f, 3.141593f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_GEAR, true, false},
        {618, 729, 192389, 700.722290f, -283.990662f, 39.517582f, 3.141593f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_PULLEY, true, false},
        {618, 729, 192390, 826.303833f, -283.996429f, 39.517582f, 0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_PULLEY, true, false},
        {618, 729, 194583, 763.632385f, -306.162384f, 25.909504f, 3.141593f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_PILLAR, true, false},
        {618, 729, 194584, 723.644287f, -284.493256f, 24.648525f, 3.141593f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_PILLAR, true, false},
        {618, 729, 194585, 763.611145f, -261.856750f, 25.909504f, 0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_PILLAR, true, false},
        {618, 729, 194587, 802.211609f, -284.493256f, 24.648525f, 0.000000f, 0.0f, 0.0f, 0.0f, 0.0f, RTG_REPLAY_OBJECT_RV_PILLAR, true, false},
    };

    static bool ReplayDynamicObjectsEnabled()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.Enable", true);
    }

    static bool ReplayDynamicObjectsDebugEnabled()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.Debug", true);
    }

    static uint32 SecondsToMs(uint32 seconds)
    {
        return seconds * IN_MILLISECONDS;
    }

    static uint32 ResolveReplayMapId(uint32 nativeMapId)
    {
        uint32 configuredMapId = 0;
        switch (nativeMapId)
        {
            case 559:
                configuredMapId = sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayMap.Nagrand", 725u);
                break;
            case 562:
                configuredMapId = sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayMap.BladesEdge", 726u);
                break;
            case 572:
                configuredMapId = sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayMap.RuinsOfLordaeron", 727u);
                break;
            case 617:
                configuredMapId = sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayMap.DalaranSewers", 728u);
                break;
            case 618:
                configuredMapId = sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayMap.RingOfValor", 729u);
                break;
            default:
                break;
        }

        if (configuredMapId != 0)
            return configuredMapId;

        if (!sConfigMgr->GetOption<bool>("ArenaReplay.ReplayMap.RequireResolved", true))
            return nativeMapId;

        return 0;
    }

    static char const* ReplayDynamicObjectRoleName(uint32 role)
    {
        switch (role)
        {
            case RTG_REPLAY_OBJECT_GATE:
                return "GATE";
            case RTG_REPLAY_OBJECT_BUFF:
                return "BUFF";
            case RTG_REPLAY_OBJECT_DS_WATER_COLLISION:
                return "DS_WATER_COLLISION";
            case RTG_REPLAY_OBJECT_DS_WATER_VISUAL:
                return "DS_WATER_VISUAL";
            case RTG_REPLAY_OBJECT_RV_ELEVATOR:
                return "RV_ELEVATOR";
            case RTG_REPLAY_OBJECT_RV_FIRE:
                return "RV_FIRE";
            case RTG_REPLAY_OBJECT_RV_FIRE_DOOR:
                return "RV_FIRE_DOOR";
            case RTG_REPLAY_OBJECT_RV_GEAR:
                return "RV_GEAR";
            case RTG_REPLAY_OBJECT_RV_PULLEY:
                return "RV_PULLEY";
            case RTG_REPLAY_OBJECT_RV_PILLAR:
                return "RV_PILLAR";
            default:
                return "UNKNOWN";
        }
    }

    static char const* ReplayDynamicObjectStateName(uint32 role, bool active)
    {
        switch (role)
        {
            case RTG_REPLAY_OBJECT_GATE:
            case RTG_REPLAY_OBJECT_RV_ELEVATOR:
            case RTG_REPLAY_OBJECT_RV_FIRE:
            case RTG_REPLAY_OBJECT_RV_FIRE_DOOR:
                return active ? "open" : "closed";
            case RTG_REPLAY_OBJECT_RV_PILLAR:
                return active ? "raised" : "lowered";
            case RTG_REPLAY_OBJECT_RV_GEAR:
            case RTG_REPLAY_OBJECT_RV_PULLEY:
                return active ? "active" : "rest";
            case RTG_REPLAY_OBJECT_BUFF:
                return active ? "active" : "inactive";
            case RTG_REPLAY_OBJECT_DS_WATER_COLLISION:
            case RTG_REPLAY_OBJECT_DS_WATER_VISUAL:
                return active ? "active" : "off";
            default:
                return active ? "active" : "ready";
        }
    }

    static bool ShouldIncludeReplayDynamicObjectTemplate(ActiveReplaySession const& session, ReplayDynamicObjectTemplate const& def)
    {
        if (def.nativeMap != session.nativeMapId)
            return false;

        if ((def.role == RTG_REPLAY_OBJECT_DS_WATER_COLLISION || def.role == RTG_REPLAY_OBJECT_DS_WATER_VISUAL) &&
            !sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.DalaranWaterfall.Enable", true))
            return false;

        if (session.nativeMapId == 618 && !sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.RingOfValor.Enable", true))
            return false;

        return true;
    }

    static bool IsReplayDynamicObjectRequired(ReplayDynamicObjectTemplate const& def)
    {
        if (def.role == RTG_REPLAY_OBJECT_DS_WATER_COLLISION)
            return sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.DalaranWaterfall.CollisionRequired", false);

        return def.required;
    }

    static ReplayDynamicObjectTemplate const* FindReplayDynamicObjectTemplate(ReplayDynamicObjectBinding const& binding)
    {
        for (ReplayDynamicObjectTemplate const& def : ReplayDynamicObjectTemplates)
            if (def.nativeMap == binding.nativeMap && def.entry == binding.entry && uint32(def.role) == binding.role)
                return &def;

        return nullptr;
    }

    static void LogReplayDynamicObjectEvent(char const* tag, ActiveReplaySession const& session, Player* viewer, ReplayDynamicObjectBinding const* binding, uint32 entry, uint32 role, char const* state, char const* result)
    {
        if (!ReplayDynamicObjectsDebugEnabled() && (!result || std::string(result) == "ok"))
            return;

        uint64 objectGuid = binding && binding->guid ? binding->guid.GetCounter() : 0;
        LOG_INFO("server.loading", "[RTG][REPLAY][{}] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} entry={} objectEntry={} role={} objectGuid={} state={} replayTimeMs={} result={}",
            tag,
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            entry,
            entry,
            ReplayDynamicObjectRoleName(role),
            objectGuid,
            state ? state : "none",
            session.replayPlaybackMs,
            result ? result : "unknown");
    }

    static void LogReplayDynamicTimeline(ActiveReplaySession const& session, Player* viewer, char const* role, char const* state, char const* result)
    {
        if (!ReplayDynamicObjectsDebugEnabled())
            return;

        LOG_INFO("server.loading", "[RTG][REPLAY][OBJECT_TIMELINE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} objectEntry=0 role={} objectGuid=0 state={} replayTimeMs={} result={}",
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            role ? role : "UNKNOWN",
            state ? state : "none",
            session.replayPlaybackMs,
            result ? result : "ok");
    }

    static GameObject* FindReplayDynamicObject(Player* viewer, ReplayDynamicObjectBinding const& binding)
    {
        if (!viewer || !viewer->GetMap() || !binding.guid)
            return nullptr;

        return viewer->GetMap()->GetGameObject(binding.guid);
    }

    static bool SetReplayDynamicObjectState(Player* viewer, ActiveReplaySession& session, ReplayDynamicObjectBinding& binding, bool active, bool force = false)
    {
        if (!force && binding.active == active)
            return true;

        GameObject* go = FindReplayDynamicObject(viewer, binding);
        if (!go)
        {
            LogReplayDynamicObjectEvent("OBJECT_STATE", session, viewer, &binding, binding.entry, binding.role, "missing", binding.required ? "hard_fail" : "missing_optional");
            return !binding.required;
        }

        ReplayDynamicObjectTemplate const* def = FindReplayDynamicObjectTemplate(binding);
        bool effectiveActive = def && def->invertState ? !active : active;
        go->SetGoState(effectiveActive ? GO_STATE_ACTIVE : GO_STATE_READY);
        binding.active = active;
        LogReplayDynamicObjectEvent("OBJECT_STATE", session, viewer, &binding, binding.entry, binding.role, ReplayDynamicObjectStateName(binding.role, active), "ok");
        return true;
    }

    static bool SetReplayDynamicObjectsByRole(Player* viewer, ActiveReplaySession& session, uint32 role, bool active, bool force = false)
    {
        bool ok = true;
        for (ReplayDynamicObjectBinding& binding : session.dynamicObjects)
        {
            if (binding.role != role || !binding.spawned || binding.cleaned)
                continue;

            if (!SetReplayDynamicObjectState(viewer, session, binding, active, force) && binding.required)
                ok = false;
        }
        return ok;
    }

    static bool GetFirstPlayableActorZ(MatchRecord const& match, float& z)
    {
        auto consider = [&z](std::vector<ActorTrack> const& tracks) -> bool
        {
            for (ActorTrack const& track : tracks)
            {
                if (!IsReplayActorTrackPlayable(track) || track.frames.empty())
                    continue;

                z = track.frames.front().z;
                return true;
            }

            return false;
        };

        return consider(match.winnerActorTracks) || consider(match.loserActorTracks);
    }

    static void LogRingReplayObjectSummary(ActiveReplaySession const& session, Player* viewer)
    {
        if (!ReplayDynamicObjectsDebugEnabled() || session.nativeMapId != 618)
            return;

        uint32 elevators = 0;
        uint32 pillars = 0;
        uint32 fire = 0;
        uint32 fireDoors = 0;
        uint32 gears = 0;
        uint32 pulleys = 0;
        uint32 buffs = 0;

        for (ReplayDynamicObjectBinding const& binding : session.dynamicObjects)
        {
            if (!binding.spawned || binding.cleaned)
                continue;

            switch (binding.role)
            {
                case RTG_REPLAY_OBJECT_RV_ELEVATOR:
                    ++elevators;
                    break;
                case RTG_REPLAY_OBJECT_RV_PILLAR:
                    ++pillars;
                    break;
                case RTG_REPLAY_OBJECT_RV_FIRE:
                    ++fire;
                    break;
                case RTG_REPLAY_OBJECT_RV_FIRE_DOOR:
                    ++fireDoors;
                    break;
                case RTG_REPLAY_OBJECT_RV_GEAR:
                    ++gears;
                    break;
                case RTG_REPLAY_OBJECT_RV_PULLEY:
                    ++pulleys;
                    break;
                case RTG_REPLAY_OBJECT_BUFF:
                    ++buffs;
                    break;
                default:
                    break;
            }
        }

        LOG_INFO("server.loading", "[RTG][REPLAY][RV_OBJECT_SPAWN] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} elevators={} pillars={} fire={} fireDoors={} gears={} pulleys={} buffs={} result=ok",
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            elevators,
            pillars,
            fire,
            fireDoors,
            gears,
            pulleys,
            buffs);
    }

    static void LogRingReplayState(ActiveReplaySession const& session, Player* viewer, char const* result)
    {
        if (!ReplayDynamicObjectsDebugEnabled() || session.nativeMapId != 618)
            return;

        LOG_INFO("server.loading", "[RTG][REPLAY][RV_STATE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} derivedInitialState={} firstActorZ={} rvState={} pillarToggle={} replayTimeMs={} result={}",
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            session.rvInitialUpper ? "upper" : "lower",
            session.rvFirstActorZ,
            session.rvState,
            session.rvPillarToggleState,
            session.replayPlaybackMs,
            result ? result : "ok");
    }

    static bool SpawnReplayDynamicObjects(Player* viewer, ActiveReplaySession& session)
    {
        if (!ReplayDynamicObjectsEnabled())
            return true;

        if (session.dynamicObjectsSpawned)
            return true;

        if (!viewer || !viewer->GetMap() || viewer->GetMapId() != session.replayMapId)
            return false;

        bool hardFailure = false;
        for (ReplayDynamicObjectTemplate const& def : ReplayDynamicObjectTemplates)
        {
            if (!ShouldIncludeReplayDynamicObjectTemplate(session, def))
                continue;

            bool required = IsReplayDynamicObjectRequired(def);
            GameObject* go = viewer->SummonGameObject(def.entry, def.x, def.y, def.z, def.o, def.rot0, def.rot1, def.rot2, def.rot3, 0);
            if (!go)
            {
                ReplayDynamicObjectBinding failed;
                failed.entry = def.entry;
                failed.nativeMap = session.nativeMapId;
                failed.replayMap = session.replayMapId;
                failed.role = def.role;
                failed.required = required;
                LogReplayDynamicObjectEvent("OBJECT_SPAWN", session, viewer, &failed, def.entry, def.role, "spawn", required ? "hard_fail" : "missing_optional");
                if (required)
                    hardFailure = true;
                continue;
            }

            go->SetPhaseMask(session.replayPhaseMask ? session.replayPhaseMask : viewer->GetPhaseMask(), true);
            go->SetLootState(GO_READY);
            go->SetGoState(GO_STATE_READY);

            ReplayDynamicObjectBinding binding;
            binding.guid = go->GetGUID();
            binding.entry = def.entry;
            binding.nativeMap = session.nativeMapId;
            binding.replayMap = session.replayMapId;
            binding.role = def.role;
            binding.required = required;
            binding.active = false;
            binding.spawned = true;
            binding.cleaned = false;
            session.dynamicObjects.push_back(binding);
            LogReplayDynamicObjectEvent("OBJECT_SPAWN", session, viewer, &session.dynamicObjects.back(), def.entry, def.role, "spawn", "ok");
        }

        session.dynamicObjectsSpawned = true;
        LogRingReplayObjectSummary(session, viewer);
        return !hardFailure;
    }

    static bool InitializeReplayDynamicObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!ReplayDynamicObjectsEnabled())
            return true;

        if (session.dynamicObjectsInitialized)
            return true;

        if (!sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.SpawnBeforeClones", true) && ReplayDynamicObjectsDebugEnabled())
            LogReplayDynamicTimeline(session, viewer, "SPAWN_ORDER", "before_clones", "forced");

        if (!SpawnReplayDynamicObjects(viewer, session))
            return false;

        uint32 gateDelayMs = sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.GateOpenDelayMs", 0u);
        if (gateDelayMs == 0 && !SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_GATE, true))
            return false;

        if (session.nativeMapId == 617 && sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.DalaranWaterfall.Enable", true))
        {
            session.dsWaterState = 0;
            session.dsNextEventMs = SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.DalaranWaterfall.FirstDelaySeconds", 45u));
        }

        if (session.nativeMapId == 618 && sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.RingOfValor.Enable", true))
        {
            float firstActorZ = 0.0f;
            bool haveFirstActorZ = GetFirstPlayableActorZ(match, firstActorZ);
            session.rvFirstActorZ = haveFirstActorZ ? firstActorZ : 0.0f;
            session.rvInitialUpper = haveFirstActorZ &&
                sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.RingOfValor.DeriveInitialStateFromFirstActorZ", true) &&
                firstActorZ >= 20.0f;

            if (!SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_ELEVATOR, true))
                return false;

            session.rvState = 0;
            session.rvPillarToggleState = 0;
            session.rvNextEventMs = sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.RingOfValor.FirstTimerMs", 20500u);
            LogRingReplayState(session, viewer, "initialized");
        }

        session.dynamicObjectsInitialized = true;
        return true;
    }

    static void ForceRingReplayClonesToUpperFloor(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!viewer || !viewer->GetMap())
            return;

        for (ReplayCloneBinding const& binding : session.cloneBindings)
        {
            Creature* clone = FindReplayClone(viewer, session, binding.actorGuid);
            ActorTrack const* track = FindReplayActorTrackByGuid(match, binding.actorGuid);
            if (!clone || !track)
                continue;

            bool haveFrame = false;
            ActorFrame frame = GetInterpolatedActorFrame(*track, session.replayPlaybackMs, haveFrame);
            if (!haveFrame || frame.z >= 20.0f)
                continue;

            clone->NearTeleportTo(frame.x, frame.y, 28.276682f, frame.o);
            if (ReplayDynamicObjectsDebugEnabled())
            {
                LOG_INFO("server.loading", "[RTG][REPLAY][RV_STATE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} actorGuid={} actorZ={} forcedZ=28.276682 replayTimeMs={} result=forced_upper_z",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    session.replayPhaseMask,
                    binding.actorGuid,
                    frame.z,
                    session.replayPlaybackMs);
            }
        }
    }

    static void UpdateDalaranReplayWater(Player* viewer, ActiveReplaySession& session)
    {
        if (session.nativeMapId != 617 || !sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.DalaranWaterfall.Enable", true))
            return;

        uint32 warningMs = SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.DalaranWaterfall.WarningSeconds", 5u));
        uint32 durationMs = SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.DalaranWaterfall.DurationSeconds", 30u));

        while (session.dsNextEventMs != 0 && session.replayPlaybackMs >= session.dsNextEventMs)
        {
            if (session.dsWaterState == 0)
            {
                session.dsWaterState = 1;
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_VISUAL, true);
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_COLLISION, false);
                LogReplayDynamicTimeline(session, viewer, "DS_WATER", "warning", "ok");
                session.dsNextEventMs += std::max<uint32>(1u, warningMs);
            }
            else if (session.dsWaterState == 1)
            {
                session.dsWaterState = 2;
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_VISUAL, true);
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_COLLISION, true);
                LogReplayDynamicTimeline(session, viewer, "DS_WATER", "active", "ok");
                session.dsNextEventMs += std::max<uint32>(1u, durationMs);
            }
            else if (session.dsWaterState == 2)
            {
                session.dsWaterState = 3;
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_COLLISION, false);
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_VISUAL, false);
                LogReplayDynamicTimeline(session, viewer, "DS_WATER", "off", "ok");
                session.dsNextEventMs = 0;
            }
            else
                session.dsNextEventMs = 0;
        }
    }

    static void ToggleRingReplayPillars(Player* viewer, ActiveReplaySession& session)
    {
        session.rvPillarToggleState = session.rvPillarToggleState ? 0 : 1;
        bool active = session.rvPillarToggleState != 0;
        SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_PILLAR, active);
        SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_GEAR, active);
        SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_PULLEY, active);
        LogReplayDynamicTimeline(session, viewer, "RV_PILLARS", active ? "active" : "rest", "ok");
    }

    static void UpdateRingReplayObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (session.nativeMapId != 618 || !sConfigMgr->GetOption<bool>("ArenaReplay.DynamicObjects.RingOfValor.Enable", true))
            return;

        while (session.rvNextEventMs != 0 && session.replayPlaybackMs >= session.rvNextEventMs)
        {
            uint32 eventMs = session.rvNextEventMs;
            if (session.rvState == 0)
            {
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_FIRE, true);
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_FIRE_DOOR, true);
                ForceRingReplayClonesToUpperFloor(viewer, match, session);
                LogReplayDynamicTimeline(session, viewer, "RV_FIRE", "open", "ok");
                session.rvState = 1;
                session.rvNextEventMs = eventMs + std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.RingOfValor.CloseFireMs", 5000u));
            }
            else if (session.rvState == 1)
            {
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_FIRE, false);
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_FIRE_DOOR, false);
                LogReplayDynamicTimeline(session, viewer, "RV_FIRE", "closed", "ok");
                session.rvState = 2;
                session.rvNextEventMs = eventMs + std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.RingOfValor.FireToPillarMs", 20000u));
            }
            else
            {
                ToggleRingReplayPillars(viewer, session);
                session.rvState = 3;
                session.rvNextEventMs = eventMs + std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.RingOfValor.PillarSwitchMs", 25000u));
            }
        }

        if (ReplayDynamicObjectsDebugEnabled() && session.replayPlaybackMs <= 30000 && (session.nextDynamicObjectLogMs == 0 || session.replayPlaybackMs >= session.nextDynamicObjectLogMs))
        {
            session.nextDynamicObjectLogMs = session.replayPlaybackMs + 1000;
            LogRingReplayState(session, viewer, "debug_first_30s");
        }
    }

    static void UpdateReplayDynamicObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!ReplayDynamicObjectsEnabled() || !session.dynamicObjectsInitialized || session.teardownInProgress)
            return;

        uint32 gateDelayMs = sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.GateOpenDelayMs", 0u);
        if (session.replayPlaybackMs >= gateDelayMs)
            SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_GATE, true);

        uint32 buffDelayMs = SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.DynamicObjects.BuffDelaySeconds", 90u));
        if (session.replayPlaybackMs >= buffDelayMs)
            SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_BUFF, true);

        UpdateDalaranReplayWater(viewer, session);
        UpdateRingReplayObjects(viewer, match, session);
    }

    static void DespawnReplayDynamicObjects(Player* viewer, ActiveReplaySession& session)
    {
        if (session.dynamicObjects.empty())
        {
            session.dynamicObjectsSpawned = false;
            session.dynamicObjectsInitialized = false;
            return;
        }

        for (ReplayDynamicObjectBinding& binding : session.dynamicObjects)
        {
            if (binding.cleaned)
                continue;

            GameObject* go = FindReplayDynamicObject(viewer, binding);
            char const* result = "missing";
            if (go)
            {
                go->SetRespawnTime(0);
                go->Delete();
                result = "ok";
            }

            LogReplayDynamicObjectEvent("OBJECT_CLEANUP", session, viewer, &binding, binding.entry, binding.role, ReplayDynamicObjectStateName(binding.role, binding.active), result);
            binding.cleaned = true;
            binding.spawned = false;
            binding.guid.Clear();
        }

        session.dynamicObjects.clear();
        session.dynamicObjectsSpawned = false;
        session.dynamicObjectsInitialized = false;
        session.nextDynamicObjectLogMs = 0;
        session.rvState = 0;
        session.rvNextEventMs = 0;
        session.rvPillarToggleState = 0;
        session.rvInitialUpper = false;
        session.rvFirstActorZ = 0.0f;
        session.dsWaterState = 0;
        session.dsNextEventMs = 0;
    }

    static void RestoreReplayViewerState(Player* player, ActiveReplaySession const& session)
    {
        if (!player)
            return;

        player->SetCanFly(false);
        player->SetDisableGravity(false);
        player->SetHover(false);
        player->SetClientControl(player, true);
        player->StopMoving();
        player->SetIsSpectator(false);

        if (session.viewerHidden || !player->IsVisible())
            player->SetVisible(true);
    }

    static bool ShouldPreferBattlegroundLeave(ActiveReplaySession const& session)
    {
        return session.battlegroundInstanceId != 0 && session.priorBattlegroundInstanceId != session.battlegroundInstanceId;
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
        uint32 nowMs = bg ? bg->GetStartTime() : session.replayPlaybackMs;
        if (!session.teardownRequested)
        {
            session.teardownRequested = true;
            session.teardownRequestedAtMs = nowMs;
            session.teardownExecuteAtMs = nowMs + delayMs;
            session.teardownInProgress = true;
            session.lastTeardownReason = reason ? reason : "unknown";
            session.teardownPreferBattlegroundLeave = bg && ShouldPreferBattlegroundLeave(session);

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
                   << " playerPhase=" << player->GetPhaseMask()
                   << " replayPhase=" << session.replayPhaseMask
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
               << " playerPhase=" << player->GetPhaseMask()
               << " replayPhase=" << session.replayPhaseMask
               << " replayBgEnded=" << (session.replayBgEnded ? 1 : 0);
            ReplayLog(ReplayDebugFlag::Teardown, &session, player, "EXIT_BEGIN", ss.str());
        }

        auto liveIt = activeReplaySessions.find(viewerKey);
        if (liveIt != activeReplaySessions.end())
        {
            ResetActorReplayView(player, liveIt->second);
            DespawnReplayDynamicObjects(player, liveIt->second);
            DespawnReplayActorClones(player, liveIt->second);
        }

        RestoreReplayViewerState(player, session);
        RestoreReplayViewerPhase(player, session);
        loadedReplays.erase(viewerKey);

        std::string action = "return_to_anchor";
        if (session.anchorMapId != 0)
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
               << " playerBg=" << player->GetBattlegroundId()
               << " playerPhase=" << player->GetPhaseMask();
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
               << " playerPhase=" << player->GetPhaseMask()
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
        {
            ResetActorReplayView(player, it->second);
            DespawnReplayDynamicObjects(player, it->second);
            DespawnReplayActorClones(player, it->second);
            RestoreReplayViewerState(player, it->second);
            RestoreReplayViewerPhase(player, it->second);
        }
        else
        {
            player->SetCanFly(false);
            player->SetDisableGravity(false);
            player->SetHover(false);
            player->SetVisible(true);
            player->SetIsSpectator(false);
            player->SetClientControl(player, true);
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

        ClearReplayViewpoint(replayer, session);
        session.actorSpectateActive = false;
        session.nextActorTeleportMs = 0;
        session.lastAppliedActorGuid = 0;
        session.lastAppliedActorFlatIndex = 0;
        session.cameraStallCount = 0;
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

        SyncReplayActorClones(replayer, match, session, nowMs, forceImmediate);
        BindReplayViewpoint(replayer, match, session);

        if (!forceImmediate && session.replayWarmupUntilMs > nowMs)
            return false;

        uint32 flatIndex = 1;
        ActorTrack const* track = GetSelectedReplayActorTrack(match, session, &flatIndex);
        if (!track || track->frames.empty())
        {
            ResetActorReplayView(replayer, session);
            return false;
        }

        if (!session.viewerHidden || replayer->IsVisible())
        {
            replayer->SetVisible(false);
            session.viewerHidden = true;
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
        float targetO = frame.o;

        float dx = replayer->GetPositionX() - targetX;
        float dy = replayer->GetPositionY() - targetY;
        float dz = replayer->GetPositionZ() - targetZ;
        float distSq = dx * dx + dy * dy + dz * dz;
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

        if (Creature* anchor = EnsureReplayCameraAnchor(replayer, match, session))
        {
            float adx = anchor->GetPositionX() - targetX;
            float ady = anchor->GetPositionY() - targetY;
            float adz = anchor->GetPositionZ() - targetZ;
            float anchorDistSq = adx * adx + ady * ady + adz * adz;
            if (forceImmediate || anchorDistSq > (snapDistance * snapDistance) || actorChanged)
                anchor->NearTeleportTo(targetX, targetY, targetZ, targetO);
            else
                anchor->SetFacingTo(targetO);
        }

        session.lastCameraX = targetX;
        session.lastCameraY = targetY;
        session.lastCameraZ = targetZ;
        session.lastCameraO = targetO;
        session.actorSpectateActive = true;
        session.replayMovementStabilized = true;
        session.lastAppliedActorGuid = track->guid;
        session.lastAppliedActorFlatIndex = flatIndex;
        if (ReplayDebugEnabled(ReplayDebugFlag::Actors) && (forceImmediate || ReplayDebugVerbose()))
        {
            std::ostringstream ss;
            ss << "force=" << (forceImmediate ? 1 : 0)
               << " actorGuid=" << track->guid
               << " actorName=" << track->name
               << " actorIndex=" << flatIndex << '/' << GetReplayActorTotalCount(match)
               << " x=" << targetX << " y=" << targetY << " z=" << targetZ << " o=" << frame.o
               << " spectatorShell=1 viewpoint=" << (session.viewpointBound ? 1 : 0);
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
        session.priorPhaseMask = player->GetPhaseMask();
        session.battlegroundInstanceId = replayBgInstanceId;
        session.replayPhaseMask = 0;
        session.replayPhaseApplied = false;
        session.replayId = replayId;
        session.anchorMapId = player->GetMapId();
        session.anchorPosition.Relocate(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        session.nextAnchorEnforceMs = 0;
        session.nextActorTeleportMs = 0;
        session.movementLocked = false;
        session.viewerWasParticipant = false;
        session.actorSpectateActive = false;
        session.actorSpectateOnWinnerTeam = true;
        session.actorTrackIndex = 0;
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
        session.cloneSceneBuilt = false;
        session.nextCloneSyncMs = 0;
        session.cloneBindings.clear();
        session.cameraAnchorGuid.Clear();
        session.viewpointBound = false;
        session.spectatorShellActive = false;
        session.nativeMapId = 0;
        session.replayMapId = 0;
        session.replaySpawnPosition.Relocate(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        session.sandboxTeleportIssued = false;
        session.awaitingReplayMapAttach = false;
        session.replayMapAttached = false;
        session.replayAttachDeadlineMs = 0;
        session.nextAttachLogMs = 0;
        session.replayPlaybackMs = 0;
        session.replayLastServerMs = GetReplayNowMs();
        session.dynamicObjects.clear();
        session.dynamicObjectsSpawned = false;
        session.dynamicObjectsInitialized = false;
        session.nextDynamicObjectLogMs = 0;
        session.rvState = 0;
        session.rvNextEventMs = 0;
        session.rvPillarToggleState = 0;
        session.rvInitialUpper = false;
        session.rvFirstActorZ = 0.0f;
        session.dsWaterState = 0;
        session.dsNextEventMs = 0;
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
               << " priorPhase=" << session.priorPhaseMask
               << " replayPhase=" << session.replayPhaseMask
               << " anchorMap=" << session.anchorMapId
               << " anchorX=" << session.anchorPosition.GetPositionX()
               << " anchorY=" << session.anchorPosition.GetPositionY()
               << " anchorZ=" << session.anchorPosition.GetPositionZ();
            ReplayLog(ReplayDebugFlag::General, &session, player, "LOCK", ss.str());
        }

        player->SetIsSpectator(true);

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.LockMovement", true))
        {
            player->SetClientControl(player, false);
            session.movementLocked = true;
        }

        player->SetVisible(false);
        session.viewerHidden = true;
        session.spectatorShellActive = true;
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

        uint64 replayOwnerKey = bgReplayIds.at(bg->GetInstanceID());
        auto sessionItEarly = activeReplaySessions.find(replayOwnerKey);
        if (sessionItEarly != activeReplaySessions.end() && sessionItEarly->second.sandboxTeleportIssued)
            return;

        if (!bg->isArena() && !sConfigMgr->GetOption<bool>("ArenaReplay.SaveBattlegrounds", true))
            return;

        if (!bg->isRated() && !sConfigMgr->GetOption<bool>("ArenaReplay.SaveUnratedArenas", true))
            return;

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

        if (session.awaitingReplayMapAttach || !session.replayMapAttached)
        {
            if (session.replayAttachDeadlineMs == 0)
                session.replayAttachDeadlineMs = bg->GetStartTime() + 5000;

            bool attached = IsReplayViewerMapAttached(replayer, bg, session);
            if (!attached)
            {
                if (ReplayDebugEnabled(ReplayDebugFlag::General) && (session.nextAttachLogMs == 0 || bg->GetStartTime() >= session.nextAttachLogMs))
                {
                    session.nextAttachLogMs = bg->GetStartTime() + 1000;
                    std::ostringstream ss;
                    ss << "playerMap=" << replayer->GetMapId()
                       << " replayMap=" << session.replayMapId
                       << " playerBg=" << replayer->GetBattlegroundId()
                       << " sessionBg=" << session.battlegroundInstanceId
                       << " inBattleground=" << (replayer->InBattleground() ? 1 : 0)
                       << " playerPhase=" << replayer->GetPhaseMask()
                       << " replayPhase=" << session.replayPhaseMask
                       << " hasBg=" << (replayer->GetBattleground() ? 1 : 0)
                       << " isBgMap=" << ((replayer->GetMap() && replayer->GetMap()->IsBattlegroundOrArena()) ? 1 : 0)
                       << " nowMs=" << bg->GetStartTime()
                       << " deadlineMs=" << session.replayAttachDeadlineMs;
                    ReplayLog(ReplayDebugFlag::General, &session, replayer, "ATTACH_WAIT", ss.str());
                }

                if (bg->GetStartTime() >= session.replayAttachDeadlineMs)
                {
                    if (ReplayDebugEnabled(ReplayDebugFlag::Teardown))
                    {
                        std::ostringstream ss;
                        ss << "playerMap=" << replayer->GetMapId()
                           << " replayMap=" << session.replayMapId
                           << " playerBg=" << replayer->GetBattlegroundId()
                           << " sessionBg=" << session.battlegroundInstanceId
                           << " inBattleground=" << (replayer->InBattleground() ? 1 : 0)
                           << " nowMs=" << bg->GetStartTime();
                        ReplayLog(ReplayDebugFlag::Teardown, &session, replayer, "ATTACH_TIMEOUT", ss.str());
                    }
                    RequestReplayTeardown(replayer, bg, "attach_timeout", 0);
                }
                return;
            }

            session.awaitingReplayMapAttach = false;
            session.replayMapAttached = true;
            session.replayAttachDeadlineMs = 0;
            session.nextAttachLogMs = 0;
            session.replayWarmupUntilMs = bg->GetStartTime() + 1000;

            if (ReplayDebugEnabled(ReplayDebugFlag::General))
            {
                std::ostringstream ss;
                ss << "playerMap=" << replayer->GetMapId()
                   << " replayMap=" << session.replayMapId
                   << " playerBg=" << replayer->GetBattlegroundId()
                   << " sessionBg=" << session.battlegroundInstanceId
                   << " inBattleground=" << (replayer->InBattleground() ? 1 : 0)
                   << " nowMs=" << bg->GetStartTime();
                ReplayLog(ReplayDebugFlag::General, &session, replayer, "ATTACH_OK", ss.str());
            }
            else if (ReplayDynamicObjectsDebugEnabled())
            {
                LOG_INFO("server.loading", "[RTG][REPLAY][ATTACH_OK] replay={} viewerGuid={} nativeMap={} playerMap={} replayMap={} playerBg={} sessionBg={} inBattleground={} playerPhase={} replayPhase={} result=ok",
                    session.replayId,
                    replayer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    replayer->GetMapId(),
                    session.replayMapId,
                    replayer->GetBattlegroundId(),
                    session.battlegroundInstanceId,
                    replayer->InBattleground() ? 1 : 0,
                    replayer->GetPhaseMask(),
                    session.replayPhaseMask);
            }

            if (!InitializeReplayDynamicObjects(replayer, match, session))
            {
                DespawnReplayDynamicObjects(replayer, session);
                if (replayer->GetSession())
                    ChatHandler(replayer->GetSession()).PSendSysMessage("Replay arena objects could not be spawned.");
                RequestReplayTeardown(replayer, bg, "dynamic_object_spawn_failed", 0);
                return;
            }

            bool clonesOk = BuildReplayActorCloneScene(replayer, match, session);
            LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_SCENE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} clones={} result={}",
                session.replayId,
                replayer->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                session.replayPhaseMask,
                session.cloneBindings.size(),
                clonesOk ? "ok" : "fail");
            if (!clonesOk && sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.Enable", true))
            {
                DespawnReplayDynamicObjects(replayer, session);
                RequestReplayTeardown(replayer, bg, "clone_scene_failed", 0);
                return;
            }

            EnsureReplayCameraAnchor(replayer, match, session);
            BindReplayViewpoint(replayer, match, session);
            ApplyActorReplayView(replayer, match, session, bg->GetStartTime(), true);
            SendReplayHudPov(replayer, match, session, true);
            session.nextHudWatcherSyncMs = 0;
            SendReplayHudWatchers(bg, replayer, match, session, true);
        }

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
            if (session.replayMapAttached && session.replayWarmupUntilMs == 1500)
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
            session.replayPlaybackMs = bg->GetStartTime();
            SyncReplayActorClones(replayer, match, session, bg->GetStartTime());
            UpdateReplayDynamicObjects(replayer, match, session);
            bool actorViewApplied = ApplyActorReplayView(replayer, match, session, bg->GetStartTime());
            SendReplayHudPov(replayer, match, session, false);
            SendReplayHudWatchers(bg, replayer, match, session, false);
            if (!actorViewApplied && session.nextAnchorEnforceMs <= bg->GetStartTime())
            {
                session.nextAnchorEnforceMs = bg->GetStartTime() + 500;

                if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.AnchorViewer", true) && replayer->GetMapId() == bg->GetMapId())
                    ApplyActorReplayView(replayer, match, session, bg->GetStartTime(), true);
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
        CaptureReplayActorAppearanceSnapshots(bg, match, winnerTeamId);

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

        PersistReplayActorAppearanceSnapshots(insertedReplayId, match);

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

        ActorFrame bootstrapFrame;
        uint64 bootstrapActorGuid = 0;
        bool bootstrapWinnerSide = true;
        if (!GetReplayBootstrapFrame(record, bootstrapFrame, bootstrapActorGuid, bootstrapWinnerSide))
        {
            handler.PSendSysMessage("Replay has no playable actor frames.");
            return false;
        }

        LockReplayViewerControl(player, replayId, 0);
        ActiveReplaySession& replaySession = activeReplaySessions[player->GetGUID().GetCounter()];
        if (!PrepareReplayMapSandbox(player, record, replaySession))
        {
            bool mapResolveFailed = replaySession.nativeMapId != 0 && replaySession.replayMapId == 0;
            RestoreReplayViewerState(player, replaySession);
            RestoreReplayViewerPhase(player, replaySession);
            activeReplaySessions.erase(player->GetGUID().GetCounter());
            loadedReplays.erase(player->GetGUID().GetCounter());
            if (!mapResolveFailed)
                handler.PSendSysMessage("Replay sandbox could not allocate a private viewer phase or target replay map.");
            return false;
        }

        replaySession.teardownPreferBattlegroundLeave = false;
        replaySession.viewerWasParticipant = viewerWasParticipant;
        replaySession.actorSpectateOnWinnerTeam = sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.StartOnWinnerTeam", true);
        replaySession.actorTrackIndex = 0;
        replaySession.nextActorTeleportMs = 0;
        replaySession.replaySpawnPosition.Relocate(bootstrapFrame.x, bootstrapFrame.y, bootstrapFrame.z + 2.0f, bootstrapFrame.o);
        replaySession.sandboxTeleportIssued = true;
        replaySession.awaitingReplayMapAttach = true;
        replaySession.replayMapAttached = false;
        replaySession.replayAttachDeadlineMs = GetReplayNowMs() + 5000;
        replaySession.nextAttachLogMs = 0;
        replaySession.replayPlaybackMs = 0;
        replaySession.replayLastServerMs = GetReplayNowMs();
        replaySession.replayWarmupUntilMs = 1000;

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

        player->TeleportTo(replaySession.replayMapId,
            replaySession.replaySpawnPosition.GetPositionX(),
            replaySession.replaySpawnPosition.GetPositionY(),
            replaySession.replaySpawnPosition.GetPositionZ(),
            replaySession.replaySpawnPosition.GetOrientation());

        if (ReplayDebugEnabled(ReplayDebugFlag::General))
        {
            std::ostringstream ss;
            ss << "viewerWasParticipant=" << (viewerWasParticipant ? 1 : 0)
               << " nativeMap=" << replaySession.nativeMapId
               << " replayBg=" << replaySession.battlegroundInstanceId
               << " replayMap=" << replaySession.replayMapId
               << " playerBg=" << player->GetBattlegroundId()
               << " sessionBg=" << replaySession.battlegroundInstanceId
               << " inBattleground=" << (player->InBattleground() ? 1 : 0)
               << " replayPhase=" << replaySession.replayPhaseMask
               << " playerPhase=" << player->GetPhaseMask()
               << " spawnX=" << replaySession.replaySpawnPosition.GetPositionX()
               << " spawnY=" << replaySession.replaySpawnPosition.GetPositionY()
               << " spawnZ=" << replaySession.replaySpawnPosition.GetPositionZ()
               << " actorGuid=" << bootstrapActorGuid;
            ReplayLog(ReplayDebugFlag::General, &replaySession, player, "ENTER_SANDBOX", ss.str());
        }
        else if (ReplayDynamicObjectsDebugEnabled())
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][ENTER_SANDBOX] replay={} viewerGuid={} nativeMap={} replayMap={} playerBg={} sessionBg={} inBattleground={} replayPhase={} playerPhase={} spawnX={} spawnY={} spawnZ={} actorGuid={} result=ok",
                replaySession.replayId,
                player->GetGUID().GetCounter(),
                replaySession.nativeMapId,
                replaySession.replayMapId,
                player->GetBattlegroundId(),
                replaySession.battlegroundInstanceId,
                player->InBattleground() ? 1 : 0,
                replaySession.replayPhaseMask,
                player->GetPhaseMask(),
                replaySession.replaySpawnPosition.GetPositionX(),
                replaySession.replaySpawnPosition.GetPositionY(),
                replaySession.replaySpawnPosition.GetPositionZ(),
                bootstrapActorGuid);
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
        LoadReplayActorAppearanceSnapshots(record, matchId);

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

namespace
{
    static void ProcessReplaySandboxSessions(uint32 diff)
    {
        uint32 nowMs = GetReplayNowMs();
        std::vector<uint64> viewers;
        viewers.reserve(activeReplaySessions.size());
        for (auto const& pair : activeReplaySessions)
            viewers.push_back(pair.first);

        for (uint64 viewerKey : viewers)
        {
            auto sessionIt = activeReplaySessions.find(viewerKey);
            if (sessionIt == activeReplaySessions.end())
                continue;

            ActiveReplaySession& session = sessionIt->second;
            Player* replayer = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(viewerKey));
            if (!replayer)
                continue;

            auto matchIt = loadedReplays.find(viewerKey);
            if (matchIt == loadedReplays.end())
            {
                RequestReplayTeardown(replayer, nullptr, "missing_loaded_replay", 0);
                if (session.teardownRequested)
                    PerformReplayTeardown(replayer, nullptr);
                continue;
            }

            MatchRecord& match = matchIt->second;

            uint32 deltaMs = diff;
            if (session.replayLastServerMs != 0 && nowMs > session.replayLastServerMs)
                deltaMs = std::min<uint32>(250u, nowMs - session.replayLastServerMs);
            session.replayLastServerMs = nowMs;

            if (session.awaitingReplayMapAttach || !session.replayMapAttached)
            {
                if (!IsReplayViewerMapAttached(replayer, nullptr, session))
                {
                    if (ReplayDebugEnabled(ReplayDebugFlag::General) && (session.nextAttachLogMs == 0 || nowMs >= session.nextAttachLogMs))
                    {
                        session.nextAttachLogMs = nowMs + 1000;
                        std::ostringstream ss;
                        ss << "playerMap=" << replayer->GetMapId()
                           << " replayMap=" << session.replayMapId
                           << " playerBg=" << replayer->GetBattlegroundId()
                           << " sessionBg=" << session.battlegroundInstanceId
                           << " inBattleground=" << (replayer->InBattleground() ? 1 : 0)
                           << " playerPhase=" << replayer->GetPhaseMask()
                           << " replayPhase=" << session.replayPhaseMask
                           << " hasBg=" << (replayer->GetBattleground() ? 1 : 0)
                           << " isBgMap=" << ((replayer->GetMap() && replayer->GetMap()->IsBattlegroundOrArena()) ? 1 : 0)
                           << " nowMs=" << nowMs
                           << " deadlineMs=" << session.replayAttachDeadlineMs;
                        ReplayLog(ReplayDebugFlag::General, &session, replayer, "ATTACH_WAIT", ss.str());
                    }

                    if (session.replayAttachDeadlineMs != 0 && nowMs >= session.replayAttachDeadlineMs)
                    {
                        if (ReplayDebugEnabled(ReplayDebugFlag::Teardown))
                        {
                            std::ostringstream ss;
                            ss << "playerMap=" << replayer->GetMapId()
                               << " replayMap=" << session.replayMapId
                               << " playerBg=" << replayer->GetBattlegroundId()
                               << " sessionBg=" << session.battlegroundInstanceId
                               << " inBattleground=" << (replayer->InBattleground() ? 1 : 0)
                               << " playerPhase=" << replayer->GetPhaseMask()
                               << " replayPhase=" << session.replayPhaseMask
                               << " nowMs=" << nowMs;
                            ReplayLog(ReplayDebugFlag::Teardown, &session, replayer, "ATTACH_TIMEOUT", ss.str());
                        }
                        RequestReplayTeardown(replayer, nullptr, "attach_timeout", 0);
                    }

                    if (session.teardownRequested && nowMs >= session.teardownExecuteAtMs)
                        PerformReplayTeardown(replayer, nullptr);
                    continue;
                }

                session.awaitingReplayMapAttach = false;
                session.replayMapAttached = true;
                session.replayAttachDeadlineMs = 0;
                session.nextAttachLogMs = 0;
                session.replayWarmupUntilMs = session.replayPlaybackMs + 1000;

                if (ReplayDebugEnabled(ReplayDebugFlag::General))
                {
                    std::ostringstream ss;
                    ss << "playerMap=" << replayer->GetMapId()
                       << " replayMap=" << session.replayMapId
                       << " playerBg=" << replayer->GetBattlegroundId()
                       << " sessionBg=" << session.battlegroundInstanceId
                       << " inBattleground=" << (replayer->InBattleground() ? 1 : 0)
                       << " playerPhase=" << replayer->GetPhaseMask()
                       << " replayPhase=" << session.replayPhaseMask
                       << " hasBg=" << (replayer->GetBattleground() ? 1 : 0)
                       << " isBgMap=" << ((replayer->GetMap() && replayer->GetMap()->IsBattlegroundOrArena()) ? 1 : 0)
                       << " nowMs=" << nowMs;
                    ReplayLog(ReplayDebugFlag::General, &session, replayer, "ATTACH_OK", ss.str());
                }
                else if (ReplayDynamicObjectsDebugEnabled())
                {
                    LOG_INFO("server.loading", "[RTG][REPLAY][ATTACH_OK] replay={} viewerGuid={} nativeMap={} playerMap={} replayMap={} playerBg={} sessionBg={} inBattleground={} playerPhase={} replayPhase={} result=ok",
                        session.replayId,
                        replayer->GetGUID().GetCounter(),
                        session.nativeMapId,
                        replayer->GetMapId(),
                        session.replayMapId,
                        replayer->GetBattlegroundId(),
                        session.battlegroundInstanceId,
                        replayer->InBattleground() ? 1 : 0,
                        replayer->GetPhaseMask(),
                        session.replayPhaseMask);
                }

                if (!InitializeReplayDynamicObjects(replayer, match, session))
                {
                    DespawnReplayDynamicObjects(replayer, session);
                    if (replayer->GetSession())
                        ChatHandler(replayer->GetSession()).PSendSysMessage("Replay arena objects could not be spawned.");
                    RequestReplayTeardown(replayer, nullptr, "dynamic_object_spawn_failed", 0);
                    if (session.teardownRequested)
                        PerformReplayTeardown(replayer, nullptr);
                    continue;
                }

                bool clonesOk = BuildReplayActorCloneScene(replayer, match, session);
                LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_SCENE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} clones={} result={}",
                    session.replayId,
                    replayer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    session.replayPhaseMask,
                    session.cloneBindings.size(),
                    clonesOk ? "ok" : "fail");
                if (!clonesOk && sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.Enable", true))
                {
                    DespawnReplayDynamicObjects(replayer, session);
                    RequestReplayTeardown(replayer, nullptr, "clone_scene_failed", 0);
                    if (session.teardownRequested)
                        PerformReplayTeardown(replayer, nullptr);
                    continue;
                }

                EnsureReplayCameraAnchor(replayer, match, session);
                BindReplayViewpoint(replayer, match, session);
                ApplyActorReplayView(replayer, match, session, session.replayPlaybackMs, true);
                SendReplayHudPov(replayer, match, session, true);
                session.nextHudWatcherSyncMs = 0;
                SendReplayHudWatchers(nullptr, replayer, match, session, true);
            }

            session.replayPlaybackMs += deltaMs;

            uint32 packetsSentThisUpdate = 0;
            uint32 packetBudgetPerUpdate = std::max<uint32>(15u, session.packetBudgetPerUpdate);
            while (!match.packets.empty() && match.packets.front().timestamp <= session.replayPlaybackMs && packetsSentThisUpdate < packetBudgetPerUpdate)
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
                    session.replayCompleteAtMs = session.replayPlaybackMs + 1500;
                    RequestReplayTeardown(replayer, nullptr, "packet_stream_complete", 1500);
                }
            }
            else
            {
                session.replayComplete = false;
                session.replayCompleteAtMs = 0;
            }

            if (ReplayDebugEnabled(ReplayDebugFlag::Playback) && (session.lastPlaybackLogMs == 0 || (session.replayPlaybackMs >= session.lastPlaybackLogMs + 1000) || session.replayComplete))
            {
                session.lastPlaybackLogMs = session.replayPlaybackMs;
                std::ostringstream ss;
                ss << "remainingPackets=" << match.packets.size()
                   << " packetsSent=" << packetsSentThisUpdate
                   << " replayTimeMs=" << session.replayPlaybackMs
                   << " replayComplete=" << (session.replayComplete ? 1 : 0)
                   << " actorGuid=" << session.lastAppliedActorGuid
                   << " actorIndex=" << session.lastAppliedActorFlatIndex;
                ReplayLog(ReplayDebugFlag::Playback, &session, replayer, "PLAYBACK", ss.str());
            }

            if (session.teardownRequested)
            {
                if (session.replayPlaybackMs >= session.teardownExecuteAtMs)
                {
                    PerformReplayTeardown(replayer, nullptr);
                    continue;
                }

                if (ReplayDebugEnabled(ReplayDebugFlag::Teardown) && ReplayDebugVerbose())
                {
                    std::ostringstream ss;
                    ss << "reason=" << session.lastTeardownReason
                       << " nowMs=" << session.replayPlaybackMs
                       << " executeAtMs=" << session.teardownExecuteAtMs;
                    ReplayLog(ReplayDebugFlag::Teardown, &session, replayer, "EXIT_PENDING", ss.str());
                }
                continue;
            }
            if (session.teardownInProgress)
                continue;

            SyncReplayActorClones(replayer, match, session, session.replayPlaybackMs);
            UpdateReplayDynamicObjects(replayer, match, session);
            bool actorViewApplied = ApplyActorReplayView(replayer, match, session, session.replayPlaybackMs);
            SendReplayHudPov(replayer, match, session, false);
            SendReplayHudWatchers(nullptr, replayer, match, session, false);
            if (!actorViewApplied && session.nextAnchorEnforceMs <= session.replayPlaybackMs)
            {
                session.nextAnchorEnforceMs = session.replayPlaybackMs + 500;
                if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.AnchorViewer", true) && replayer->GetMapId() == session.replayMapId)
                    ApplyActorReplayView(replayer, match, session, session.replayPlaybackMs, true);
            }
        }
    }
}

class ConfigLoaderArenaReplay : public WorldScript
{
public:
    ConfigLoaderArenaReplay() : WorldScript("config_loader_arena_replay", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_UPDATE
    }) {}
    virtual void OnAfterConfigLoad(bool /*Reload*/) override
    {
        DeleteOldReplays();
    }

    virtual void OnUpdate(uint32 diff) override
    {
        ProcessReplaySandboxSessions(diff);
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
        if (bg && IsReplayViewerMapAttached(player, bg, sessionIt->second))
            ApplyActorReplayView(player, replayIt->second, sessionIt->second, bg->GetStartTime(), true);
        else if (IsReplayViewerMapAttached(player, nullptr, sessionIt->second))
            ApplyActorReplayView(player, replayIt->second, sessionIt->second, sessionIt->second.replayPlaybackMs, true);

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
        SendReplayHudWatchers(bg, player, replayIt->second, sessionIt->second, true);
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
