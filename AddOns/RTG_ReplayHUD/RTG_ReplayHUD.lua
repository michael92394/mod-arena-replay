local ADDON_NAME = ...
local PREFIX = "RTG_REPLAY"
local HUD = CreateFrame("Frame", "RTGReplayHUDFrame", UIParent)
local eventFrame = CreateFrame("Frame")

RTGReplayHUDDB = RTGReplayHUDDB or {}

local state = {
    active = false,
    actorGuid = nil,
    actorName = nil,
    actorClass = nil,
    actorIndex = 0,
    actorCount = 0,
    watcherCount = 0,
    watchers = {},
}

local CLASS_COLORS = CUSTOM_CLASS_COLORS or RAID_CLASS_COLORS or {}
local MAX_WATCHERS_SHOWN = 8
local WATCHER_SIZE = 28

local function Split(str, sep)
    local t = {}
    if not str or str == "" then
        return t
    end

    sep = sep or "|"
    local pattern = string.format("([^%s]+)", sep)
    for token in string.gmatch(str, pattern) do
        t[#t + 1] = token
    end
    return t
end

local function Trim(s)
    if not s then return "" end
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function GetClassColorHex(classToken)
    local c = classToken and CLASS_COLORS[classToken]
    if not c then
        return "|cffffffff"
    end

    local r = math.floor((c.r or 1) * 255 + 0.5)
    local g = math.floor((c.g or 1) * 255 + 0.5)
    local b = math.floor((c.b or 1) * 255 + 0.5)
    return string.format("|cff%02x%02x%02x", r, g, b)
end

local function SafeNumber(v, fallback)
    local n = tonumber(v)
    if not n then
        return fallback or 0
    end
    return n
end


local function RunReplayCommand(command)
    command = Trim(command or "")
    if command == "" then
        return
    end

    if ChatFrameEditBox and ChatFrameEditBox:IsShown() then
        ChatFrameEditBox:SetText(command)
        ChatEdit_SendText(ChatFrameEditBox, 0)
        return
    end

    local editBox = DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.editBox
    if editBox then
        editBox:SetText(command)
        ChatEdit_SendText(editBox, 0)
        return
    end

    SendChatMessage(command, "SAY")
end

local function SavePosition()
    local p, _, _, x, y = HUD:GetPoint(1)
    RTGReplayHUDDB.point = p
    RTGReplayHUDDB.x = x
    RTGReplayHUDDB.y = y
end

local function ApplySavedPosition()
    HUD:ClearAllPoints()
    HUD:SetPoint(
        RTGReplayHUDDB.point or "BOTTOM",
        UIParent,
        RTGReplayHUDDB.point or "BOTTOM",
        RTGReplayHUDDB.x or 0,
        RTGReplayHUDDB.y or 185
    )
end

local function BuildWatcherTooltip(frame)
    GameTooltip:SetOwner(frame, "ANCHOR_TOP")
    GameTooltip:ClearLines()
    GameTooltip:AddLine("Watching", 1, 0.82, 0)

    if frame.fullName and frame.fullName ~= "" then
        GameTooltip:AddLine(frame.fullName, 1, 1, 1)
    end

    if frame.classToken and frame.classToken ~= "" then
        local color = CLASS_COLORS[frame.classToken]
        if color then
            GameTooltip:AddLine(frame.classToken, color.r, color.g, color.b)
        else
            GameTooltip:AddLine(frame.classToken, 0.8, 0.8, 0.8)
        end
    end

    GameTooltip:Show()
end

local function HideTooltip()
    GameTooltip:Hide()
end

HUD:SetSize(420, 94)
HUD:SetMovable(true)
HUD:EnableMouse(true)
HUD:RegisterForDrag("LeftButton")
HUD:SetClampedToScreen(true)
HUD:SetFrameStrata("DIALOG")
HUD:SetToplevel(true)
HUD:Hide()

HUD:SetScript("OnDragStart", function(self)
    if IsShiftKeyDown() then
        self:StartMoving()
    end
end)

HUD:SetScript("OnDragStop", function(self)
    self:StopMovingOrSizing()
    SavePosition()
end)

HUD.bg = HUD:CreateTexture(nil, "BACKGROUND")
HUD.bg:SetAllPoints(HUD)
HUD.bg:SetTexture("Interface\\Tooltips\\UI-Tooltip-Background")
HUD.bg:SetVertexColor(0, 0, 0, 0.72)

HUD.border = CreateFrame("Frame", nil, HUD, BackdropTemplateMixin and "BackdropTemplate")
HUD.border:SetAllPoints(HUD)
HUD.border:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true,
    tileSize = 16,
    edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 }
})
HUD.border:SetBackdropColor(0, 0, 0, 0)
HUD.border:SetBackdropBorderColor(0.8, 0.7, 0.2, 1)

