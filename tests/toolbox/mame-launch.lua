-- Boot, launch the app on the LokaDev disk with the keyboard, then take a
-- screen snapshot as verification evidence and exit (or wait without a snapshot
-- for a self-quitting standalone application). Keyboard sequence and
-- Lua 5.4 cautions follow scripts/mame-find-base.lua.

local BOOT_WAIT = tonumber(os.getenv("LOKA_LAUNCH_WAIT") or "90")
local STANDALONE = os.getenv("LOKA_STANDALONE") == "1"
local RUN_WAIT = tonumber(os.getenv("LOKA_RUN_WAIT") or "90")
if STANDALONE then
    assert(RUN_WAIT and RUN_WAIT > 0 and RUN_WAIT < math.huge, "LOKA_RUN_WAIT must be finite and positive")
end
local SETTLE_TIMEOUT = tonumber(os.getenv("LOKA_SETTLE_TIMEOUT") or "30")
local SETTLE_SAMPLE_WAIT = 0.5
local SETTLE_STABLE_SAMPLES = 3

assert(SETTLE_TIMEOUT > 0, "LOKA_SETTLE_TIMEOUT must be positive")

local log = assert(io.open(os.getenv("LOKA_SNAP_LOG") or "mame-launch.log", "w"))
local function say(fmt, ...)
    local line = "LOKA-SNAP: " .. string.format(fmt, ...)
    print(line)
    log:write(line .. "\n")
    log:flush()
end

local function field(portTag, name)
    return assert(manager.machine.ioport.ports[portTag].fields[name])
end

local lKey = field(":macadb:KEY2", "l  L")
local oKey = field(":macadb:KEY1", "o  O")
local commandKey = field(":macadb:KEY3", "Command / Open Apple")

-- Keys live spread across the :macadb:KEY* ports; find one by field name.
local function keyByName(name)
    for tag, port in pairs(manager.machine.ioport.ports) do
        if tag:find("^:macadb:KEY") then
            local found = port.fields[name]
            if found then
                return found
            end
        end
    end
    error("no key field named " .. name)
end

