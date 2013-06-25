#!/usr/bin/env astra

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

parse_source.file = function(addr, source)
    source.filename = addr
end

function parse_options(options, source)
    local x = options:find("?")
    if x ~= 1 then
        return
    end
    options = options:sub(2)

    function parse_option(option)
        local x = option:find("=")
        if not x then return nil end
        local key = option:sub(1, x - 1)
        source[key] = option:sub(x + 1)
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
    local x,_,source_type,source_addr,source_options = url:find("^(%a+)://([%a%d%.:@_/%-]+)(.*)$" )
    if not source_type then
        return nil
    end
    if type(parse_source[source_type]) ~= 'function' then return nil end

    local source = { type = source_type }
    parse_source[source_type](source_addr, source)
    if source_options then parse_options(source_options, source) end

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

function on_analyze(data)
    if data.error then
        dump_table(log.error, data)
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
UDP:
    Template: udp://[localaddr@]ip[:port]
              localaddr - IP address of the local interface
              port      - default: 1234

    Examples: udp://239.255.2.1
              udp://192.168.1.1@239.255.1.1:1234

File:
    Template: file:///path/to/file.ts
]])
    astra.exit()
end

if #argv == 0 or argv[1] == "-h" or argv[1] == "--help" then usage() end

local input_conf = parse_url(argv[1])
if not input_conf then
    print("ERROR: wrong address format\n")
    usage()
end

local input_modules = {}
input_modules.udp = udp_input
input_modules.file = file_input

if not input_modules[input_conf.type] then
    print("ERROR: unknown source type\n")
    usage()
end

i = input_modules[input_conf.type](input_conf)
a = analyze({ upstream = i:stream(), name = "Test Channel", callback = on_analyze })