HUD.title = HUD:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
HUD.title:SetPoint("TOP", HUD, "TOP", 0, -10)
HUD.title:SetText("Replay View")
HUD.title:SetTextColor(1, 0.82, 0)

HUD.prev = CreateFrame("Button", nil, HUD, "UIPanelButtonTemplate")
HUD.prev:SetSize(34, 24)
HUD.prev:SetPoint("TOP", HUD, "TOP", -120, -28)
HUD.prev:SetText("<")
HUD.prev:SetScript("OnClick", function()
    if not state.active then return end
    RunReplayCommand(".rtgreplay prev")
end)

HUD.next = CreateFrame("Button", nil, HUD, "UIPanelButtonTemplate")
HUD.next:SetSize(34, 24)
HUD.next:SetPoint("TOP", HUD, "TOP", 120, -28)
HUD.next:SetText(">")
HUD.next:SetScript("OnClick", function()
    if not state.active then return end
    RunReplayCommand(".rtgreplay next")
end)

HUD.name = HUD:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
HUD.name:SetPoint("TOP", HUD, "TOP", 0, -30)
HUD.name:SetWidth(230)
HUD.name:SetJustifyH("CENTER")
HUD.name:SetText("-")

HUD.meta = HUD:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
HUD.meta:SetPoint("TOP", HUD.name, "BOTTOM", 0, -3)
HUD.meta:SetTextColor(0.78, 0.78, 0.78)
HUD.meta:SetText("")

HUD.watchLabel = HUD:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
HUD.watchLabel:SetPoint("TOP", HUD.meta, "BOTTOM", 0, -7)
HUD.watchLabel:SetText("Watching: 0")
HUD.watchLabel:SetTextColor(1, 0.82, 0)

HUD.watchStrip = CreateFrame("Frame", nil, HUD)
HUD.watchStrip:SetSize(320, 30)
HUD.watchStrip:SetPoint("TOP", HUD.watchLabel, "BOTTOM", 0, -5)

HUD.watchFrames = {}
for i = 1, MAX_WATCHERS_SHOWN do
    local f = CreateFrame("Button", nil, HUD.watchStrip)
    f:SetSize(WATCHER_SIZE, WATCHER_SIZE)

    if i == 1 then
        f:SetPoint("LEFT", HUD.watchStrip, "LEFT", 0, 0)
    else
        f:SetPoint("LEFT", HUD.watchFrames[i - 1], "RIGHT", 6, 0)
    end

    f.bg = f:CreateTexture(nil, "BACKGROUND")
    f.bg:SetAllPoints(f)
    f.bg:SetTexture("Interface\\Buttons\\WHITE8X8")
    f.bg:SetVertexColor(0.08, 0.08, 0.08, 0.9)

    f.portrait = f:CreateTexture(nil, "ARTWORK")
    f.portrait:SetPoint("TOPLEFT", f, "TOPLEFT", 2, -2)
    f.portrait:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -2, 2)
    f.portrait:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")

    f.border = f:CreateTexture(nil, "OVERLAY")
    f.border:SetAllPoints(f)
    f.border:SetTexture("Interface\\Buttons\\UI-Quickslot2")

    f:SetScript("OnEnter", BuildWatcherTooltip)
    f:SetScript("OnLeave", HideTooltip)
    f:Hide()

    HUD.watchFrames[i] = f
