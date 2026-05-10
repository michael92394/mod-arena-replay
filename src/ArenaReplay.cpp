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
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerGossip.h"
#include "PlayerGossipMgr.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "ChatCommand.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
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

struct PacketRecord
{
    uint32 timestamp = 0;
    WorldPacket packet;
    ObjectGuid receiverGuid;
    TeamId receiverTeam = TeamId(2);
};
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
    // Base playable-model display ids only. Creature clones cannot reconstruct
    // full player armor from these fields.
    uint32 displayId = 0;
    uint32 nativeDisplayId = 0;
    // Legacy display ids are retained for old rows/inline snapshots, but are
    // never written into UNIT_VIRTUAL_ITEM_SLOT_ID during playback.
    uint32 mainhandDisplayId = 0;
    uint32 offhandDisplayId = 0;
    uint32 rangedDisplayId = 0;
    uint32 mainhandItemEntry = 0;
    uint32 offhandItemEntry = 0;
    uint32 rangedItemEntry = 0;
    uint8 skin = 0;
    uint8 face = 0;
    uint8 hairStyle = 0;
    uint8 hairColor = 0;
    uint8 facialHair = 0;
    uint32 playerBytes = 0;
    uint32 playerBytes2 = 0;
    uint32 playerFlags = 0;
    uint32 shapeshiftDisplayId = 0;
    uint32 shapeshiftForm = 0;
    uint32 headItemEntry = 0;
    uint32 shouldersItemEntry = 0;
    uint32 chestItemEntry = 0;
    uint32 waistItemEntry = 0;
    uint32 legsItemEntry = 0;
    uint32 feetItemEntry = 0;
    uint32 wristsItemEntry = 0;
    uint32 handsItemEntry = 0;
    uint32 backItemEntry = 0;
    uint32 tabardItemEntry = 0;
};
struct MatchRecord
{
    BattlegroundTypeId typeId;
    uint8 arenaTypeId;
    uint32 mapId;
    std::deque<PacketRecord> packets;
    uint32 team0PacketCount = 0;
    uint32 team1PacketCount = 0;
    uint32 neutralPacketCount = 0;
    uint32 skippedPacketCount = 0;
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
    RTG_REPLAY_OBJECT_STRUCTURAL,
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
enum ReplayActorVisualBackend
{
    RTG_REPLAY_ACTOR_VISUAL_CREATURE_SILHOUETTE = 0,
    RTG_REPLAY_ACTOR_VISUAL_PLAYERBOT_BODY_EXPERIMENTAL = 1,
    RTG_REPLAY_ACTOR_VISUAL_SYNTHETIC_PLAYER_OBJECT_EXPERIMENTAL = 2,
    RTG_REPLAY_ACTOR_VISUAL_RECORDED_PACKET_STREAM = 3,
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
    bool anchorCaptured = false;
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
    bool cameraAnchorFailed = false;
    bool cameraAnchorFailureLogged = false;
    uint32 nextCameraAnchorRetryMs = 0;
    bool fixedCameraFallbackApplied = false;
    uint32 cameraAnchorDisplayId = 0;
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
    bool cloneTimelineInitialized = false;
    uint32 packetStartMs = 0;
    uint32 firstPlayableActorFrameMs = 0;
    uint32 lastPlayableActorFrameMs = 0;
    uint32 cloneSceneStartMs = 0;
    uint32 matchOpenMs = 0;
    std::vector<ReplayDynamicObjectBinding> replayObjects;
    bool replayObjectsSpawned = false;
    bool replayObjectsInitialized = false;
    uint32 nextDynamicObjectLogMs = 0;
    uint32 rvState = 0;
    uint32 rvNextEventMs = 0;
    uint32 rvPillarToggleState = 0;
    bool rvInitialUpper = false;
    float rvFirstActorZ = 0.0f;
    uint32 dsWaterState = 0;
    uint32 dsNextEventMs = 0;
    uint32 dsWaterCycle = 0;
    uint32 cameraStallCount = 0;
    uint32 priorDisplayId = 0;
    bool invisibleDisplayApplied = false;
    uint32 priorVirtualItemSlot[3] = { 0, 0, 0 };
    bool virtualItemsStripped = false;
    uint64 viewerReplayVisualGuidRaw = 0;
    std::unordered_map<uint64, uint64> syntheticReplayVisualGuids;
    uint32 syntheticActorsPlanned = 0;
    bool syntheticPlanLogged = false;
    std::unordered_set<uint64> syntheticReplayVisualsCreated;
    uint32 nextSyntheticSyncMs = 0;
    uint32 syntheticVisualCreateCount = 0;
    uint32 syntheticVisualMoveCount = 0;
    bool syntheticDestroySent = false;
    uint32 viewerGuidRemapPacketCount = 0;
    uint32 viewerGuidSkipPacketCount = 0;
    bool viewerGuidRemapLogged = false;
    bool viewerGuidSkipLogged = false;
    float lastCameraX = 0.0f;
    float lastCameraY = 0.0f;
    float lastCameraZ = 0.0f;
    float lastCameraO = 0.0f;
    float smoothedCameraX = 0.0f;
    float smoothedCameraY = 0.0f;
    float smoothedCameraZ = 0.0f;
    float smoothedCameraO = 0.0f;
    bool hasSmoothedCamera = false;
    uint32 lastCameraMoveMs = 0;
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
    static MatchRecord& EnsureLiveMatchRecord(Battleground* bg);
    static void CaptureOrUpdateReplayActorAppearance(Battleground* bg, Player* player, MatchRecord& match, char const* source);
    static void ReconcileReplayActorAppearanceWinnerSides(MatchRecord& match);
    static Creature* EnsureReplayCameraAnchor(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void BindReplayViewpoint(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void ClearReplayViewpoint(Player* viewer, ActiveReplaySession& session);
    static bool IsReplayViewerMapAttached(Player* viewer, Battleground* bg, ActiveReplaySession const& session);
    static bool GetReplayBootstrapFrame(MatchRecord const& match, ActorFrame& frame, uint64& actorGuid, bool& winnerSide);
    static bool ReplayDynamicObjectsDebugEnabled();
    static bool IsInvalidReplayCloneDisplay(uint32 displayId, ActiveReplaySession const& session);
    static uint32 ResolveVisibleReplayFallbackDisplay(ActorTrack const& track, bool winnerSide);
    static bool ReplayCreatureSilhouetteUsePlayerDisplayIds();
    static uint32 ResolveCreatureSilhouetteDisplay(ReplayActorAppearanceSnapshot const* snapshot, ActorTrack const& track, bool winnerSide, char const*& reason);
    static ReplayActorVisualBackend GetReplayActorVisualBackend();
    static bool ReplaySyntheticReplayPacketEmitterBackendEnabled();
    static bool ReplayRecordedPacketVisualBackendEnabled();
    static void EnsureSyntheticReplayActorsCreated(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void SyncSyntheticReplayActors(Player* viewer, MatchRecord const& match, ActiveReplaySession& session, uint32 nowMs, bool forceImmediate = false);
    static void DestroySyntheticReplayActorVisuals(Player* viewer, ActiveReplaySession& session);
    static uint32 ResolveReplayMapId(uint32 nativeMapId);
    static uint32 AllocateReplayPrivatePhaseMask(uint64 viewerKey);
    static bool ApplyReplayViewerPrivatePhase(Player* viewer, ActiveReplaySession& session);
    static bool PrepareReplayMapSandbox(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void RestoreReplayViewerPhase(Player* player, ActiveReplaySession const& session);
    static bool InitializeReplayDynamicObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void UpdateReplayDynamicObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session);
    static void DespawnReplayDynamicObjects(Player* viewer, ActiveReplaySession& session);
    static void CancelReplayStartup(Player* player, ActiveReplaySession& session, char const* reason);
    static void StripReplayViewerVisualEquipment(Player* player, ActiveReplaySession& session);
    static void RestoreReplayViewerVisualEquipment(Player* player, ActiveReplaySession& session);
    static uint32 SecondsToMs(uint32 seconds);
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

    static std::string SanitizeReplayToken(std::string value)
    {
        for (char& ch : value)
            if (ch == ';' || ch == '|' || ch == ',' || ch == '\n' || ch == '\r')
                ch = ' ';
        return value;
    }

    static bool ReplayColumnExists(char const* tableName, char const* columnName)
    {
        if (!tableName || !columnName)
            return false;

        QueryResult result = CharacterDatabase.Query(
            "SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}' AND COLUMN_NAME = '{}' LIMIT 1",
            tableName, columnName);
        return result != nullptr;
    }

    static bool ReplayAppearanceInlineColumnAvailable()
    {
        static bool checked = false;
        static bool available = false;

        if (!checked)
        {
            available = ReplayColumnExists("character_arena_replays", "actorAppearanceSnapshots");
            checked = true;
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_SCHEMA] inlineColumn={} result=checked", available ? 1 : 0);
        }

        return available;
    }

    static bool ReplayActorSnapshotItemEntryColumnsAvailable()
    {
        static bool checked = false;
        static bool available = false;

        if (!checked)
        {
            available =
                ReplayColumnExists("character_arena_replay_actor_snapshot", "mainhand_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "offhand_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "ranged_item_entry");
            checked = true;
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_SCHEMA] itemEntryColumns={} result=checked", available ? 1 : 0);
        }

        return available;
    }

    static bool ReplayActorSnapshotFullColumnsAvailable()
    {
        static bool checked = false;
        static bool available = false;

        if (!checked)
        {
            available =
                ReplayActorSnapshotItemEntryColumnsAvailable() &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "skin") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "face") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "hair_style") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "hair_color") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "facial_hair") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "player_bytes") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "player_bytes_2") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "player_flags") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "shapeshift_display_id") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "shapeshift_form") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "head_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "shoulders_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "chest_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "waist_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "legs_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "feet_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "wrists_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "hands_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "back_item_entry") &&
                ReplayColumnExists("character_arena_replay_actor_snapshot", "tabard_item_entry");
            checked = true;
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_SCHEMA] fullAppearanceColumns={} result=checked", available ? 1 : 0);
        }

        return available;
    }


    static MatchRecord& EnsureLiveMatchRecord(Battleground* bg)
    {
        MatchRecord& record = records[bg ? bg->GetInstanceID() : 0];
        if (bg)
        {
            record.typeId = bg->GetBgTypeID();
            record.arenaTypeId = bg->GetArenaType();
            record.mapId = bg->GetMapId();
        }
        return record;
    }

    static bool IsValidReplayAppearanceDisplay(uint32 displayId)
    {
        return displayId != 0;
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

    static std::string SerializeReplayActorAppearanceSnapshots(std::unordered_map<uint64, ReplayActorAppearanceSnapshot> const& snapshots)
    {
        std::ostringstream out;
        bool first = true;
        for (auto const& pair : snapshots)
        {
            ReplayActorAppearanceSnapshot const& snapshot = pair.second;
            if (!snapshot.guid)
                continue;

            if (!first)
                out << "||";
            first = false;

            out << snapshot.guid << ';'
                << (snapshot.winnerSide ? 1 : 0) << ';'
                << SanitizeReplayToken(snapshot.name) << ';'
                << uint32(snapshot.playerClass) << ';'
                << uint32(snapshot.race) << ';'
                << uint32(snapshot.gender) << ';'
                << snapshot.displayId << ';'
                << snapshot.nativeDisplayId << ';'
                << snapshot.mainhandDisplayId << ';'
                << snapshot.offhandDisplayId << ';'
                << snapshot.rangedDisplayId << ';'
                << snapshot.mainhandItemEntry << ';'
                << snapshot.offhandItemEntry << ';'
                << snapshot.rangedItemEntry << ';'
                << uint32(snapshot.skin) << ';'
                << uint32(snapshot.face) << ';'
                << uint32(snapshot.hairStyle) << ';'
                << uint32(snapshot.hairColor) << ';'
                << uint32(snapshot.facialHair) << ';'
                << snapshot.playerBytes << ';'
                << snapshot.playerBytes2 << ';'
                << snapshot.playerFlags << ';'
                << snapshot.shapeshiftDisplayId << ';'
                << snapshot.shapeshiftForm << ';'
                << snapshot.headItemEntry << ';'
                << snapshot.shouldersItemEntry << ';'
                << snapshot.chestItemEntry << ';'
                << snapshot.waistItemEntry << ';'
                << snapshot.legsItemEntry << ';'
                << snapshot.feetItemEntry << ';'
                << snapshot.wristsItemEntry << ';'
                << snapshot.handsItemEntry << ';'
                << snapshot.backItemEntry << ';'
                << snapshot.tabardItemEntry;
        }
        return out.str();
    }

    static uint32 DeserializeReplayActorAppearanceSnapshots(std::unordered_map<uint64, ReplayActorAppearanceSnapshot>& snapshots, std::string const& encoded)
    {
        if (encoded.empty())
            return 0;

        uint32 loaded = 0;
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
            while (true)
            {
                size_t pend = entry.find(';', pstart);
                if (pend == std::string::npos)
                {
                    parts.push_back(entry.substr(pstart));
                    break;
                }
                parts.push_back(entry.substr(pstart, pend - pstart));
                pstart = pend + 1;
            }

            if (parts.size() < 11)
                continue;

            ReplayActorAppearanceSnapshot snapshot;
            try
            {
                snapshot.guid = std::stoull(parts[0]);
                snapshot.winnerSide = std::stoul(parts[1]) != 0;
                snapshot.name = parts[2];
                snapshot.playerClass = uint8(std::stoul(parts[3]));
                snapshot.race = uint8(std::stoul(parts[4]));
                snapshot.gender = uint8(std::stoul(parts[5]));
                snapshot.displayId = uint32(std::stoul(parts[6]));
                snapshot.nativeDisplayId = uint32(std::stoul(parts[7]));
                snapshot.mainhandDisplayId = uint32(std::stoul(parts[8]));
                snapshot.offhandDisplayId = uint32(std::stoul(parts[9]));
                snapshot.rangedDisplayId = uint32(std::stoul(parts[10]));
                if (parts.size() >= 14)
                {
                    snapshot.mainhandItemEntry = uint32(std::stoul(parts[11]));
                    snapshot.offhandItemEntry = uint32(std::stoul(parts[12]));
                    snapshot.rangedItemEntry = uint32(std::stoul(parts[13]));
                }
                if (parts.size() >= 34)
                {
                    snapshot.skin = uint8(std::stoul(parts[14]));
                    snapshot.face = uint8(std::stoul(parts[15]));
                    snapshot.hairStyle = uint8(std::stoul(parts[16]));
                    snapshot.hairColor = uint8(std::stoul(parts[17]));
                    snapshot.facialHair = uint8(std::stoul(parts[18]));
                    snapshot.playerBytes = uint32(std::stoul(parts[19]));
                    snapshot.playerBytes2 = uint32(std::stoul(parts[20]));
                    snapshot.playerFlags = uint32(std::stoul(parts[21]));
                    snapshot.shapeshiftDisplayId = uint32(std::stoul(parts[22]));
                    snapshot.shapeshiftForm = uint32(std::stoul(parts[23]));
                    snapshot.headItemEntry = uint32(std::stoul(parts[24]));
                    snapshot.shouldersItemEntry = uint32(std::stoul(parts[25]));
                    snapshot.chestItemEntry = uint32(std::stoul(parts[26]));
                    snapshot.waistItemEntry = uint32(std::stoul(parts[27]));
                    snapshot.legsItemEntry = uint32(std::stoul(parts[28]));
                    snapshot.feetItemEntry = uint32(std::stoul(parts[29]));
                    snapshot.wristsItemEntry = uint32(std::stoul(parts[30]));
                    snapshot.handsItemEntry = uint32(std::stoul(parts[31]));
                    snapshot.backItemEntry = uint32(std::stoul(parts[32]));
                    snapshot.tabardItemEntry = uint32(std::stoul(parts[33]));
                }
            }
            catch (...)
            {
                continue;
            }

            if (!snapshot.guid)
                continue;

            snapshots[snapshot.guid] = std::move(snapshot);
            ++loaded;
        }

        return loaded;
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

        MatchRecord& match = EnsureLiveMatchRecord(bg);
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

            if (sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.AppearanceCaptureOnSample", true))
                CaptureOrUpdateReplayActorAppearance(bg, actor, match, "sample");

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

    static bool ReplayCloneModeEnabled()
    {
        ReplayActorVisualBackend backend = GetReplayActorVisualBackend();
        if (backend != RTG_REPLAY_ACTOR_VISUAL_CREATURE_SILHOUETTE)
            return false;

        return sConfigMgr->GetOption<uint32>("ArenaReplay.Playback.Mode", 1u) == 1 &&
            sConfigMgr->GetOption<bool>("ArenaReplay.Playback.CloneMode.Enable", true) &&
            sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.Enable", true);
    }

    static bool ReplaySyntheticReplayPacketEmitterBackendEnabled()
    {
        return GetReplayActorVisualBackend() == RTG_REPLAY_ACTOR_VISUAL_SYNTHETIC_PLAYER_OBJECT_EXPERIMENTAL &&
            sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.Enable", true);
    }

    static bool ReplayRecordedPacketVisualBackendEnabled()
    {
        return GetReplayActorVisualBackend() == RTG_REPLAY_ACTOR_VISUAL_RECORDED_PACKET_STREAM &&
            sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.RecordedPacketStream.Enable", true);
    }

    static bool ReplayCloneModeSendRecordedWorldPackets()
    {
        if (ReplayRecordedPacketVisualBackendEnabled())
            return true;
        return sConfigMgr->GetOption<bool>("ArenaReplay.Playback.CloneMode.SendRecordedWorldPackets", false);
    }

    static bool ReplayCloneModeFilterObjectUpdatePackets()
    {
        if (ReplayRecordedPacketVisualBackendEnabled())
            return false;
        return sConfigMgr->GetOption<bool>("ArenaReplay.Playback.CloneMode.FilterObjectUpdatePackets", true);
    }

    static bool ReplayCloneModeFilterMovementPackets()
    {
        if (ReplayRecordedPacketVisualBackendEnabled())
            return false;
        return sConfigMgr->GetOption<bool>("ArenaReplay.Playback.CloneMode.FilterMovementPackets", true);
    }

    static bool ReplayCloneModeStartAtFirstActorFrame()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.Playback.CloneMode.StartAtFirstActorFrame", true);
    }

    static bool ReplayCloneModeTrimPreMatchDeadAir()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.Playback.CloneMode.TrimPreMatchDeadAir", true);
    }

    static bool ReplayCloneModePrewarmClones()
    {
        if (ReplayRecordedPacketVisualBackendEnabled())
            return false;
        return sConfigMgr->GetOption<bool>("ArenaReplay.Playback.CloneMode.PrewarmClonesAtFirstFrame", true);
    }

    static bool ReplayCloneModeMatchOpenFromFirstActorFrame()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.Playback.CloneMode.MatchOpenFromFirstActorFrame", true);
    }

    static bool IsReplayObjectUpdateOpcode(uint16 opcode)
    {
        switch (opcode)
        {
            case SMSG_UPDATE_OBJECT:
            case SMSG_COMPRESSED_UPDATE_OBJECT:
            case SMSG_DESTROY_OBJECT:
            case SMSG_GAMEOBJECT_DESPAWN_ANIM:
                return true;
            default:
                return false;
        }
    }

    static bool IsReplayMovementOpcode(uint16 opcode)
    {
        switch (opcode)
        {
            case MSG_MOVE_START_FORWARD:
            case MSG_MOVE_START_BACKWARD:
            case MSG_MOVE_STOP:
            case MSG_MOVE_START_STRAFE_LEFT:
            case MSG_MOVE_START_STRAFE_RIGHT:
            case MSG_MOVE_STOP_STRAFE:
            case MSG_MOVE_START_TURN_LEFT:
            case MSG_MOVE_START_TURN_RIGHT:
            case MSG_MOVE_STOP_TURN:
            case MSG_MOVE_SET_FACING:
            case MSG_MOVE_HEARTBEAT:
            case MSG_MOVE_JUMP:
            case MSG_MOVE_FALL_LAND:
            case SMSG_MONSTER_MOVE:
            case SMSG_FORCE_RUN_SPEED_CHANGE:
            case SMSG_FORCE_FLIGHT_SPEED_CHANGE:
            case SMSG_FORCE_SWIM_SPEED_CHANGE:
                return true;
            default:
                return false;
        }
    }

    static bool IsReplayPacketOpcodeAllowedForPlayback(uint16 opcode)
    {
        if (ReplaySyntheticReplayPacketEmitterBackendEnabled())
            return false;

        switch (opcode)
        {
            case CMSG_CAST_SPELL:
            case CMSG_CANCEL_CAST:
            case CMSG_MESSAGECHAT:
            case CMSG_MOUNTSPECIAL_ANIM:
                return false;
            default:
                break;
        }

        if (ReplayRecordedPacketVisualBackendEnabled() &&
            opcode == SMSG_DESTROY_OBJECT &&
            sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.RecordedPacketStream.FilterDestroyObjectPackets", true))
            return false;

        if (ReplayCloneModeEnabled() && !ReplayCloneModeSendRecordedWorldPackets())
        {
            if (ReplayCloneModeFilterObjectUpdatePackets() && IsReplayObjectUpdateOpcode(opcode))
                return false;

            if (ReplayCloneModeFilterMovementPackets() && IsReplayMovementOpcode(opcode))
                return false;
        }

        return true;
    }


    static std::vector<uint8> ReplayGuidRawBytes(uint64 raw)
    {
        std::vector<uint8> out(8, 0);
        for (uint8 i = 0; i < 8; ++i)
            out[i] = uint8((raw >> (i * 8)) & 0xFF);
        return out;
    }

    static std::vector<uint8> ReplayGuidPackedBytes(uint64 raw)
    {
        std::vector<uint8> out;
        uint8 mask = 0;
        std::array<uint8, 8> bytes{};
        for (uint8 i = 0; i < 8; ++i)
        {
            bytes[i] = uint8((raw >> (i * 8)) & 0xFF);
            if (bytes[i] != 0)
                mask |= uint8(1u << i);
        }

        out.push_back(mask);
        for (uint8 i = 0; i < 8; ++i)
        {
            if (mask & uint8(1u << i))
                out.push_back(bytes[i]);
        }
        return out;
    }

    static uint64 MakeReplayViewerVisualGuidRaw(uint64 raw)
    {
        for (uint8 i = 0; i < 8; ++i)
        {
            uint64 shift = uint64(i) * 8u;
            uint8 b = uint8((raw >> shift) & 0xFF);
            if (b != 0 && b != 0x80)
                return raw ^ (uint64(0x80) << shift);
        }

        return raw ^ uint64(0x01);
    }

    static uint32 ReplaceReplayPacketBytes(std::vector<uint8>& data, std::vector<uint8> const& from, std::vector<uint8> const& to)
    {
        if (from.empty() || from.size() != to.size() || data.size() < from.size())
            return 0;

        uint32 replacements = 0;
        for (size_t i = 0; i + from.size() <= data.size(); ++i)
        {
            if (std::memcmp(data.data() + i, from.data(), from.size()) != 0)
                continue;

            std::memcpy(data.data() + i, to.data(), to.size());
            ++replacements;
            i += from.size() - 1;
        }
        return replacements;
    }

    static bool ReplayPacketContainsBytes(std::vector<uint8> const& data, std::vector<uint8> const& needle)
    {
        if (needle.empty() || data.size() < needle.size())
            return false;

        for (size_t i = 0; i + needle.size() <= data.size(); ++i)
        {
            if (std::memcmp(data.data() + i, needle.data(), needle.size()) == 0)
                return true;
        }
        return false;
    }

    static bool BuildViewerSafeReplayPacket(WorldPacket const& source, Player* viewer, ActiveReplaySession& session, WorldPacket& out)
    {
        if (!viewer || !ReplayRecordedPacketVisualBackendEnabled())
        {
            out = WorldPacket(source);
            return true;
        }

        std::vector<uint8> data;
        if (source.size() > 0)
        {
            data.resize(source.size());
            std::memcpy(data.data(), source.contents(), source.size());
        }

        uint64 viewerRaw = viewer->GetGUID().GetRawValue();
        std::vector<uint8> oldRaw = ReplayGuidRawBytes(viewerRaw);
        std::vector<uint8> oldPacked = ReplayGuidPackedBytes(viewerRaw);

        // Important: do not blindly byte-rewrite player GUIDs inside arbitrary
        // 3.3.5a packets.  The same byte sequence can legally appear inside
        // movement, aura, item, damage, packed-object, or compressed update
        // payloads as non-GUID data.  The previous raw remap path could corrupt
        // packet structure and trigger client ACCESS_VIOLATION crashes.
        //
        // Until opcode-aware object-update rewriting exists, the safe behavior is
        // to skip packets that still reference the live viewer's own GUID.  This
        // intentionally omits the viewer's replay actor from packet visuals, but
        // prevents the hidden spectator shell from turning into floating weapons
        // or becoming the packet target at replay end.
        bool containsViewerGuid = ReplayPacketContainsBytes(data, oldPacked) || ReplayPacketContainsBytes(data, oldRaw);
        if (containsViewerGuid && sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.RecordedPacketStream.SkipViewerGuidPackets", true))
        {
            ++session.viewerGuidSkipPacketCount;
            if (!session.viewerGuidSkipLogged || ReplayDebugVerbose())
            {
                LOG_WARN("server.loading", "[RTG][REPLAY][PACKET_VIEWER_GUID_SKIP] replay={} viewerGuid={} opcode={} skipped={} reason=viewer_guid_packet result=skip",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    uint32(source.GetOpcode()),
                    session.viewerGuidSkipPacketCount);
                session.viewerGuidSkipLogged = true;
            }
            return false;
        }

        uint32 replacements = 0;
        if (!containsViewerGuid && sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.RecordedPacketStream.RemapViewerGuid", false))
        {
            // Kept only for future opcode-aware implementations.  With the safe
            // skip above this branch should not run for viewer packets.
            if (session.viewerReplayVisualGuidRaw == 0)
                session.viewerReplayVisualGuidRaw = MakeReplayViewerVisualGuidRaw(viewerRaw);
        }

        bool stillContainsViewerGuid = ReplayPacketContainsBytes(data, oldPacked) || ReplayPacketContainsBytes(data, oldRaw);
        if (stillContainsViewerGuid && sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.RecordedPacketStream.SkipUnmappedViewerGuidPackets", true))
        {
            ++session.viewerGuidSkipPacketCount;
            if (!session.viewerGuidSkipLogged || ReplayDebugVerbose())
            {
                LOG_WARN("server.loading", "[RTG][REPLAY][PACKET_VIEWER_GUID_SKIP] replay={} viewerGuid={} opcode={} skipped={} reason=unmapped_viewer_guid result=skip",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    uint32(source.GetOpcode()),
                    session.viewerGuidSkipPacketCount);
                session.viewerGuidSkipLogged = true;
            }
            return false;
        }

        if (replacements != 0)
        {
            session.viewerGuidRemapPacketCount += replacements;
            if (!session.viewerGuidRemapLogged || ReplayDebugVerbose())
            {
                LOG_INFO("server.loading", "[RTG][REPLAY][PACKET_VIEWER_GUID_REMAP] replay={} viewerGuid={} fakeRaw={} opcode={} replacements={} totalReplacements={} result=ok",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.viewerReplayVisualGuidRaw,
                    uint32(source.GetOpcode()),
                    replacements,
                    session.viewerGuidRemapPacketCount);
                session.viewerGuidRemapLogged = true;
            }
        }

        out = WorldPacket(source.GetOpcode(), data.size());
        if (!data.empty())
            out.append(data.data(), data.size());
        return true;
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
        if (body != "END" && body != "STARTUP_CANCEL" && !EvaluateReplayHudAllowance(player, &reason))
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

    static bool IsCopiedReplayMapId(uint32 mapId)
    {
        return mapId == 725 || mapId == 726 || mapId == 727 || mapId == 728 || mapId == 729;
    }

    static bool IsAllianceReplayViewer(Player const* player)
    {
        return player && player->GetTeamId() == TEAM_ALLIANCE;
    }

    static void GetReplaySafeFallbackPosition(Player const* player, uint32& mapId, float& x, float& y, float& z, float& o)
    {
        if (IsAllianceReplayViewer(player))
        {
            mapId = sConfigMgr->GetOption<uint32>("ArenaReplay.Return.SafeFallbackAllianceMap", 0u);
            x = sConfigMgr->GetOption<float>("ArenaReplay.Return.SafeFallbackAllianceX", -8833.38f);
            y = sConfigMgr->GetOption<float>("ArenaReplay.Return.SafeFallbackAllianceY", 628.628f);
            z = sConfigMgr->GetOption<float>("ArenaReplay.Return.SafeFallbackAllianceZ", 94.0066f);
            o = sConfigMgr->GetOption<float>("ArenaReplay.Return.SafeFallbackAllianceO", 1.06535f);
            return;
        }

        mapId = sConfigMgr->GetOption<uint32>("ArenaReplay.Return.SafeFallbackHordeMap", 1u);
        x = sConfigMgr->GetOption<float>("ArenaReplay.Return.SafeFallbackHordeX", 1629.85f);
        y = sConfigMgr->GetOption<float>("ArenaReplay.Return.SafeFallbackHordeY", -4373.64f);
        z = sConfigMgr->GetOption<float>("ArenaReplay.Return.SafeFallbackHordeZ", 31.5573f);
        o = sConfigMgr->GetOption<float>("ArenaReplay.Return.SafeFallbackHordeO", 3.69762f);
    }

    static bool ReturnReplayViewerToAnchor(Player* player, ActiveReplaySession const& session)
    {
        if (!player)
            return false;

        uint32 anchorMap = session.anchorMapId;
        float x = session.anchorPosition.GetPositionX();
        float y = session.anchorPosition.GetPositionY();
        float z = session.anchorPosition.GetPositionZ();
        float o = session.anchorPosition.GetOrientation();

        char const* returnSource = "anchor";
        if (!session.anchorCaptured || (IsCopiedReplayMapId(anchorMap) && sConfigMgr->GetOption<bool>("ArenaReplay.Return.UseFallbackWhenAnchorIsReplayMap", true)))
        {
            GetReplaySafeFallbackPosition(player, anchorMap, x, y, z, o);
            returnSource = !session.anchorCaptured ? "fallback_no_anchor" : "fallback_anchor_was_replay_map";
        }

        uint32 playerMapBefore = player->GetMapId();
        bool teleportOk = player->TeleportTo(anchorMap, x, y, z, o);
        LOG_INFO("server.loading", "[RTG][REPLAY][RETURN_TELEPORT] replay={} viewerGuid={} nativeMap={} replayMap={} source={} targetMap={} x={} y={} z={} o={} playerMapBefore={} result={}",
            session.replayId,
            player->GetGUID().GetCounter(),
            session.nativeMapId,
            session.replayMapId,
            returnSource,
            anchorMap,
            x,
            y,
            z,
            o,
            playerMapBefore,
            teleportOk ? "ok" : "failed");

        bool alreadyUsingNoAnchorFallback = std::strcmp(returnSource, "fallback_no_anchor") == 0;
        if (!teleportOk && !alreadyUsingNoAnchorFallback)
        {
            GetReplaySafeFallbackPosition(player, anchorMap, x, y, z, o);
            teleportOk = player->TeleportTo(anchorMap, x, y, z, o);
            LOG_WARN("server.loading", "[RTG][REPLAY][RETURN_TELEPORT_FALLBACK] replay={} viewerGuid={} nativeMap={} replayMap={} targetMap={} x={} y={} z={} o={} result={}",
                session.replayId,
                player->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                anchorMap,
                x,
                y,
                z,
                o,
                teleportOk ? "ok" : "failed");
        }

        return teleportOk;
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

    static bool ApplyReplayViewerPrivatePhase(Player* viewer, ActiveReplaySession& session)
    {
        if (!viewer)
            return false;

        if (session.replayPhaseMask == 0)
            session.replayPhaseMask = AllocateReplayPrivatePhaseMask(viewer->GetGUID().GetCounter());

        if (session.replayPhaseMask == 0)
            return false;

        if (!session.replayPhaseApplied || viewer->GetPhaseMask() != session.replayPhaseMask)
            viewer->SetPhaseMask(session.replayPhaseMask, true);

        session.replayPhaseApplied = true;
        return true;
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

        if (sConfigMgr->GetOption<bool>("ArenaReplay.Sandbox.ApplyPhaseBeforeTeleport", false) &&
            !ApplyReplayViewerPrivatePhase(viewer, session))
            return false;

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

    static uint32 GetPlayerEquippedItemEntry(Player* player, uint8 slot)
    {
        if (!player)
            return 0;

        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        return item ? item->GetEntry() : 0;
    }

    static uint8 GetReplayPackedByte(uint32 packed, uint8 index)
    {
        return uint8((packed >> (uint32(index) * 8u)) & 0xFFu);
    }

    static uint32 CountReplaySnapshotEquipmentEntries(ReplayActorAppearanceSnapshot const& snapshot)
    {
        uint32 count = 0;
        if (snapshot.headItemEntry) ++count;
        if (snapshot.shouldersItemEntry) ++count;
        if (snapshot.chestItemEntry) ++count;
        if (snapshot.waistItemEntry) ++count;
        if (snapshot.legsItemEntry) ++count;
        if (snapshot.feetItemEntry) ++count;
        if (snapshot.wristsItemEntry) ++count;
        if (snapshot.handsItemEntry) ++count;
        if (snapshot.backItemEntry) ++count;
        if (snapshot.tabardItemEntry) ++count;
        if (snapshot.mainhandItemEntry) ++count;
        if (snapshot.offhandItemEntry) ++count;
        if (snapshot.rangedItemEntry) ++count;
        return count;
    }

    static bool ReplayActorAppearanceSnapshotMatches(ReplayActorAppearanceSnapshot const& lhs, ReplayActorAppearanceSnapshot const& rhs)
    {
        return lhs.playerClass == rhs.playerClass &&
            lhs.race == rhs.race &&
            lhs.gender == rhs.gender &&
            lhs.displayId == rhs.displayId &&
            lhs.nativeDisplayId == rhs.nativeDisplayId &&
            lhs.mainhandDisplayId == rhs.mainhandDisplayId &&
            lhs.offhandDisplayId == rhs.offhandDisplayId &&
            lhs.rangedDisplayId == rhs.rangedDisplayId &&
            lhs.mainhandItemEntry == rhs.mainhandItemEntry &&
            lhs.offhandItemEntry == rhs.offhandItemEntry &&
            lhs.rangedItemEntry == rhs.rangedItemEntry &&
            lhs.skin == rhs.skin &&
            lhs.face == rhs.face &&
            lhs.hairStyle == rhs.hairStyle &&
            lhs.hairColor == rhs.hairColor &&
            lhs.facialHair == rhs.facialHair &&
            lhs.playerBytes == rhs.playerBytes &&
            lhs.playerBytes2 == rhs.playerBytes2 &&
            lhs.playerFlags == rhs.playerFlags &&
            lhs.shapeshiftDisplayId == rhs.shapeshiftDisplayId &&
            lhs.shapeshiftForm == rhs.shapeshiftForm &&
            lhs.headItemEntry == rhs.headItemEntry &&
            lhs.shouldersItemEntry == rhs.shouldersItemEntry &&
            lhs.chestItemEntry == rhs.chestItemEntry &&
            lhs.waistItemEntry == rhs.waistItemEntry &&
            lhs.legsItemEntry == rhs.legsItemEntry &&
            lhs.feetItemEntry == rhs.feetItemEntry &&
            lhs.wristsItemEntry == rhs.wristsItemEntry &&
            lhs.handsItemEntry == rhs.handsItemEntry &&
            lhs.backItemEntry == rhs.backItemEntry &&
            lhs.tabardItemEntry == rhs.tabardItemEntry;
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
        snapshot.playerBytes = player->GetUInt32Value(PLAYER_BYTES);
        snapshot.playerBytes2 = player->GetUInt32Value(PLAYER_BYTES_2);
        snapshot.playerFlags = player->GetUInt32Value(PLAYER_FLAGS);
        snapshot.skin = GetReplayPackedByte(snapshot.playerBytes, 0);
        snapshot.face = GetReplayPackedByte(snapshot.playerBytes, 1);
        snapshot.hairStyle = GetReplayPackedByte(snapshot.playerBytes, 2);
        snapshot.hairColor = GetReplayPackedByte(snapshot.playerBytes, 3);
        snapshot.facialHair = GetReplayPackedByte(snapshot.playerBytes2, 0);
        snapshot.shapeshiftDisplayId = (snapshot.displayId && snapshot.nativeDisplayId && snapshot.displayId != snapshot.nativeDisplayId) ? snapshot.displayId : 0;
        snapshot.shapeshiftForm = uint32(player->GetShapeshiftForm());
        snapshot.headItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_HEAD);
        snapshot.shouldersItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_SHOULDERS);
        snapshot.chestItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_CHEST);
        snapshot.waistItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_WAIST);
        snapshot.legsItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_LEGS);
        snapshot.feetItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_FEET);
        snapshot.wristsItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_WRISTS);
        snapshot.handsItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_HANDS);
        snapshot.backItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_BACK);
        snapshot.tabardItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_TABARD);
        snapshot.mainhandDisplayId = GetPlayerEquippedItemDisplayId(player, EQUIPMENT_SLOT_MAINHAND);
        snapshot.offhandDisplayId = GetPlayerEquippedItemDisplayId(player, EQUIPMENT_SLOT_OFFHAND);
        snapshot.rangedDisplayId = GetPlayerEquippedItemDisplayId(player, EQUIPMENT_SLOT_RANGED);
        snapshot.mainhandItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_MAINHAND);
        snapshot.offhandItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_OFFHAND);
        snapshot.rangedItemEntry = GetPlayerEquippedItemEntry(player, EQUIPMENT_SLOT_RANGED);
        return snapshot;
    }

    static void CaptureOrUpdateReplayActorAppearance(Battleground* bg, Player* player, MatchRecord& match, char const* source)
    {
        if (!bg || !player || player->IsSpectator())
            return;

        if (!sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.RecordAppearanceSnapshots", true))
            return;

        ReplayActorAppearanceSnapshot snapshot = BuildReplayActorAppearanceSnapshot(player, false);
        if (!snapshot.guid)
            return;

        if (!IsValidReplayAppearanceDisplay(snapshot.displayId) && !IsValidReplayAppearanceDisplay(snapshot.nativeDisplayId))
        {
            LOG_WARN("server.loading", "[RTG][REPLAY][APPEARANCE_CAPTURE] replayInstance={} actorGuid={} name={} race={} class={} gender={} displayId={} nativeDisplayId={} itemDisplayCount=0 source={} result=invalid_display",
                bg->GetInstanceID(),
                snapshot.guid,
                snapshot.name,
                uint32(snapshot.race),
                uint32(snapshot.playerClass),
                uint32(snapshot.gender),
                snapshot.displayId,
                snapshot.nativeDisplayId,
                source ? source : "unknown");
            return;
        }

        uint32 itemEntryCount = CountReplaySnapshotEquipmentEntries(snapshot);

        auto existing = match.actorAppearanceSnapshots.find(snapshot.guid);
        bool changed = existing == match.actorAppearanceSnapshots.end();
        if (existing != match.actorAppearanceSnapshots.end())
        {
            // Preserve an already-valid display if a late sample is somehow less useful.
            if (!snapshot.displayId && existing->second.displayId)
                snapshot.displayId = existing->second.displayId;
            if (!snapshot.nativeDisplayId && existing->second.nativeDisplayId)
                snapshot.nativeDisplayId = existing->second.nativeDisplayId;

            ReplayActorAppearanceSnapshot const& prior = existing->second;
            changed = !ReplayActorAppearanceSnapshotMatches(prior, snapshot);
        }

        match.actorAppearanceSnapshots[snapshot.guid] = snapshot;

        bool const isSample = source && std::string(source) == "sample";
        if (sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.AppearanceDebug", true) && (!isSample || changed))
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_CAPTURE] replayInstance={} actorGuid={} name={} race={} class={} gender={} displayId={} nativeDisplayId={} mainhandEntry={} offhandEntry={} rangedEntry={} mainhandDisplayId={} offhandDisplayId={} rangedDisplayId={} itemEntryCount={} source={} changed={} result=ok",
                bg->GetInstanceID(),
                snapshot.guid,
                snapshot.name,
                uint32(snapshot.race),
                uint32(snapshot.playerClass),
                uint32(snapshot.gender),
                snapshot.displayId,
                snapshot.nativeDisplayId,
                snapshot.mainhandItemEntry,
                snapshot.offhandItemEntry,
                snapshot.rangedItemEntry,
                snapshot.mainhandDisplayId,
                snapshot.offhandDisplayId,
                snapshot.rangedDisplayId,
                itemEntryCount,
                source ? source : "unknown",
                changed ? 1 : 0);
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_LIMIT] replayInstance={} actorGuid={} name={} displayId={} nativeDisplayId={} result=creature_silhouette_debug reason=creature_clone_cannot_show_full_player_armor",
                bg->GetInstanceID(),
                snapshot.guid,
                snapshot.name,
                snapshot.displayId,
                snapshot.nativeDisplayId);
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_CAPTURE_FULL] replayInstance={} actorGuid={} name={} race={} class={} gender={} skin={} face={} hairStyle={} hairColor={} facialHair={} playerBytes={} playerBytes2={} playerFlags={} displayId={} nativeDisplayId={} shapeshiftDisplayId={} shapeshiftForm={} headEntry={} shouldersEntry={} chestEntry={} waistEntry={} legsEntry={} feetEntry={} wristsEntry={} handsEntry={} backEntry={} tabardEntry={} mainhandEntry={} offhandEntry={} rangedEntry={} equipmentCount={} source={} changed={} result=ok",
                bg->GetInstanceID(),
                snapshot.guid,
                snapshot.name,
                uint32(snapshot.race),
                uint32(snapshot.playerClass),
                uint32(snapshot.gender),
                uint32(snapshot.skin),
                uint32(snapshot.face),
                uint32(snapshot.hairStyle),
                uint32(snapshot.hairColor),
                uint32(snapshot.facialHair),
                snapshot.playerBytes,
                snapshot.playerBytes2,
                snapshot.playerFlags,
                snapshot.displayId,
                snapshot.nativeDisplayId,
                snapshot.shapeshiftDisplayId,
                snapshot.shapeshiftForm,
                snapshot.headItemEntry,
                snapshot.shouldersItemEntry,
                snapshot.chestItemEntry,
                snapshot.waistItemEntry,
                snapshot.legsItemEntry,
                snapshot.feetItemEntry,
                snapshot.wristsItemEntry,
                snapshot.handsItemEntry,
                snapshot.backItemEntry,
                snapshot.tabardItemEntry,
                snapshot.mainhandItemEntry,
                snapshot.offhandItemEntry,
                snapshot.rangedItemEntry,
                itemEntryCount,
                source ? source : "unknown",
                changed ? 1 : 0);
        }
    }

    static void ReconcileReplayActorAppearanceWinnerSides(MatchRecord& match)
    {
        for (ActorTrack const& track : match.winnerActorTracks)
        {
            auto it = match.actorAppearanceSnapshots.find(track.guid);
            if (it != match.actorAppearanceSnapshots.end())
                it->second.winnerSide = true;
        }

        for (ActorTrack const& track : match.loserActorTracks)
        {
            auto it = match.actorAppearanceSnapshots.find(track.guid);
            if (it != match.actorAppearanceSnapshots.end())
                it->second.winnerSide = false;
        }
    }

    static void CaptureReplayActorAppearanceSnapshots(Battleground* bg, MatchRecord& match, TeamId winnerTeamId)
    {
        // Do not clear here. End-of-match captures can run after players have already been
        // removed from the arena; clearing would erase valid join/sample snapshots.
        if (!bg)
            return;

        uint32 before = uint32(match.actorAppearanceSnapshots.size());
        for (auto const& playerPair : bg->GetPlayers())
        {
            Player* player = playerPair.second;
            if (!player || player->IsSpectator())
                continue;

            CaptureOrUpdateReplayActorAppearance(bg, player, match, "save");
            auto it = match.actorAppearanceSnapshots.find(player->GetGUID().GetRawValue());
            if (it != match.actorAppearanceSnapshots.end())
                it->second.winnerSide = player->GetBgTeamId() == winnerTeamId;
        }

        ReconcileReplayActorAppearanceWinnerSides(match);
        LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_CAPTURE] replayInstance={} snapshotCountBefore={} snapshotCountAfter={} source=save result=ok",
            bg->GetInstanceID(),
            before,
            uint32(match.actorAppearanceSnapshots.size()));
    }

    static void PersistReplayActorAppearanceSnapshots(uint32 replayId, MatchRecord const& match)
    {
        if (!replayId)
            return;

        CharacterDatabase.Execute("DELETE FROM `character_arena_replay_actor_snapshot` WHERE `replay_id` = {}", replayId);

        uint32 persisted = 0;
        bool const fullColumns = ReplayActorSnapshotFullColumnsAvailable();
        bool const itemEntryColumns = ReplayActorSnapshotItemEntryColumnsAvailable();
        for (auto const& pair : match.actorAppearanceSnapshots)
        {
            ReplayActorAppearanceSnapshot const& snapshot = pair.second;
            if (fullColumns)
            {
                CharacterDatabase.Execute(
                    "INSERT INTO `character_arena_replay_actor_snapshot` "
                    "(`replay_id`, `actor_guid`, `winner_side`, `actor_name`, `player_class`, `race`, `gender`, `display_id`, `native_display_id`, `mainhand_display_id`, `offhand_display_id`, `ranged_display_id`, `mainhand_item_entry`, `offhand_item_entry`, `ranged_item_entry`, `skin`, `face`, `hair_style`, `hair_color`, `facial_hair`, `player_bytes`, `player_bytes_2`, `player_flags`, `shapeshift_display_id`, `shapeshift_form`, `head_item_entry`, `shoulders_item_entry`, `chest_item_entry`, `waist_item_entry`, `legs_item_entry`, `feet_item_entry`, `wrists_item_entry`, `hands_item_entry`, `back_item_entry`, `tabard_item_entry`) "
                    "VALUES ({}, {}, {}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
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
                    snapshot.rangedDisplayId,
                    snapshot.mainhandItemEntry,
                    snapshot.offhandItemEntry,
                    snapshot.rangedItemEntry,
                    uint32(snapshot.skin),
                    uint32(snapshot.face),
                    uint32(snapshot.hairStyle),
                    uint32(snapshot.hairColor),
                    uint32(snapshot.facialHair),
                    snapshot.playerBytes,
                    snapshot.playerBytes2,
                    snapshot.playerFlags,
                    snapshot.shapeshiftDisplayId,
                    snapshot.shapeshiftForm,
                    snapshot.headItemEntry,
                    snapshot.shouldersItemEntry,
                    snapshot.chestItemEntry,
                    snapshot.waistItemEntry,
                    snapshot.legsItemEntry,
                    snapshot.feetItemEntry,
                    snapshot.wristsItemEntry,
                    snapshot.handsItemEntry,
                    snapshot.backItemEntry,
                    snapshot.tabardItemEntry
                );
            }
            else if (itemEntryColumns)
            {
                CharacterDatabase.Execute(
                    "INSERT INTO `character_arena_replay_actor_snapshot` "
                    "(`replay_id`, `actor_guid`, `winner_side`, `actor_name`, `player_class`, `race`, `gender`, `display_id`, `native_display_id`, `mainhand_display_id`, `offhand_display_id`, `ranged_display_id`, `mainhand_item_entry`, `offhand_item_entry`, `ranged_item_entry`) "
                    "VALUES ({}, {}, {}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
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
                    snapshot.rangedDisplayId,
                    snapshot.mainhandItemEntry,
                    snapshot.offhandItemEntry,
                    snapshot.rangedItemEntry
                );
            }
            else
            {
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
            ++persisted;
        }

        LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_PERSIST] replayId={} snapshotCount={} itemEntryColumns={} fullAppearanceColumns={} result=ok", replayId, persisted, itemEntryColumns ? 1 : 0, fullColumns ? 1 : 0);
    }

    static void LoadReplayActorAppearanceSnapshots(MatchRecord& record, uint32 replayId)
    {
        record.actorAppearanceSnapshots.clear();

        if (!replayId)
            return;

        bool const fullColumns = ReplayActorSnapshotFullColumnsAvailable();
        bool const itemEntryColumns = ReplayActorSnapshotItemEntryColumnsAvailable();
        QueryResult result = fullColumns
            ? CharacterDatabase.Query(
                "SELECT `actor_guid`, `winner_side`, `actor_name`, `player_class`, `race`, `gender`, `display_id`, `native_display_id`, `mainhand_display_id`, `offhand_display_id`, `ranged_display_id`, `mainhand_item_entry`, `offhand_item_entry`, `ranged_item_entry`, `skin`, `face`, `hair_style`, `hair_color`, `facial_hair`, `player_bytes`, `player_bytes_2`, `player_flags`, `shapeshift_display_id`, `shapeshift_form`, `head_item_entry`, `shoulders_item_entry`, `chest_item_entry`, `waist_item_entry`, `legs_item_entry`, `feet_item_entry`, `wrists_item_entry`, `hands_item_entry`, `back_item_entry`, `tabard_item_entry` "
                "FROM `character_arena_replay_actor_snapshot` WHERE `replay_id` = {}",
                replayId)
            : itemEntryColumns
            ? CharacterDatabase.Query(
                "SELECT `actor_guid`, `winner_side`, `actor_name`, `player_class`, `race`, `gender`, `display_id`, `native_display_id`, `mainhand_display_id`, `offhand_display_id`, `ranged_display_id`, `mainhand_item_entry`, `offhand_item_entry`, `ranged_item_entry` "
                "FROM `character_arena_replay_actor_snapshot` WHERE `replay_id` = {}",
                replayId)
            : CharacterDatabase.Query(
                "SELECT `actor_guid`, `winner_side`, `actor_name`, `player_class`, `race`, `gender`, `display_id`, `native_display_id`, `mainhand_display_id`, `offhand_display_id`, `ranged_display_id` "
                "FROM `character_arena_replay_actor_snapshot` WHERE `replay_id` = {}",
                replayId);
        if (!result)
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_LOAD] replayId={} snapshotCount=0 result=empty", replayId);
            return;
        }

        uint32 loaded = 0;
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
            if (itemEntryColumns)
            {
                snapshot.mainhandItemEntry = fields[11].Get<uint32>();
                snapshot.offhandItemEntry = fields[12].Get<uint32>();
                snapshot.rangedItemEntry = fields[13].Get<uint32>();
            }
            if (fullColumns)
            {
                snapshot.skin = fields[14].Get<uint8>();
                snapshot.face = fields[15].Get<uint8>();
                snapshot.hairStyle = fields[16].Get<uint8>();
                snapshot.hairColor = fields[17].Get<uint8>();
                snapshot.facialHair = fields[18].Get<uint8>();
                snapshot.playerBytes = fields[19].Get<uint32>();
                snapshot.playerBytes2 = fields[20].Get<uint32>();
                snapshot.playerFlags = fields[21].Get<uint32>();
                snapshot.shapeshiftDisplayId = fields[22].Get<uint32>();
                snapshot.shapeshiftForm = fields[23].Get<uint32>();
                snapshot.headItemEntry = fields[24].Get<uint32>();
                snapshot.shouldersItemEntry = fields[25].Get<uint32>();
                snapshot.chestItemEntry = fields[26].Get<uint32>();
                snapshot.waistItemEntry = fields[27].Get<uint32>();
                snapshot.legsItemEntry = fields[28].Get<uint32>();
                snapshot.feetItemEntry = fields[29].Get<uint32>();
                snapshot.wristsItemEntry = fields[30].Get<uint32>();
                snapshot.handsItemEntry = fields[31].Get<uint32>();
                snapshot.backItemEntry = fields[32].Get<uint32>();
                snapshot.tabardItemEntry = fields[33].Get<uint32>();
            }
            if (snapshot.guid)
            {
                record.actorAppearanceSnapshots[snapshot.guid] = std::move(snapshot);
                ++loaded;
            }
        }
        while (result->NextRow());

        LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_LOAD] replayId={} snapshotCount={} itemEntryColumns={} fullAppearanceColumns={} result=ok", replayId, loaded, itemEntryColumns ? 1 : 0, fullColumns ? 1 : 0);
    }

    static ReplayActorAppearanceSnapshot const* FindReplayActorAppearanceSnapshot(MatchRecord const& match, uint64 actorGuid)
    {
        auto it = match.actorAppearanceSnapshots.find(actorGuid);
        if (it == match.actorAppearanceSnapshots.end())
            return nullptr;
        return &it->second;
    }

    static uint32 ApplyReplayActorAppearanceToClone(Creature* clone, ReplayActorAppearanceSnapshot const* snapshot, ActorTrack const& track, bool winnerSide, ActiveReplaySession const& session)
    {
        if (!clone)
            return 0;

        uint32 originalDisplay = clone->GetDisplayId();
        char const* source = "fallback";
        uint32 finalDisplay = 0;
        uint32 mainhandEntry = 0;
        uint32 offhandEntry = 0;
        uint32 rangedEntry = 0;
        uint32 playerDisplayId = snapshot ? (snapshot->displayId ? snapshot->displayId : snapshot->nativeDisplayId) : 0;
        char const* silhouetteReason = "none";

        if (snapshot)
        {
            uint32 snapshotDisplay = ResolveCreatureSilhouetteDisplay(snapshot, track, winnerSide, silhouetteReason);
            if (!IsInvalidReplayCloneDisplay(snapshotDisplay, session))
            {
                clone->SetNativeDisplayId(snapshotDisplay);
                clone->SetDisplayId(snapshotDisplay);
                mainhandEntry = snapshot->mainhandItemEntry;
                offhandEntry = snapshot->offhandItemEntry;
                rangedEntry = snapshot->rangedItemEntry;
                clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, mainhandEntry);
                clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1, offhandEntry);
                clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2, rangedEntry);
                finalDisplay = snapshotDisplay;
                source = ReplayCreatureSilhouetteUsePlayerDisplayIds() && finalDisplay == playerDisplayId ? "snapshot" : "silhouette";

                if (std::string(silhouetteReason) == "avoid_raw_player_display_on_creature")
                {
                    LOG_INFO("server.loading", "[RTG][REPLAY][CREATURE_SILHOUETTE_DISPLAY] replay={} actorGuid={} actorName={} race={} gender={} class={} playerDisplayId={} nativeDisplayId={} fallbackCreatureDisplayId={} reason={} result=ok",
                        session.replayId,
                        track.guid,
                        track.name,
                        snapshot ? uint32(snapshot->race) : uint32(track.race),
                        snapshot ? uint32(snapshot->gender) : uint32(track.gender),
                        GetClassToken(snapshot && snapshot->playerClass ? snapshot->playerClass : track.playerClass),
                        playerDisplayId,
                        snapshot ? snapshot->nativeDisplayId : 0,
                        finalDisplay,
                        silhouetteReason);
                }

                if (!mainhandEntry && !offhandEntry && !rangedEntry &&
                    (snapshot->mainhandDisplayId || snapshot->offhandDisplayId || snapshot->rangedDisplayId))
                {
                    LOG_WARN("server.loading", "[RTG][REPLAY][CLONE_WEAPON_APPLY] replay={} actorGuid={} cloneGuid={} mainhandEntry=0 offhandEntry=0 rangedEntry=0 legacyMainhandDisplayId={} legacyOffhandDisplayId={} legacyRangedDisplayId={} result=legacy_display_ids_ignored",
                        session.replayId,
                        track.guid,
                        clone->GetGUID().GetCounter(),
                        snapshot->mainhandDisplayId,
                        snapshot->offhandDisplayId,
                        snapshot->rangedDisplayId);
                }
            }
        }

        if (IsInvalidReplayCloneDisplay(finalDisplay, session))
        {
            silhouetteReason = "display_repair";
            finalDisplay = ResolveCreatureSilhouetteDisplay(snapshot, track, winnerSide, silhouetteReason);
            clone->SetNativeDisplayId(finalDisplay);
            clone->SetDisplayId(finalDisplay);
            mainhandEntry = 0;
            offhandEntry = 0;
            rangedEntry = 0;
            clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, 0);
            clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1, 0);
            clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2, 0);
            source = snapshot ? "repair" : "fallback";
        }

        if (IsInvalidReplayCloneDisplay(clone->GetDisplayId(), session))
        {
            char const* repairReason = "display_repair";
            uint32 repairedDisplay = ResolveCreatureSilhouetteDisplay(snapshot, track, winnerSide, repairReason);
            clone->SetNativeDisplayId(repairedDisplay);
            clone->SetDisplayId(repairedDisplay);
            LOG_WARN("server.loading", "[RTG][REPLAY][CLONE_DISPLAY_REPAIR] replay={} actorGuid={} actorName={} class={} cloneGuid={} originalDisplay={} repairedDisplay={} result={}",
                session.replayId,
                track.guid,
                track.name,
                GetClassToken(track.playerClass),
                clone->GetGUID().GetCounter(),
                finalDisplay,
                clone->GetDisplayId(),
                IsInvalidReplayCloneDisplay(clone->GetDisplayId(), session) ? "still_invisible" : "ok");
            source = "repair";
        }

        LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_DISPLAY] replay={} actorGuid={} actorName={} class={} cloneGuid={} originalDisplay={} finalDisplay={} source={} result={}",
            session.replayId,
            track.guid,
            track.name,
            GetClassToken(track.playerClass),
            clone->GetGUID().GetCounter(),
            originalDisplay,
            clone->GetDisplayId(),
            source,
            IsInvalidReplayCloneDisplay(clone->GetDisplayId(), session) ? "invisible" : "ok");

        LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_WEAPON_APPLY] replay={} actorGuid={} actorName={} cloneGuid={} mainhandEntry={} offhandEntry={} rangedEntry={} result=ok",
            session.replayId,
            track.guid,
            track.name,
            clone->GetGUID().GetCounter(),
            mainhandEntry,
            offhandEntry,
            rangedEntry);

        if (snapshot)
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_LIMIT] replay={} actorGuid={} actorName={} displayId={} nativeDisplayId={} result=creature_silhouette_debug reason=creature_clone_cannot_show_full_player_armor",
                session.replayId,
                track.guid,
                track.name,
                snapshot->displayId,
                snapshot->nativeDisplayId);
        }

        return clone->GetDisplayId();
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

    static ActorTrack const* SelectFirstReplayActorWithClone(Player* viewer, MatchRecord const& match, ActiveReplaySession& session, uint64 avoidActorGuid, uint32* flatIndex = nullptr)
    {
        std::vector<ReplayActorSelectionRef> playable = BuildPlayableReplayActorSelections(match);
        for (uint32 i = 0; i < playable.size(); ++i)
        {
            ReplayActorSelectionRef const& ref = playable[i];
            auto const* tracks = SelectTracks(match, ref.winnerSide);
            if (!tracks || ref.trackIndex >= tracks->size())
                continue;

            ActorTrack const& candidate = (*tracks)[ref.trackIndex];
            if (candidate.guid == avoidActorGuid)
                continue;

            if (!FindReplayClone(viewer, session, candidate.guid))
                continue;

            session.actorSpectateOnWinnerTeam = ref.winnerSide;
            session.actorTrackIndex = ref.trackIndex;
            session.nextActorTeleportMs = 0;
            if (flatIndex)
                *flatIndex = i + 1;
            return &candidate;
        }

        return nullptr;
    }

    static uint32 GetReplayCloneEntry()
    {
        return sConfigMgr->GetOption<uint32>("ArenaReplay.CloneScene.CloneEntry", 98501u);
    }

    static uint32 GetReplayCameraAnchorEntry()
    {
        return sConfigMgr->GetOption<uint32>("ArenaReplay.CloneScene.CameraAnchorEntry", 98502u);
    }

    static uint32 GetHardcodedReplayFallbackDisplay()
    {
        return 49u;
    }

    static bool IsKnownInvisibleReplayDisplay(uint32 displayId)
    {
        return displayId == 0 || displayId == 11686u;
    }

    static bool IsInvalidReplayCloneDisplay(uint32 displayId, ActiveReplaySession const& session)
    {
        if (IsKnownInvisibleReplayDisplay(displayId))
            return true;

        return session.cameraAnchorDisplayId != 0 && displayId == session.cameraAnchorDisplayId;
    }

    static char const* GetReplayFallbackDisplayClassKey(uint8 classId)
    {
        switch (classId)
        {
            case CLASS_DRUID:
                return "Druid";
            case CLASS_MAGE:
                return "Mage";
            case CLASS_HUNTER:
                return "Hunter";
            case CLASS_WARRIOR:
                return "Warrior";
            case CLASS_SHAMAN:
                return "Shaman";
            case CLASS_PALADIN:
                return "Paladin";
            case CLASS_PRIEST:
                return "Priest";
            case CLASS_ROGUE:
                return "Rogue";
            case CLASS_WARLOCK:
                return "Warlock";
            case CLASS_DEATH_KNIGHT:
                return "DeathKnight";
            default:
                return "Default";
        }
    }

    static char const* GetReplayRaceToken(uint8 race)
    {
        switch (race)
        {
            case RACE_HUMAN:
                return "Human";
            case RACE_ORC:
                return "Orc";
            case RACE_DWARF:
                return "Dwarf";
            case RACE_NIGHTELF:
                return "NightElf";
            case RACE_UNDEAD_PLAYER:
                return "Undead";
            case RACE_TAUREN:
                return "Tauren";
            case RACE_GNOME:
                return "Gnome";
            case RACE_TROLL:
                return "Troll";
            case RACE_BLOODELF:
                return "BloodElf";
            case RACE_DRAENEI:
                return "Draenei";
            default:
                return "Default";
        }
    }

    static char const* GetReplayGenderToken(uint8 gender)
    {
        return gender == GENDER_FEMALE ? "Female" : "Male";
    }

    static uint32 ResolveVisibleReplayFallbackDisplay(ActorTrack const& track, bool /*winnerSide*/)
    {
        uint32 hardcodedFallback = GetHardcodedReplayFallbackDisplay();
        uint32 displayId = sConfigMgr->GetOption<uint32>("ArenaReplay.CloneScene.FallbackDisplay.Default", hardcodedFallback);

        if (sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.GenericFallbackDisplayByClass", true))
        {
            std::string key = std::string("ArenaReplay.CloneScene.FallbackDisplay.") + GetReplayFallbackDisplayClassKey(track.playerClass);
            displayId = sConfigMgr->GetOption<uint32>(key, displayId);
        }

        if (IsKnownInvisibleReplayDisplay(displayId))
            displayId = hardcodedFallback;

        if (IsKnownInvisibleReplayDisplay(displayId))
            return hardcodedFallback;

        return displayId;
    }

    static bool ReplayCreatureSilhouetteUsePlayerDisplayIds()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.CreatureSilhouette.UsePlayerDisplayIds", false);
    }

    static bool ReplayCreatureSilhouetteUseNpcRaceFallbackDisplays()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.CreatureSilhouette.UseNpcRaceFallbackDisplays", true);
    }

    static uint32 GetHardcodedReplayNpcSilhouetteDisplay()
    {
        return 27800u;
    }

    static uint32 ResolveCreatureSilhouetteNpcDisplay(ReplayActorAppearanceSnapshot const* snapshot, ActorTrack const& track, bool winnerSide)
    {
        uint32 hardcodedFallback = GetHardcodedReplayNpcSilhouetteDisplay();
        uint32 displayId = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.CreatureSilhouette.Display.Default", hardcodedFallback);

        // Keep fallback resolution quiet by default. AzerothCore logs every
        // missing GetOption key, so probing Race.Gender.Class dynamically creates
        // dozens of false-positive missing-config warnings during every replay.
        // Define Team0/Team1 in the module config for coarse overrides; enable the
        // specific resolver only when intentionally testing per-race display IDs.
        if (sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.CreatureSilhouette.Display.UseSpecificOverrides", false) && snapshot)
        {
            std::string const raceKey = GetReplayRaceToken(snapshot->race ? snapshot->race : track.race);
            std::string const genderKey = GetReplayGenderToken(snapshot->gender);
            std::string const classKey = GetReplayFallbackDisplayClassKey(snapshot->playerClass ? snapshot->playerClass : track.playerClass);

            std::string const baseKey = "ArenaReplay.ActorVisual.CreatureSilhouette.Display.";
            displayId = sConfigMgr->GetOption<uint32>(baseKey + raceKey + "." + genderKey + "." + classKey, displayId);
            displayId = sConfigMgr->GetOption<uint32>(baseKey + raceKey + "." + genderKey, displayId);
            displayId = sConfigMgr->GetOption<uint32>(baseKey + raceKey, displayId);
        }

        displayId = sConfigMgr->GetOption<uint32>(winnerSide ? "ArenaReplay.ActorVisual.CreatureSilhouette.Display.Team0" : "ArenaReplay.ActorVisual.CreatureSilhouette.Display.Team1", displayId);

        if (IsKnownInvisibleReplayDisplay(displayId))
            displayId = hardcodedFallback;

        if (IsKnownInvisibleReplayDisplay(displayId))
            return hardcodedFallback;

        return displayId;
    }

    static uint32 ResolveCreatureSilhouetteDisplay(ReplayActorAppearanceSnapshot const* snapshot, ActorTrack const& track, bool winnerSide, char const*& reason)
    {
        uint32 playerDisplayId = snapshot ? (snapshot->displayId ? snapshot->displayId : snapshot->nativeDisplayId) : 0;
        if (ReplayCreatureSilhouetteUsePlayerDisplayIds() && playerDisplayId != 0)
        {
            reason = "use_player_display_config";
            return playerDisplayId;
        }

        if (snapshot && snapshot->shapeshiftDisplayId && snapshot->shapeshiftForm && !IsKnownInvisibleReplayDisplay(snapshot->shapeshiftDisplayId))
        {
            reason = "shapeshift_display";
            return snapshot->shapeshiftDisplayId;
        }

        if (ReplayCreatureSilhouetteUseNpcRaceFallbackDisplays())
        {
            reason = "avoid_raw_player_display_on_creature";
            return ResolveCreatureSilhouetteNpcDisplay(snapshot, track, winnerSide);
        }

        reason = "generic_fallback";
        return ResolveVisibleReplayFallbackDisplay(track, winnerSide);
    }

    static ReplayActorVisualBackend GetReplayActorVisualBackend()
    {
        uint32 backend = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.Backend", uint32(RTG_REPLAY_ACTOR_VISUAL_SYNTHETIC_PLAYER_OBJECT_EXPERIMENTAL));
        if (backend > uint32(RTG_REPLAY_ACTOR_VISUAL_RECORDED_PACKET_STREAM))
            backend = uint32(RTG_REPLAY_ACTOR_VISUAL_RECORDED_PACKET_STREAM);
        return ReplayActorVisualBackend(backend);
    }

    static char const* GetReplayActorVisualBackendName(ReplayActorVisualBackend backend)
    {
        switch (backend)
        {
            case RTG_REPLAY_ACTOR_VISUAL_CREATURE_SILHOUETTE:
                return "creature_silhouette_debug";
            case RTG_REPLAY_ACTOR_VISUAL_PLAYERBOT_BODY_EXPERIMENTAL:
                return "playerbot_body_experimental";
            case RTG_REPLAY_ACTOR_VISUAL_SYNTHETIC_PLAYER_OBJECT_EXPERIMENTAL:
                return "synthetic_replay_packet_emitter";
            case RTG_REPLAY_ACTOR_VISUAL_RECORDED_PACKET_STREAM:
                return "recorded_packet_stream";
            default:
                return "unknown";
        }
    }

    static bool ReplayPlayerBodyBackendEnabled()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.PlayerBody.Enable", false);
    }

    static void LogReplayPlayerBodyPlan(Player* viewer, MatchRecord const& match, ActiveReplaySession const& session)
    {
        ReplayActorVisualBackend backend = GetReplayActorVisualBackend();
        if (backend != RTG_REPLAY_ACTOR_VISUAL_PLAYERBOT_BODY_EXPERIMENTAL)
            return;

        bool backendAvailable = false;
        bool playerBodyEnabled = ReplayPlayerBodyBackendEnabled();
        bool useCapturedEquipment = sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.PlayerBody.UseCapturedEquipment", true);
        bool useCapturedCustomization = sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.PlayerBody.UseCapturedCustomization", true);
        std::string accountPrefix = sConfigMgr->GetOption<std::string>("ArenaReplay.ActorVisual.PlayerBody.AccountPrefix", "rtgreplay");
        uint32 bodyLevel = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.PlayerBody.Level", 19u);
        uint32 planned = 0;

        for (ReplayActorSelectionRef const& ref : BuildPlayableReplayActorSelections(match))
        {
            auto const* tracks = SelectTracks(match, ref.winnerSide);
            if (!tracks || ref.trackIndex >= tracks->size())
                continue;

            ActorTrack const& track = (*tracks)[ref.trackIndex];
            ReplayActorAppearanceSnapshot const* snapshot = FindReplayActorAppearanceSnapshot(match, track.guid);
            uint32 equipmentCount = snapshot ? CountReplaySnapshotEquipmentEntries(*snapshot) : 0;

            LOG_INFO("server.loading", "[RTG][REPLAY][PLAYER_BODY_PLAN] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} actorGuid={} actorName={} race={} class={} gender={} skin={} face={} hairStyle={} hairColor={} facialHair={} playerBytes={} playerBytes2={} playerFlags={} shapeshiftDisplayId={} shapeshiftForm={} equipmentCount={} headEntry={} shouldersEntry={} chestEntry={} waistEntry={} legsEntry={} feetEntry={} wristsEntry={} handsEntry={} backEntry={} tabardEntry={} mainhandEntry={} offhandEntry={} rangedEntry={} backend={} playerBodyEnable={} useCapturedEquipment={} useCapturedCustomization={} accountPrefix={} level={} backendAvailable={} result=planned_only",
                session.replayId,
                viewer ? viewer->GetGUID().GetCounter() : 0,
                session.nativeMapId,
                session.replayMapId,
                session.replayPhaseMask,
                track.guid,
                track.name,
                snapshot ? uint32(snapshot->race) : uint32(track.race),
                snapshot ? uint32(snapshot->playerClass) : uint32(track.playerClass),
                snapshot ? uint32(snapshot->gender) : uint32(track.gender),
                snapshot ? uint32(snapshot->skin) : 0,
                snapshot ? uint32(snapshot->face) : 0,
                snapshot ? uint32(snapshot->hairStyle) : 0,
                snapshot ? uint32(snapshot->hairColor) : 0,
                snapshot ? uint32(snapshot->facialHair) : 0,
                snapshot ? snapshot->playerBytes : 0,
                snapshot ? snapshot->playerBytes2 : 0,
                snapshot ? snapshot->playerFlags : 0,
                snapshot ? snapshot->shapeshiftDisplayId : 0,
                snapshot ? snapshot->shapeshiftForm : 0,
                equipmentCount,
                snapshot ? snapshot->headItemEntry : 0,
                snapshot ? snapshot->shouldersItemEntry : 0,
                snapshot ? snapshot->chestItemEntry : 0,
                snapshot ? snapshot->waistItemEntry : 0,
                snapshot ? snapshot->legsItemEntry : 0,
                snapshot ? snapshot->feetItemEntry : 0,
                snapshot ? snapshot->wristsItemEntry : 0,
                snapshot ? snapshot->handsItemEntry : 0,
                snapshot ? snapshot->backItemEntry : 0,
                snapshot ? snapshot->tabardItemEntry : 0,
                snapshot ? snapshot->mainhandItemEntry : 0,
                snapshot ? snapshot->offhandItemEntry : 0,
                snapshot ? snapshot->rangedItemEntry : 0,
                GetReplayActorVisualBackendName(backend),
                playerBodyEnabled ? 1 : 0,
                useCapturedEquipment ? 1 : 0,
                useCapturedCustomization ? 1 : 0,
                accountPrefix,
                bodyLevel,
                backendAvailable ? 1 : 0);
            ++planned;
        }

        LOG_WARN("server.loading", "[RTG][REPLAY][ACTOR_VISUAL_BACKEND] replay={} viewerGuid={} backend={} plannedActors={} backendAvailable=0 result=fallback_to_creature_silhouette",
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            GetReplayActorVisualBackendName(backend),
            planned);
    }


    static uint64 MakeSyntheticReplayVisualGuidRaw(ActiveReplaySession const& session, uint64 /*originalGuid*/, uint32 index)
    {
        // Session-local replay visual identity.  Use a real HighGuid::Unit-style
        // raw GUID instead of a low/player-shaped GUID.  Player-shaped GUIDs can
        // be interpreted by the client as player objects; this backend is meant
        // to create remote UNIT visuals only, never controlled/player movers.
        uint32 unitEntry = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UnitEntry", 98501u) & 0x00FFFFFFu;
        uint32 lowBase = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.GuidLowBase", 0x00E00000u) & 0x00FFFF00u;
        uint32 low = (lowBase | ((session.replayId & 0x0FFFu) << 8) | (index & 0x00FFu)) & 0x00FFFFFFu;
        if (low == 0)
            low = uint32(index & 0x00FFu) + 1u;

        // Trinity/AzerothCore 3.3.5 creature/unit GUID layout:
        // (HighGuid::Unit/Creature 0xF130 << 48) | (entry << 24) | lowGuid.
        return (uint64(0xF130u) << 48) | (uint64(unitEntry) << 24) | uint64(low);
    }

    static bool ReplaySyntheticUsePlayerDisplayIds()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UsePlayerDisplayIds", false);
    }

    static bool ReplaySyntheticUseNpcRaceFallbackDisplays()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UseNpcRaceFallbackDisplays", true);
    }

    static bool ReplaySyntheticUseShapeshiftDisplays()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UseShapeshiftDisplays", false);
    }

    static bool ReplaySyntheticServerCloneFallbackEnabled()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.ServerCloneFallback.Enable", true);
    }

    static bool ReplaySyntheticEmitUnitVisualPackets()
    {
        // Backend 2 packet-only UNIT visuals are still experimental. When the
        // server-clone fallback is enabled, default packet UNIT emission off so
        // the viewer does not see invisible hitboxes or orphaned ranged weapons.
        bool fallback = ReplaySyntheticServerCloneFallbackEnabled();
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.EmitUnitVisualPackets", !fallback);
    }

    static bool ReplaySyntheticEmitEquipment()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.EmitEquipment", false);
    }

    static bool ReplaySyntheticSkipViewerActorPackets()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SkipViewerActorPackets", true);
    }

    static uint32 ReplaySyntheticFallbackDisplayId()
    {
        // Keep the first Option-C UNIT smoke tests independent from the large
        // creature-silhouette display resolver. The resolver is useful later,
        // but it causes active-config spam and can still choose player-shaped
        // displays. This single, known-visible fallback keeps synthetic actors
        // easy to validate: create, move, destroy.
        uint32 display = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.FallbackDisplayId", 49u);
        if (!display || IsKnownInvisibleReplayDisplay(display))
            display = 49u;
        return display;
    }

    static bool ReplaySyntheticUseCreatureSilhouetteFallbackResolver()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UseCreatureSilhouetteFallbackResolver", false);
    }

    static void LogSyntheticReplayPacketEmitterPlan(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        bool emitterEnabled = sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.Enable", true);
        bool destroyOnTeardown = sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.DestroyVisualsOnTeardown", true);
        bool rawPacketsDisabled = !ReplayRecordedPacketVisualBackendEnabled();
        bool serverCloneFallback = ReplaySyntheticServerCloneFallbackEnabled();
        bool emitUnitPackets = ReplaySyntheticEmitUnitVisualPackets();
        bool emitEquipment = ReplaySyntheticEmitEquipment();
        uint32 planned = 0;

        session.syntheticReplayVisualGuids.clear();
        session.syntheticActorsPlanned = 0;

        LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_BACKEND] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} emitterEnable={} rawPacketsDisabled={} clonesDisabled={} serverCloneFallback={} emitUnitVisualPackets={} emitEquipment={} destroyOnTeardown={} result=enabled_foundation",
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            emitterEnabled ? 1 : 0,
            rawPacketsDisabled ? 1 : 0,
            serverCloneFallback ? 0 : 1,
            serverCloneFallback ? 1 : 0,
            emitUnitPackets ? 1 : 0,
            emitEquipment ? 1 : 0,
            destroyOnTeardown ? 1 : 0);

        for (ReplayActorSelectionRef const& ref : BuildPlayableReplayActorSelections(match))
        {
            auto const* tracks = SelectTracks(match, ref.winnerSide);
            if (!tracks || ref.trackIndex >= tracks->size())
                continue;

            ActorTrack const& track = (*tracks)[ref.trackIndex];
            ReplayActorAppearanceSnapshot const* snapshot = FindReplayActorAppearanceSnapshot(match, track.guid);
            uint32 visualIndex = planned + 1;
            uint64 visualGuid = MakeSyntheticReplayVisualGuidRaw(session, track.guid, visualIndex);
            session.syntheticReplayVisualGuids[track.guid] = visualGuid;
            uint32 equipmentCount = snapshot ? CountReplaySnapshotEquipmentEntries(*snapshot) : 0;

            LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_GUID_MAP] replay={} viewerGuid={} originalGuid={} visualGuid={} actorName={} visualIndex={} winnerSide={} result=ok",
                session.replayId,
                viewer ? viewer->GetGUID().GetCounter() : 0,
                track.guid,
                visualGuid,
                track.name,
                visualIndex,
                ref.winnerSide ? 1 : 0);

            LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_ACTOR_PLAN] replay={} viewerGuid={} originalGuid={} visualGuid={} actorName={} race={} class={} gender={} skin={} face={} hairStyle={} hairColor={} facialHair={} displayId={} nativeDisplayId={} shapeshiftDisplayId={} shapeshiftForm={} equipmentCount={} headEntry={} shouldersEntry={} chestEntry={} waistEntry={} legsEntry={} feetEntry={} wristsEntry={} handsEntry={} backEntry={} tabardEntry={} mainhandEntry={} offhandEntry={} rangedEntry={} frames={} result=planned_only",
                session.replayId,
                viewer ? viewer->GetGUID().GetCounter() : 0,
                track.guid,
                visualGuid,
                track.name,
                snapshot ? uint32(snapshot->race) : uint32(track.race),
                snapshot ? uint32(snapshot->playerClass) : uint32(track.playerClass),
                snapshot ? uint32(snapshot->gender) : uint32(track.gender),
                snapshot ? uint32(snapshot->skin) : 0,
                snapshot ? uint32(snapshot->face) : 0,
                snapshot ? uint32(snapshot->hairStyle) : 0,
                snapshot ? uint32(snapshot->hairColor) : 0,
                snapshot ? uint32(snapshot->facialHair) : 0,
                snapshot ? snapshot->displayId : 0,
                snapshot ? snapshot->nativeDisplayId : 0,
                snapshot ? snapshot->shapeshiftDisplayId : 0,
                snapshot ? snapshot->shapeshiftForm : 0,
                equipmentCount,
                snapshot ? snapshot->headItemEntry : 0,
                snapshot ? snapshot->shouldersItemEntry : 0,
                snapshot ? snapshot->chestItemEntry : 0,
                snapshot ? snapshot->waistItemEntry : 0,
                snapshot ? snapshot->legsItemEntry : 0,
                snapshot ? snapshot->feetItemEntry : 0,
                snapshot ? snapshot->wristsItemEntry : 0,
                snapshot ? snapshot->handsItemEntry : 0,
                snapshot ? snapshot->backItemEntry : 0,
                snapshot ? snapshot->tabardItemEntry : 0,
                snapshot ? snapshot->mainhandItemEntry : 0,
                snapshot ? snapshot->offhandItemEntry : 0,
                snapshot ? snapshot->rangedItemEntry : 0,
                uint32(track.frames.size()));
            ++planned;
        }

        session.syntheticActorsPlanned = planned;
        session.syntheticPlanLogged = true;

        LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_SCENE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} plannedActors={} packetStreamPackets={} result=synthetic_unit_visual_packets_ready",
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            planned,
            match.packets.size());
    }


    static uint32 FloatToUpdateUInt32(float value)
    {
        uint32 out = 0;
        static_assert(sizeof(out) == sizeof(value), "float update packing expects uint32 size");
        std::memcpy(&out, &value, sizeof(out));
        return out;
    }

    static void AppendReplayPackedGuidRaw(WorldPacket& packet, uint64 raw)
    {
        uint8 mask = 0;
        std::array<uint8, 8> bytes{};
        for (uint8 i = 0; i < 8; ++i)
        {
            bytes[i] = uint8((raw >> (uint64(i) * 8u)) & 0xFFu);
            if (bytes[i] != 0)
                mask |= uint8(1u << i);
        }

        packet << mask;
        for (uint8 i = 0; i < 8; ++i)
            if (mask & uint8(1u << i))
                packet << bytes[i];
    }

    static void AppendSyntheticUpdateValues(WorldPacket& packet, std::vector<std::pair<uint16, uint32>> values)
    {
        if (values.empty())
        {
            packet << uint8(0);
            return;
        }

        std::sort(values.begin(), values.end(), [](auto const& a, auto const& b)
        {
            return a.first < b.first;
        });

        values.erase(std::unique(values.begin(), values.end(), [](auto const& a, auto const& b)
        {
            return a.first == b.first;
        }), values.end());

        uint16 maxIndex = values.back().first;
        uint8 blocks = uint8((maxIndex / 32u) + 1u);
        std::vector<uint32> masks(blocks, 0);
        for (auto const& value : values)
            masks[value.first / 32u] |= uint32(1u << (value.first % 32u));

        packet << blocks;
        for (uint32 mask : masks)
            packet << mask;
        for (auto const& value : values)
            packet << value.second;
    }

    static uint32 GetSyntheticReplayActorDisplay(ReplayActorAppearanceSnapshot const* snapshot, ActorTrack const& track, bool winnerSide, char const*& reason)
    {
        reason = "fallback";

        if (snapshot && ReplaySyntheticUseShapeshiftDisplays() && snapshot->shapeshiftDisplayId && snapshot->shapeshiftForm && !IsKnownInvisibleReplayDisplay(snapshot->shapeshiftDisplayId))
        {
            reason = "shapeshift_display";
            return snapshot->shapeshiftDisplayId;
        }

        if (snapshot && ReplaySyntheticUsePlayerDisplayIds())
        {
            uint32 playerDisplay = snapshot->displayId ? snapshot->displayId : snapshot->nativeDisplayId;
            if (playerDisplay && !IsKnownInvisibleReplayDisplay(playerDisplay))
            {
                reason = "player_display_config";
                return playerDisplay;
            }
        }

        if (ReplaySyntheticUseNpcRaceFallbackDisplays())
        {
            if (ReplaySyntheticUseCreatureSilhouetteFallbackResolver())
            {
                reason = "npc_race_fallback_resolver";
                return ResolveCreatureSilhouetteNpcDisplay(snapshot, track, winnerSide);
            }

            reason = "synthetic_stable_fallback";
            return ReplaySyntheticFallbackDisplayId();
        }

        reason = "synthetic_stable_fallback";
        return ReplaySyntheticFallbackDisplayId();
    }

    static void BuildSyntheticReplayUnitValues(std::vector<std::pair<uint16, uint32>>& values, uint64 visualGuid, ActorTrack const& track, ReplayActorAppearanceSnapshot const* snapshot, bool winnerSide)
    {
        char const* displayReason = "unknown";
        uint32 displayId = GetSyntheticReplayActorDisplay(snapshot, track, winnerSide, displayReason);
        uint32 nativeDisplayId = displayId;
        uint32 race = snapshot ? uint32(snapshot->race) : uint32(track.race);
        uint32 playerClass = snapshot ? uint32(snapshot->playerClass) : uint32(track.playerClass);
        uint32 gender = snapshot ? uint32(snapshot->gender) : uint32(track.gender);
        uint32 bytes0 = (race & 0xFFu) | ((playerClass & 0xFFu) << 8) | ((gender & 0xFFu) << 16);
        uint32 unitFlags = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UnitFlags", 0u);
        uint32 unitFlags2 = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UnitFlags2", 0u);
        uint32 faction = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.Faction", 35u);
        uint32 unitEntry = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UnitEntry", 98501u);
        uint32 level = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.Level", 19u);
        uint32 health = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.Health", 100u);
        uint32 mainhand = (snapshot && ReplaySyntheticEmitEquipment()) ? snapshot->mainhandItemEntry : 0;
        uint32 offhand = (snapshot && ReplaySyntheticEmitEquipment()) ? snapshot->offhandItemEntry : 0;
        uint32 ranged = (snapshot && ReplaySyntheticEmitEquipment()) ? snapshot->rangedItemEntry : 0;

        // 3.3.5 update field indexes. General type value 9 is Unit; player is
        // intentionally not used here so the replay actor cannot become the
        // client's active mover/controlled object.
        values.push_back({0, uint32(visualGuid & 0xFFFFFFFFu)});                 // OBJECT_FIELD_GUID low
        values.push_back({1, uint32((visualGuid >> 32) & 0xFFFFFFFFu)});        // OBJECT_FIELD_GUID high
        values.push_back({2, 9u});                                              // OBJECT_FIELD_TYPE = Unit
        values.push_back({3, unitEntry});                                       // OBJECT_FIELD_ENTRY
        values.push_back({4, FloatToUpdateUInt32(1.0f)});                       // OBJECT_FIELD_SCALE_X
        values.push_back({23, bytes0});                                         // UNIT_FIELD_BYTES_0
        values.push_back({24, health});                                         // UNIT_FIELD_HEALTH
        values.push_back({32, health});                                         // UNIT_FIELD_MAXHEALTH
        values.push_back({54, level});                                          // UNIT_FIELD_LEVEL
        values.push_back({55, faction});                                        // UNIT_FIELD_FACTIONTEMPLATE
        values.push_back({56, mainhand});                                       // UNIT_VIRTUAL_ITEM_SLOT_ID 0
        values.push_back({57, offhand});                                        // UNIT_VIRTUAL_ITEM_SLOT_ID 1
        values.push_back({58, ranged});                                         // UNIT_VIRTUAL_ITEM_SLOT_ID 2
        values.push_back({59, unitFlags});                                      // UNIT_FIELD_FLAGS
        values.push_back({60, unitFlags2});                                     // UNIT_FIELD_FLAGS_2
        values.push_back({62, sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.BaseAttackTime", 2000u)}); // UNIT_FIELD_BASEATTACKTIME
        values.push_back({63, sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.BaseAttackTime", 2000u)}); // UNIT_FIELD_BASEATTACKTIME offhand
        values.push_back({64, sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.RangedAttackTime", 2000u)}); // UNIT_FIELD_RANGEDATTACKTIME
        values.push_back({65, FloatToUpdateUInt32(sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.BoundingRadius", 0.3889999986f))}); // UNIT_FIELD_BOUNDINGRADIUS
        values.push_back({66, FloatToUpdateUInt32(sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.CombatReach", 1.5f))}); // UNIT_FIELD_COMBATREACH
        values.push_back({67, displayId});                                      // UNIT_FIELD_DISPLAYID
        values.push_back({68, nativeDisplayId});                                // UNIT_FIELD_NATIVEDISPLAYID
        values.push_back({74, 0u});                                             // UNIT_FIELD_BYTES_1
        values.push_back({79, 0u});                                             // UNIT_DYNAMIC_FLAGS
        values.push_back({82, 0u});                                             // UNIT_NPC_FLAGS
        values.push_back({146, FloatToUpdateUInt32(1.0f)});                     // UNIT_FIELD_HOVERHEIGHT
    }

    static uint32 GetSyntheticReplayMovementBlockMode()
    {
        return std::min<uint32>(2u, sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.MovementBlockMode", 2u));
    }

    static void AppendSyntheticReplayStationaryPositionBlock(WorldPacket& packet, ActorFrame const& frame)
    {
        // Legacy smoke-test mode: non-living stationary position block. Kept as
        // a config fallback only; UNIT visuals render more reliably with a full
        // living movement block on 3.3.5 clients.
        packet << uint8(0x40); // UPDATEFLAG_HAS_POSITION
        packet << frame.x << frame.y << frame.z << frame.o;
    }

    static void AppendSyntheticReplayMovementSpeeds(WorldPacket& packet)
    {
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedWalk", 2.5f);
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedRun", 7.0f);
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedRunBack", 4.5f);
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedSwim", 4.722222f);
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedSwimBack", 2.5f);
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedTurnRate", 3.141594f);
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedFlight", 7.0f);
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedFlightBack", 4.5f);
        packet << sConfigMgr->GetOption<float>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SpeedPitchRate", 3.141594f);
    }

    static void AppendSyntheticReplayLivingMovementBlock(WorldPacket& packet, uint64 visualGuid, ActorFrame const& frame)
    {
        // 3.3.5 UNIT create/movement blocks expect UPDATEFLAG_LIVING followed by
        // MovementInfo and the server-side movement speed table. Omitting the
        // speed table can make the client parse the following update-value mask
        // incorrectly, producing invisible actor bodies or weapon-only ghosts.
        packet << uint8(0x20); // UPDATEFLAG_LIVING
        AppendReplayPackedGuidRaw(packet, visualGuid);
        packet << uint32(0); // movement flags
        packet << uint16(0); // movement flags 2
        packet << uint32(GetReplayNowMs());
        packet << frame.x << frame.y << frame.z << frame.o;
        packet << uint32(0); // fall time

        if (GetSyntheticReplayMovementBlockMode() >= 2)
            AppendSyntheticReplayMovementSpeeds(packet);
    }

    static void AppendSyntheticReplayMovementBlock(WorldPacket& packet, uint64 visualGuid, ActorFrame const& frame)
    {
        if (GetSyntheticReplayMovementBlockMode() == 0)
        {
            AppendSyntheticReplayStationaryPositionBlock(packet, frame);
            return;
        }

        AppendSyntheticReplayLivingMovementBlock(packet, visualGuid, frame);
    }

    static WorldPacket BuildSyntheticReplayCreateUnitPacket(uint64 visualGuid, ActorTrack const& track, ReplayActorAppearanceSnapshot const* snapshot, bool winnerSide, ActorFrame const& frame)
    {
        WorldPacket packet(SMSG_UPDATE_OBJECT, 512);
        packet << uint32(1);   // update count
        packet << uint8(0);    // no transport
        packet << uint8(3);    // UPDATETYPE_CREATE_OBJECT2
        AppendReplayPackedGuidRaw(packet, visualGuid);
        packet << uint8(3);    // TYPEID_UNIT
        AppendSyntheticReplayMovementBlock(packet, visualGuid, frame);

        std::vector<std::pair<uint16, uint32>> values;
        BuildSyntheticReplayUnitValues(values, visualGuid, track, snapshot, winnerSide);
        AppendSyntheticUpdateValues(packet, std::move(values));
        return packet;
    }

    static WorldPacket BuildSyntheticReplayMovementPacket(uint64 visualGuid, ActorFrame const& frame)
    {
        WorldPacket packet(SMSG_UPDATE_OBJECT, 96);
        packet << uint32(1);   // update count
        packet << uint8(0);    // no transport
        packet << uint8(1);    // UPDATETYPE_MOVEMENT
        AppendReplayPackedGuidRaw(packet, visualGuid);
        AppendSyntheticReplayMovementBlock(packet, visualGuid, frame);
        return packet;
    }

    static WorldPacket BuildSyntheticReplayDestroyPacket(std::vector<uint64> const& visualGuids)
    {
        WorldPacket packet(SMSG_UPDATE_OBJECT, 16 + visualGuids.size() * 9);
        packet << uint32(1);   // one out-of-range update block
        packet << uint8(0);    // no transport
        packet << uint8(4);    // UPDATETYPE_OUT_OF_RANGE_OBJECTS
        packet << uint32(visualGuids.size());
        for (uint64 visualGuid : visualGuids)
            AppendReplayPackedGuidRaw(packet, visualGuid);
        return packet;
    }

    static bool GetSyntheticReplayActorFrame(ActorTrack const& track, uint32 nowMs, ActorFrame& frame)
    {
        bool ok = false;
        frame = GetInterpolatedActorFrame(track, nowMs, ok);
        return ok;
    }

    static void EnsureSyntheticReplayActorsCreated(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!viewer || !viewer->GetSession() || !ReplaySyntheticReplayPacketEmitterBackendEnabled())
            return;

        if (!ReplaySyntheticEmitUnitVisualPackets())
            return;

        if (session.syntheticReplayVisualGuids.empty())
            LogSyntheticReplayPacketEmitterPlan(viewer, match, session);

        uint32 createdThisPass = 0;
        for (ReplayActorSelectionRef const& ref : BuildPlayableReplayActorSelections(match))
        {
            auto const* tracks = SelectTracks(match, ref.winnerSide);
            if (!tracks || ref.trackIndex >= tracks->size())
                continue;

            ActorTrack const& track = (*tracks)[ref.trackIndex];
            if (ReplaySyntheticSkipViewerActorPackets() && viewer && track.guid == viewer->GetGUID().GetCounter())
                continue;

            auto guidIt = session.syntheticReplayVisualGuids.find(track.guid);
            if (guidIt == session.syntheticReplayVisualGuids.end())
                continue;

            uint64 visualGuid = guidIt->second;
            if (session.syntheticReplayVisualsCreated.find(visualGuid) != session.syntheticReplayVisualsCreated.end())
                continue;

            ActorFrame frame;
            if (!GetSyntheticReplayActorFrame(track, session.replayPlaybackMs ? session.replayPlaybackMs : session.cloneSceneStartMs, frame))
                continue;

            ReplayActorAppearanceSnapshot const* snapshot = FindReplayActorAppearanceSnapshot(match, track.guid);
            WorldPacket packet = BuildSyntheticReplayCreateUnitPacket(visualGuid, track, snapshot, ref.winnerSide, frame);
            viewer->GetSession()->SendPacket(&packet);
            session.syntheticReplayVisualsCreated.insert(visualGuid);
            ++session.syntheticVisualCreateCount;
            ++createdThisPass;

            char const* displayReason = "unknown";
            uint32 syntheticDisplay = GetSyntheticReplayActorDisplay(snapshot, track, ref.winnerSide, displayReason);
            LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_DISPLAY] replay={} viewerGuid={} originalGuid={} visualGuid={} actorName={} displayId={} displaySource={} usePlayerDisplayIds={} useNpcFallback={} result=ok",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                track.guid,
                visualGuid,
                track.name,
                syntheticDisplay,
                displayReason,
                ReplaySyntheticUsePlayerDisplayIds() ? 1 : 0,
                ReplaySyntheticUseNpcRaceFallbackDisplays() ? 1 : 0);

            LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_ACTOR_CREATE] replay={} viewerGuid={} originalGuid={} visualGuid={} actorName={} x={} y={} z={} o={} displayId={} equipmentCount={} result=sent",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                track.guid,
                visualGuid,
                track.name,
                frame.x,
                frame.y,
                frame.z,
                frame.o,
                syntheticDisplay,
                snapshot ? CountReplaySnapshotEquipmentEntries(*snapshot) : 0);
        }

        if (createdThisPass || ReplayDebugVerbose())
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_CREATE_SUMMARY] replay={} viewerGuid={} createdThisPass={} totalCreated={} plannedActors={} result={}",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                createdThisPass,
                session.syntheticVisualCreateCount,
                session.syntheticActorsPlanned,
                session.syntheticVisualCreateCount == session.syntheticActorsPlanned ? "ok" : "partial");
        }
    }

    static void SyncSyntheticReplayActors(Player* viewer, MatchRecord const& match, ActiveReplaySession& session, uint32 nowMs, bool forceImmediate)
    {
        if (!viewer || !viewer->GetSession() || session.teardownInProgress || !ReplaySyntheticReplayPacketEmitterBackendEnabled())
            return;

        if (!ReplaySyntheticEmitUnitVisualPackets())
            return;

        EnsureSyntheticReplayActorsCreated(viewer, match, session);

        uint32 syncMs = std::max<uint32>(50u, sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SyncMs", 125u));
        if (!forceImmediate && session.nextSyntheticSyncMs > nowMs)
            return;
        session.nextSyntheticSyncMs = nowMs + syncMs;

        uint32 moved = 0;
        for (ReplayActorSelectionRef const& ref : BuildPlayableReplayActorSelections(match))
        {
            auto const* tracks = SelectTracks(match, ref.winnerSide);
            if (!tracks || ref.trackIndex >= tracks->size())
                continue;

            ActorTrack const& track = (*tracks)[ref.trackIndex];
            if (ReplaySyntheticSkipViewerActorPackets() && viewer && track.guid == viewer->GetGUID().GetCounter())
                continue;

            auto guidIt = session.syntheticReplayVisualGuids.find(track.guid);
            if (guidIt == session.syntheticReplayVisualGuids.end())
                continue;

            uint64 visualGuid = guidIt->second;
            if (session.syntheticReplayVisualsCreated.find(visualGuid) == session.syntheticReplayVisualsCreated.end())
                continue;

            ActorFrame frame;
            if (!GetSyntheticReplayActorFrame(track, nowMs, frame))
                continue;

            WorldPacket packet = BuildSyntheticReplayMovementPacket(visualGuid, frame);
            viewer->GetSession()->SendPacket(&packet);
            ++moved;
        }

        session.syntheticVisualMoveCount += moved;
        if ((moved != 0 && ReplayDebugEnabled(ReplayDebugFlag::Actors) && ReplayDebugVerbose()) || forceImmediate)
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_ACTOR_SYNC] replay={} viewerGuid={} moved={} totalMovePackets={} nowMs={} result=ok",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                moved,
                session.syntheticVisualMoveCount,
                nowMs);
        }
    }

    static void DestroySyntheticReplayActorVisuals(Player* viewer, ActiveReplaySession& session)
    {
        if (!viewer || !viewer->GetSession() || session.syntheticDestroySent || session.syntheticReplayVisualsCreated.empty())
            return;

        if (!sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.DestroyVisualsOnTeardown", true))
            return;

        std::vector<uint64> visualGuids(session.syntheticReplayVisualsCreated.begin(), session.syntheticReplayVisualsCreated.end());
        WorldPacket packet = BuildSyntheticReplayDestroyPacket(visualGuids);
        viewer->GetSession()->SendPacket(&packet);
        session.syntheticDestroySent = true;

        LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_DESTROY] replay={} viewerGuid={} destroyed={} result=sent",
            session.replayId,
            viewer->GetGUID().GetCounter(),
            uint32(visualGuids.size()));
    }

    static bool ReplayCameraAnchorRequired()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.RequireCameraAnchor", false);
    }

    static bool ReplayCameraAnchorFailLogOnce()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.CameraAnchorFailLogOnce", true);
    }

    static uint32 ReplayCameraAnchorRetryMs()
    {
        return sConfigMgr->GetOption<uint32>("ArenaReplay.CloneScene.CameraAnchorRetryMs", 5000u);
    }

    static uint32 ReplayCameraFallbackMode()
    {
        return std::min<uint32>(2u, sConfigMgr->GetOption<uint32>("ArenaReplay.CloneScene.FallbackMode", 1u));
    }

    static bool ReplayBodyChaseFallbackDisabled()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.DisableBodyChaseFallback", true);
    }

    static char const* ReplayCameraFallbackModeName(uint32 mode)
    {
        switch (mode)
        {
            case 0:
                return "legacy_body_chase";
            case 1:
                return "fixed_overview";
            case 2:
                return "cancel";
            default:
                return "unknown";
        }
    }

    static bool GetReplayFixedCameraFallbackPosition(ActiveReplaySession const& session, float& x, float& y, float& z, float& o)
    {
        char const* arenaKey = nullptr;
        switch (session.replayMapId)
        {
            case 725:
                arenaKey = "Nagrand";
                x = 4055.0f;
                y = 2920.0f;
                z = 20.0f;
                o = 0.0f;
                break;
            case 726:
                arenaKey = "BladesEdge";
                x = 6240.0f;
                y = 260.0f;
                z = 12.0f;
                o = 0.0f;
                break;
            case 727:
                arenaKey = "RuinsOfLordaeron";
                x = 1287.0f;
                y = 1666.0f;
                z = 38.0f;
                o = 0.0f;
                break;
            case 728:
                arenaKey = "DalaranSewers";
                x = 1291.0f;
                y = 791.0f;
                z = 24.0f;
                o = 0.0f;
                break;
            case 729:
                arenaKey = "RingOfValor";
                x = 763.0f;
                y = -284.0f;
                z = 35.0f;
                o = 0.0f;
                break;
            default:
                return false;
        }

        std::string base = std::string("ArenaReplay.CloneScene.Fallback.") + arenaKey;
        x = sConfigMgr->GetOption<float>(base + ".X", x);
        y = sConfigMgr->GetOption<float>(base + ".Y", y);
        z = sConfigMgr->GetOption<float>(base + ".Z", z);
        o = sConfigMgr->GetOption<float>(base + ".O", o);
        return true;
    }

    static bool ApplyReplayFixedCameraFallback(Player* viewer, ActiveReplaySession& session, char const* reason)
    {
        if (!viewer)
            return false;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float o = 0.0f;
        if (!GetReplayFixedCameraFallbackPosition(session, x, y, z, o))
        {
            LOG_ERROR("server.loading", "[RTG][REPLAY][CAMERA_FALLBACK] replay={} viewerGuid={} nativeMap={} replayMap={} mode={} x=0 y=0 z=0 o=0 reason={} result=no_position",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                ReplayCameraFallbackModeName(ReplayCameraFallbackMode()),
                reason ? reason : "unknown");
            return false;
        }

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.UseFlightForParking", false))
        {
            viewer->SetCanFly(true);
            viewer->SetDisableGravity(true);
            viewer->SetHover(true);
        }
        viewer->StopMoving();
        viewer->SetClientControl(viewer, false);
        session.movementLocked = true;
        if (!session.viewerHidden || viewer->IsVisible())
        {
            viewer->SetVisible(false);
            session.viewerHidden = true;
        }

        bool moved = !session.fixedCameraFallbackApplied ||
            std::abs(viewer->GetPositionX() - x) > 0.5f ||
            std::abs(viewer->GetPositionY() - y) > 0.5f ||
            std::abs(viewer->GetPositionZ() - z) > 0.5f;
        if (moved)
            viewer->NearTeleportTo(x, y, z, o);
        else
            viewer->SetFacingTo(o);

        if (!session.fixedCameraFallbackApplied || ReplayDynamicObjectsDebugEnabled())
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][CAMERA_FALLBACK] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} mode={} x={} y={} z={} o={} reason={} result=ok",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                session.replayPhaseMask,
                ReplayCameraFallbackModeName(ReplayCameraFallbackMode()),
                x,
                y,
                z,
                o,
                reason ? reason : "unknown");
        }

        session.fixedCameraFallbackApplied = true;
        session.lastCameraX = x;
        session.lastCameraY = y;
        session.lastCameraZ = z;
        session.lastCameraO = o;
        session.actorSpectateActive = true;
        session.replayMovementStabilized = true;
        return true;
    }

    static void ParkReplaySpectatorShell(Player* viewer, ActiveReplaySession& session, char const* reason)
    {
        if (!viewer || !ReplayCloneModeEnabled())
            return;

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.HideViewerBody", true))
        {
            viewer->SetVisible(false);
            session.viewerHidden = true;
        }

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.DisableGravity", true))
        {
            if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.UseFlightForParking", false))
            {
                viewer->SetCanFly(true);
                viewer->SetDisableGravity(true);
                viewer->SetHover(true);
            }
            viewer->StopMoving();
        }

        viewer->SetClientControl(viewer, false);
        session.movementLocked = true;

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.UseInvisibleDisplay", true))
        {
            if (!session.invisibleDisplayApplied)
                session.priorDisplayId = viewer->GetDisplayId();

            viewer->SetDisplayId(sConfigMgr->GetOption<uint32>("ArenaReplay.SpectatorShell.InvisibleDisplayId", 11686u));
            session.invisibleDisplayApplied = true;
        }

        StripReplayViewerVisualEquipment(viewer, session);

        if (!sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.MoveBodyToHoldingPoint", true))
            return;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float o = 0.0f;
        if (!GetReplayFixedCameraFallbackPosition(session, x, y, z, o))
            return;

        if (viewer->GetMapId() == session.replayMapId)
            viewer->NearTeleportTo(x, y, z, o);

        LOG_INFO("server.loading", "[RTG][REPLAY][SPECTATOR_SHELL] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} x={} y={} z={} o={} hidden={} invisibleDisplay={} reason={} result=parked",
            session.replayId,
            viewer->GetGUID().GetCounter(),
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            x,
            y,
            z,
            o,
            session.viewerHidden ? 1 : 0,
            session.invisibleDisplayApplied ? 1 : 0,
            reason ? reason : "unknown");
    }

    static bool ShouldCancelReplayForCameraAnchor(ActiveReplaySession const& session)
    {
        return session.cameraAnchorFailed && (ReplayCameraAnchorRequired() || ReplayCameraFallbackMode() == 2);
    }

    static void ApplyReplayUtilityUnitFlags(Creature* unit)
    {
        if (!unit)
            return;

        unit->SetReactState(REACT_PASSIVE);
        unit->SetFaction(35);
        unit->StopMoving();
        unit->SetUInt32Value(UNIT_FIELD_FLAGS, unit->GetUInt32Value(UNIT_FIELD_FLAGS) | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_PACIFIED);
    }

    static Creature* SummonReplayActorClone(Player* viewer, ActiveReplaySession const& session, uint32 entry, float x, float y, float z, float o)
    {
        if (!viewer)
            return nullptr;

        Creature* unit = viewer->SummonCreature(entry, x, y, z, o, TEMPSUMMON_MANUAL_DESPAWN, 0, nullptr, true);
        if (!unit)
            return nullptr;

        unit->SetPhaseMask(session.replayPhaseMask ? session.replayPhaseMask : viewer->GetPhaseMask(), true);
        ApplyReplayUtilityUnitFlags(unit);
        unit->SetVisible(true);
        return unit;
    }

    static Creature* SummonReplayCameraAnchor(Player* viewer, ActiveReplaySession const& session, uint32 entry, float x, float y, float z, float o)
    {
        if (!viewer)
            return nullptr;

        Creature* unit = viewer->SummonCreature(entry, x, y, z, o, TEMPSUMMON_MANUAL_DESPAWN, 0, nullptr, true);
        if (!unit)
            return nullptr;

        unit->SetPhaseMask(session.replayPhaseMask ? session.replayPhaseMask : viewer->GetPhaseMask(), true);
        ApplyReplayUtilityUnitFlags(unit);
        unit->SetCanFly(true);
        unit->SetDisableGravity(true);
        unit->SetHover(true);
        unit->SetUInt32Value(UNIT_FIELD_FLAGS, unit->GetUInt32Value(UNIT_FIELD_FLAGS) | UNIT_FLAG_NOT_SELECTABLE);
        unit->SetVisible(false);
        return unit;
    }

    static Creature* EnsureReplayCameraAnchor(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!viewer || !viewer->GetMap())
            return nullptr;

        if (Creature* existing = FindReplayCameraAnchor(viewer, session))
            return existing;

        uint32 nowMs = GetReplayNowMs();
        if (session.cameraAnchorFailed)
        {
            if (session.nextCameraAnchorRetryMs == 0 && ReplayCameraAnchorFailLogOnce())
                return nullptr;

            if (session.nextCameraAnchorRetryMs != 0 && nowMs < session.nextCameraAnchorRetryMs)
                return nullptr;
        }

        uint32 cameraEntry = GetReplayCameraAnchorEntry();
        if (!sObjectMgr->GetCreatureTemplate(cameraEntry))
        {
            if (!session.cameraAnchorFailureLogged || !ReplayCameraAnchorFailLogOnce())
            {
                LOG_ERROR("server.loading", "[RTG][REPLAY][CAMERA_ANCHOR_TEMPLATE_MISSING] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} entry={} result=missing_template",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    session.replayPhaseMask,
                    cameraEntry);
            }

            session.cameraAnchorFailed = true;
            session.cameraAnchorFailureLogged = true;
            session.nextCameraAnchorRetryMs = 0;
            return nullptr;
        }

        float x = viewer->GetPositionX();
        float y = viewer->GetPositionY();
        float z = viewer->GetPositionZ();
        float o = viewer->GetOrientation();

        uint32 flatIndex = 1;
        if (ActorTrack const* track = GetSelectedReplayActorTrack(const_cast<MatchRecord&>(match), session, &flatIndex))
        {
            bool haveFrame = false;
            ActorFrame frame = GetInterpolatedActorFrame(*track, session.replayPlaybackMs, haveFrame);
            if (haveFrame)
            {
                x = frame.x;
                y = frame.y;
                z = frame.z + sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.FollowHeight", 1.75f);
                o = frame.o;
            }
        }

        Creature* anchor = SummonReplayCameraAnchor(viewer, session, cameraEntry, x, y, z, o);
        if (!anchor)
        {
            if (!session.cameraAnchorFailureLogged || !ReplayCameraAnchorFailLogOnce())
            {
                LOG_ERROR("server.loading", "[RTG][REPLAY][CAMERA_ANCHOR_FAIL] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} entry={} x={} y={} z={} o={} result=summon_failed",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    session.replayPhaseMask,
                    cameraEntry,
                    x,
                    y,
                    z,
                    o);
            }

            session.cameraAnchorFailed = true;
            session.cameraAnchorFailureLogged = true;
            uint32 retryMs = ReplayCameraAnchorRetryMs();
            session.nextCameraAnchorRetryMs = retryMs ? nowMs + retryMs : 0;
            return nullptr;
        }

        session.cameraAnchorGuid = anchor->GetGUID();
        session.cameraAnchorDisplayId = anchor->GetDisplayId();
        session.cameraAnchorFailed = false;
        session.cameraAnchorFailureLogged = false;
        session.nextCameraAnchorRetryMs = 0;
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

        uint32 clonesCleaned = 0;
        uint64 anchorGuid = session.cameraAnchorGuid ? session.cameraAnchorGuid.GetCounter() : 0;
        char const* anchorResult = session.cameraAnchorGuid ? "missing" : "none";
        if (viewer && viewer->GetMap())
        {
            if (Creature* anchor = FindReplayCameraAnchor(viewer, session))
            {
                anchor->DespawnOrUnsummon(Milliseconds(0));
                anchorResult = "ok";
            }

            for (ReplayCloneBinding const& binding : session.cloneBindings)
                if (Creature* clone = viewer->GetMap()->GetCreature(binding.cloneGuid))
                {
                    clone->DespawnOrUnsummon(Milliseconds(0));
                    ++clonesCleaned;
                }
        }

        LOG_INFO("server.loading", "[RTG][REPLAY][CAMERA_ANCHOR_CLEANUP] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} anchorGuid={} result={}",
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            anchorGuid,
            anchorResult);
        LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_CLEANUP] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} clones={} cleaned={} result=ok",
            session.replayId,
            viewer ? viewer->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            session.cloneBindings.size(),
            clonesCleaned);

        session.cameraAnchorGuid.Clear();
        session.cloneBindings.clear();
        session.cloneSceneBuilt = false;
        session.nextCloneSyncMs = 0;
        session.cameraAnchorFailed = false;
        session.cameraAnchorFailureLogged = false;
        session.nextCameraAnchorRetryMs = 0;
        session.fixedCameraFallbackApplied = false;
        session.cameraAnchorDisplayId = 0;
        session.hasSmoothedCamera = false;
        session.lastCameraMoveMs = 0;
    }

    static bool GetFirstValidActorFrame(ActorTrack const& track, ActorFrame& out)
    {
        for (ActorFrame const& frame : track.frames)
        {
            if (!std::isfinite(frame.x) || !std::isfinite(frame.y) || !std::isfinite(frame.z) || !std::isfinite(frame.o))
                continue;

            out = frame;
            return true;
        }

        return false;
    }

    static bool GetLastValidActorFrame(ActorTrack const& track, ActorFrame& out)
    {
        for (auto itr = track.frames.rbegin(); itr != track.frames.rend(); ++itr)
        {
            ActorFrame const& frame = *itr;
            if (!std::isfinite(frame.x) || !std::isfinite(frame.y) || !std::isfinite(frame.z) || !std::isfinite(frame.o))
                continue;

            out = frame;
            return true;
        }

        return false;
    }

    static bool GetPlayableActorFrameBounds(MatchRecord const& match, uint32& firstFrameMs, uint32& lastFrameMs, uint32& actorCount, uint32& team0Count, uint32& team1Count)
    {
        bool haveFrame = false;
        firstFrameMs = 0;
        lastFrameMs = 0;
        actorCount = 0;
        team0Count = 0;
        team1Count = 0;

        std::vector<ReplayActorSelectionRef> playable = BuildPlayableReplayActorSelections(match);
        for (ReplayActorSelectionRef const& ref : playable)
        {
            std::vector<ActorTrack> const* tracks = ref.winnerSide ? &match.winnerActorTracks : &match.loserActorTracks;
            if (!tracks || ref.trackIndex >= tracks->size())
                continue;

            ActorTrack const& track = (*tracks)[ref.trackIndex];
            ActorFrame firstFrame;
            ActorFrame lastFrame;
            if (!GetFirstValidActorFrame(track, firstFrame) || !GetLastValidActorFrame(track, lastFrame))
                continue;

            if (!haveFrame || firstFrame.timestamp < firstFrameMs)
                firstFrameMs = firstFrame.timestamp;
            if (!haveFrame || lastFrame.timestamp > lastFrameMs)
                lastFrameMs = lastFrame.timestamp;

            haveFrame = true;
            ++actorCount;
            if (ref.winnerSide)
                ++team0Count;
            else
                ++team1Count;
        }

        return haveFrame;
    }

    static uint32 GetReplayMatchElapsedMs(ActiveReplaySession const& session)
    {
        if (session.replayPlaybackMs <= session.matchOpenMs)
            return 0;

        return session.replayPlaybackMs - session.matchOpenMs;
    }

    static uint32 GetReplayGateOpenRawMs(ActiveReplaySession const& session)
    {
        uint32 delayMs = sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.GateOpenDelayMs", 0u);
        if (sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.OpenGatesAtMatchOpen", true))
            return session.matchOpenMs + delayMs;

        return delayMs;
    }

    static uint32 GetReplayBuffActivateRawMs(ActiveReplaySession const& session)
    {
        return session.matchOpenMs + SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.BuffDelaySeconds", 90u));
    }

    static char const* GetReplayGateStateName(ActiveReplaySession const& session)
    {
        uint32 gates = 0;
        uint32 open = 0;
        for (ReplayDynamicObjectBinding const& binding : session.replayObjects)
        {
            if (binding.role != RTG_REPLAY_OBJECT_GATE || !binding.spawned || binding.cleaned)
                continue;

            ++gates;
            if (binding.active)
                ++open;
        }

        if (gates == 0)
            return "none";
        if (open == gates)
            return "open";
        if (open == 0)
            return "closed";
        return "partial";
    }

    static bool IsReplayTimelineComplete(MatchRecord const& match, ActiveReplaySession const& session)
    {
        if (ReplaySyntheticReplayPacketEmitterBackendEnabled())
            return session.lastPlayableActorFrameMs == 0 || session.replayPlaybackMs >= session.lastPlayableActorFrameMs;

        if (!match.packets.empty())
            return false;

        if (ReplayCloneModeEnabled() && session.lastPlayableActorFrameMs != 0)
            return session.replayPlaybackMs >= session.lastPlayableActorFrameMs;

        return true;
    }

    static void InitializeReplayCloneTimeline(MatchRecord const& match, ActiveReplaySession& session)
    {
        if (session.cloneTimelineInitialized)
            return;

        uint32 actorCount = 0;
        uint32 team0Count = 0;
        uint32 team1Count = 0;
        uint32 firstFrameMs = 0;
        uint32 lastFrameMs = 0;
        bool haveActorFrames = GetPlayableActorFrameBounds(match, firstFrameMs, lastFrameMs, actorCount, team0Count, team1Count);

        session.packetStartMs = match.packets.empty() ? 0 : match.packets.front().timestamp;
        session.firstPlayableActorFrameMs = haveActorFrames ? firstFrameMs : 0;
        session.lastPlayableActorFrameMs = haveActorFrames ? lastFrameMs : 0;
        session.cloneSceneStartMs = haveActorFrames && ReplayCloneModeStartAtFirstActorFrame() ? firstFrameMs : 0;
        session.matchOpenMs = haveActorFrames && ReplayCloneModeMatchOpenFromFirstActorFrame() ? firstFrameMs : session.cloneSceneStartMs;

        if ((ReplayCloneModeEnabled() || ReplaySyntheticReplayPacketEmitterBackendEnabled()) && ReplayCloneModeTrimPreMatchDeadAir() && session.cloneSceneStartMs != 0)
            session.replayPlaybackMs = session.cloneSceneStartMs;

        session.cloneTimelineInitialized = true;

        uint32 gateOpenMs = ReplayCloneModeTrimPreMatchDeadAir() ? 0 : GetReplayGateOpenRawMs(session);
        uint32 buffActivateMs = ReplayCloneModeTrimPreMatchDeadAir()
            ? SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.BuffDelaySeconds", 90u))
            : GetReplayBuffActivateRawMs(session);

        LOG_INFO("server.loading", "[RTG][REPLAY][SCENE_TIMELINE] replay={} nativeMap={} replayMap={} packetStartMs={} firstPlayableActorFrameMs={} cloneSceneStartMs={} trimPreMatchDeadAir={} gateOpenMs={} buffActivateMs={} actorCount={} team0Count={} team1Count={}",
            session.replayId,
            session.nativeMapId,
            session.replayMapId,
            session.packetStartMs,
            session.firstPlayableActorFrameMs,
            session.cloneSceneStartMs,
            ReplayCloneModeTrimPreMatchDeadAir() ? 1 : 0,
            gateOpenMs,
            buffActivateMs,
            actorCount,
            team0Count,
            team1Count);
    }

    struct ReplayCloneVisibilityAudit
    {
        uint32 totalBindings = 0;
        uint32 team0Bindings = 0;
        uint32 team1Bindings = 0;
        uint32 visibleClones = 0;
        uint32 team0Visible = 0;
        uint32 team1Visible = 0;
        uint32 wrongPhase = 0;
        uint32 invisibleDisplay = 0;
        uint32 hiddenFlag = 0;
        uint32 notOnMap = 0;
        uint32 noGuid = 0;
        uint32 noFirstSync = 0;
    };

    static ReplayCloneVisibilityAudit AuditReplayCloneVisibility(Player* viewer, MatchRecord const& match, ActiveReplaySession const& session)
    {
        ReplayCloneVisibilityAudit audit;
        audit.totalBindings = uint32(session.cloneBindings.size());

        if (!viewer || !viewer->GetMap())
            return audit;

        for (ReplayCloneBinding const& binding : session.cloneBindings)
        {
            if (binding.winnerSide)
                ++audit.team0Bindings;
            else
                ++audit.team1Bindings;

            if (!binding.cloneGuid)
            {
                ++audit.noGuid;
                continue;
            }

            Creature* clone = viewer->GetMap()->GetCreature(binding.cloneGuid);
            if (!clone)
            {
                ++audit.notOnMap;
                continue;
            }

            ActorTrack const* track = FindReplayActorTrackByGuid(match, binding.actorGuid);
            ActorFrame firstFrame;
            if (!track || !GetFirstValidActorFrame(*track, firstFrame))
                ++audit.noFirstSync;

            bool wrongPhase = session.replayPhaseMask && clone->GetPhaseMask() != session.replayPhaseMask;
            bool hidden = !clone->IsVisible();
            bool invisibleDisplay = IsInvalidReplayCloneDisplay(clone->GetDisplayId(), session);
            if (wrongPhase)
                ++audit.wrongPhase;
            if (hidden)
                ++audit.hiddenFlag;
            if (invisibleDisplay)
                ++audit.invisibleDisplay;

            if (!wrongPhase && !hidden && !invisibleDisplay)
            {
                ++audit.visibleClones;
                if (binding.winnerSide)
                    ++audit.team0Visible;
                else
                    ++audit.team1Visible;
            }
        }

        LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_VISIBILITY_AUDIT] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} totalBindings={} team0Bindings={} team1Bindings={} visibleClones={} team0Visible={} team1Visible={} wrongPhase={} invisibleDisplay={} hiddenFlag={} notOnMap={} noGuid={} noFirstSync={} result={}",
            session.replayId,
            viewer->GetGUID().GetCounter(),
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            audit.totalBindings,
            audit.team0Bindings,
            audit.team1Bindings,
            audit.visibleClones,
            audit.team0Visible,
            audit.team1Visible,
            audit.wrongPhase,
            audit.invisibleDisplay,
            audit.hiddenFlag,
            audit.notOnMap,
            audit.noGuid,
            audit.noFirstSync,
            (audit.team0Bindings == audit.team0Visible && audit.team1Bindings == audit.team1Visible) ? "ok" : "warn");

        return audit;
    }

    static void RepairReplayCloneDisplays(Player* viewer, MatchRecord const& match, ActiveReplaySession const& session, char const* reason)
    {
        if (!viewer || !viewer->GetMap())
            return;

        for (ReplayCloneBinding const& binding : session.cloneBindings)
        {
            Creature* clone = viewer->GetMap()->GetCreature(binding.cloneGuid);
            ActorTrack const* track = FindReplayActorTrackByGuid(match, binding.actorGuid);
            if (!clone || !track)
                continue;

            uint32 originalDisplay = clone->GetDisplayId();
            if (!IsInvalidReplayCloneDisplay(originalDisplay, session))
                continue;

            ReplayActorAppearanceSnapshot const* snapshot = FindReplayActorAppearanceSnapshot(match, binding.actorGuid);
            char const* repairReason = "audit_repair";
            uint32 repairedDisplay = ResolveCreatureSilhouetteDisplay(snapshot, *track, binding.winnerSide, repairReason);
            clone->SetNativeDisplayId(repairedDisplay);
            clone->SetDisplayId(repairedDisplay);
            clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, 0);
            clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1, 0);
            clone->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2, 0);
            clone->SetVisible(true);

            LOG_WARN("server.loading", "[RTG][REPLAY][CLONE_DISPLAY_REPAIR] replay={} actorGuid={} actorName={} class={} cloneGuid={} originalDisplay={} repairedDisplay={} reason={} result={}",
                session.replayId,
                track->guid,
                track->name,
                GetClassToken(track->playerClass),
                clone->GetGUID().GetCounter(),
                originalDisplay,
                clone->GetDisplayId(),
                reason ? reason : "audit",
                IsInvalidReplayCloneDisplay(clone->GetDisplayId(), session) ? "still_invisible" : "ok");
        }
    }

    static bool BuildReplayActorCloneScene(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!viewer || !viewer->GetMap())
            return false;

        bool cloneSceneEnabled = sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.Enable", true);
        bool syntheticCloneFallbackRequested = GetReplayActorVisualBackend() == RTG_REPLAY_ACTOR_VISUAL_SYNTHETIC_PLAYER_OBJECT_EXPERIMENTAL && ReplaySyntheticServerCloneFallbackEnabled();
        if (!cloneSceneEnabled && !ReplayRecordedPacketVisualBackendEnabled() && !syntheticCloneFallbackRequested)
            return false;

        if (session.cloneSceneBuilt)
            return ReplayRecordedPacketVisualBackendEnabled() || !session.cloneBindings.empty();

        uint32 totalActors = uint32(match.winnerActorTracks.size() + match.loserActorTracks.size());
        uint32 playableActors = 0;
        uint32 excludedNoFrames = 0;
        uint32 excludedNoAppearance = 0;
        uint32 missingAppearanceFallback = 0;
        uint32 team0Count = 0;
        uint32 team1Count = 0;
        std::unordered_set<uint64> seenClassifyGuids;

        auto classifyTracks = [&](std::vector<ActorTrack> const& tracks, bool winnerSide)
        {
            for (ActorTrack const& track : tracks)
            {
                bool hasFrames = IsReplayActorTrackPlayable(track);
                bool hasAppearance = FindReplayActorAppearanceSnapshot(match, track.guid) != nullptr;
                char const* excludedReason = "none";
                if (!hasFrames)
                {
                    excludedReason = "no_frames";
                    ++excludedNoFrames;
                }
                else if (seenClassifyGuids.find(track.guid) != seenClassifyGuids.end())
                    excludedReason = "duplicate_guid";
                else
                {
                    ++playableActors;
                    if (winnerSide)
                        ++team0Count;
                    else
                        ++team1Count;
                    if (!hasAppearance)
                        ++missingAppearanceFallback;
                    seenClassifyGuids.insert(track.guid);
                }

                LOG_INFO("server.loading", "[RTG][REPLAY][ACTOR_CLASSIFY] replay={} viewerGuid={} nativeMap={} replayMap={} actorGuid={} actorName={} team={} class={} hasFrames={} hasAppearance={} excludedReason={}",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    track.guid,
                    track.name,
                    winnerSide ? "winner" : "loser",
                    GetClassToken(track.playerClass),
                    hasFrames ? 1 : 0,
                    hasAppearance ? 1 : 0,
                    excludedReason);
            }
        };

        classifyTracks(match.winnerActorTracks, true);
        classifyTracks(match.loserActorTracks, false);

        std::vector<ReplayActorSelectionRef> playable = BuildPlayableReplayActorSelections(match);
        ReplayActorVisualBackend visualBackend = GetReplayActorVisualBackend();
        LOG_INFO("server.loading", "[RTG][REPLAY][ACTOR_VISUAL_BACKEND] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} backend={} result={}",
            session.replayId,
            viewer->GetGUID().GetCounter(),
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            GetReplayActorVisualBackendName(visualBackend),
            visualBackend == RTG_REPLAY_ACTOR_VISUAL_RECORDED_PACKET_STREAM ? "recorded_packet_stream_authoritative" :
                (visualBackend == RTG_REPLAY_ACTOR_VISUAL_SYNTHETIC_PLAYER_OBJECT_EXPERIMENTAL ? "synthetic_replay_packet_emitter_foundation" :
                    (visualBackend == RTG_REPLAY_ACTOR_VISUAL_CREATURE_SILHOUETTE ? "creature_silhouette_debug" : "experimental_planned_only")));
        LogReplayPlayerBodyPlan(viewer, match, session);

        bool syntheticServerCloneFallback = false;
        if (visualBackend == RTG_REPLAY_ACTOR_VISUAL_SYNTHETIC_PLAYER_OBJECT_EXPERIMENTAL)
        {
            if (!ReplaySyntheticReplayPacketEmitterBackendEnabled())
            {
                LOG_ERROR("server.loading", "[RTG][REPLAY][SYNTHETIC_BACKEND] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} result=disabled",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    session.replayPhaseMask);
                return false;
            }

            LogSyntheticReplayPacketEmitterPlan(viewer, match, session);
            EnsureSyntheticReplayActorsCreated(viewer, match, session);

            syntheticServerCloneFallback = ReplaySyntheticServerCloneFallbackEnabled();
            LOG_INFO("server.loading", "[RTG][REPLAY][SYNTHETIC_CLONE_FALLBACK] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} enabled={} emitUnitVisualPackets={} result={}",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                session.replayPhaseMask,
                syntheticServerCloneFallback ? 1 : 0,
                ReplaySyntheticEmitUnitVisualPackets() ? 1 : 0,
                syntheticServerCloneFallback ? "server_clone_visibility_enabled" : "packet_only");

            if (!syntheticServerCloneFallback)
            {
                session.cloneSceneBuilt = true;
                return true;
            }
        }

        if (visualBackend == RTG_REPLAY_ACTOR_VISUAL_RECORDED_PACKET_STREAM)
        {
            bool requirePackets = sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.RecordedPacketStream.RequirePackets", true);
            if (requirePackets && match.packets.empty())
            {
                LOG_ERROR("server.loading", "[RTG][REPLAY][PACKET_VISUAL_FAIL] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} totalActors={} playableActors={} team0Count={} team1Count={} packets=0 reason=no_packets result=cancel",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    session.replayPhaseMask,
                    totalActors,
                    playableActors,
                    team0Count,
                    team1Count);
                return false;
            }

            session.cloneSceneBuilt = true;
            LOG_INFO("server.loading", "[RTG][REPLAY][PACKET_VISUAL_SCENE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} totalActors={} playableActors={} team0Count={} team1Count={} packets={} result=use_recorded_packets_no_creature_clones",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                session.replayPhaseMask,
                totalActors,
                playableActors,
                team0Count,
                team1Count,
                match.packets.size());
            return true;
        }

        uint32 prewarmTeam0 = 0;
        uint32 prewarmTeam1 = 0;
        if (GetReplayCloneEntry() == GetReplayCameraAnchorEntry())
        {
            LOG_ERROR("server.loading", "[RTG][REPLAY][CLONE_TEMPLATE_INVISIBLE] replay={} viewerGuid={} nativeMap={} replayMap={} cloneEntry={} cameraAnchorEntry={} result=same_template_for_clone_and_anchor",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                GetReplayCloneEntry(),
                GetReplayCameraAnchorEntry());
        }

        for (ReplayActorSelectionRef const& ref : playable)
        {
            std::vector<ActorTrack> const* tracks = ref.winnerSide ? &match.winnerActorTracks : &match.loserActorTracks;
            if (!tracks || ref.trackIndex >= tracks->size())
                continue;

            ActorTrack const& track = (*tracks)[ref.trackIndex];
            if (!IsReplayActorTrackPlayable(track) || track.frames.empty())
                continue;

            ActorFrame frame;
            if (!GetFirstValidActorFrame(track, frame))
            {
                LOG_ERROR("server.loading", "[RTG][REPLAY][CLONE_PREWARM_FAIL] replay={} viewerGuid={} nativeMap={} replayMap={} actorGuid={} actorName={} team={} reason=no_first_valid_frame result=fail",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    track.guid,
                    track.name,
                    ref.winnerSide ? "winner" : "loser");
                continue;
            }

            Creature* clone = SummonReplayActorClone(viewer, session, GetReplayCloneEntry(), frame.x, frame.y, frame.z, frame.o);
            if (!clone)
            {
                LOG_ERROR("server.loading", "[RTG][REPLAY][CLONE_PREWARM_FAIL] replay={} viewerGuid={} nativeMap={} replayMap={} actorGuid={} actorName={} team={} firstFrameMs={} x={} y={} z={} o={} reason=summon_failed result=fail",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    track.guid,
                    track.name,
                    ref.winnerSide ? "winner" : "loser",
                    frame.timestamp,
                    frame.x,
                    frame.y,
                    frame.z,
                    frame.o);
                continue;
            }

            uint32 originalDisplay = clone->GetDisplayId();
            if (IsInvalidReplayCloneDisplay(originalDisplay, session))
            {
                LOG_WARN("server.loading", "[RTG][REPLAY][CLONE_TEMPLATE_INVISIBLE] replay={} viewerGuid={} nativeMap={} replayMap={} actorGuid={} actorName={} cloneEntry={} cloneGuid={} originalDisplay={} result=force_fallback",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    track.guid,
                    track.name,
                    GetReplayCloneEntry(),
                    clone->GetGUID().GetCounter(),
                    originalDisplay);
            }

            ReplayActorAppearanceSnapshot const* snapshot = FindReplayActorAppearanceSnapshot(match, track.guid);
            if (!snapshot)
            {
                LOG_WARN("server.loading", "[RTG][REPLAY][ACTOR_CLASSIFY] replay={} viewerGuid={} nativeMap={} replayMap={} actorGuid={} actorName={} team={} class={} hasFrames=1 hasAppearance=0 excludedReason=missingAppearanceFallback",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    track.guid,
                    track.name,
                    ref.winnerSide ? "winner" : "loser",
                    GetClassToken(track.playerClass));
            }
            uint32 finalDisplay = ApplyReplayActorAppearanceToClone(clone, snapshot, track, ref.winnerSide, session);
            if (IsInvalidReplayCloneDisplay(finalDisplay, session) && sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.AbortInvisibleFallbackClones", true))
            {
                LOG_ERROR("server.loading", "[RTG][REPLAY][CLONE_PREWARM_FAIL] replay={} viewerGuid={} nativeMap={} replayMap={} actorGuid={} actorName={} team={} cloneGuid={} display={} reason=invisible_display_after_repair result=fail",
                    session.replayId,
                    viewer->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    track.guid,
                    track.name,
                    ref.winnerSide ? "winner" : "loser",
                    clone->GetGUID().GetCounter(),
                    finalDisplay);
                clone->DespawnOrUnsummon(Milliseconds(0));
                continue;
            }
            session.cloneBindings.push_back({ track.guid, ref.winnerSide, clone->GetGUID() });
            if (ref.winnerSide)
                ++prewarmTeam0;
            else
                ++prewarmTeam1;

            LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_PREWARM] replay={} viewerGuid={} nativeMap={} replayMap={} actorGuid={} actorName={} team={} firstFrameMs={} x={} y={} z={} o={} cloneGuid={} result=ok",
                session.replayId,
                viewer->GetGUID().GetCounter(),
                session.nativeMapId,
                session.replayMapId,
                track.guid,
                track.name,
                ref.winnerSide ? "winner" : "loser",
                frame.timestamp,
                frame.x,
                frame.y,
                frame.z,
                frame.o,
                clone->GetGUID().GetCounter());
        }

        session.cloneSceneBuilt = true;
        LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_PREWARM] replay={} viewerGuid={} nativeMap={} replayMap={} team0Count={} team1Count={} total={} result=summary",
            session.replayId,
            viewer->GetGUID().GetCounter(),
            session.nativeMapId,
            session.replayMapId,
            prewarmTeam0,
            prewarmTeam1,
            prewarmTeam0 + prewarmTeam1);
        LOG_INFO("server.loading", "[RTG][REPLAY][CLONE_SCENE] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} totalActors={} playableActors={} clonedActors={} excludedNoFrames={} excludedNoAppearance={} missingAppearanceFallback={} team0Count={} team1Count={} result={}",
            session.replayId,
            viewer->GetGUID().GetCounter(),
            session.nativeMapId,
            session.replayMapId,
            session.replayPhaseMask,
            totalActors,
            playableActors,
            session.cloneBindings.size(),
            excludedNoFrames,
            excludedNoAppearance,
            missingAppearanceFallback,
            team0Count,
            team1Count,
            session.cloneBindings.empty() ? "fail" : "ok");
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
        {559, 725, 183977, 4023.709f, 2981.777f, 10.70117f, -2.648788f, 0.0f, 0.0f, 0.9697962f, -0.2439165f, RTG_REPLAY_OBJECT_STRUCTURAL, true, false},
        {559, 725, 183979, 4090.064f, 2858.438f, 10.23631f, 0.4928045f, 0.0f, 0.0f, 0.2439165f, 0.9697962f, RTG_REPLAY_OBJECT_STRUCTURAL, true, false},
        {559, 725, 183978, 4031.854f, 2966.833f, 12.0462f, -2.648788f, 0.0f, 0.0f, 0.9697962f, -0.2439165f, RTG_REPLAY_OBJECT_GATE, true, false},
        {559, 725, 183980, 4081.179f, 2874.970f, 12.00171f, 0.4928045f, 0.0f, 0.0f, 0.2439165f, 0.9697962f, RTG_REPLAY_OBJECT_GATE, true, false},
        {559, 725, 184663, 4009.189941f, 2895.250000f, 13.052700f, -1.448624f, 0.0f, 0.0f, 0.6626201f, -0.7489557f, RTG_REPLAY_OBJECT_BUFF, false, false},
        {559, 725, 184664, 4103.330078f, 2946.350098f, 13.051300f, -0.06981307f, 0.0f, 0.0f, 0.03489945f, -0.9993908f, RTG_REPLAY_OBJECT_BUFF, false, false},

        {562, 726, 183970, 6299.116f, 296.5494f, 3.308032f, 0.8813917f, 0.0f, 0.0f, 0.4265689f, 0.9044551f, RTG_REPLAY_OBJECT_STRUCTURAL, true, false},
        {562, 726, 183972, 6177.708f, 227.3481f, 3.604374f, -2.260201f, 0.0f, 0.0f, 0.9044551f, -0.4265689f, RTG_REPLAY_OBJECT_STRUCTURAL, true, false},
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
        return sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.Enable", true);
    }

    static bool ReplayDynamicObjectsDebugEnabled()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.Debug", false);
    }

    static bool ReplayDalaranWaterAllowedForCurrentBackend()
    {
        if (GetReplayActorVisualBackend() == RTG_REPLAY_ACTOR_VISUAL_SYNTHETIC_PLAYER_OBJECT_EXPERIMENTAL)
            return sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.DalaranWater.EnableInSyntheticBackend", false);

        return true;
    }

    static bool ReplayBuffObjectsSpawnOnlyOnActivation()
    {
        return sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.Buff.SpawnOnlyOnActivation", true);
    }

    static uint32 SecondsToMs(uint32 seconds)
    {
        return seconds * IN_MILLISECONDS;
    }


    static uint32 GetDalaranWaterRandomDelayMs(ActiveReplaySession const& session)
    {
        uint32 minSeconds = sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.DalaranWater.TimerMinSeconds", 30u);
        uint32 maxSeconds = sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.DalaranWater.TimerMaxSeconds", 60u);
        if (maxSeconds < minSeconds)
            std::swap(minSeconds, maxSeconds);

        if (!sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.DalaranWater.RandomizeLikeLive", true))
            return SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.DalaranWater.FirstDelaySeconds", minSeconds));

        uint32 span = maxSeconds - minSeconds + 1;
        uint32 seed = session.replayId ? session.replayId : session.nativeMapId;
        seed ^= (session.dsWaterCycle + 1u) * 1103515245u;
        seed ^= 0x9E3779B9u;
        return SecondsToMs(minSeconds + (span ? (seed % span) : 0u));
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
            case RTG_REPLAY_OBJECT_STRUCTURAL:
                return "STRUCTURAL";
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
            case RTG_REPLAY_OBJECT_STRUCTURAL:
                return active ? "active" : "ready";
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

        if (def.role == RTG_REPLAY_OBJECT_STRUCTURAL &&
            !sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.SpawnAllArenaObjects", true))
            return false;

        if ((def.role == RTG_REPLAY_OBJECT_DS_WATER_COLLISION || def.role == RTG_REPLAY_OBJECT_DS_WATER_VISUAL) &&
            (!sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.DalaranWater.Enable", true) || !ReplayDalaranWaterAllowedForCurrentBackend()))
            return false;

        if (def.role == RTG_REPLAY_OBJECT_DS_WATER_COLLISION &&
            !sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.DalaranWater.CollisionEnable", false))
            return false;

        if (session.nativeMapId == 618 && !sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.Ring.Enable", true))
            return false;

        return true;
    }

    static bool IsReplayDynamicObjectRequired(ReplayDynamicObjectTemplate const& def)
    {
        if (def.role == RTG_REPLAY_OBJECT_DS_WATER_COLLISION)
            return sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.DalaranWater.CollisionRequired", false);

        if (def.role == RTG_REPLAY_OBJECT_STRUCTURAL)
            return sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.AbortIfRequiredStructuralObjectFails", true);

        if (def.role == RTG_REPLAY_OBJECT_GATE)
            return sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.AbortIfRequiredGateFails", true);

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
        std::string tagName = tag ? tag : "";
        bool okResult = result && std::string(result) == "ok";
        if (!ReplayDynamicObjectsDebugEnabled() && okResult && tagName != "OBJECT_SPAWN" && tagName != "OBJECT_STATE" && tagName != "OBJECT_CLEANUP")
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
            GetReplayMatchElapsedMs(session),
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
            GetReplayMatchElapsedMs(session),
            result ? result : "ok");
    }

    static GameObject* FindReplayDynamicObject(Player* viewer, ReplayDynamicObjectBinding const& binding)
    {
        if (!viewer || !viewer->GetMap() || !binding.guid)
            return nullptr;

        return viewer->GetMap()->GetGameObject(binding.guid);
    }

    static bool SpawnReplayDynamicObjectFromTemplate(Player* viewer, ActiveReplaySession& session, ReplayDynamicObjectTemplate const& def, bool activeOnSpawn, char const* reason)
    {
        if (!viewer || !viewer->GetMap() || viewer->GetMapId() != session.replayMapId)
            return false;

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
            LogReplayDynamicObjectEvent("OBJECT_SPAWN", session, viewer, &failed, def.entry, def.role, reason ? reason : "spawn", required ? "hard_fail" : "missing_optional");
            return !required;
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
        session.replayObjects.push_back(binding);
        ReplayDynamicObjectBinding& stored = session.replayObjects.back();

        LogReplayDynamicObjectEvent("OBJECT_SPAWN", session, viewer, &stored, def.entry, def.role, reason ? reason : "spawn", "ok");

        if (activeOnSpawn)
        {
            bool effectiveActive = def.invertState ? !activeOnSpawn : activeOnSpawn;
            go->SetGoState(effectiveActive ? GO_STATE_ACTIVE : GO_STATE_READY);
            stored.active = activeOnSpawn;
            LogReplayDynamicObjectEvent("OBJECT_STATE", session, viewer, &stored, stored.entry, stored.role, ReplayDynamicObjectStateName(stored.role, activeOnSpawn), "ok");
        }

        return true;
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
        bool hasRoleBinding = false;
        for (ReplayDynamicObjectBinding const& binding : session.replayObjects)
        {
            if (binding.role == role && binding.spawned && !binding.cleaned)
            {
                hasRoleBinding = true;
                break;
            }
        }

        if (!hasRoleBinding && active && role == RTG_REPLAY_OBJECT_BUFF && ReplayBuffObjectsSpawnOnlyOnActivation())
        {
            for (ReplayDynamicObjectTemplate const& def : ReplayDynamicObjectTemplates)
            {
                if (def.role != RTG_REPLAY_OBJECT_BUFF || !ShouldIncludeReplayDynamicObjectTemplate(session, def))
                    continue;

                if (!SpawnReplayDynamicObjectFromTemplate(viewer, session, def, true, "activate_spawn"))
                    ok = false;
            }
        }

        for (ReplayDynamicObjectBinding& binding : session.replayObjects)
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
        if (session.nativeMapId != 618)
            return;

        uint32 elevators = 0;
        uint32 pillars = 0;
        uint32 fire = 0;
        uint32 fireDoors = 0;
        uint32 gears = 0;
        uint32 pulleys = 0;
        uint32 buffs = 0;

        for (ReplayDynamicObjectBinding const& binding : session.replayObjects)
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
        if (session.nativeMapId != 618)
            return;

        if (!ReplayDynamicObjectsDebugEnabled() && (!result || std::string(result) != "initialized"))
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
            GetReplayMatchElapsedMs(session),
            result ? result : "ok");
    }

    static bool SpawnReplayDynamicObjects(Player* viewer, ActiveReplaySession& session)
    {
        if (!ReplayDynamicObjectsEnabled())
            return true;

        if (session.replayObjectsSpawned)
            return true;

        if (!viewer || !viewer->GetMap() || viewer->GetMapId() != session.replayMapId)
            return false;

        bool hardFailure = false;
        uint32 delayedBuffs = 0;
        for (ReplayDynamicObjectTemplate const& def : ReplayDynamicObjectTemplates)
        {
            if (!ShouldIncludeReplayDynamicObjectTemplate(session, def))
                continue;

            if (def.role == RTG_REPLAY_OBJECT_BUFF && ReplayBuffObjectsSpawnOnlyOnActivation())
            {
                ++delayedBuffs;
                LogReplayDynamicObjectEvent("OBJECT_SPAWN", session, viewer, nullptr, def.entry, def.role, "delayed_until_activation", "ok");
                continue;
            }

            bool required = IsReplayDynamicObjectRequired(def);
            if (!SpawnReplayDynamicObjectFromTemplate(viewer, session, def, false, "spawn") && required)
                hardFailure = true;
        }

        session.replayObjectsSpawned = true;
        if (delayedBuffs != 0)
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][BUFF_OBJECT_DELAY] replay={} viewerGuid={} nativeMap={} replayMap={} phase={} delayedBuffs={} activateMs={} result=spawn_on_activation",
                session.replayId,
                viewer ? viewer->GetGUID().GetCounter() : 0,
                session.nativeMapId,
                session.replayMapId,
                session.replayPhaseMask,
                delayedBuffs,
                GetReplayBuffActivateRawMs(session));
        }
        LogRingReplayObjectSummary(session, viewer);
        return !hardFailure;
    }

    static bool InitializeReplayDynamicObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!ReplayDynamicObjectsEnabled())
            return true;

        if (session.replayObjectsInitialized)
            return true;

        if (!sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.SpawnBeforeClones", true) && ReplayDynamicObjectsDebugEnabled())
            LogReplayDynamicTimeline(session, viewer, "SPAWN_ORDER", "before_clones", "forced");

        if (!SpawnReplayDynamicObjects(viewer, session))
            return false;

        bool startWithGatesOpen = sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.StartWithGatesOpen", true);
        if (startWithGatesOpen && session.replayPlaybackMs >= GetReplayGateOpenRawMs(session) && !SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_GATE, true))
            return false;

        if (session.nativeMapId == 617 && sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.DalaranWater.Enable", true) && ReplayDalaranWaterAllowedForCurrentBackend())
        {
            session.dsWaterState = 0;
            session.dsWaterCycle = 0;
            SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_COLLISION, false);
            SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_VISUAL, false);
            session.dsNextEventMs = session.matchOpenMs + GetDalaranWaterRandomDelayMs(session);
            LogReplayDynamicTimeline(session, viewer, "DS_WATER", "scheduled", "ok");
        }

        if (session.nativeMapId == 618 && sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.Ring.Enable", true))
        {
            float firstActorZ = 0.0f;
            bool haveFirstActorZ = GetFirstPlayableActorZ(match, firstActorZ);
            session.rvFirstActorZ = haveFirstActorZ ? firstActorZ : 0.0f;
            session.rvInitialUpper = haveFirstActorZ &&
                sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.Ring.DeriveInitialStateFromFirstActorZ", true) &&
                firstActorZ >= 20.0f;

            if (!SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_ELEVATOR, true))
                return false;

            session.rvState = 0;
            session.rvPillarToggleState = 0;
            session.rvNextEventMs = session.matchOpenMs + sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.Ring.FirstTimerMs", 20500u);
            LogRingReplayState(session, viewer, "initialized");
        }

        session.replayObjectsInitialized = true;
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
                    GetReplayMatchElapsedMs(session));
            }
        }
    }

    static void UpdateDalaranReplayWater(Player* viewer, ActiveReplaySession& session)
    {
        if (session.nativeMapId != 617 || !sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.DalaranWater.Enable", true) || !ReplayDalaranWaterAllowedForCurrentBackend())
            return;

        uint32 warningMs = SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.DalaranWater.WarningSeconds", 5u));
        uint32 durationMs = SecondsToMs(sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.DalaranWater.DurationSeconds", 30u));
        bool collisionEnabled = sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.DalaranWater.CollisionEnable", false);

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
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_COLLISION, collisionEnabled);
                LogReplayDynamicTimeline(session, viewer, "DS_WATER", collisionEnabled ? "active_collision" : "active_visual", "ok");
                session.dsNextEventMs += std::max<uint32>(1u, durationMs);
            }
            else if (session.dsWaterState == 2)
            {
                session.dsWaterState = 0;
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_COLLISION, false);
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_DS_WATER_VISUAL, false);
                ++session.dsWaterCycle;
                LogReplayDynamicTimeline(session, viewer, "DS_WATER", "off", "ok");
                session.dsNextEventMs += GetDalaranWaterRandomDelayMs(session);
            }
            else
            {
                session.dsWaterState = 0;
                session.dsNextEventMs += GetDalaranWaterRandomDelayMs(session);
            }
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
        if (session.nativeMapId != 618 || !sConfigMgr->GetOption<bool>("ArenaReplay.ReplayObjects.Ring.Enable", true))
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
                session.rvNextEventMs = eventMs + std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.Ring.CloseFireMs", 5000u));
            }
            else if (session.rvState == 1)
            {
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_FIRE, false);
                SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_RV_FIRE_DOOR, false);
                LogReplayDynamicTimeline(session, viewer, "RV_FIRE", "closed", "ok");
                session.rvState = 2;
                session.rvNextEventMs = eventMs + std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.Ring.FireToPillarMs", 20000u));
            }
            else
            {
                ToggleRingReplayPillars(viewer, session);
                session.rvState = 3;
                session.rvNextEventMs = eventMs + std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("ArenaReplay.ReplayObjects.Ring.PillarSwitchMs", 25000u));
            }
        }

        uint32 matchElapsedMs = GetReplayMatchElapsedMs(session);
        if (ReplayDynamicObjectsDebugEnabled() && matchElapsedMs <= 30000 && (session.nextDynamicObjectLogMs == 0 || matchElapsedMs >= session.nextDynamicObjectLogMs))
        {
            session.nextDynamicObjectLogMs = matchElapsedMs + 1000;
            LogRingReplayState(session, viewer, "debug_first_30s");
        }
    }

    static void UpdateReplayDynamicObjects(Player* viewer, MatchRecord const& match, ActiveReplaySession& session)
    {
        if (!ReplayDynamicObjectsEnabled() || !session.replayObjectsInitialized || session.teardownInProgress)
            return;

        if (session.replayPlaybackMs >= GetReplayGateOpenRawMs(session))
            SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_GATE, true);

        if (session.replayPlaybackMs >= GetReplayBuffActivateRawMs(session))
            SetReplayDynamicObjectsByRole(viewer, session, RTG_REPLAY_OBJECT_BUFF, true);

        UpdateDalaranReplayWater(viewer, session);
        UpdateRingReplayObjects(viewer, match, session);
    }

    static void DespawnReplayDynamicObjects(Player* viewer, ActiveReplaySession& session)
    {
        if (session.replayObjects.empty())
        {
            session.replayObjectsSpawned = false;
            session.replayObjectsInitialized = false;
            return;
        }

        for (ReplayDynamicObjectBinding& binding : session.replayObjects)
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

        session.replayObjects.clear();
        session.replayObjectsSpawned = false;
        session.replayObjectsInitialized = false;
        session.nextDynamicObjectLogMs = 0;
        session.rvState = 0;
        session.rvNextEventMs = 0;
        session.rvPillarToggleState = 0;
        session.rvInitialUpper = false;
        session.rvFirstActorZ = 0.0f;
        session.dsWaterState = 0;
        session.dsNextEventMs = 0;
        session.dsWaterCycle = 0;
    }

    static bool ReplayViewerHasDisableGravity(Player* player)
    {
        return player && player->HasUnitMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY);
    }

    static bool ReplayViewerHasHover(Player* player)
    {
        return player && player->HasUnitMovementFlag(MOVEMENTFLAG_HOVER);
    }

    static void ForceReplayViewerMovementRestore(Player* player)
    {
        if (!player)
            return;

        player->SetIsSpectator(false);
        player->SetClientControl(player, true);
        player->SetHover(false);
        player->SetDisableGravity(false);
        player->SetCanFly(false);
        player->RemoveUnitMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY);
        player->RemoveUnitMovementFlag(MOVEMENTFLAG_HOVER);
