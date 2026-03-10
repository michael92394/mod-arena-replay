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
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
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
    uint32 battlegroundInstanceId = 0;
    uint32 replayId = 0;
    uint32 anchorMapId = 0;
    Position anchorPosition;
    uint32 nextAnchorEnforceMs = 0;
    bool movementLocked = false;
    bool viewerWasParticipant = false;
    bool actorSpectateActive = false;
    bool actorSpectateOnWinnerTeam = true;
    uint32 actorTrackIndex = 0;
    uint32 nextActorCycleMs = 0;
    uint32 nextActorTeleportMs = 0;
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

namespace
{
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

    static void ResetActorReplayView(Player* replayer, ActiveReplaySession& session)
    {
        if (!replayer)
            return;
        session.actorSpectateActive = false;
        session.nextActorCycleMs = 0;
        session.nextActorTeleportMs = 0;
    }

    static bool ApplyActorReplayView(Player* replayer, MatchRecord& match, ActiveReplaySession& session, uint32 nowMs)
    {
        if (!replayer || !sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.Enable", true))
            return false;

        auto const* preferred = SelectTracks(match, session.actorSpectateOnWinnerTeam);
        auto const* alternate = SelectTracks(match, !session.actorSpectateOnWinnerTeam);
        if (preferred->empty() && alternate->empty())
        {
            ResetActorReplayView(replayer, session);
            return false;
        }

        if (preferred->empty() && !alternate->empty())
        {
            session.actorSpectateOnWinnerTeam = !session.actorSpectateOnWinnerTeam;
            preferred = alternate;
        }

        if (preferred->empty())
            return false;

        if (session.actorTrackIndex >= preferred->size())
            session.actorTrackIndex = 0;

        uint32 cycleMs = sConfigMgr->GetOption<uint32>("ArenaReplay.ActorSpectate.AutoCycleMs", 7000);
        if (session.nextActorCycleMs == 0)
            session.nextActorCycleMs = nowMs + cycleMs;

        if (cycleMs > 0 && session.nextActorCycleMs <= nowMs)
        {
            session.nextActorCycleMs = nowMs + cycleMs;
            if (!preferred->empty())
                session.actorTrackIndex = (session.actorTrackIndex + 1) % preferred->size();
            if (!SelectTracks(match, !session.actorSpectateOnWinnerTeam)->empty())
                session.actorSpectateOnWinnerTeam = !session.actorSpectateOnWinnerTeam;
            preferred = SelectTracks(match, session.actorSpectateOnWinnerTeam);
            if (session.actorTrackIndex >= preferred->size())
                session.actorTrackIndex = 0;
        }

        ActorTrack const& track = (*preferred)[session.actorTrackIndex];
        if (track.frames.empty())
            return false;

        if (session.nextActorTeleportMs > nowMs)
            return true;
        session.nextActorTeleportMs = nowMs + sConfigMgr->GetOption<uint32>("ArenaReplay.ActorSpectate.TeleportMs", 200);

        ActorFrame const* frame = &track.frames.front();
        for (ActorFrame const& candidate : track.frames)
        {
            frame = &candidate;
            if (candidate.timestamp >= nowMs)
                break;
        }

        float followDistance = sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.FollowDistance", 8.0f);
        float followHeight = sConfigMgr->GetOption<float>("ArenaReplay.ActorSpectate.FollowHeight", 3.0f);
        float backX = frame->x - std::cos(frame->o) * followDistance;
        float backY = frame->y - std::sin(frame->o) * followDistance;
        float backZ = frame->z + followHeight;
        replayer->NearTeleportTo(backX, backY, backZ, frame->o);
        session.actorSpectateActive = true;
        return true;
    }

    static void ReleaseReplayViewerControl(Player* player)
    {
        if (!player)
            return;

        auto it = activeReplaySessions.find(player->GetGUID().GetCounter());
        if (it != activeReplaySessions.end() && it->second.movementLocked)
            player->SetClientControl(player, true);

        activeReplaySessions.erase(player->GetGUID().GetCounter());
    }

    static void LockReplayViewerControl(Player* player, uint32 replayId)
    {
        if (!player)
            return;

        ActiveReplaySession& session = activeReplaySessions[player->GetGUID().GetCounter()];
        session.battlegroundInstanceId = player->GetBattlegroundId();
        session.replayId = replayId;
        session.anchorMapId = player->GetMapId();
        session.anchorPosition.Relocate(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        session.nextAnchorEnforceMs = 0;

        if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.LockMovement", true))
        {
            player->SetClientControl(player, false);
            session.movementLocked = true;
        }
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

        uint32 replayId = bgReplayIds.at(bg->GetInstanceID());

        int32 startDelayTime = bg->GetStartDelayTime();
        if (startDelayTime > 1000) // reduces StartTime only when watching Replay
        {
            bg->SetStartDelayTime(1000);
            bg->SetStartTime(bg->GetStartTime() + (startDelayTime - 1000));
        }

        if (bg->GetStatus() != BattlegroundStatus::STATUS_IN_PROGRESS)
            return;

        // retrieve arena replay data
        auto it = loadedReplays.find(replayId);
        if (it == loadedReplays.end())
            return;

        MatchRecord& match = it->second;

        // if replay ends or spectator left > free arena replay data and/or kick player
        if (match.packets.empty() || bg->GetPlayers().empty())
        {
            if (!bg->GetPlayers().empty())
            {
                Player* replayer = bg->GetPlayers().begin()->second;
                ReleaseReplayViewerControl(replayer);
                replayer->LeaveBattleground(bg);
            }

            loadedReplays.erase(it);
            return;
        }

        Player* replayer = bg->GetPlayers().begin()->second;
        auto sessionIt = activeReplaySessions.find(replayer->GetGUID().GetCounter());
        if (sessionIt != activeReplaySessions.end())
        {
            ActiveReplaySession& session = sessionIt->second;
            bool actorViewApplied = ApplyActorReplayView(replayer, match, session, bg->GetStartTime());
            if (!actorViewApplied && session.nextAnchorEnforceMs <= bg->GetStartTime())
            {
                session.nextAnchorEnforceMs = bg->GetStartTime() + 500;

                if (sConfigMgr->GetOption<bool>("ArenaReplay.SpectatorOnly.AnchorViewer", true) && replayer->GetMapId() == session.anchorMapId)
                {
                    float dx = replayer->GetPositionX() - session.anchorPosition.GetPositionX();
                    float dy = replayer->GetPositionY() - session.anchorPosition.GetPositionY();
                    float dz = replayer->GetPositionZ() - session.anchorPosition.GetPositionZ();
                    float distSq = dx * dx + dy * dy + dz * dz;
                    if (distSq > 1.0f)
                        replayer->NearTeleportTo(session.anchorPosition.GetPositionX(), session.anchorPosition.GetPositionY(), session.anchorPosition.GetPositionZ(), session.anchorPosition.GetOrientation());
                }
            }
        }

        //send replay data to spectator
        while (!match.packets.empty() && match.packets.front().timestamp <= bg->GetStartTime())
        {
            WorldPacket* myPacket = &match.packets.front().packet;
            replayer->GetSession()->SendPacket(myPacket);
            match.packets.pop_front();
        }
    }

    void OnBattlegroundAddPlayer(Battleground* bg, Player* player) override
    {
        if (!player)
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
            ReleaseReplayViewerControl(bg->GetPlayers().begin()->second);

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
    REPLAY_MOST_WATCHED_ALLTIME = 14
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
                    "ORDER BY id DESC LIMIT 30",
                    playerGuidStr, playerGuidStr);
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

                    ReplayInfo info;
                    info.matchId = fields[0].Get<uint32>();
                    info.winnerTeamName = fields[1].Get<std::string>();
                    info.winnerTeamRating = fields[2].Get<uint32>();
                    info.winnerPlayerGuids = fields[3].Get<std::string>();
                    info.loserTeamName = fields[4].Get<std::string>();
                    info.loserTeamRating = fields[5].Get<uint32>();
                    info.loserPlayerGuids = fields[6].Get<std::string>();
                    infos.push_back(std::move(info));
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

    void ShowReplaysAllTime(Player* player, Creature* creature, uint8 arenaTypeId)
    {
        auto matchInfos = loadReplaysAllTimeByArenaType(arenaTypeId);
        ShowReplays(player, creature, matchInfos);
    }

    std::vector<ReplayInfo> loadReplaysAllTimeByArenaType(uint8 arenaTypeId)
    {
        std::vector<ReplayInfo> records;
        QueryResult result = CharacterDatabase.Query("SELECT id, winnerTeamName, winnerTeamRating, winnerPlayerGuids, loserTeamName, loserTeamRating, loserPlayerGuids FROM character_arena_replays WHERE arenaTypeId = {} ORDER BY winnerTeamRating DESC LIMIT 20", arenaTypeId);

        if (!result)
            return records;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                return records;

            ReplayInfo info;
            info.matchId = fields[0].Get<uint32>();
            info.winnerTeamName = fields[1].Get<std::string>();
            info.winnerTeamRating = fields[2].Get<uint32>();
            info.winnerPlayerGuids = fields[3].Get<std::string>();
            info.loserTeamName = fields[4].Get<std::string>();
            info.loserTeamRating = fields[5].Get<uint32>();
            info.loserPlayerGuids = fields[6].Get<std::string>();

            records.push_back(info);
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
            "ORDER BY id DESC LIMIT 20", arenaTypeId, thirtyDaysAgo.c_str());

        if (!result)
            return records;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                return records;

            ReplayInfo info;
            info.matchId = fields[0].Get<uint32>();
            info.winnerTeamName = fields[1].Get<std::string>();
            info.winnerTeamRating = fields[2].Get<uint32>();
            info.winnerPlayerGuids = fields[3].Get<std::string>();
            info.loserTeamName = fields[4].Get<std::string>();
            info.loserTeamRating = fields[5].Get<uint32>();
            info.loserPlayerGuids = fields[6].Get<std::string>();

            records.push_back(info);
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
            "LIMIT 28");

        if (!result)
            return records;

        do
        {
            Field* fields = result->Fetch();
            if (!fields)
                return records;

            ReplayInfo info;
            info.matchId = fields[0].Get<uint32>();
            info.winnerTeamName = fields[1].Get<std::string>();
            info.winnerTeamRating = fields[2].Get<uint32>();
            info.winnerPlayerGuids = fields[3].Get<std::string>();
            info.loserTeamName = fields[4].Get<std::string>();
            info.loserTeamRating = fields[5].Get<uint32>();
            info.loserPlayerGuids = fields[6].Get<std::string>();

            records.push_back(info);
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
            "WHERE s.character_id = {} ORDER BY s.id {} LIMIT 29",
            player->GetGUID().GetCounter(), sortOrder);
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

                ReplayInfo info;
                info.matchId = fields[0].Get<uint32>();
                info.winnerTeamName = fields[1].Get<std::string>();
                info.winnerTeamRating = fields[2].Get<uint32>();
                info.winnerPlayerGuids = fields[3].Get<std::string>();
                info.loserTeamName = fields[4].Get<std::string>();
                info.loserTeamRating = fields[5].Get<uint32>();
                info.loserPlayerGuids = fields[6].Get<std::string>();
                infos.push_back(std::move(info));
            } while (result->NextRow());

