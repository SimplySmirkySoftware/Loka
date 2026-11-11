-- Report the runtime load base of a Retro68 68K application running in MAME.
--
-- gdb needs one number to make a Retro68 image debuggable: the address the
-- CODE resources were loaded at. The image is linked contiguously from vaddr
-- 0, so `add-symbol-file <elf> -o <base>` relocates every section at once.
--
-- Method: search the application heap for the opening bytes of a function
-- that carries no relocations in the linked ELF. Those bytes are identical in
-- the file and in memory, so a match gives base = found - link_address.
-- Picking a relocation-free function is what makes this independent of
-- Retro68's startup fixups; see docs/MAME_DEVELOPMENT.md for how to choose
-- one with readelf.
--
-- Environment:
--   LOKA_PATTERN_WORD0  first 4 pattern bytes as hex   (required)
--   LOKA_PATTERN_WORD1  next 4 pattern bytes as hex    (required)
--   LOKA_PATTERN_LINK   the function's link address    (required)
--   LOKA_LAUNCH_WAIT    emulated seconds to wait for boot (default 90)
--   LOKA_GDB_LOG        log file path (default mame-find-base.log)
--   LOKA_STAY_ALIVE     set to keep the machine running after reporting,
--                       so a gdb session can attach in the same run
--
-- MAME's Lua is 5.4: string.format("%d", x) raises on a non-integral float,
-- which kills this coroutine while MAME keeps running at full speed. Every
-- division that reaches %d below uses // for that reason.

local function required(name)
    return assert(os.getenv(name), name .. " is required")
end

local WORD0 = tonumber(required("LOKA_PATTERN_WORD0"), 16)
local WORD1 = tonumber(required("LOKA_PATTERN_WORD1"), 16)
local LINK_ADDRESS = tonumber(required("LOKA_PATTERN_LINK"), 16)
local BOOT_WAIT = tonumber(os.getenv("LOKA_LAUNCH_WAIT") or "90")

local log = assert(io.open(os.getenv("LOKA_GDB_LOG") or "mame-find-base.log", "w"))
local function say(fmt, ...)
    local line = "LOKA-BASE: " .. string.format(fmt, ...)
    print(line)
    log:write(line .. "\n")
    log:flush()
end

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local keys = dofile(here .. "mame-keys.lua")(say)
local space = manager.machine.devices[":maincpu"].spaces["program"]

-- Boot, open the LokaDev volume, select the application, open it.
say("booting (%d emulated seconds)", BOOT_WAIT)
emu.wait(BOOT_WAIT)
keys.decideKeyMapReadable()
keys.openVolume()
keys.tap(keys.keyByName("Esc"))
-- Select only the application. Cmd-A would also open the staged data files:
-- LokaTest.cfg lands in TeachText, which takes the foreground, and a
-- cannotBackground vehicle then freezes mid-scenario -- a debug session that
-- looks exactly like the hang or crash under investigation (#398). Tab
-- cycles the Finder selection the same way mame-launch.lua uses it; the
-- landing count is a per-boot-image fact the caller owns (LOKA_TAB_COUNT,
-- default 1). Verify the landing through the heap-size line below: the
-- wrong application shows the wrong partition size.
local tabCount = tonumber(os.getenv("LOKA_TAB_COUNT") or "1")
assert(tabCount and tabCount > 0 and tabCount == math.floor(tabCount),
    "LOKA_TAB_COUNT must be a positive integer")
say("selecting the application (%d tabs)", tabCount)
keys.selectApplication(tabCount)
keys.commandTap(keys.field(":macadb:KEY1", "o  O"))
emu.wait(20)
say("application launched")

-- The CODE resources live in the application heap, which the app's SIZE
-- partition keeps far smaller than RAM. Bound the search with it.
local applZone = space:read_u32(0x02AA)
local applLimit = space:read_u32(0x0130)
say("ApplZone 0x%08x  ApplLimit 0x%08x", applZone, applLimit)

-- Collect every match rather than taking the first. The pattern is only eight
-- bytes of a compiler prologue, so a second function can begin identically;
-- accepting the first hit would silently compute a base that is off by the
-- distance between them and shift every symbol. A duplicate is a signal to
-- pick a different function, not something to resolve by guessing.
local function scan(from, to)
    local hits = {}
    local address = from - (from % 2)
    while address < to - 8 do
        if space:read_u32(address) == WORD0 and space:read_u32(address + 4) == WORD1 then
            hits[#hits + 1] = address
        end
        address = address + 2
    end
    return hits
end

local hits = {}
if applZone > 0x1000 and applLimit > applZone and applLimit < 0x00800000 then
    say("searching the application heap (%d KB)", (applLimit - applZone) // 1024)
    hits = scan(applZone, applLimit)
end
if #hits == 0 then
    say("falling back to a full RAM sweep")
    hits = scan(0x00001000, 0x007ffff0)
end

if #hits == 0 then
    say("pattern NOT found; check that the pattern matches this build")
    log:close()
    manager.machine:exit()
    return
end
if #hits > 1 then
    say("pattern is NOT unique: %d matches, so the base cannot be derived", #hits)
    for i = 1, #hits do
        say("  match %d at 0x%08x", i, hits[i])
    end
    say("pick a different relocation-free function and try again")
    log:close()
    manager.machine:exit()
    return
end

local found = hits[1]
local base = found - LINK_ADDRESS
say("found at 0x%08x (unique)", found)
say("BASE 0x%08x", base)
say("gdb: add-symbol-file <elf> -o 0x%08x", base)

if os.getenv("LOKA_STAY_ALIVE") then
    say("staying alive for a gdb session")
    while true do
        emu.wait(5)
    end
end

log:close()
manager.machine:exit()
