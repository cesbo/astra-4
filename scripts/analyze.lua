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

input = { parse = {}, start = {}, stop = {}, }

-- ooooo  oooo oooooooooo  ooooo
--  888    88   888    888  888
--  888    88   888oooo88   888
--  888    88   888  88o    888      o
--   888oo88   o888o  88o8 o888ooooo88

parse_option = {}

input.parse.udp = function(addr, options)
    local x = addr:find("@")
    if x then
        if x > 1 then
            options.localaddr = addr:sub(1, x - 1)
        end
        addr = addr:sub(x + 1)
    end
    local x = addr:find(":")
    if x then
        options.addr = addr:sub(1, x - 1)
        options.port = tonumber(addr:sub(x + 1))
        if not options.port then options.port = 1234 end
    else
        options.addr = addr
        options.port = 1234
    end
end

input.parse.rtp = function(addr, options)
    input.parse.udp(addr, options)
    options.rtp = true
end

input.parse.file = function(addr, options)
    options.filename = addr
end

input.parse.http = function(addr, options)
    local x = addr:find("@")
    if x then
        options.auth = base64.encode(addr:sub(1, x - 1))
        addr = addr:sub(x + 1)
    end
    local x = addr:find("/")
    if x then
        options.path = addr:sub(x)
        addr = addr:sub(1, x - 1)
    else
        options.path = "/"
    end
    local x = addr:find(":")
    if x then
        options.host = addr:sub(1, x - 1)
        options.port = tonumber(addr:sub(x + 1))
        if not options.port then options.port = 80 end
    else
        options.host = addr
        options.port = 80
    end
end

function parse_options(address, options)
    function parse_key_val(option)
        local x, key, val
        x = option:find("=")
        if x then
            key = option:sub(1, x - 1)
            val = option:sub(x + 1)
        else
            key = option
            val = true
        end

        x = key:find("%.")
        if x then
            local _key = key:sub(x + 1)
            key = key:sub(1, x - 1)

            local _val = {}
            _val[_key] = val
            val = _val
        end

        if parse_option[key] then
            parse_option[key](val, options)
        else
            if type(options[key]) == 'table' and type(val) == 'table' then
                for _key,_val in pairs(val) do
                    options[key][_key] = _val
                end
            else
                options[key] = val
            end
        end
    end

    local pos = 1
    while true do
        local x = address:find("&", pos)
        if x then
            parse_key_val(address:sub(pos, x - 1))
            pos = x + 1
        else
            parse_key_val(address:sub(pos))
            return nil
        end
    end
end

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
        for _,item in pairs(data.analyze) do
            bitrate = bitrate + item.bitrate

            if item.cc_error > 0 then
                cc_error = cc_error .. "PID:" .. tostring(item.pid) .. "=" .. tostring(item.cc_error) .. " "
            end

            if item.pes_error > 0 then
                pes_error = pes_error .. tostring(item.pid) .. "=" .. tostring(item.pes_error) .. " "
            end
        end
        log.info("Bitrate: " .. tostring(bitrate) .. " Kbit/s")
        if #cc_error > 0 then log.error("CC: " .. cc_error) end
        if #pes_error > 0 then log.error("PES: " .. pes_error) end
    end
end

-- oooo     oooo      o      ooooo oooo   oooo
--  8888o   888      888      888   8888o  88
--  88 888o8 88     8  88     888   88 888o88
--  88  888  88    8oooo88    888   88   8888
-- o88o  8  o88o o88o  o888o o888o o88o    88

input.start.udp = function(instance, options)
    instance.input = udp_input(options)
end

input.stop.udp = function(instance)
    instance.input = nil
end

input.start.rtp = function(instance, options)
    instance.input = udp_input(options)
end

input.stop.rtp = function(instance)
    instance.input = nil
end

input.start.file = function(instance, options)
    if utils.stat(options.filename).type ~= "file" then
        local err = "file not found"
        log.error(err)
        astra.exit()
        return
    end

    options.callback = function()
        local err = "end of file"
        log.info("[analyze] " .. err)
        astra.exit()
    end
    instance.input = file_input(options)
end

input.stop.file = function(instance)
    instance.input = nil
end

input.start.http = function(instance, options)
    local http_conf = {}

    http_conf.host = options.host
    http_conf.port = options.port
    http_conf.path = options.path

    http_conf.headers =
    {
        "User-Agent: Astra " .. astra.version,
        "Host: " .. options.host .. ":" .. options.port,
        "Connection: close",
    }
    if options.auth then
        table.insert(http_conf.headers, "Authorization: Basic " .. options.auth)
    end

    http_conf.stream = true
    if options.sync then http_conf.sync = options.sync end

    http_conf.callback = function(self, response)
        if not response then
            instance.request:close()
            instance.request = nil

            stop_analyze(instance)

        elseif response.code == 200 then
            instance.input:set_upstream(self:stream())

        elseif response.code == 301 or response.code == 302 then
            instance.request:close()
            instance.request = nil

            local location = response.headers['location']
            if not location then
                local err = "Redirect failed"

                log.error(err)
                astra.exit()
                return
            end

            local b,e = location:find("://")
            if b then
                local o = {}
                input.parse.http(location:sub(e + 1), o)
                http_conf.host = o.host
                http_conf.port = o.port
                http_conf.path = o.path
                http_conf.headers[2] = "Host: " .. http_conf.host .. ":" .. http_conf.path
                log.info("Redirect to http://" ..
                         http_conf.host .. ":" .. http_conf.port .. http_conf.path)
            else
                http_conf.path = location
                log.info("Redirect to " .. http_conf.path)
            end

            instance.request = http_request(http_conf)

        else
            instance.request:close()
            instance.request = nil

            local err = "HTTP Error " .. response.code .. ":" .. response.message
            log.error(err)
            astra.exit()
        end
    end

    instance.input = transmit()
    instance.request = http_request(http_conf)
end

input.stop.http = function(instance)
    if instance.request then
        instance.request:close()
        instance.request = nil
    end

    instance.input = nil
end

function start_analyze(instance, addr)
    local b,e = addr:find("://")
    if not b then
        local err = "wrong address format"
        log.error("[analyze] " .. err)
        astra.exit()
        return
    end
    instance.proto = addr:sub(1, b - 1)
    if not input.parse[instance.proto] then
        local err = "unwknown source type"
        log.error("[analyze] " .. err)
        astra.exit()
        return
    end
    instance.addr = addr:sub(e + 1)

    local options = {}
    local addr = instance.addr
    local b = addr:find("#")
    if b then
        parse_options(addr:sub(b + 1), options)
        addr = addr:sub(1, b - 1)
    end
    input.parse[instance.proto](addr, options)

    input.start[instance.proto](instance, options)
    instance.tail = instance.input
    if options.pnr then
        instance.channel = channel({
            upstream = instance.tail:stream(),
            name = "Test",
            pnr = options.pnr,
            cas = true,
            sdt = true,
            eit = true,
        })
        instance.tail = instance.channel
    end

    instance.analyze = analyze({
        upstream = instance.tail:stream(),
        name = "Test",
        rate_stat = true,
        callback = function(data)
            on_analyze(instance, data)
        end,
    })
end

function stop_analyze(instance)
    if not instance.proto then return nil end

    input.stop[instance.proto](instance)
    instance.proto = nil
    instance.addr = nil
    instance.analyze = nil

    collectgarbage()
end

options_usage = [[
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