            ShowReplays(player, creature, infos);
            return;
        }
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Back", REPLAY_GOSSIP_SENDER_SELECT, GOSSIP_ACTION_INFO_DEF);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature ? creature->GetGUID() : player->GetGUID());
    }

    void FavoriteMatchId(uint64 playerGuid, uint32 code)
    {
        // Need to check if the match exists in character_arena_replays, then insert in character_saved_replays
        QueryResult result = CharacterDatabase.Query("SELECT id FROM character_arena_replays WHERE id = " + std::to_string(code));
        if (result)
        {
            std::string query = "INSERT INTO character_saved_replays (character_id, replay_id) VALUES (" + std::to_string(playerGuid) + ", " + std::to_string(code) + ")";
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

        TeamId teamId = Player::TeamIdForRace(player->getRace());

        uint32 queueSlot = 0;
        WorldPacket data;

        // TEAM_NEUTRAL can leave the replay instance without a valid team start location on some maps,
        // which can cascade into bad teleports/homebinds. Keep the player on their real faction team
        // while still marking them as spectator via SetPendingSpectatorForBG().
        player->SetBattlegroundId(bg->GetInstanceID(), bgTypeId, queueSlot, true, false, teamId);
        player->SetEntryPoint();
        sBattlegroundMgr->SendToBattleground(player, bg->GetInstanceID(), bgTypeId);
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_IN_PROGRESS, 0, bg->GetStartTime(), bg->GetArenaType(), teamId);
        player->GetSession()->SendPacket(&data);
        LockReplayViewerControl(player, replayId);
        ActiveReplaySession& replaySession = activeReplaySessions[player->GetGUID().GetCounter()];
        replaySession.viewerWasParticipant = viewerWasParticipant;
        replaySession.actorSpectateOnWinnerTeam = sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.StartOnWinnerTeam", true);
        replaySession.actorTrackIndex = 0;
        replaySession.nextActorCycleMs = 0;
        replaySession.nextActorTeleportMs = 0;

        if (viewerWasParticipant && sConfigMgr->GetOption<bool>("ArenaReplay.ActorSpectate.StartOnSelfWhenParticipant", true))
        {
            auto const* winnerTracks = SelectTracks(record, true);
            auto const* loserTracks = SelectTracks(record, false);
            auto findSelf = [player](std::vector<ActorTrack> const* tracks) -> int32
            {
                if (!tracks)
                    return -1;
                for (size_t i = 0; i < tracks->size(); ++i)
                    if ((*tracks)[i].guid == player->GetGUID().GetRawValue())
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

        loadedReplays[p->GetGUID().GetCounter()] = std::move(record);
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

            WorldPacket packet(opcode, packetSize);

            if (packetSize > 0)
            {
                std::vector<uint8> tmp(packetSize, 0);
                buffer.read(tmp.data(), packetSize);
                packet.append(tmp.data(), packetSize);
            }

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

    void OnLogout(Player* player) override
    {
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

void AddArenaReplayScripts()
{
    new ConfigLoaderArenaReplay();
    new ArenaReplayServerScript();
    new ArenaReplayBGScript();
    new ArenaReplayArenaScript();
    new ArenaReplayPlayerScript();
    new ReplayGossip();
    new PlayerGossip_ArenaReplayService();
}