end

HUD.watchOverflow = HUD.watchStrip:CreateFontString(nil, "OVERLAY", "GameFontNormal")
HUD.watchOverflow:SetPoint("LEFT", HUD.watchFrames[MAX_WATCHERS_SHOWN], "RIGHT", 10, 0)
HUD.watchOverflow:SetText("")
HUD.watchOverflow:SetTextColor(1, 0.82, 0)

local function SetWatcherPortrait(frame, watcher)
    frame.fullName = watcher.name or "Unknown"
    frame.classToken = watcher.classToken

    if watcher.icon and watcher.icon ~= "" then
        frame.portrait:SetTexture(watcher.icon)
    else
        frame.portrait:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
    end

    local classColor = CLASS_COLORS[watcher.classToken or ""]
    if classColor then
        frame.bg:SetVertexColor(classColor.r * 0.35, classColor.g * 0.35, classColor.b * 0.35, 0.95)
    else
        frame.bg:SetVertexColor(0.08, 0.08, 0.08, 0.9)
    end

    frame:Show()
end

local function UpdateName()
    if not state.actorName or state.actorName == "" then
        HUD.name:SetText("-")
        HUD.meta:SetText("")
        return
    end

    local colorHex = GetClassColorHex(state.actorClass)
    HUD.name:SetText(colorHex .. state.actorName .. "|r")

    local left = state.actorIndex > 0 and state.actorIndex or 0
    local total = state.actorCount > 0 and state.actorCount or 0
    local classText = state.actorClass or "UNKNOWN"
    HUD.meta:SetText(string.format("Player %d/%d  •  %s", left, total, classText))
end

local function UpdateWatchers()
    local shown = 0
    for i = 1, MAX_WATCHERS_SHOWN do
        local frame = HUD.watchFrames[i]
        local watcher = state.watchers[i]
        if watcher then
            shown = shown + 1
            SetWatcherPortrait(frame, watcher)
        else
            frame:Hide()
        end
    end

    local overflow = state.watcherCount - shown
    if overflow > 0 then
        HUD.watchOverflow:SetText("+" .. overflow)
    else
        HUD.watchOverflow:SetText("")
    end

    HUD.watchLabel:SetText(string.format("Watching: %d", state.watcherCount or 0))
end

local function ResetState()
    state.active = false
    state.actorGuid = nil
    state.actorName = nil
    state.actorClass = nil
    state.actorIndex = 0
    state.actorCount = 0
    state.watcherCount = 0
    wipe(state.watchers)
end

local function HideHUD()
    ResetState()
    UpdateName()
    UpdateWatchers()
    HUD:Hide()
end

local function ShowHUD()
    UpdateName()
    UpdateWatchers()
    HUD:Show()
end

local function ParseWatcherEntry(entry)
    local parts = Split(entry, ",")
    return {
        guid = Trim(parts[1] or ""),
        name = Trim(parts[2] or "Unknown"),
        classToken = Trim(parts[3] or ""),
        icon = Trim(parts[4] or ""),
    }
end

local function ApplyStart(payload)
    local parts = Split(payload, "|")
    state.active = true
    state.actorGuid = Trim(parts[1] or "")
    state.actorName = Trim(parts[2] or "Unknown")
    state.actorClass = Trim(parts[3] or "")
    state.actorIndex = SafeNumber(parts[4], 1)
    state.actorCount = SafeNumber(parts[5], 1)
    ShowHUD()
end

local function ApplyPOV(payload)
    local parts = Split(payload, "|")
    state.actorGuid = Trim(parts[1] or state.actorGuid or "")
    state.actorName = Trim(parts[2] or state.actorName or "Unknown")
    state.actorClass = Trim(parts[3] or state.actorClass or "")
    state.actorIndex = SafeNumber(parts[4], state.actorIndex)
    state.actorCount = SafeNumber(parts[5], state.actorCount)
    UpdateName()
    if state.active then
        HUD:Show()
    end
