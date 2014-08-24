-- MPEG-TS Analyzer
-- https://cesbo.com/solutions/analyze/
--
-- Copyright (C) 2012-2014, Andrey Dyldin <and@cesbo.com>
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

arg_n = nil

--      o      oooo   oooo     o      ooooo    ooooo  oooo ooooooooooo ooooooooooo
--     888      8888o  88     888      888       888  88   88    888    888    88
--    8  88     88 888o88    8  88     888         888         888      888ooo8
--   8oooo88    88   8888   8oooo88    888      o  888       888    oo  888    oo
-- o88o  o888o o88o    88 o88o  o888o o888ooooo88 o888o    o888oooo888 o888ooo8888

dump_psi_info = {}

dump_psi_info["pat"] = function(info)
    log.info(("PAT: tsid: %d"):format(info.tsid))
    for _, program_info in pairs(info.programs) do
        if program_info.pnr == 0 then
            log.info(("PAT: pid: %d NIT"):format(program_info.pid))
        else
            log.info(("PAT: pid: %d PMT pnr: %d"):format(program_info.pid, program_info.pnr))
        end
    end
    log.info(("PAT: crc32: 0x%X"):format(info.crc32))
end

function dump_descriptor(prefix, descriptor_info)
    if descriptor_info.type_name == "cas" then
        local data = ""
        if descriptor_info.data then data = " data: " .. descriptor_info.data end
        log.info((prefix .. "CAS: caid: 0x%04X pid: %d%s"):format(descriptor_info.caid,
                                                               descriptor_info.pid,
                                                               data))
    elseif descriptor_info.type_name == "lang" then
        log.info(prefix .. "Language: " .. descriptor_info.lang)
    elseif descriptor_info.type_name == "stream_id" then
        log.info(prefix .. "Stream ID: " .. descriptor_info.stream_id)
    elseif descriptor_info.type_name == "service" then
        log.info(prefix .. "Service: " .. descriptor_info.service_name)
        log.info(prefix .. "Provider: " .. descriptor_info.service_provider)
    elseif descriptor_info.type_name == "unknown" then
        log.info(prefix .. "descriptor: " .. descriptor_info.data)
    else
        log.info((prefix .. "unknown descriptor. type: %s 0x%02X")
                 :format(tostring(descriptor_info.type_name), descriptor_info.type_id))
    end
end

dump_psi_info["cat"] = function(info)
    for _, descriptor_info in pairs(info.descriptors) do
        dump_descriptor("CAT: ", descriptor_info)
    end
end

dump_psi_info["pmt"] = function(info)
    log.info(("PMT: pnr: %d"):format(info.pnr))
    log.info(("PMT: pid: %d PCR"):format(info.pcr))

    for _, descriptor_info in pairs(info.descriptors) do
        dump_descriptor("PMT: ", descriptor_info)
    end

    for _, stream_info in pairs(info.streams) do
        log.info(("%s: pid: %d type: 0x%02X"):format(stream_info.type_name,
                                                      stream_info.pid,
                                                      stream_info.type_id))
        for _, descriptor_info in pairs(stream_info.descriptors) do
            dump_descriptor(stream_info.type_name .. ": ", descriptor_info)
        end
    end
    log.info(("PMT: crc32: 0x%X"):format(info.crc32))
end

dump_psi_info["sdt"] = function(info)
    log.info(("SDT: tsid: %d"):format(info.tsid))

    for _, service in pairs(info.services) do
        log.info(("SDT: sid: %d"):format(service.sid))
        for _, descriptor_info in pairs(service.descriptors) do
            dump_descriptor("SDT:     ", descriptor_info)
        end
    end
    log.info(("SDT: crc32: 0x%X"):format(info.crc32))
end

function on_analyze(instance, data)
    if data.error then
        log.error(data.error)
    elseif data.psi then
        if dump_psi_info[data.psi] then
            dump_psi_info[data.psi](data)
        else
            log.error("Unknown PSI: " .. data.psi)
        end
    elseif data.analyze then
        local bitrate = 0
        local cc_error = ""
        local pes_error = ""
        local sc_error = ""
        for _,item in pairs(data.analyze) do
            bitrate = bitrate + item.bitrate

            if item.cc_error > 0 then
                cc_error = cc_error .. "PID:" .. tostring(item.pid) .. "=" .. tostring(item.cc_error) .. " "
            end

            if item.pes_error > 0 then
                pes_error = pes_error .. tostring(item.pid) .. "=" .. tostring(item.pes_error) .. " "
            end

            if item.sc_error > 0 then
                sc_error = sc_error .. tostring(item.pid) .. "=" .. tostring(item.sc_error) .. " "
            end
        end
        log.info("Bitrate: " .. tostring(bitrate) .. " Kbit/s")
        if #cc_error > 0 then
            log.error("CC: " .. cc_error)
        end
        if #sc_error > 0 then
            log.error("Scrambled: " .. sc_error)
        else
            if #pes_error > 0 then
                log.error("PES: " .. pes_error)
            end
        end
        if arg_n then
            arg_n = arg_n - 1
            if arg_n == 0 then astra.exit() end
        end
    end
end

-- oooo     oooo      o      ooooo oooo   oooo
--  8888o   888      888      888   8888o  88
--  88 888o8 88     8  88     888   88 888o88
--  88  888  88    8oooo88    888   88   8888
-- o88o  8  o88o o88o  o888o o888o o88o    88

function start_analyze(instance, addr)
    local conf = parse_url(addr)
    conf.name = "Analyze"
    instance.input = init_input(conf)
    instance.analyze = analyze({
        upstream = instance.input.tail:stream(),
        name = conf.name,
        callback = function(data)
            on_analyze(instance, data)
        end,
    })
end

function stop_analyze(instance)
    kill_input(instance.input)
    instance.analyze = nil
    collectgarbage()
end

options_usage = [[
    -n S                stop analyzer and exit after S seconds
    ADDRESS             source address. Available formats:

UDP:
    Template: udp://[localaddr@]ip[:port]
              localaddr - IP address of the local interface
              port      - default: 1234

    Examples: udp://239.255.2.1
              udp://192.168.1.1@239.255.1.1:1234

RTP:
    Template: rtp://[localaddr@]ip[:port]

File:
    Template: file:///path/to/file.ts

HTTP:
    Template: http://[login:password@]host[:port][/uri]
              login:password
                        - Basic authentication
              host      - Server hostname
              port      - default: 80
              /uri      - resource identifier. default: '/'
]]

input_url = nil

options = {
    ["-n"] = function(idx)
        arg_n = tonumber(argv[idx + 1])
        return 1
    end,
    ["*"] = function(idx)
        input_url = argv[idx]
        return 0
    end,
}

function main()
    log.info("Starting Astra " .. astra.version)

    if input_url then
        _G.instance = {}
        start_analyze(_G.instance, input_url)
        return
    end

    astra_usage()
end
