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

local function tap(key)
    key:set_value(1)
    emu.wait(0.10)
    key:clear_value()
    emu.wait(0.20)
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
    -- PROBE (not for merge): real ADB clicks on the click-gate probe's Target button.
    if os.getenv("LOKA_PROBE_CLICKS") then
        local probeOk, probeError = pcall(function()
            local mem = manager.machine.devices[":maincpu"].spaces["program"]
            local function ptr(address) return mem:read_u32(address) & 0xFFFFFF end
            local function signed(address)
                local n = mem:read_u16(address)
                return n >= 32768 and n - 65536 or n
            end
            local function buttonPoint()
                local window = ptr(0x9D6)
                for _ = 1, 16 do
                    if window == 0 then return nil end
                    local handle = ptr(window + 140)
                    for _ = 1, 64 do
                        if handle == 0 then break end
                        local control = ptr(handle)
                        if control == 0 then break end
                        local top, left = signed(control + 8), signed(control + 10)
                        local bottom, right = signed(control + 12), signed(control + 14)
                        if right - left >= 40 and bottom - top >= 12 and bottom - top <= 30 then
                            return (left + right) // 2 - signed(window + 10), (top + bottom) // 2 - signed(window + 8)
                        end
                        handle = ptr(control)
                    end
                    window = ptr(window + 144)
                end
                return nil
            end
            local button = field(":macadb:MOUSE0", "Mouse Button 0")
            emu.wait(14)
            local x, y = buttonPoint()
            assert(x, "probe button not found")
            say("probe button at %d,%d", x, y)
            mem:write_u16(0x828, y); mem:write_u16(0x82A, x)
            mem:write_u16(0x82C, y); mem:write_u16(0x82E, x)
            mem:write_u8(0x8CE, mem:read_u8(0x8CF))
            emu.wait(0.5)
            math.randomseed(1461)
            for _ = 1, 100 do
                button:set_value(1); emu.wait(0.08); button:clear_value()
                emu.wait(0.12 + math.random() * 0.3)
            end
            say("probe clicks done")
        end)
        if not probeOk then say("probe clicks failed: %s", tostring(probeError)) end
    end
    emu.wait(RUN_WAIT)
    say("standalone wait complete")
    manager.machine:exit()
    return
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
