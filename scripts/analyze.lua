#!/usr/bin/astra

-- Astra MPEG-TS Analyzer
-- http://cesbo.com/astra
--
-- Copyright (C) 2012-2014, Andrey Dyldin <and@cesbo.com>

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

-- ooooo  oooo oooooooooo  ooooo
--  888    88   888    888  888
--  888    88   888oooo88   888
--  888    88   888  88o    888      o
--   888oo88   o888o  88o8 o888ooooo88

local parse_source = {}

parse_source.udp = function(addr, source)
    local x = addr:find("@")
    if x then
        if x > 1 then
            source.localaddr = addr:sub(1, x - 1)
        end
        addr = addr:sub(x + 1)
    end
    local x = addr:find(":")
    if x then
        source.addr = addr:sub(1, x - 1)
        source.port = tonumber(addr:sub(x + 1))
    else
        source.addr = addr
        source.port = 1234
    end
end

parse_source.rtp = function(addr, source)
    parse_source.udp(addr, source)
    source.rtp = true
end

parse_source.file = function(addr, source)
    source.filename = addr
end

parse_source.http = function(addr, source)
    local x = addr:find("@")
    if x then
        source.auth = base64.encode(addr:sub(1, x - 1))
        addr = addr:sub(x + 1)
    end
    local x = addr:find("/")
    if x then
        source.path = addr:sub(x)
        addr = addr:sub(1, x - 1)
    else
        source.path = "/"
    end
    local x = addr:find(":")
    if x then
        source.host = addr:sub(1, x - 1)
        source.port = tonumber(addr:sub(x + 1))
    else
        source.host = addr
        source.port = 80
    end
end

function parse_options(options, source)
    function parse_option(option)
        local x = option:find("=")
        local key, value
        if x then
            key = option:sub(1, x - 1)
            value = option:sub(x + 1)
        else
            key = option
            value = true
        end
        source[key] = value
    end

    local pos = 1
    while true do
        local x = options:find("&", pos)
        if x then
            parse_option(options:sub(pos, x - 1))
            pos = x + 1
        else
            parse_option(options:sub(pos))
            return nil
        end
    end
end

function parse_url(url)
    local x = url:find("://")
    if not x then return nil end

    local source = {}

    source.type = url:sub(1, x - 1)
    if type(parse_source[source.type]) ~= 'function' then return nil end

    url = url:sub(x + 3)

    local options = nil
    local x = url:find("#")
    if x then
        options = url:sub(x + 1)
        url = url:sub(1, x - 1)
    end

    parse_source[source.type](url, source)
    if options then parse_options(options, source) end

    return source
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

function on_analyze(data)
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

function usage()
    print([[Usage: astra analyze.lua ADDRESS
RTP:
    Template: rtp://[localaddr@]ip[:port]
UDP:
    Template: udp://[localaddr@]ip[:port]
              localaddr - IP address of the local interface
              port      - default: 1234

    Examples: rtp://239.255.2.1
              udp://192.168.1.1@239.255.1.1:1234

File:
    Template: file:///path/to/file.ts

HTTP:
    Template: http://[login:password@]host[:port][/uri]
              login:password
                        - Basic authentication
              host      - Server hostname
              port      - default: 80
              /uri      - resource identifier. default: '/'

]])
    astra.exit()
end

if #argv < 2 or argv[2] == "-h" or argv[2] == "--help" then usage() end

local input_conf = parse_url(argv[2])
if not input_conf then
    print("ERROR: wrong address format\n")
    usage()
end

local instance = {}
local input_modules = {}

function start_analyze()
    instance.a = analyze({
        upstream = instance.i:stream(),
        name = "Test Channel",
        callback = on_analyze
    })
end

input_modules.udp = function(input_conf)
    instance.i = udp_input(input_conf)
    start_analyze()
end

input_modules.rtp = function(input_conf)
    input_modules.udp(input_conf)
end

input_modules.file = function(input_conf)
    instance.i = file_input(input_conf)
    start_analyze()
end

function http_parse_location(headers)
    for _, header in pairs(headers) do
        local _,_,location = header:find("^Location: http://(.*)")
        if location then
            local x = {}
            parse_source.http(location, x)
            return x.host, x.port, x.path
        end
    end
    return nil
end

input_modules.http = function(input_conf)
    local http_conf = {}

    http_conf.host = input_conf.host
    http_conf.port = input_conf.port
    http_conf.path = input_conf.path

    http_conf.headers =
    {
        "User-Agent: Astra " .. astra.version,
        "Host: " .. input_conf.host .. ":" .. input_conf.port,
        "Connection: close",
    }
    if input_conf.auth then
        table.insert(http_conf.headers, "Authorization: Basic " .. input_conf.auth)
    end

    http_conf.stream = true
    if input_conf.no_sync == true then
        http_conf.no_sync = true
    end

    http_conf.callback = function(self, data)
            if not data then
                astra.exit()
            elseif data.code == 200 then
                instance.i = self
                start_analyze()
            elseif data.code == 301 or data.code == 302 then
                local host, port, path = http_parse_location(data.headers)
                if host then
                    http_conf.host = host
                    http_conf.port = port
                    http_conf.path = path
                    http_conf.headers[2] = "Host: " .. host .. ":" .. port
                    http_request(http_conf)
                end
            else
                log.error("ERROR: " .. data.code .. ":" .. data.message)
                astra.exit()
            end
        end

    http_request(http_conf)
end

if not input_modules[input_conf.type] then
    print("ERROR: unknown source type\n")
    usage()
else
    input_modules[input_conf.type](input_conf)
end