#ifdef MOVEMENTFLAG_CAN_FLY
        player->RemoveUnitMovementFlag(MOVEMENTFLAG_CAN_FLY);
#endif
#ifdef MOVEMENTFLAG_FLYING
        player->RemoveUnitMovementFlag(MOVEMENTFLAG_FLYING);
#endif
        player->StopMoving();
    }

    static void LogReplayStateRestoreVerify(Player* player, ActiveReplaySession const& session, char const* reason, char const* result)
    {
        LOG_INFO("server.loading", "[RTG][REPLAY][STATE_RESTORE_VERIFY] replay={} viewerGuid={} nativeMap={} replayMap={} reason={} hidden={} canFly={} disableGravity={} hover={} clientControl=1 displayId={} phase={} result={}",
            session.replayId,
            player ? player->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            reason ? reason : "unknown",
            player && !player->IsVisible() ? 1 : 0,
            player && player->CanFly() ? 1 : 0,
            ReplayViewerHasDisableGravity(player) ? 1 : 0,
            ReplayViewerHasHover(player) ? 1 : 0,
            player ? player->GetDisplayId() : 0,
            player ? player->GetPhaseMask() : 0,
            result ? result : "ok");
    }

    static void StripReplayViewerVisualEquipment(Player* player, ActiveReplaySession& session)
    {
        if (!player || !sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.ClearViewerVisibleWeapons", true))
            return;

        if (session.viewerHidden || session.spectatorShellActive)
        {
            if (player->IsVisible())
                player->SetVisible(false);

            if (session.invisibleDisplayApplied && sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.UseInvisibleDisplay", true))
                player->SetDisplayId(sConfigMgr->GetOption<uint32>("ArenaReplay.SpectatorShell.InvisibleDisplayId", 11686u));
        }

        if (!session.virtualItemsStripped)
        {
            session.priorVirtualItemSlot[0] = player->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0);
            session.priorVirtualItemSlot[1] = player->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1);
            session.priorVirtualItemSlot[2] = player->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2);
            session.virtualItemsStripped = true;
        }

        player->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, 0);
        player->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1, 0);
        player->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2, 0);
    }

    static void RestoreReplayViewerVisualEquipment(Player* player, ActiveReplaySession& session)
    {
        if (!player || !session.virtualItemsStripped)
            return;

        player->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, session.priorVirtualItemSlot[0]);
        player->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1, session.priorVirtualItemSlot[1]);
        player->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2, session.priorVirtualItemSlot[2]);
        session.priorVirtualItemSlot[0] = 0;
        session.priorVirtualItemSlot[1] = 0;
        session.priorVirtualItemSlot[2] = 0;
        session.virtualItemsStripped = false;
    }

    static void RestoreReplayViewerState(Player* player, ActiveReplaySession& session)
    {
        if (!player)
            return;

        ForceReplayViewerMovementRestore(player);
        RestoreReplayViewerVisualEquipment(player, session);

        bool forceNativeDisplayOnExit = sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.RestoreNativeDisplayOnExit", true);
        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.RestoreOnExit", true) &&
            (session.invisibleDisplayApplied || forceNativeDisplayOnExit || IsCopiedReplayMapId(player->GetMapId())))
        {
            uint32 nativeDisplayId = player->GetNativeDisplayId();
            uint32 restoreDisplayId = forceNativeDisplayOnExit ? nativeDisplayId : session.priorDisplayId;
            char const* displayRestoreSource = forceNativeDisplayOnExit ? "native_display_forced" : "prior_display";

            if (restoreDisplayId == 0 || IsKnownInvisibleReplayDisplay(restoreDisplayId))
            {
                restoreDisplayId = nativeDisplayId;
                displayRestoreSource = "native_display_after_unsafe_prior";
            }

            if (restoreDisplayId != 0 && !IsKnownInvisibleReplayDisplay(restoreDisplayId))
                player->SetDisplayId(restoreDisplayId);
            else
                displayRestoreSource = "skipped_no_safe_display";

            LOG_INFO("server.loading", "[RTG][REPLAY][DISPLAY_RESTORE] replay={} viewerGuid={} priorDisplay={} nativeDisplay={} finalDisplay={} source={} result={}",
                session.replayId,
                player->GetGUID().GetCounter(),
                session.priorDisplayId,
                nativeDisplayId,
                player->GetDisplayId(),
                displayRestoreSource,
                IsKnownInvisibleReplayDisplay(player->GetDisplayId()) ? "still_invisible" : "ok");
        }

        if (session.viewerHidden || !player->IsVisible())
            player->SetVisible(true);

        char const* result = "ok";
        if (player->CanFly() || ReplayViewerHasDisableGravity(player) || ReplayViewerHasHover(player))
        {
            ForceReplayViewerMovementRestore(player);
            result = (player->CanFly() || ReplayViewerHasDisableGravity(player) || ReplayViewerHasHover(player)) ? "still_flying_after_force" : "forced_again";
            if (std::string(result) == "still_flying_after_force")
            {
                LOG_WARN("server.loading", "[RTG][REPLAY][STATE_RESTORE_VERIFY] replay={} viewerGuid={} nativeMap={} replayMap={} hidden={} canFly={} disableGravity={} hover={} clientControl=1 displayId={} phase={} result=still_flying_after_force",
                    session.replayId,
                    player->GetGUID().GetCounter(),
                    session.nativeMapId,
                    session.replayMapId,
                    !player->IsVisible() ? 1 : 0,
                    player->CanFly() ? 1 : 0,
                    ReplayViewerHasDisableGravity(player) ? 1 : 0,
                    ReplayViewerHasHover(player) ? 1 : 0,
                    player->GetDisplayId(),
                    player->GetPhaseMask());
            }
        }

        session.movementLocked = false;
        session.spectatorShellActive = false;
        session.viewerHidden = false;
        session.invisibleDisplayApplied = false;

        LogReplayStateRestoreVerify(player, session, "restore_state", result);
    }

    static void LogReplayStateRestore(Player* player, ActiveReplaySession const& session, char const* reason, char const* result)
    {
        LOG_INFO("server.loading", "[RTG][REPLAY][STATE_RESTORE] replay={} viewerGuid={} nativeMap={} replayMap={} reason={} playerMap={} playerBg={} sessionBg={} inBattleground={} playerPhase={} replayPhase={} hidden={} canFly={} result={}",
            session.replayId,
            player ? player->GetGUID().GetCounter() : 0,
            session.nativeMapId,
            session.replayMapId,
            reason ? reason : "unknown",
            player ? player->GetMapId() : 0,
            player ? player->GetBattlegroundId() : 0,
            session.battlegroundInstanceId,
            player && player->InBattleground() ? 1 : 0,
            player ? player->GetPhaseMask() : 0,
            session.replayPhaseMask,
            player && !player->IsVisible() ? 1 : 0,
            player && player->CanFly() ? 1 : 0,
            result ? result : "ok");
    }

    static void CancelReplayStartup(Player* player, ActiveReplaySession& session, char const* reason)
    {
        if (!player)
            return;

        uint64 viewerKey = player->GetGUID().GetCounter();
        bool returnToAnchor = session.sandboxTeleportIssued && session.anchorCaptured && player->GetMapId() == session.replayMapId;

        LOG_INFO("server.loading", "[RTG][REPLAY][STARTUP_CANCEL] replay={} viewerGuid={} nativeMap={} replayMap={} reason={} playerMap={} playerBg={} sessionBg={} inBattleground={} playerPhase={} replayPhase={} replayObjects={} clones={} cameraAnchor={} result=begin",
            session.replayId,
            viewerKey,
            session.nativeMapId,
            session.replayMapId,
            reason ? reason : "unknown",
            player->GetMapId(),
            player->GetBattlegroundId(),
            session.battlegroundInstanceId,
            player->InBattleground() ? 1 : 0,
            player->GetPhaseMask(),
            session.replayPhaseMask,
            session.replayObjects.size(),
            session.cloneBindings.size(),
            session.cameraAnchorGuid ? 1 : 0);

        SendReplayHudMessage(player, "STARTUP_CANCEL");
        DespawnReplayDynamicObjects(player, session);
        DespawnReplayActorClones(player, session);
        RestoreReplayViewerState(player, session);
        RestoreReplayViewerPhase(player, session);

        if (returnToAnchor)
        {
            ReturnReplayViewerToAnchor(player, session);
            RestoreReplayViewerState(player, session);
        }

        session.hudStarted = false;
        session.lastHudActorGuid = 0;
        session.lastHudActorFlatIndex = 0;
        session.lastHudActorCount = 0;
        session.lastHudWatcherCount = 0;
        session.lastHudWatcherPayload.clear();
        session.nextHudWatcherSyncMs = 0;
        LogReplayStateRestore(player, session, reason, "startup_cancel");

        activeReplaySessions.erase(viewerKey);
        loadedReplays.erase(viewerKey);
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
            DestroySyntheticReplayActorVisuals(player, liveIt->second);
            DespawnReplayDynamicObjects(player, liveIt->second);
            DespawnReplayActorClones(player, liveIt->second);
            ResetActorReplayView(player, liveIt->second);
        }

        RestoreReplayViewerState(player, session);
        RestoreReplayViewerPhase(player, session);
        loadedReplays.erase(viewerKey);

        std::string action = "return_to_anchor";
        if (session.anchorCaptured || IsCopiedReplayMapId(player->GetMapId()))
            ReturnReplayViewerToAnchor(player, session);
        else
            action = "restore_only_no_anchor";

        RestoreReplayViewerState(player, session);
        LogReplayStateRestore(player, session, session.lastTeardownReason.c_str(), "teardown");

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

        if (session.viewerGuidRemapPacketCount != 0 || session.viewerGuidSkipPacketCount != 0)
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][PACKET_VIEWER_GUID_SUMMARY] replay={} viewerGuid={} remapReplacements={} skippedPackets={} result=teardown",
                session.replayId,
                player->GetGUID().GetCounter(),
                session.viewerGuidRemapPacketCount,
                session.viewerGuidSkipPacketCount);
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
            DestroySyntheticReplayActorVisuals(player, it->second);
            DespawnReplayDynamicObjects(player, it->second);
            DespawnReplayActorClones(player, it->second);
            ResetActorReplayView(player, it->second);
            RestoreReplayViewerState(player, it->second);
            RestoreReplayViewerPhase(player, it->second);
            LogReplayStateRestore(player, it->second, "release_control", "release");
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
        session.fixedCameraFallbackApplied = false;
        session.hasSmoothedCamera = false;
        session.lastCameraMoveMs = 0;
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

        if (sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.UseViewpoint", true) && !session.viewpointBound && session.cameraAnchorFailed)
        {
            if (ShouldCancelReplayForCameraAnchor(session))
            {
                if (session.hudStarted)
                    RequestReplayTeardown(replayer, nullptr, "camera_anchor_failed", 0);
                return false;
            }

            if (ReplayCameraFallbackMode() == 1 || ReplayBodyChaseFallbackDisabled())
            {
                if (!session.fixedCameraFallbackApplied && replayer->GetSession())
                    ChatHandler(replayer->GetSession()).PSendSysMessage("Replay camera anchor unavailable; using fixed spectator view.");

                session.lastAppliedActorGuid = track->guid;
                session.lastAppliedActorFlatIndex = flatIndex;
                return ApplyReplayFixedCameraFallback(replayer, session, "camera_anchor_unavailable");
            }
        }

        uint64 const viewerActorGuid = replayer->GetGUID().GetCounter();
        bool syntheticBackendWithoutCloneFallback = ReplaySyntheticReplayPacketEmitterBackendEnabled() && !ReplaySyntheticServerCloneFallbackEnabled();
        bool noCloneVisualBackend = ReplayRecordedPacketVisualBackendEnabled() || syntheticBackendWithoutCloneFallback;
        Creature* selectedClone = noCloneVisualBackend ? nullptr : FindReplayClone(replayer, session, track->guid);
        bool selectedSelf = track->guid == viewerActorGuid;
        if (selectedSelf && !selectedClone && !noCloneVisualBackend)
        {
            uint32 fallbackFlatIndex = flatIndex;
            if (ActorTrack const* fallbackTrack = SelectFirstReplayActorWithClone(replayer, match, session, viewerActorGuid, &fallbackFlatIndex))
            {
                if (replayer->GetSession())
                    ChatHandler(replayer->GetSession()).PSendSysMessage("Your replay clone was not available; switching to another actor.");

                LOG_WARN("server.loading", "[RTG][REPLAY][SELF_POV] replay={} viewerGuid={} actorGuid={} cloneGuid=0 viewMode=camera_anchor result=missing_clone_switch",
                    session.replayId,
                    replayer->GetGUID().GetCounter(),
                    viewerActorGuid);
                track = fallbackTrack;
                flatIndex = fallbackFlatIndex;
                selectedSelf = false;
                selectedClone = FindReplayClone(replayer, session, track->guid);
                forceImmediate = true;
            }
            else
            {
                LOG_ERROR("server.loading", "[RTG][REPLAY][SELF_POV] replay={} viewerGuid={} actorGuid={} cloneGuid=0 viewMode=camera_anchor result=no_clone_binding",
                    session.replayId,
                    replayer->GetGUID().GetCounter(),
                    viewerActorGuid);
                if (ReplayCloneModeEnabled() && (ReplayCameraFallbackMode() == 1 || ReplayBodyChaseFallbackDisabled()))
                    return ApplyReplayFixedCameraFallback(replayer, session, "self_clone_missing");
                return false;
            }
        }

        bool actorChanged = (session.lastAppliedActorGuid != track->guid || session.lastAppliedActorFlatIndex != flatIndex);
        if (actorChanged)
            forceImmediate = true;

        if (selectedSelf && (forceImmediate || actorChanged || ReplayDebugVerbose()))
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][SELF_POV] replay={} viewerGuid={} actorGuid={} cloneGuid={} viewMode=camera_anchor result=ok",
                session.replayId,
                replayer->GetGUID().GetCounter(),
                track->guid,
                selectedClone ? selectedClone->GetGUID().GetCounter() : 0);
        }

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

        float baseX = frame.x;
        float baseY = frame.y;
        float baseZ = frame.z;
        float baseO = frame.o;
        bool usedClonePosition = false;
        if (sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.CameraSmoothing.UseClonePosition", true) && selectedClone)
        {
            baseX = selectedClone->GetPositionX();
            baseY = selectedClone->GetPositionY();
            baseZ = selectedClone->GetPositionZ();
            baseO = selectedClone->GetOrientation();
            usedClonePosition = true;
        }

        float targetX = baseX - std::cos(baseO) * followDistance;
        float targetY = baseY - std::sin(baseO) * followDistance;
        float targetZ = baseZ + followHeight;
        float targetO = baseO;

        float rawTargetZ = targetZ;
        bool smoothSnap = forceImmediate || actorChanged || !session.hasSmoothedCamera;
        float smoothDeltaZ = 0.0f;
        if (sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.CameraSmoothing.Enable", true))
        {
            float xyLerpPct = std::min(1.0f, std::max(0.01f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.CameraSmoothing.XYLerpPct", 0.30f)));
            float zLerpPct = std::min(1.0f, std::max(0.01f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.CameraSmoothing.ZLerpPct", 0.05f)));
            float zDeadband = std::max(0.0f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.CameraSmoothing.ZDeadband", 1.00f));
            float zSnapDistance = std::max(zDeadband, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.CameraSmoothing.ZSnapDistance", 8.0f));

            if (!session.hasSmoothedCamera || forceImmediate || actorChanged)
            {
                session.smoothedCameraX = targetX;
                session.smoothedCameraY = targetY;
                session.smoothedCameraZ = targetZ;
                session.smoothedCameraO = targetO;
                session.hasSmoothedCamera = true;
                smoothSnap = true;
            }
            else
            {
                smoothDeltaZ = targetZ - session.smoothedCameraZ;
                session.smoothedCameraX += (targetX - session.smoothedCameraX) * xyLerpPct;
                session.smoothedCameraY += (targetY - session.smoothedCameraY) * xyLerpPct;

                if (std::abs(smoothDeltaZ) > zSnapDistance)
                {
                    session.smoothedCameraZ = targetZ;
                    smoothSnap = true;
                }
                else if (std::abs(smoothDeltaZ) >= zDeadband)
                    session.smoothedCameraZ += smoothDeltaZ * zLerpPct;

                session.smoothedCameraO = targetO;
            }

            targetX = session.smoothedCameraX;
            targetY = session.smoothedCameraY;
            targetZ = session.smoothedCameraZ;
            targetO = session.smoothedCameraO;

            if (ReplayDebugEnabled(ReplayDebugFlag::Actors) && (forceImmediate || ReplayDebugVerbose()))
            {
                LOG_INFO("server.loading", "[RTG][REPLAY][CAMERA_SMOOTH] replay={} viewerGuid={} actorGuid={} targetZ={} smoothedZ={} deltaZ={} snap={} useClonePosition={} result=ok",
                    session.replayId,
                    replayer->GetGUID().GetCounter(),
                    track->guid,
                    rawTargetZ,
                    targetZ,
                    smoothDeltaZ,
                    smoothSnap ? 1 : 0,
                    usedClonePosition ? 1 : 0);
            }
        }

        if (sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.UseViewpoint", true) && (session.viewpointBound || session.cameraAnchorGuid))
        {
            if (Creature* anchor = EnsureReplayCameraAnchor(replayer, match, session))
            {
                float adx = anchor->GetPositionX() - targetX;
                float ady = anchor->GetPositionY() - targetY;
                float adz = anchor->GetPositionZ() - targetZ;
                float anchorDistSq = adx * adx + ady * ady + adz * adz;
                float anchorXYDistSq = adx * adx + ady * ady;
                float minAnchorMoveDistance = std::max(0.0f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.CameraSmoothing.MinAnchorMoveDistance", 0.35f));
                float minAnchorZMoveDistance = std::max(0.0f, sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.CameraSmoothing.MinAnchorZMoveDistance", 0.75f));
                uint32 minMoveMs = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorSpectate.CameraSmoothing.MinMoveMs", 100u);
                bool moveDue = minMoveMs == 0 || session.lastCameraMoveMs == 0 || nowMs >= session.lastCameraMoveMs + minMoveMs;
                bool smallMoveExceeded = anchorXYDistSq > (minAnchorMoveDistance * minAnchorMoveDistance) || std::abs(adz) > minAnchorZMoveDistance;
                if (forceImmediate || anchorDistSq > (snapDistance * snapDistance) || actorChanged || (moveDue && smallMoveExceeded))
                {
                    anchor->NearTeleportTo(targetX, targetY, targetZ, targetO);
                    session.lastCameraMoveMs = nowMs;
                }
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
                   << " x=" << targetX << " y=" << targetY << " z=" << targetZ << " o=" << targetO
                   << " spectatorShell=0 viewpoint=" << (session.viewpointBound ? 1 : 0);
                ReplayLog(ReplayDebugFlag::Actors, &session, replayer, "APPLY", ss.str());
            }
            return true;
        }

        if (ReplayCloneModeEnabled() && ReplayBodyChaseFallbackDisabled())
        {
            session.lastAppliedActorGuid = track->guid;
            session.lastAppliedActorFlatIndex = flatIndex;
            return ApplyReplayFixedCameraFallback(replayer, session, "body_chase_disabled");
        }

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

        if (session.viewpointBound || session.cameraAnchorGuid)
        {
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
        session.anchorCaptured = true;
        if (IsCopiedReplayMapId(player->GetMapId()))
        {
            uint32 safeMap = 0;
            float safeX = 0.0f;
            float safeY = 0.0f;
            float safeZ = 0.0f;
            float safeO = 0.0f;
            GetReplaySafeFallbackPosition(player, safeMap, safeX, safeY, safeZ, safeO);
            session.anchorMapId = safeMap;
            session.anchorPosition.Relocate(safeX, safeY, safeZ, safeO);

            LOG_WARN("server.loading", "[RTG][REPLAY][ANCHOR_SANITIZE] replay={} viewerGuid={} copiedMap={} safeMap={} x={} y={} z={} o={} result=fallback_anchor",
                replayId,
                player->GetGUID().GetCounter(),
                player->GetMapId(),
                safeMap,
                safeX,
                safeY,
                safeZ,
                safeO);
        }
        else
        {
            session.anchorMapId = player->GetMapId();
            session.anchorPosition.Relocate(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        }
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
        session.cameraAnchorFailed = false;
        session.cameraAnchorFailureLogged = false;
        session.nextCameraAnchorRetryMs = 0;
        session.fixedCameraFallbackApplied = false;
        session.cameraAnchorDisplayId = 0;
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
        session.cloneTimelineInitialized = false;
        session.packetStartMs = 0;
        session.firstPlayableActorFrameMs = 0;
        session.lastPlayableActorFrameMs = 0;
        session.cloneSceneStartMs = 0;
        session.matchOpenMs = 0;
        session.replayObjects.clear();
        session.replayObjectsSpawned = false;
        session.replayObjectsInitialized = false;
        session.nextDynamicObjectLogMs = 0;
        session.rvState = 0;
        session.rvNextEventMs = 0;
        session.rvPillarToggleState = 0;
        session.rvInitialUpper = false;
        session.rvFirstActorZ = 0.0f;
        session.dsWaterState = 0;
        session.dsNextEventMs = 0;
        session.dsWaterCycle = 0;
        session.cameraStallCount = 0;
        session.priorDisplayId = player->GetDisplayId();
        session.invisibleDisplayApplied = false;
        session.priorVirtualItemSlot[0] = player->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0);
        session.priorVirtualItemSlot[1] = player->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1);
        session.priorVirtualItemSlot[2] = player->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2);
        session.virtualItemsStripped = false;
        session.viewerReplayVisualGuidRaw = 0;
        session.viewerGuidRemapPacketCount = 0;
        session.viewerGuidSkipPacketCount = 0;
        session.viewerGuidRemapLogged = false;
        session.viewerGuidSkipLogged = false;
        session.lastCameraX = player->GetPositionX();
        session.lastCameraY = player->GetPositionY();
        session.lastCameraZ = player->GetPositionZ();
        session.lastCameraO = player->GetOrientation();
        session.smoothedCameraX = session.lastCameraX;
        session.smoothedCameraY = session.lastCameraY;
        session.smoothedCameraZ = session.lastCameraZ;
        session.smoothedCameraO = session.lastCameraO;
        session.hasSmoothedCamera = false;
        session.lastCameraMoveMs = 0;

        if (ReplayDebugEnabled(ReplayDebugFlag::General))
        {
            std::ostringstream ss;
            ss << "priorBg=" << session.priorBattlegroundInstanceId
               << " replayBg=" << session.battlegroundInstanceId
               << " priorPhase=" << session.priorPhaseMask
               << " replayPhase=" << session.replayPhaseMask
               << " anchorCaptured=" << (session.anchorCaptured ? 1 : 0)
               << " anchorMap=" << session.anchorMapId
               << " anchorX=" << session.anchorPosition.GetPositionX()
               << " anchorY=" << session.anchorPosition.GetPositionY()
               << " anchorZ=" << session.anchorPosition.GetPositionZ();
            ReplayLog(ReplayDebugFlag::General, &session, player, "LOCK", ss.str());
        }

    }

    static void ActivateReplayViewerControl(Player* player, ActiveReplaySession& session)
    {
        if (!player)
            return;

        player->SetIsSpectator(true);

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.LockMovement", true))
        {
            player->SetClientControl(player, false);
            session.movementLocked = true;
        }

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.HideViewerBody", true))
        {
            player->SetVisible(false);
            session.viewerHidden = true;
        }

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.DisableGravity", true))
        {
            // Historical builds used fly/no-gravity/hover to park the hidden viewer body.
            // On some branches those movement flags survive teardown. Keep the body hidden
            // and locked, but only apply flight-style parking when explicitly requested.
            if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.UseFlightForParking", false))
            {
                player->SetCanFly(true);
                player->SetDisableGravity(true);
                player->SetHover(true);
            }

            player->StopMoving();
        }

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorShell.UseInvisibleDisplay", true))
        {
            if (!session.invisibleDisplayApplied)
                session.priorDisplayId = player->GetDisplayId();

            player->SetDisplayId(sConfigMgr->GetOption<uint32>("ArenaReplay.SpectatorShell.InvisibleDisplayId", 11686u));
            session.invisibleDisplayApplied = true;
        }

        StripReplayViewerVisualEquipment(player, session);

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

        // Packet-stream replays need the original object-create packets, which are
        // commonly sent during arena preparation before STATUS_IN_PROGRESS. Do not
        // wait for the gates to open; only stop once the match is leaving/finished.
        if (bg->GetStatus() == BattlegroundStatus::STATUS_WAIT_LEAVE)
            return true;

        Player* receiver = session->GetPlayer();
        if (!receiver || receiver->IsSpectator())
            return true;

        bool recordAllParticipants = sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.RecordedPacketStream.RecordAllParticipantPackets", true);
        if (!recordAllParticipants)
        {
            // Legacy deterministic recorder selection: choose one recorder per team.
            ObjectGuid recorder = GetOrAssignRecorderGuid(bg, receiver);
            if (recorder && receiver->GetGUID() != recorder)
                return true;
        }
        else
            GetOrAssignRecorderGuid(bg, receiver); // keep legacy recorder metadata populated for diagnostics.

        // ignore packets not in watch list
        if (std::find(watchList.begin(), watchList.end(), packet.GetOpcode()) == watchList.end())
            return true;

        MatchRecord& record = EnsureLiveMatchRecord(bg);
        uint32 maxPackets = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorVisual.RecordedPacketStream.MaxPacketsPerReplay", 25000u);
        if (maxPackets != 0 && record.packets.size() >= maxPackets)
        {
            ++record.skippedPacketCount;
            return true;
        }

        uint32 timestamp = bg->GetStartTime();
        PacketRecord packetRecord;
        packetRecord.timestamp = timestamp;
        packetRecord.packet = WorldPacket(packet);
        packetRecord.receiverGuid = receiver->GetGUID();
        packetRecord.receiverTeam = receiver->GetBgTeamId();
        record.packets.push_back(packetRecord);

        if (packetRecord.receiverTeam == TEAM_ALLIANCE)
            ++record.team0PacketCount;
        else if (packetRecord.receiverTeam == TEAM_HORDE)
            ++record.team1PacketCount;
        else
            ++record.neutralPacketCount;
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

        if (session.awaitingReplayMapAttach)
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
                    LOG_INFO("server.loading", "[RTG][REPLAY][ATTACH_TIMEOUT] replay={} viewerGuid={} nativeMap={} replayMap={} playerMap={} playerBg={} sessionBg={} inBattleground={} playerPhase={} replayPhase={} nowMs={} result=timeout",
                        session.replayId,
                        replayer->GetGUID().GetCounter(),
                        session.nativeMapId,
                        session.replayMapId,
                        replayer->GetMapId(),
                        replayer->GetBattlegroundId(),
                        session.battlegroundInstanceId,
                        replayer->InBattleground() ? 1 : 0,
                        replayer->GetPhaseMask(),
                        session.replayPhaseMask,
                        bg->GetStartTime());
                    CancelReplayStartup(replayer, session, "attach_timeout");
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
            else
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

            InitializeReplayCloneTimeline(match, session);
            if (ReplayCloneModePrewarmClones())
                session.replayWarmupUntilMs = session.replayPlaybackMs;
            ParkReplaySpectatorShell(replayer, session, "attach_ok");

            if (!InitializeReplayDynamicObjects(replayer, match, session))
            {
                DespawnReplayDynamicObjects(replayer, session);
                if (replayer->GetSession())
                    ChatHandler(replayer->GetSession()).PSendSysMessage("Replay arena objects could not be spawned.");
                CancelReplayStartup(replayer, session, "dynamic_object_spawn_failed");
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
                CancelReplayStartup(replayer, session, "clone_scene_failed");
                return;
            }

            BindReplayViewpoint(replayer, match, session);
            uint32 actorStartMs = (ReplayCloneModeEnabled() || ReplaySyntheticReplayPacketEmitterBackendEnabled()) ? session.replayPlaybackMs : bg->GetStartTime();
            bool actorViewApplied = ApplyActorReplayView(replayer, match, session, actorStartMs, true);
            if (!actorViewApplied && ShouldCancelReplayForCameraAnchor(session))
            {
                CancelReplayStartup(replayer, session, "camera_anchor_failed");
                return;
            }
            RepairReplayCloneDisplays(replayer, match, session, "post_camera_anchor");
            ReplayCloneVisibilityAudit audit = AuditReplayCloneVisibility(replayer, match, session);
            LOG_INFO("server.loading", "[RTG][REPLAY][SCENE_READY] replay={} nativeMap={} replayMap={} objectsSpawned={} gatesState={} clonesPrewarmed={} team0Visible={} team1Visible={} cameraAnchorBound={} viewerBodyHidden={} result={}",
                session.replayId,
                session.nativeMapId,
                session.replayMapId,
                session.replayObjectsSpawned ? 1 : 0,
                GetReplayGateStateName(session),
                session.cloneBindings.size(),
                audit.team0Visible,
                audit.team1Visible,
                session.viewpointBound ? 1 : 0,
                session.viewerHidden ? 1 : 0,
                (audit.team0Bindings == audit.team0Visible && audit.team1Bindings == audit.team1Visible) ? "ok" : "warn");
            SendReplayHudPov(replayer, match, session, true);
            session.nextHudWatcherSyncMs = 0;
            SendReplayHudWatchers(bg, replayer, match, session, true);
        }
        if (!session.replayMapAttached)
            return;

        // send replay data to spectator first so actors exist client-side before camera positioning,
        // but cap burst size to avoid hitching and client overload on older replays.
        uint32 packetsSentThisUpdate = 0;
        uint32 packetBudgetPerUpdate = std::max<uint32>(15u, session.packetBudgetPerUpdate);
        if (ReplayRecordedPacketVisualBackendEnabled())
        {
            while (!match.packets.empty() && match.packets.front().timestamp <= bg->GetStartTime() && packetsSentThisUpdate < packetBudgetPerUpdate)
            {
                WorldPacket* myPacket = &match.packets.front().packet;
                if (IsReplayPacketOpcodeAllowedForPlayback(myPacket->GetOpcode()))
                {
                    WorldPacket safePacket;
                    if (BuildViewerSafeReplayPacket(*myPacket, replayer, session, safePacket))
                    {
                        replayer->GetSession()->SendPacket(&safePacket);
                        ++packetsSentThisUpdate;
                    }
                }
                match.packets.pop_front();
            }
        }
        StripReplayViewerVisualEquipment(replayer, session);

        if (IsReplayTimelineComplete(match, session))
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
            SyncSyntheticReplayActors(replayer, match, session, bg->GetStartTime(), false);
            SyncReplayActorClones(replayer, match, session, bg->GetStartTime());
            UpdateReplayDynamicObjects(replayer, match, session);
            bool actorViewApplied = ApplyActorReplayView(replayer, match, session, bg->GetStartTime());
            SendReplayHudPov(replayer, match, session, false);
            SendReplayHudWatchers(bg, replayer, match, session, false);
            if (!actorViewApplied && !session.teardownRequested && session.nextAnchorEnforceMs <= bg->GetStartTime())
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

        if (!bg)
            return;

        // Ensure recorder slots are assigned deterministically as players join.
        // (Recording still starts only once the arena is in progress.)
        GetOrAssignRecorderGuid(bg, player);

        if (sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.AppearanceCaptureOnJoin", true))
        {
            MatchRecord& match = EnsureLiveMatchRecord(bg);
            CaptureOrUpdateReplayActorAppearance(bg, player, match, "join");
        }

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
        ReconcileReplayActorAppearanceWinnerSides(match);
        if (sConfigMgr->GetOption<bool>("ArenaReplay.CloneScene.AppearanceCaptureOnSave", true))
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

        LOG_INFO("server.loading", "[RTG][REPLAY][PACKET_CAPTURE_SUMMARY] replayInstance={} team0Packets={} team1Packets={} neutralPackets={} totalPackets={} skippedPackets={} recordAllParticipants={} result={}",
            bg->GetInstanceID(),
            match.team0PacketCount,
            match.team1PacketCount,
            match.neutralPacketCount,
            match.packets.size(),
            match.skippedPacketCount,
            sConfigMgr->GetOption<bool>("ArenaReplay.ActorVisual.RecordedPacketStream.RecordAllParticipantPackets", true) ? 1 : 0,
            (match.team0PacketCount > 0 && match.team1PacketCount > 0) ? "ok" : "single_team_or_empty");

        std::string encodedContents = Acore::Encoding::Base32::Encode(buffer.contentsAsVector());
        std::string encodedWinnerTracks = SerializeActorTracks(match.winnerActorTracks);
        std::string encodedLoserTracks = SerializeActorTracks(match.loserActorTracks);
        std::string encodedActorAppearance = SerializeReplayActorAppearanceSnapshots(match.actorAppearanceSnapshots);
        bool const inlineAppearanceColumn = ReplayAppearanceInlineColumnAvailable();

        if (inlineAppearanceColumn)
        {
            CharacterDatabase.Execute("INSERT INTO `character_arena_replays` "
                "(`arenaTypeId`, `typeId`, `contentSize`, `contents`, `mapId`, `winnerTeamName`, `winnerTeamRating`, `winnerTeamMMR`, "
                "`loserTeamName`, `loserTeamRating`, `loserTeamMMR`, `winnerPlayerGuids`, `loserPlayerGuids`, `winnerActorTrack`, `loserActorTrack`, `actorAppearanceSnapshots`) "
                "VALUES ({}, {}, {}, '{}', {}, '{}', {}, {}, '{}', {}, {}, '{}', '{}', '{}', '{}', '{}')",
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
                EscapeSqlString(encodedLoserTracks),
                EscapeSqlString(encodedActorAppearance)
            );
        }
        else
        {
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
        }

        QueryResult insertedIdResult = CharacterDatabase.Query("SELECT LAST_INSERT_ID()");
        uint32 insertedReplayId = 0;
        if (insertedIdResult)
            insertedReplayId = insertedIdResult->Fetch()[0].Get<uint32>();

        if (!insertedReplayId)
        {
            QueryResult fallbackIdResult = CharacterDatabase.Query(
                "SELECT `id` FROM `character_arena_replays` "
                "WHERE `mapId` = {} AND `arenaTypeId` = {} AND `typeId` = {} "
                "AND `winnerPlayerGuids` = '{}' AND `loserPlayerGuids` = '{}' "
                "ORDER BY `id` DESC LIMIT 1",
                bg->GetMapId(),
                uint32(match.arenaTypeId),
                uint32(match.typeId),
                EscapeSqlString(winnerGuids),
                EscapeSqlString(loserGuids));
            if (fallbackIdResult)
                insertedReplayId = fallbackIdResult->Fetch()[0].Get<uint32>();
        }

        if (!insertedReplayId)
            LOG_WARN("server.loading", "[RTG][REPLAY][APPEARANCE_PERSIST] replayId=0 snapshotCount={} inlineColumn={} result=no_replay_id", uint32(match.actorAppearanceSnapshots.size()), inlineAppearanceColumn ? 1 : 0);
        else
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_PERSIST] replayId={} snapshotCount={} inlineColumn={} result=inline_saved", insertedReplayId, uint32(match.actorAppearanceSnapshots.size()), inlineAppearanceColumn ? 1 : 0);
            PersistReplayActorAppearanceSnapshots(insertedReplayId, match);
        }

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
            CancelReplayStartup(player, replaySession, mapResolveFailed ? "map_resolve_fail" : "sandbox_prepare_fail");
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

        uint32 playerMapBefore = player->GetMapId();
        uint32 playerPhaseBefore = player->GetPhaseMask();
        float spawnX = replaySession.replaySpawnPosition.GetPositionX();
        float spawnY = replaySession.replaySpawnPosition.GetPositionY();
        float spawnZ = replaySession.replaySpawnPosition.GetPositionZ();
        float spawnO = replaySession.replaySpawnPosition.GetOrientation();

        LOG_INFO("server.loading", "[RTG][REPLAY][TELEPORT_ATTEMPT] replay={} viewerGuid={} nativeMap={} replayMap={} x={} y={} z={} o={} playerMapBefore={} playerPhaseBefore={} result=begin",
            replaySession.replayId,
            player->GetGUID().GetCounter(),
            replaySession.nativeMapId,
            replaySession.replayMapId,
            spawnX,
            spawnY,
            spawnZ,
            spawnO,
            playerMapBefore,
            playerPhaseBefore);

        bool teleportOk = player->TeleportTo(replaySession.replayMapId, spawnX, spawnY, spawnZ, spawnO);
        uint32 playerPhaseAfter = player->GetPhaseMask();
        if (!teleportOk)
        {
            LOG_ERROR("server.loading", "[RTG][REPLAY][TELEPORT_FAIL] replay={} viewerGuid={} nativeMap={} replayMap={} x={} y={} z={} o={} playerMapBefore={} playerPhaseBefore={} playerPhaseAfter={} result=failed",
                replaySession.replayId,
                player->GetGUID().GetCounter(),
                replaySession.nativeMapId,
                replaySession.replayMapId,
                spawnX,
                spawnY,
                spawnZ,
                spawnO,
                playerMapBefore,
                playerPhaseBefore,
                playerPhaseAfter);
            CancelReplayStartup(player, replaySession, "teleport_fail");
            handler.PSendSysMessage("Replay map could not be entered. Replay startup was cancelled.");
            return false;
        }

        LOG_INFO("server.loading", "[RTG][REPLAY][TELEPORT_OK] replay={} viewerGuid={} nativeMap={} replayMap={} x={} y={} z={} o={} mapBefore={} phase={} result=ok",
            replaySession.replayId,
            player->GetGUID().GetCounter(),
            replaySession.nativeMapId,
            replaySession.replayMapId,
            spawnX,
            spawnY,
            spawnZ,
            spawnO,
            playerMapBefore,
            playerPhaseAfter);

        replaySession.sandboxTeleportIssued = true;

        if (!ApplyReplayViewerPrivatePhase(player, replaySession))
        {
            CancelReplayStartup(player, replaySession, "phase_apply_failed");
            handler.PSendSysMessage("Replay sandbox could not allocate a private viewer phase or target replay map.");
            return false;
        }

        ActivateReplayViewerControl(player, replaySession);
        replaySession.awaitingReplayMapAttach = true;
        replaySession.replayMapAttached = false;
        replaySession.replayAttachDeadlineMs = GetReplayNowMs() + 5000;
        replaySession.nextAttachLogMs = 0;
        replaySession.replayPlaybackMs = 0;
        replaySession.replayLastServerMs = GetReplayNowMs();
        replaySession.replayWarmupUntilMs = 1000;

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
        bool const inlineAppearanceColumn = ReplayAppearanceInlineColumnAvailable();
        QueryResult result = inlineAppearanceColumn
            ? CharacterDatabase.Query("SELECT id, arenaTypeId, typeId, contentSize, contents, mapId, timesWatched, winnerPlayerGuids, loserPlayerGuids, winnerActorTrack, loserActorTrack, actorAppearanceSnapshots FROM character_arena_replays WHERE id = {}", matchId)
            : CharacterDatabase.Query("SELECT id, arenaTypeId, typeId, contentSize, contents, mapId, timesWatched, winnerPlayerGuids, loserPlayerGuids, winnerActorTrack, loserActorTrack FROM character_arena_replays WHERE id = {}", matchId);
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
        if (inlineAppearanceColumn)
        {
            uint32 inlineLoaded = DeserializeReplayActorAppearanceSnapshots(record.actorAppearanceSnapshots, fields[11].Get<std::string>());
            LOG_INFO("server.loading", "[RTG][REPLAY][APPEARANCE_LOAD_INLINE] replayId={} snapshotCount={} totalSnapshots={} result={}",
                matchId,
                inlineLoaded,
                uint32(record.actorAppearanceSnapshots.size()),
                inlineLoaded ? "ok" : "empty");
        }

        const bool hasPlayableActorTracks = !BuildPlayableReplayActorSelections(record).empty();
        if (record.packets.empty() && !hasPlayableActorTracks)
        {
            ChatHandler(p->GetSession()).PSendSysMessage("Replay data is incomplete or unsafe for playback.");
            CloseGossipMenuFor(p);
            LOG_WARN("server.loading", "[RTG][REPLAY][LOAD_FAIL] replay={} reason=no_packets_or_actor_tracks winnerTracks={} loserTracks={} snapshots={}",
                matchId,
                record.winnerActorTracks.size(),
                record.loserActorTracks.size(),
                record.actorAppearanceSnapshots.size());
            return false;
        }

        if (record.packets.empty() && hasPlayableActorTracks)
        {
            LOG_INFO("server.loading", "[RTG][REPLAY][LOAD_COMPAT] replay={} mode=clone_only_no_packets winnerTracks={} loserTracks={} snapshots={} result=ok",
                matchId,
                record.winnerActorTracks.size(),
                record.loserActorTracks.size(),
                record.actorAppearanceSnapshots.size());
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

            PacketRecord packetRecord;
            packetRecord.timestamp = packetTimestamp;
            packetRecord.packet = packet;
            record.packets.push_back(packetRecord);
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
                if (!session.replayMapAttached)
                {
                    CancelReplayStartup(replayer, session, "missing_loaded_replay");
                    continue;
                }

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

            if (session.awaitingReplayMapAttach)
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
                        LOG_INFO("server.loading", "[RTG][REPLAY][ATTACH_TIMEOUT] replay={} viewerGuid={} nativeMap={} replayMap={} playerMap={} playerBg={} sessionBg={} inBattleground={} playerPhase={} replayPhase={} nowMs={} result=timeout",
                            session.replayId,
                            replayer->GetGUID().GetCounter(),
                            session.nativeMapId,
                            session.replayMapId,
                            replayer->GetMapId(),
                            replayer->GetBattlegroundId(),
                            session.battlegroundInstanceId,
                            replayer->InBattleground() ? 1 : 0,
                            replayer->GetPhaseMask(),
                            session.replayPhaseMask,
                            nowMs);
                        CancelReplayStartup(replayer, session, "attach_timeout");
                        continue;
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
                else
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

                InitializeReplayCloneTimeline(match, session);
                if (ReplayCloneModePrewarmClones())
                    session.replayWarmupUntilMs = session.replayPlaybackMs;
                ParkReplaySpectatorShell(replayer, session, "attach_ok");

                if (!InitializeReplayDynamicObjects(replayer, match, session))
                {
                    DespawnReplayDynamicObjects(replayer, session);
                    if (replayer->GetSession())
                        ChatHandler(replayer->GetSession()).PSendSysMessage("Replay arena objects could not be spawned.");
                    CancelReplayStartup(replayer, session, "dynamic_object_spawn_failed");
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
                    CancelReplayStartup(replayer, session, "clone_scene_failed");
                    continue;
                }

                BindReplayViewpoint(replayer, match, session);
                bool actorViewApplied = ApplyActorReplayView(replayer, match, session, session.replayPlaybackMs, true);
                if (!actorViewApplied && ShouldCancelReplayForCameraAnchor(session))
                {
                    CancelReplayStartup(replayer, session, "camera_anchor_failed");
                    continue;
                }
                RepairReplayCloneDisplays(replayer, match, session, "post_camera_anchor");
                ReplayCloneVisibilityAudit audit = AuditReplayCloneVisibility(replayer, match, session);
                LOG_INFO("server.loading", "[RTG][REPLAY][SCENE_READY] replay={} nativeMap={} replayMap={} objectsSpawned={} gatesState={} clonesPrewarmed={} team0Visible={} team1Visible={} cameraAnchorBound={} viewerBodyHidden={} result={}",
                    session.replayId,
                    session.nativeMapId,
                    session.replayMapId,
                    session.replayObjectsSpawned ? 1 : 0,
                    GetReplayGateStateName(session),
                    session.cloneBindings.size(),
                    audit.team0Visible,
                    audit.team1Visible,
                    session.viewpointBound ? 1 : 0,
                    session.viewerHidden ? 1 : 0,
                    (audit.team0Bindings == audit.team0Visible && audit.team1Bindings == audit.team1Visible) ? "ok" : "warn");
                SendReplayHudPov(replayer, match, session, true);
                session.nextHudWatcherSyncMs = 0;
                SendReplayHudWatchers(nullptr, replayer, match, session, true);
            }
            if (!session.replayMapAttached)
                continue;

            session.replayPlaybackMs += deltaMs;

            uint32 packetsSentThisUpdate = 0;
            uint32 packetBudgetPerUpdate = std::max<uint32>(15u, session.packetBudgetPerUpdate);
            if (ReplayRecordedPacketVisualBackendEnabled())
            {
                while (!match.packets.empty() && match.packets.front().timestamp <= session.replayPlaybackMs && packetsSentThisUpdate < packetBudgetPerUpdate)
                {
                    WorldPacket* myPacket = &match.packets.front().packet;
                    if (IsReplayPacketOpcodeAllowedForPlayback(myPacket->GetOpcode()))
                    {
                        WorldPacket safePacket;
                        if (BuildViewerSafeReplayPacket(*myPacket, replayer, session, safePacket))
                        {
                            replayer->GetSession()->SendPacket(&safePacket);
                            ++packetsSentThisUpdate;
                        }
                    }
                    match.packets.pop_front();
                }
            }
            StripReplayViewerVisualEquipment(replayer, session);

            if (IsReplayTimelineComplete(match, session))
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

            SyncSyntheticReplayActors(replayer, match, session, session.replayPlaybackMs, false);
            SyncReplayActorClones(replayer, match, session, session.replayPlaybackMs);
            UpdateReplayDynamicObjects(replayer, match, session);
            bool actorViewApplied = ApplyActorReplayView(replayer, match, session, session.replayPlaybackMs);
            SendReplayHudPov(replayer, match, session, false);
            SendReplayHudWatchers(nullptr, replayer, match, session, false);
            if (!actorViewApplied && !session.teardownRequested && session.nextAnchorEnforceMs <= session.replayPlaybackMs)
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