-- The guest sees a key only if it polls ADB while the key is down, and it can
-- go more than 100 ms without polling: the O of the launch Command-O fell into
-- such a gap, so the application never opened and the cell timed out (#1674).
-- tap() therefore holds a key past its 0.10 s until the guest's KeyMap (0x174)
-- shows it down, and after the release waits until KeyMap shows it up. A key
-- seen within the usual timing adds no time. MAME's ADB keyboard lays the Mac
-- virtual key codes out 16 to a :macadb:KEYn port.
local KEY_MAP = 0x174
local KEY_SEEN_TIMEOUT = 5.0
local KEY_SEEN_STEP = 0.01
local keyMemory = manager.machine.devices[":maincpu"].spaces["program"]

local function keyCode(key)
    local port = assert(tonumber(key.port.tag:match("^:macadb:KEY(%d+)$")), "not an ADB key: " .. key.name)
    assert(key.mask ~= 0 and (key.mask & (key.mask - 1)) == 0, "ADB key field is not one bit: " .. key.name)
    local bit = 0
    while (key.mask >> bit) ~= 1 do
        bit = bit + 1
    end
    return port * 16 + bit
end

local function awaitKeySeen(key, down)
    local code = keyCode(key)
    local waited = 0.0
    while (((keyMemory:read_u8(KEY_MAP + (code >> 3)) >> (code & 7)) & 1) == 1) ~= down do
        if waited >= KEY_SEEN_TIMEOUT then
            say("key %s not seen %s after %.2f emulated seconds", key.name, down and "down" or "up", waited)
            manager.machine:exit()
            error("key not seen", 0)
        end
        emu.wait(KEY_SEEN_STEP)
        waited = waited + KEY_SEEN_STEP
    end
    if waited > 0 then
        say("key %s seen %s after %.2f more emulated seconds", key.name, down and "down" or "up", waited)
    end
end

local function tap(key)
    key:set_value(1)
    emu.wait(0.10)
    awaitKeySeen(key, true)
    key:clear_value()
    emu.wait(0.20)
    awaitKeySeen(key, false)
end

local function findScreen()
    local screen = manager.machine.screens[":screen"]
    if not screen then
        for tag, candidate in pairs(manager.machine.screens) do
            say("found screen %s", tostring(tag))
            screen = candidate
            break
        end
    end
    assert(screen, "no screen device")
    return screen
end

local function captureSnapshotPixels(video)
    local width, height = video:snapshot_size()
    return video:snapshot_pixels(), width, height
end

local function snapshotPixel(pixels, width, x, y)
    local offset = (y * width + x) * 4 + 1
    return string.unpack("=I4", pixels, offset)
end

local function completionSignalPixels(pixels, width, height)
    return {
        snapshotPixel(pixels, width, width - 11, height - 11),
        snapshotPixel(pixels, width, width - 8, height - 11),
        snapshotPixel(pixels, width, width - 11, height - 8),
        snapshotPixel(pixels, width, width - 8, height - 8)
    }
end

local function completionSignalVisible(frame, width, height, baseline)
    local signal = completionSignalPixels(frame, width, height)
    local solid = signal[1] == signal[2] and signal[1] == signal[3] and signal[1] == signal[4]
    local changed = signal[1] ~= baseline[1] or signal[2] ~= baseline[2]
        or signal[3] ~= baseline[3] or signal[4] ~= baseline[4]
    return solid and changed, signal
end

say("booting (%s emulated seconds)", BOOT_WAIT)
emu.wait(BOOT_WAIT)
tap(lKey)
commandKey:set_value(1); tap(oKey); commandKey:clear_value()
emu.wait(5)
-- Select only the application. A select-all would also open the plain data
-- files beside it, and their "application not found" alerts land on top of
-- the scene the snapshot is supposed to capture. Letter type-selection is
-- unusable here: on a KanjiTalk system the input method swallows typed
-- romaji into its kana window and the Finder never sees it. Tab is immune;
-- it cycles the Finder selection. Empirically (per-tab snapshot diagnostics,
-- 2026-07-31) the first Tab in the freshly opened LokaDev window lands on
-- LokaTestsToolbox68K, then cycles ASSETS.LRP -> LokaTest.cfg -> app again.
-- The shell runner owns the navigation fact beside the files
-- it stages and exports LOKA_TAB_COUNT; callers may override it for diagnosis.
local tabKey = keyByName("Tab")
local tabCount = assert(tonumber(os.getenv("LOKA_TAB_COUNT")),
    "LOKA_TAB_COUNT must be set by the runner")
assert(tabCount > 0 and tabCount == math.floor(tabCount),
    "LOKA_TAB_COUNT must be a positive integer")
for _ = 1, tabCount do
    tap(tabKey)
end
emu.wait(1)
local screen, video, baselineFrame, baselineWidth, baselineHeight, baselineSignal
if not STANDALONE then
    screen = findScreen()
    video = manager.machine.video
    baselineFrame, baselineWidth, baselineHeight = captureSnapshotPixels(video)
    baselineSignal = completionSignalPixels(baselineFrame, baselineWidth, baselineHeight)
end
commandKey:set_value(1); tap(oKey); commandKey:clear_value()
if STANDALONE then
    -- These apps have no screen completion marker. Allow a bounded interval
    -- for their own quit; the shell retrieves LOG.TXT only after MAME exits.
    -- Elapsed time alone is not success: --expect checks the complete audit.
    say("standalone opened; waiting %s emulated seconds", RUN_WAIT)
    emu.wait(RUN_WAIT)
    say("standalone wait complete")
    manager.machine:exit()
    return
end

-- Two real close confirms: Cancel must keep the row, Save must close it.
-- WindowList (0x9D6) is a per-process low-memory global, and while the modal
-- loop yields the Process Manager can have another process's list switched in
-- (CurApName is not switched at the same moment, so it cannot vouch for the
-- list). A reading counts only when its chain holds one of this application's
-- own windows, learned from the first confirm's chain: no other process shows
-- a dialog in front here. A key that reaches a confirm before its modal loop
-- runs can be lost (the next dialog is built at the freed one's address and
-- briefly carries another kind), so each answer waits 0.3 s and is repeated,
-- at most three times, until no dialog is left in the app's list. A repeated
-- key means the same thing, and a confirm that never appears is never answered.
if os.getenv("LOKA_SCENARIO_CELL") == "simpletext/close-dirty" then
    local mem = manager.machine.devices[":maincpu"].spaces["program"]
    local own = nil
    local function chain()
        local windows = {}
        local window = mem:read_u32(0x9D6) & 0xFFFFFF
        for _ = 1, 16 do
            if window == 0 then break end
            windows[#windows + 1] = {window, mem:read_u16(window + 0x6C)}
            window = mem:read_u32(window + 0x90) & 0xFFFFFF
        end
        return windows
    end
    local function ours(windows)
        for _, entry in ipairs(windows) do
            if own[entry[1]] then return true end
        end
        return false
    end
    -- nil when another process's list is switched in.
    local function frontIsDialog()
        local windows = chain()
        if not ours(windows) then return nil end
        return windows[1][2] == 2
    end
    local function anyDialog()
        local windows = chain()
        if not ours(windows) then return nil end
        for _, entry in ipairs(windows) do
            if entry[2] == 2 then return true end
        end
        return false
    end
    local function await(predicate, wanted, step, limit)
        for _ = 1, limit do
            if predicate() == wanted then return true end
            emu.wait(step)
        end
        return false
    end
    local function learnOwnWindows()
        for _ = 1, 3000 do
            local windows = chain()
            if #windows > 1 and windows[1][2] == 2 then
                own = {}
                for i = 2, #windows do own[windows[i][1]] = true end
                return true
            end
            emu.wait(0.01)
        end
        return false
    end
    -- Hold the key while polling finely: the next confirm follows within a
    -- fraction of a second, and a coarse poll would take it for the same one.
    local function answer(label, keyName)
        if not await(frontIsDialog, true, 0.01, 3000) then
            say("close-dirty timeout waiting for the %s confirm", label)
            return false
        end
        emu.wait(0.3)
        local key = keyByName(keyName)
        for attempt = 1, 3 do
            key:set_value(1)
            local gone = await(anyDialog, false, 0.01, 100)
            key:clear_value()
            if gone then
                say("close-dirty answered the %s confirm with %s (attempt %d)", label, keyName, attempt)
                return true
            end
            emu.wait(0.3)
        end
        say("close-dirty the %s confirm did not leave after %s", label, keyName)
        return false
    end
    if not learnOwnWindows() then
        say("close-dirty timeout waiting for the Cancel confirm")
    elseif answer("Cancel", "Esc") then
        answer("Save", "Return")
    end
end

-- Native gestures for the editor bar cells (#1414, #1416), with coordinates
-- read from the live native bar, not a guessed screen position. The record
-- offsets are pinned at compile time in each cell's scenario header.
local function editorBarTransport()
    local cpu = manager.machine.devices[":maincpu"]
    assert(not cpu.shortname:match("^ppc"), "editor gesture cells currently target maciix")
    local mem = cpu.spaces["program"]
    local function ptr(address) return mem:read_u32(address) & 0xFFFFFF end
    local function signed(address)
        local n = mem:read_u16(address)
        return n >= 32768 and n - 65536 or n
    end
    -- Walk WindowList front to back: the input method can put a window with no
    -- controls in front of the app's window (KanjiTalk, after a key press).
    local function barPoint(active)
        local window = ptr(0x9D6) -- WindowList, frontmost NewWindow GrafPort.
        for _ = 1, 16 do
            if window == 0 then return nil end
            local handle = ptr(window + 140) -- offsetof(WindowRecord, controlList)
            for _ = 1, 64 do
                if handle == 0 then break end
                local control = ptr(handle)
                if control == 0 then break end
                local top, left = signed(control + 8), signed(control + 10)
                local bottom, right = signed(control + 12), signed(control + 14)
                local maximum = signed(control + 22)
                if right - left == 16 and bottom - top > 48
                    and mem:read_u8(control + 16) ~= 0 and (maximum > 0) == active then
                    -- GrafPort.portBits.bounds is in local coordinates; negate its
                    -- origin to convert the audited local point to screen coordinates.
                    return left + 8 - signed(window + 10), bottom - 24 - signed(window + 8),
                        top - signed(window + 8), signed(control + 18), maximum
                end
                handle = ptr(control)
            end
            window = ptr(window + 144) -- offsetof(WindowRecord, nextWindow)
        end
        return nil
    end
    -- wanted = nil accepts any value; a number waits for the app to settle there.
    local function awaitBar(active, wanted)
        for _ = 1, 200 do
            local x, y, top, value, maximum = barPoint(active)
            if x and (wanted == nil or value == wanted) then return x, y, top, value, maximum end
            emu.wait(0.1)
        end
        error("editor bar did not reach requested presentation")
    end
    return mem, barPoint, awaitBar
end

if os.getenv("LOKA_SCENARIO_CELL") == "smirkbench/text-editor-keys" then
    local _, barPoint, awaitBar = editorBarTransport()
    local function tapScroll(name)
        local key = field(":macadb:KEY7", name)
        key:set_value(1); emu.wait(0.1); key:clear_value(); emu.wait(0.5)
        say("editor key delivered: %s", name)
    end
    local keysOk, keysError = pcall(function()
        local _, _, _, _, maximum = awaitBar(true, 0)
        emu.wait(1) -- The cell focuses and snapshots the selection before input.
        -- Clicking the editor synchronizes the keyboard to its Roman font;
        -- switch back to the boot template's Japanese script before the keys.
        commandKey:set_value(1); emu.wait(0.1)
        tap(field(":macadb:KEY3", "Space")); commandKey:clear_value(); emu.wait(1)
        say("editor keyboard switched with Command-Space")
        tapScroll("Page Down")
        local _, _, _, down = awaitBar(true)
        assert(down > 0, "Page Down did not move the view")
        tapScroll("Page Up"); awaitBar(true, 0)
        tapScroll("End"); awaitBar(true, maximum)
        tapScroll("Home"); awaitBar(true, 0)
        -- Hold Command for one ADB poll before the key, or the key event can
        -- be posted before the modifier and arrive without cmdKey.
        commandKey:set_value(1); emu.wait(0.1); tapScroll("Page Down"); commandKey:clear_value()
        local _, _, _, value = barPoint(true)
        assert(value == 0, "Command Page Down moved the view")
        -- The cell moves to line one and drops focus after auditing Command.
        -- This marker cannot be produced by an ignored Command Page Down.
        awaitBar(true, 1)
        tapScroll("Page Down"); awaitBar(true, 1)
    end)
    commandKey:clear_value()
    -- As in text-editor-scroll: a failed key must not stop the completion
    -- transport below; the cell's own audit records how far the app got.
    if not keysOk then say("editor keys failed: %s", tostring(keysError)) end
end

if os.getenv("LOKA_SCENARIO_CELL") == "smirkbench/text-editor-japanese" then
    local inputOk, inputError = pcall(function()
        local _, _, awaitBar = editorBarTransport()
        awaitBar(false, 0)
        emu.wait(1) -- The short-document cell focuses and snapshots before input.
        -- TextEdit synchronizes the keyboard to the editor font's script when
        -- the cell clicks, so the keyboard is Roman here: switch back first.
        say("switch to Japanese with Command-Space")
        commandKey:set_value(1); emu.wait(0.1)
        tap(keyByName("Space")); commandKey:clear_value(); emu.wait(1)
        local function typeRoman(word)
            for letter in word:gmatch(".") do
                tap(keyByName(letter .. "  " .. letter:upper()))
            end
        end
        say("Japanese keyboard unchanged: type nihongo")
        typeRoman("nihongo")
        say("convert nihongo with Space")
        tap(keyByName("Space"))
        say("commit nihongo with Return")
        tap(keyByName("Return")); emu.wait(1)
        say("type ka and commit with Return")
        typeRoman("ka"); tap(keyByName("Return")); emu.wait(1)
        -- Paired with the cell's 15-second unchanged observation. Leave a
        -- generous quiet interval before the Roman positive control.
        say("Japanese commits complete; waiting for unchanged audit")
        emu.wait(20)
        say("switch to Roman with Command-Space")
        commandKey:set_value(1); emu.wait(0.1)
        tap(keyByName("Space")); commandKey:clear_value(); emu.wait(1)
        say("Roman positive control: x")
        typeRoman("x")
    end)
    commandKey:clear_value()
    if not inputOk then say("editor Japanese input failed: %s", tostring(inputError)) end
end

-- Real ADB traversal; the scenario driver only observes the resulting fields.
if os.getenv("LOKA_SCENARIO_CELL") == "helloworld/bmi-tab" then
    local inputOk, inputError = pcall(function()
        local mem = manager.machine.devices[":maincpu"].spaces["program"]
        local button = field(":macadb:MOUSE0", "Mouse Button 0")
        local function move(x, y)
            mem:write_u16(0x828, y); mem:write_u16(0x82A, x)
            mem:write_u16(0x82C, y); mem:write_u16(0x82E, x)
            mem:write_u8(0x8CE, mem:read_u8(0x8CF))
            emu.wait(0.2)
        end
        emu.wait(8)
        move(430, 222)
        button:set_value(1); emu.wait(0.3); button:clear_value(); emu.wait(2)
        button:set_value(1); emu.wait(0.3); button:clear_value(); emu.wait(1)
        commandKey:set_value(1); emu.wait(0.1)
        tap(keyByName("Space")); commandKey:clear_value(); emu.wait(1)
        tap(keyByName("1  !")); emu.wait(0.5)
        tap(keyByName("Tab")); emu.wait(0.5)
        tap(keyByName("2  @")); emu.wait(0.5)
        local shift = keyByName("Shift")
        shift:set_value(1); emu.wait(0.1)
        tap(keyByName("Tab")); shift:clear_value(); emu.wait(0.5)
        tap(keyByName("3  #")); emu.wait(0.5)
        move(600, 450)
    end)
    if not inputOk then say("BMI Tab input failed: %s", tostring(inputError)) end
end

if os.getenv("LOKA_SCENARIO_CELL") == "smirkbench/text-editor-scroll" then
    local mem, barPoint, awaitBar = editorBarTransport()
    local button = field(":macadb:MOUSE0", "Mouse Button 0")
    local function move(x, y)
        mem:write_u16(0x828, y); mem:write_u16(0x82A, x)
        mem:write_u16(0x82C, y); mem:write_u16(0x82E, x)
        mem:write_u8(0x8CE, mem:read_u8(0x8CF))
        emu.wait(0.2)
    end
    local function press() button:set_value(1); say("editor operation press") end
    local function release() button:clear_value(); say("editor operation release") end
    -- A failed gesture must not stop the completion transport below; the
    -- cell's own audit then records how far the app got.
    local gestureOk, gestureError = pcall(function()
    local x, y, top = awaitBar(true)
    say("editor selection drag from=(%d,%d) below=(%d,%d)", x - 50, top + 5, x - 50, y + 60)
    -- The ADB driver stamps mouseDown with the cursor position when it posts
    -- the event, so hold still after each press before moving.
    move(x - 50, top + 5); press(); emu.wait(0.3)
    for step = 1, 4 do move(x - 50, top + 5 + step * (y + 60 - top - 5) // 4) end
    emu.wait(0.5); release()
    -- The cell scrolls back to the top after it sees the drag auto-scroll.
    x, y, top = awaitBar(true, 0)
    say("editor thumb cancellation")
    move(x, top + 24); press(); emu.wait(0.3)
    move(x + 50, top + 24); move(x + 100, top + 24); emu.wait(0.3); release(); emu.wait(0.5)
    local _, _, _, value = barPoint(true)
    assert(value == 0, "cancelled thumb drag changed the value")
    say("editor page point global=(%d,%d), release outside", x, y)
    move(x, y); press(); emu.wait(0.2); move(x + 100, y); release()
    x, y = awaitBar(false)
    say("editor inactive point global=(%d,%d)", x, y)
    move(x, y); press(); emu.wait(0.2); release()
    move(0, 0)
    end)
    if not gestureOk then say("editor gesture failed: %s", tostring(gestureError)) end
end

-- Completion transport: reading a marker from the development HFS disk while
-- the guest owns it would add unsafe cross-mount coordination. After the
-- durable audit write returns, the test app opens an owned black marker.
-- MAME observes that signal through its existing live-screen seam, then waits
-- for consecutive identical full-screen samples. This is bounded and fails
-- closed; it never falls back to a fixed capture delay.
say("application opened; waiting up to %.2f emulated seconds for stable pixels", SETTLE_TIMEOUT)
local elapsed = 0.0
local completionSignaled = false
local completionPixels = baselineSignal
local stableSamples = 0
local previousPixels = nil
local previousWidth = 0
local previousHeight = 0
while elapsed < SETTLE_TIMEOUT and stableSamples < SETTLE_STABLE_SAMPLES do
    emu.wait(SETTLE_SAMPLE_WAIT)
    elapsed = elapsed + SETTLE_SAMPLE_WAIT
    local pixels, width, height = captureSnapshotPixels(video)
    if width == baselineWidth and height == baselineHeight then
        local visible = false
        visible, completionPixels = completionSignalVisible(pixels, width, height, baselineSignal)
        completionSignaled = completionSignaled or visible
    end
    if completionSignaled then
        if previousPixels == pixels and previousWidth == width and previousHeight == height then
            stableSamples = stableSamples + 1
        else
            stableSamples = 1
        end
    end
    previousPixels = pixels
    previousWidth = width
    previousHeight = height
end
if stableSamples < SETTLE_STABLE_SAMPLES then
    say("settle failed after %.2f emulated seconds (completion=%s stable=%d/%d)",
        elapsed, tostring(completionSignaled), stableSamples, SETTLE_STABLE_SAMPLES)
    say("completion pixels current=%s,%s,%s,%s baseline=%s,%s,%s,%s",
        tostring(completionPixels[1]), tostring(completionPixels[2]),
        tostring(completionPixels[3]), tostring(completionPixels[4]),
        tostring(baselineSignal[1]), tostring(baselineSignal[2]),
        tostring(baselineSignal[3]), tostring(baselineSignal[4]))
    local snapshotOk, snapshotError = pcall(function() screen:snapshot() end)
    say("failure snapshot %s%s", snapshotOk and "written" or "failed",
        snapshotOk and "" or ": " .. tostring(snapshotError))
    manager.machine:exit()
    return
end
say("settled after %.2f emulated seconds (%d stable samples)", elapsed, stableSamples)

-- manager.machine.screens is a device enumerator, not a Lua table: index it
-- by tag or iterate with pairs(); next() raises a type error.
local ok, err = pcall(function()
    screen:snapshot()
end)
if ok then
    say("snapshot written")
else
    say("snapshot failed: %s", tostring(err))
end
emu.wait(1)
say("done")
manager.machine:exit()
