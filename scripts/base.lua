-- Astra Script: Utils
-- http://cesbo.com/astra
--
-- Copyright (C) 2014, Andrey Dyldin <and@cesbo.com>
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program.  If not, see <http://www.gnu.org/licenses/>.

function dump_table(t, p, i)
    if not p then p = print end
    if not i then
        p("{")
        dump_table(t, p, "    ")
        p("}")
        return
    end

    for key,val in pairs(t) do
        if type(val) == 'table' then
            p(i .. tostring(key) .. " = {")
            dump_table(val, p, i .. "    ")
            p(i .. "}")
        elseif type(val) == 'string' then
            p(i .. tostring(key) .. " = \"" .. val .. "\"")
        else
            p(i .. tostring(key) .. " = " .. tostring(val))
        end
    end
end

function split(s, d)
    local p = 1
    local t = {}
    while true do
        b = s:find(d, p)
        if not b then table.insert(t, s:sub(p)) return t end
        table.insert(t, s:sub(p, b - 1))
        p = b + 1
    end
end

-- ooooo         ooooooo      o      ooooooooo
--  888        o888   888o   888      888    88o
--  888        888     888  8  88     888    888
--  888      o 888o   o888 8oooo88    888    888
-- o888ooooo88   88ooo88 o88o  o888o o888ooo88

function astra_usage()
    print([[
Usage: astra [APP] [OPTIONS]

Available Applications:
    --stream            Digital TV Streaming application. Used by default
    --analyze           MPEG-TS Analyzer
    --xproxy            xProxy
    SCRIPT              execute lua-script

Astra Options:
    -h                  command line arguments
    -v                  version number
    --pid FILE          create PID-file
    --syslog NAME       send log messages to syslog
    --log FILE          write log to file
    --no-stdout         do not print log messages into console
    --debug             print debug messages
]])

    if _G.options_usage then
        print("Application Options:")
        print(_G.options_usage)
    end
    astra.exit()
end

astra_options = {
    ["-h"] = function(idx)
        log.info("Starting Astra " .. astra.version)
        astra_usage()
        return 0
    end,
    ["-v"] = function(idx)
        log.info("Starting Astra " .. astra.version)
        astra.exit()
        return 0
    end,
    ["--pid"] = function(idx)
        pidfile(argv[idx + 1])
        return 1
    end,
    ["--syslog"] = function(idx)
        log.set({ syslog = argv[idx + 1] })
        return 1
    end,
    ["--log"] = function(idx)
        log.set({ filename = argv[idx + 1] })
        return 1
    end,
    ["--no-stdout"] = function(idx)
        log.set({ stdout = false })
        return 0
    end,
    ["--debug"] = function(idx)
        log.set({ debug = true })
        return 0
    end,
}

function astra_parse_options(idx)
    function set_option(idx)
        local a = argv[idx]
        local c = nil

        if _G.options then c = _G.options[a] end
        if not c then c = astra_options[a] end
        if not c and _G.options then c = _G.options['*'] end

        if not c then return -1 end
        local ac = c(idx)
        if ac == -1 then return -1 end
        idx = idx + ac + 1
        return idx
    end

    while idx <= #argv do
        local next_idx = set_option(idx)
        if next_idx == -1 then
            print("unknown option: " .. argv[idx])
            astra.exit()
        end
        idx = next_idx
    end
end
