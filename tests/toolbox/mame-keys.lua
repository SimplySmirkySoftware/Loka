-- Shared MAME ADB key handling. Each caller supplies its own logger.
return function(say)
    -- Diagnostic fixture, not a tuning parameter: zero can release and re-press
    -- a repeated Tab within one Lua slice. The guest can miss that release;
    -- the up-wait restores the second key by waiting for the guest to see it.
    local releaseHold = tonumber(os.getenv("LOKA_KEY_RELEASE_HOLD") or "0.20")
    assert(releaseHold and releaseHold >= 0 and releaseHold < math.huge,
        "LOKA_KEY_RELEASE_HOLD must be finite and non-negative")
    if releaseHold ~= 0.20 then
        say("LOKA_KEY_RELEASE_HOLD=%s emulated seconds (diagnostic)", releaseHold)
    end

    local function field(portTag, name)
        return assert(manager.machine.ioport.ports[portTag].fields[name])
    end

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

    -- KeyMap is read at its low-memory address, which is also its physical
    -- address only where the guest keeps low memory at physical 0 (pmac6100 maps
    -- it elsewhere). The launch starts with the Finder current, so CurApName
    -- (0x910) reading "Finder" there confirms the address; otherwise tap() keeps
    -- its fixed timing and the launcher says so once.
    local CUR_AP_NAME = 0x910
    local keyMapReadable = nil

    local function decideKeyMapReadable()
        local length = keyMemory:read_u8(CUR_AP_NAME)
        local name = {}
        for i = 1, math.min(length, 31) do
            name[i] = string.char(keyMemory:read_u8(CUR_AP_NAME + i))
        end
        keyMapReadable = table.concat(name) == "Finder"
        if not keyMapReadable then
            say("[skip] key waits: CurApName (0x%x) does not read Finder at launch, so KeyMap is not at 0x%x on this machine",
                CUR_AP_NAME, KEY_MAP)
        end
    end

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
        assert(keyMapReadable ~= nil, "decideKeyMapReadable() runs before the first key")
        if not keyMapReadable then
            return
        end
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
        if releaseHold > 0 then emu.wait(releaseHold) end
        awaitKeySeen(key, false)
    end

    local function commandTap(key)
        commandKey:set_value(1)
        tap(key)
        commandKey:clear_value()
    end

    local function openVolume()
        tap(field(":macadb:KEY2", "l  L"))
        commandTap(field(":macadb:KEY1", "o  O"))
        emu.wait(5)
    end

    local function selectApplication(tabCount)
        assert(tabCount and tabCount > 0 and tabCount == math.floor(tabCount),
            "LOKA_TAB_COUNT must be a positive integer")
        local tabKey = keyByName("Tab")
        for _ = 1, tabCount do tap(tabKey) end
        emu.wait(1)
    end

    return {
        field = field, keyByName = keyByName, tap = tap, commandTap = commandTap,
        decideKeyMapReadable = decideKeyMapReadable,
        openVolume = openVolume, selectApplication = selectApplication
    }
end