end

local function ApplyWatchers(payload)
    wipe(state.watchers)

    local countPart, listPart = payload:match("^(%d+)%|(.*)$")
    if countPart then
        state.watcherCount = tonumber(countPart) or 0
    else
        state.watcherCount = 0
        listPart = payload
    end

    listPart = listPart or ""
    if listPart ~= "" then
        local entries = Split(listPart, ";")
        for _, entry in ipairs(entries) do
            if entry and entry ~= "" then
                state.watchers[#state.watchers + 1] = ParseWatcherEntry(entry)
            end
        end
    end

    if state.watcherCount == 0 then
        state.watcherCount = #state.watchers
    end

    UpdateWatchers()
end

local function HandleMessage(msg)
    local cmd, payload = msg:match("^(.-)|(.+)$")
    if not cmd then
        cmd = msg
        payload = ""
    end

    if cmd == "START" then
        ApplyStart(payload)
    elseif cmd == "POV" then
        ApplyPOV(payload)
    elseif cmd == "WATCHERS" then
        ApplyWatchers(payload)
    elseif cmd == "END" then
        HideHUD()
    elseif cmd == "PING" then
        SendAddonMessage(PREFIX, "PONG|1.0.0", "WHISPER", UnitName("player"))
    end
end

SLASH_RTGREPLAYHUD1 = "/rtgreplay"
SlashCmdList.RTGREPLAYHUD = function(msg)
    msg = string.lower(Trim(msg or ""))

    if msg == "test" then
        HandleMessage("START|Player-0-00000001|Michaeldev|ROGUE|2|8")
        HandleMessage("WATCHERS|6|g1,ViewerOne,MAGE,Interface\\Icons\\INV_Helmet_21;g2,ViewerTwo,PRIEST,Interface\\Icons\\INV_Helmet_125;g3,ViewerThree,WARRIOR,Interface\\Icons\\Ability_Warrior_DefensiveStance;g4,ViewerFour,HUNTER,Interface\\Icons\\INV_Helmet_05;g5,ViewerFive,DRUID,Interface\\Icons\\Ability_Druid_CatForm;g6,ViewerSix,SHAMAN,Interface\\Icons\\Spell_Nature_BloodLust")
    elseif msg == "hide" then
        HideHUD()
    elseif msg == "show" then
        state.active = true
        ShowHUD()
    else
        DEFAULT_CHAT_FRAME:AddMessage("|cffffcc00RTG Replay HUD commands:|r /rtgreplay test, /rtgreplay hide, /rtgreplay show")
    end
end

eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:RegisterEvent("PLAYER_LOGIN")
eventFrame:RegisterEvent("CHAT_MSG_ADDON")
eventFrame:RegisterEvent("CHAT_MSG_SYSTEM")
eventFrame:RegisterEvent("PLAYER_ENTERING_WORLD")
eventFrame:SetScript("OnEvent", function(_, event, ...)
    if event == "ADDON_LOADED" then
        local addon = ...
        if addon == ADDON_NAME then
            if C_ChatInfo and C_ChatInfo.RegisterAddonMessagePrefix then
                C_ChatInfo.RegisterAddonMessagePrefix(PREFIX)
            elseif RegisterAddonMessagePrefix then
                RegisterAddonMessagePrefix(PREFIX)
            end
            ApplySavedPosition()
        end
    elseif event == "PLAYER_LOGIN" then
        ApplySavedPosition()
    elseif event == "PLAYER_ENTERING_WORLD" then
        if not state.active then
            HUD:Hide()
        end
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, msg = ...
        if prefix ~= PREFIX then
            return
        end
        HandleMessage(msg or "")
    elseif event == "CHAT_MSG_SYSTEM" then
        local msg = ...
        local payload = msg and msg:match("^%[RTG_REPLAY%]%s*(.+)$")
        if payload and payload ~= "" then
            HandleMessage(payload)
        end
    end
end)
