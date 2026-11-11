-- Partition sizing probe (#1103): boot, launch the application on the LokaDev
-- disk (keyboard sequence as in mame-launch.lua), run a step workload, and
-- track the largest unpurgeable footprint of the application zone by walking
-- its 24-bit or 32-bit block list after every step and every 0.25 emulated
-- seconds of a wait. measure-example-heaps.sh owns the workloads and turns
-- the reported peak into a partition need.
--
-- Block accounting (Inside Macintosh: Memory, 24-bit zones): each block
-- starts with a tag byte (bits 7-6: 00 free, 01 nonrelocatable,
-- 10 relocatable) and a 3-byte physical size; a relocatable block's second
-- long is its master pointer's offset from the zone, and the master pointer's
-- high byte carries the locked (0x80) and purgeable (0x40) flags. In 32-bit
-- zones the block header is 12 bytes: tag at byte 0, relocatable flags at byte
-- 1 (also resource 0x20), size correction at byte 3, physical size at +4,
-- relative handle / nonrelocatable zone pointer at +8. Both zone headers are
-- 52 bytes. On PPC (#1109, pmac6100 with Mac OS 8.1) the program space is
-- physical and logical RAM sits at one constant offset: scan for the
-- low-memory page after boot, then translate every logical address by that
-- page's base. Live bytes are everything the Memory Manager cannot reclaim:
-- nonrelocatable blocks, locked relocatable blocks (the loaded code), and
-- unlocked relocatable blocks that are not purgeable. Lua 5.4 cautions follow
-- scripts/mame-find-base.lua.

local ZONE_HEADER_BYTES = 52 -- Zone record size; heapData starts here.

local log = assert(io.open(os.getenv("LOKA_SNAP_LOG") or "mame-measure-heap.log", "w"))
local function say(fmt, ...)
    log:write(string.format(fmt, ...) .. "\n")
    log:flush()
end

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local lowmem = 0
local zone32 = false
local function logical(address)
    return lowmem + address
end

-- KanjiTalk's input method takes letters and Space; Command-Space switches it
-- to Roman input. Workloads that type start with "K space".
local ALIASES = {
    down = { "Down Arrow", "Down", "Cursor Down" },
    ret = { "Return" },
    space = { "Space" },
    tab = { "Tab" },
    l = { "l  L" },
    o = { "o  O" },
    s = { "s  S" },
    cmd = { "Command / Open Apple" },
    zero = { "0  )" },
    five = { "5  %" },
}

local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or ""
local keys = dofile(here .. "mame-keys.lua")(say)
local tap = keys.tap
local function key(name)
    for _, fieldName in ipairs(ALIASES[name] or { name }) do
        local ok, found = pcall(keys.keyByName, fieldName)
        if ok then return found end
    end
    error("no key field for " .. name)
end

local function commandTap(name)
    keys.commandTap(key(name))
end

local finderZone = nil
local appZone = nil
local peak = { live = 0, at = "none" }
local tapSamples, tapDiscarded = 0, 0

-- Sum one zone's blocks. Returns nil when the block list does not tile the
-- zone exactly, which is what a walk sees in the middle of a Memory Manager
-- operation.
local function measureZone(zone)
    local bkLim = mem:read_u32(logical(zone))
    local address = zone + ZONE_HEADER_BYTES
    local free, nonrel, locked, unlocked, purgeable, blocks = 0, 0, 0, 0, 0, 0
    while address < bkLim do
        local header = mem:read_u32(logical(address))
        local tag = (header >> 30) & 3
        local size = zone32 and mem:read_u32(logical(address + 4)) or (header & 0xFFFFFF)
        if size < (zone32 and 12 or 8) or address + size > bkLim then
            return nil
        end
        if tag == 0 then
            free = free + size
        elseif tag == 1 then
            nonrel = nonrel + size
        elseif tag == 2 then
            local flags
            if zone32 then
                flags = mem:read_u8(logical(address + 1))
            else
                flags = mem:read_u8(logical(zone + mem:read_u32(logical(address + 4))))
            end
            if flags & 0x80 ~= 0 then
                locked = locked + size
            elseif flags & 0x40 ~= 0 then
                purgeable = purgeable + size
            else
                unlocked = unlocked + size
            end
        else
            return nil
        end
        blocks = blocks + 1
        address = address + size
    end
    if address ~= bkLim then
        return nil
    end
    return {
        bkLim = bkLim, blocks = blocks, free = free, nonrel = nonrel, locked = locked,
        unlocked = unlocked, purgeable = purgeable, live = nonrel + locked + unlocked,
    }
end

local function notePeak(live, label)
    if live > peak.live then
        peak.live = live
        peak.at = label
    end
end

-- The Process Manager swaps ApplZone on every context switch, so a sample can
-- land while the Finder is current. Walk the application's own zone directly,
-- and call the application gone only when its zone stays away from ApplZone.
local function applicationAlive()
    for _ = 1, 40 do
        if mem:read_u32(logical(0x2AA)) == appZone then
            return true
        end
        emu.wait(0.05)
    end
    return false
end

local function walk(label, verbose)
    local m = measureZone(appZone)
    if not m then
        error(string.format("%s: zone %08x does not tile into blocks", label, appZone))
    end
    notePeak(m.live, label)
    if verbose then
        say("%s zone=%08x size=%d blocks=%d free=%d nonrel=%d locked=%d unlocked=%d purgeable=%d live=%d",
            label, appZone, m.bkLim - appZone, m.blocks, m.free, m.nonrel, m.locked, m.unlocked,
            m.purgeable, m.live)
    end
end

-- Transient peaks: an allocation made and released inside one event never
-- shows in a settled walk, so walk the zone on every write to its zcbFree
-- (offset 12), which the Memory Manager updates on each allocation and
-- release. Mid-operation walks that do not tile are discarded.
local zoneTap = nil
local tapLabel = "launch"
local function tapZone(zone)
    -- A tap covers whole bus words. On the 64-bit PPC bus the word holding
    -- zcbFree also holds hFstFree, whose write mask (0xFFFFFFFF00000000) does
    -- not fit a Lua integer and takes MAME down, so PPC keeps the settled walks
    -- only and its peak can miss a transient inside one event (#1109).
    if mem.data_width > 32 then
        say("zone tap unavailable on a %d-bit bus: settled walks only", mem.data_width)
        return
    end
    zoneTap = mem:install_write_tap(logical(zone + 12), logical(zone + 15), "loka-zcbfree", function(offset, data, mask)
        local m = measureZone(zone)
        if m then
            tapSamples = tapSamples + 1
            notePeak(m.live, tapLabel)
        else
            tapDiscarded = tapDiscarded + 1
        end
        return data
    end)
end

local couple = 1
local function warp(h, v)
    mem:write_u16(logical(0x828), v) -- MTemp
    mem:write_u16(logical(0x82A), h)
    mem:write_u16(logical(0x82C), v) -- RawMouse
    mem:write_u16(logical(0x82E), h)
    mem:write_u8(logical(0x8CE), couple) -- CrsrNew := CrsrCouple
    emu.wait(0.5)
end

local button = manager.machine.ioport.ports[":macadb:MOUSE0"].fields["Mouse Button 0"]

local function doubleClick(h, v)
    warp(h, v)
    for _ = 1, 2 do
        button:set_value(1)
        emu.wait(0.1)
        button:clear_value()
        emu.wait(0.1)
    end
end

-- CurApName (0x910) is the current process's name as a Pascal string.
local function currentApplicationName()
    local length = math.min(mem:read_u8(logical(0x910)), 31)
    local name = {}
    for i = 1, length do
        name[i] = string.char(mem:read_u8(logical(0x910 + i)))
    end
    return table.concat(name)
end

local function sampledWait(seconds, label)
    tapLabel = label
    local elapsed = 0
    while elapsed < seconds do
        emu.wait(0.25)
        elapsed = elapsed + 0.25
        walk(label, false)
    end
end

local ok, err = pcall(function()
    emu.wait(tonumber(os.getenv("LOKA_LAUNCH_WAIT") or "90"))
    if cpu.shortname:match("^ppc") then
        lowmem = nil
        local ramsize = manager.machine.options.entries["ramsize"]:value()
        local megabytes = assert(tonumber(ramsize:match("^(%d+)[Mm]$")), "ramsize must be given in M: " .. ramsize)
        for attempt = 0, 24 do
            for base = 0, megabytes * 1024 * 1024 - 4096, 4096 do
                if mem:read_u32(base + 0x2AE) == 0x40800000
                    and mem:read_u32(base + 0x2A6) == 0x2800 then
                    lowmem = base
                    break
                end
            end
            if lowmem then
                say("lowmem found after %d extra seconds", attempt * 5)
                break
            end
            emu.wait(5)
        end
        if not lowmem then
            manager.machine.video:snapshot()
            -- pmac6100 sometimes hangs at boot with a black screen after
            -- mounting the boot volume. On 2026-10-10 every run did for a
            -- quarter of an hour while the WSL page cache held about 27 GB of
            -- old disk copies, and booted again once they were deleted.
            error("PPC low-memory page not found: the machine did not boot; run it again")
        end
    end
    keys.decideKeyMapReadable()
    say("lowmem base=%08x", lowmem)
    zone32 = mem:read_u8(logical(0xCB2)) ~= 0
    say("zone format=%s", zone32 and "32-bit" or "24-bit")
    -- #1102 probe: catch the first SysError and save its stack and the
    -- application zone, then clear the breakpoint and continue.
    local dbg = manager.machine.debugger
    if os.getenv("LOKA_CATCH_SYSERROR") ~= "1" then
        say("SysError catch disabled")
    elseif cpu.shortname:match("^ppc") then
        say("SysError catch unsupported on %s", cpu.shortname)
    elseif dbg then
        local entry = mem:read_u32(logical(0x0E00 + 0x1C9 * 4))
        say("SysError trap entry %08x", entry)
        dbg:command(string.format(
            'bpset %x,1,{logerror "SYSERR pc=%%08X a7=%%08X a6=%%08X d0=%%08X d1=%%08X a0=%%08X a1=%%08X applzone=%%08X\\n",pc,a7,a6,d0,d1,a0,a1,d@2aa; save stack.bin,a7,d@908-a7; save heap.bin,d@2aa,d@(d@2aa)-d@2aa; save lowmem.bin,0,2000; bpclear; g}',
            entry))
        say("SysError breakpoint armed")
    else
        say("SysError catch requested but no debugger")
    end
    local storedCouple = mem:read_u8(logical(0x8CF))
    if storedCouple ~= 0 then
        couple = storedCouple
    end
    -- Mac OS 8.1's Finder hands typed letters to the Japanese input method
    -- part of the time, so the PPC runner launches with double-clicks on the
    -- LokaDev icon and then the application's icon (#1109).
    local clicks = os.getenv("LOKA_LAUNCH_CLICKS")
    if clicks then
        local diskH, diskV, appH, appV = clicks:match("^(%d+) (%d+) (%d+) (%d+)$")
        assert(diskH, "LOKA_LAUNCH_CLICKS must be 'diskH diskV appH appV': " .. clicks)
        doubleClick(tonumber(diskH), tonumber(diskV))
        emu.wait(8)
        manager.machine.video:snapshot()
        finderZone = mem:read_u32(logical(0x2AA))
        doubleClick(tonumber(appH), tonumber(appV))
    else
        keys.openVolume()
        keys.selectApplication(tonumber(os.getenv("LOKA_TAB_COUNT")))
        finderZone = mem:read_u32(logical(0x2AA))
        commandTap("o")
    end
    -- Watch the switch into the application's zone closely so the tap sees
    -- its mount, then let the launch settle. Background processes also take
    -- ApplZone on Mac OS 8.1; when the runner names the application, only its
    -- own zone (CurApName at the switch) counts.
    local appName = os.getenv("LOKA_APP_NAME")
    local waited = 0
    while waited < 15 do
        emu.wait(0.01)
        waited = waited + 0.01
        local zone = mem:read_u32(logical(0x2AA))
        if not appZone and zone ~= finderZone and (not appName or currentApplicationName() == appName) then
            appZone = zone
            tapZone(zone)
            say("application zone %08x after %.2f s (Finder zone %08x)", zone, waited, finderZone)
        end
    end
    if not appZone or not applicationAlive() then
        error(string.format("the application is not running after launch (ApplZone %08x, Finder zone %08x)",
            mem:read_u32(logical(0x2AA)), finderZone))
    end
    -- ApplLimit belongs to the current process, which applicationAlive just
    -- confirmed is the application.
    say("launched max=%d", mem:read_u32(logical(0x130)) - appZone)
    walk("launched", true)
    manager.machine.video:snapshot()

    -- Steps, each ending in ';': "c H V" click, "k NAME" key, "K NAME"
    -- Command-key, "w SECONDS" sampled wait, "s" logged walk plus snapshot.
    local index = 0
    for op, first, second in (os.getenv("LOKA_STEPS") or ""):gmatch("(%a)%s*([^%s;]*)%s*([^;]*);") do
        index = index + 1
        local label = string.format("step%03d-%s", index, op)
        tapLabel = label
        if op == "c" then
            warp(tonumber(first), tonumber(second))
            button:set_value(1)
            emu.wait(0.2)
            button:clear_value()
            sampledWait(0.5, label)
        elseif op == "k" then
            tap(key(first))
            walk(label, false)
        elseif op == "K" then
            commandTap(first)
            sampledWait(1, label)
        elseif op == "w" then
            sampledWait(tonumber(first), label)
        elseif op == "s" then
            walk(label, true)
            manager.machine.video:snapshot()
        else
            error("unknown step " .. op)
        end
    end
    if applicationAlive() then
        walk("end", true)
        manager.machine.video:snapshot()
        say("tap samples=%d discarded=%d", tapSamples, tapDiscarded)
        say("PEAK live=%d at=%s", peak.live, peak.at)
    else
        say("end application zone gone (ApplZone %08x)", mem:read_u32(logical(0x2AA)))
    end
    say("LOKA-MEASURE: complete")
end)
if not ok then
    say("LOKA-MEASURE: lua error: %s", tostring(err))
end
manager.machine:exit()
