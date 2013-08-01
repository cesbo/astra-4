
log.info("Starting Astra " .. astra.version)

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

--      o      oooo   oooo     o      ooooo    ooooo  oooo ooooooooooo ooooooooooo
--     888      8888o  88     888      888       888  88   88    888    888    88
--    8  88     88 888o88    8  88     888         888         888      888ooo8
--   8oooo88    88   8888   8oooo88    888      o  888       888    oo  888    oo
-- o88o  o888o o88o    88 o88o  o888o o888ooooo88 o888o    o888oooo888 o888ooo8888

dump_psi_info = {}

dump_psi_info["pat"] = function(name, info)
    log.info(name .. ("PAT: tsid: %d"):format(info.tsid))
    for _, program_info in pairs(info.programs) do
        if program_info.pnr == 0 then
            log.info(name .. ("PAT: pid: %d NIT"):format(program_info.pid))
        else
            log.info(name .. ("PAT: pid: %d PMT pnr: %d"):format(program_info.pid, program_info.pnr))
        end
    end
    log.info(name .. ("PAT: crc32: 0x%X"):format(info.crc32))
end

function dump_descriptor(prefix, descriptor_info)
    if descriptor_info.type_name == "cas" then
        local data = ""
        if descriptor_info.data then data = " data: " .. descriptor_info.data end
        log.info(prefix .. ("CAS: caid: 0x%04X pid: %d%s")
                           :format(descriptor_info.caid, descriptor_info.pid, data))
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
        log.info(prefix .. ("unknown descriptor. type: %s 0x%02X")
                           :format(tostring(descriptor_info.type_name), descriptor_info.type_id))
    end
end

dump_psi_info["cat"] = function(name, info)
    for _, descriptor_info in pairs(info.descriptors) do
        dump_descriptor(name .. "CAT: ", descriptor_info)
    end
end

dump_psi_info["pmt"] = function(name, info)
    log.info(name .. ("PMT: pnr: %d"):format(info.pnr))
    log.info(name .. ("PMT: pid: %d PCR"):format(info.pcr))

    for _, descriptor_info in pairs(info.descriptors) do
        dump_descriptor(name .. "PMT: ", descriptor_info)
    end

    for _, stream_info in pairs(info.streams) do
        log.info(name .. ("%s: pid: %d type: 0x%02X")
                         :format(stream_info.type_name, stream_info.pid, stream_info.type_id))
        for _, descriptor_info in pairs(stream_info.descriptors) do
            dump_descriptor(name .. stream_info.type_name .. ": ", descriptor_info)
        end
    end
    log.info(name .. ("PMT: crc32: 0x%X"):format(info.crc32))
end

dump_psi_info["sdt"] = function(name, info)
    log.info(name .. ("SDT: tsid: %d"):format(info.tsid))

    for _, service in pairs(info.services) do
        log.info(name .. ("SDT: sid: %d"):format(service.sid))
        for _, descriptor_info in pairs(service.descriptors) do
            dump_descriptor(name .. "SDT:    ", descriptor_info)
        end
    end
    log.info(name .. ("SDT: crc32: 0x%X"):format(info.crc32))
end

function on_analyze(channel_data, input_id, data)
    local name = "[" .. channel_data.config.name .. " #" .. input_id .. "] "

    if data.error then
        log.error(name .. "Error: " .. data.error)

    elseif data.psi then
        if dump_psi_info[data.psi] then
            dump_psi_info[data.psi](name, data)
        else
            log.error(name .. "Unknown PSI: " .. data.psi)
        end
    end
end

-- ooooo  oooo oooooooooo  ooooo
--  888    88   888    888  888
--  888    88   888oooo88   888
--  888    88   888  88o    888      o
--   888oo88   o888o  88o8 o888ooooo88

local parse_option = {}

parse_option.cam = function(val, result)
    if val == true then
        result.dvbcam = true
    elseif _G[val] then
        result.cam = _G[val]:cam()
    else
        log.error("[stream.lua] CAM \"" .. val .. "\" not found")
        astra.abort()
    end
end

--

local ifaddrs
if utils.ifaddrs then ifaddrs = utils.ifaddrs() end

local parse_addr = {}

parse_addr.dvb = function(addr, result)
    if _G[addr] then
        result._instance = _G[addr]
    else
        log.error("[stream.lua] DVB \"" .. addr .. "\" not found")
        astra.abort()
    end
end

parse_addr.udp = function(addr, result)
    local x = addr:find("@")
    if x then
        if x > 1 then
            local localaddr = addr:sub(1, x - 1)
            if ifaddrs and ifaddrs[localaddr] and ifaddrs[localaddr].ipv4 then
                result.localaddr = ifaddrs[localaddr].ipv4[1]
            else
                result.localaddr = localaddr
            end
        end
        addr = addr:sub(x + 1)
    end
    local x = addr:find(":")
    if x then
        result.addr = addr:sub(1, x - 1)
        result.port = tonumber(addr:sub(x + 1))
    else
        result.addr = addr
        result.port = 1234
    end
end

parse_addr.file = function(addr, result)
    result.filename = addr
end

parse_addr.http = function(addr, result)
    local x = addr:find("@")
    if x then
        local login_pass = addr:sub(1, x - 1)
        result.auth = login_pass:base64_encode()
        addr = addr:sub(x + 1)
    end
    local x = addr:find("/")
    if x then
        result.uri = addr:sub(x)
        addr = addr:sub(1, x - 1)
    else
        result.uri = "/"
    end
    local x = addr:find(":")
    if x then
        result.host = addr:sub(1, x - 1)
        result.port = tonumber(addr:sub(x + 1))
    else
        result.host = addr
        result.port = 80
    end
end

function parse_options(options, result)
    function parse_key_val(option)
        local x = option:find("=")
        local key, val
        if x then
            key = option:sub(1, x - 1)
            val = option:sub(x + 1)
        else
            key = option
            val = true
        end

        if parse_option[key] then
            parse_option[key](val, result)
        else
            result[key] = val
        end
    end

    local pos = 1
    while true do
        local x = options:find("&", pos)
        if x then
            parse_key_val(options:sub(pos, x - 1))
            pos = x + 1
        else
            parse_key_val(options:sub(pos))
            return nil
        end
    end
end

function parse_url(url)
    local module_name, url_addr, url_options
    local b, e = url:find("://")
    if not b then return nil end
    module_name = url:sub(1, b - 1)
    local b = url:find("#", e + 1)
    if b then
        url_addr = url:sub(e + 1, b - 1)
        url_options = url:sub(b + 1)
    else
        url_addr = url:sub(e + 1)
    end

    if module_name ~= "http" and module_name ~= "rtsp" and url_addr:find("?") then
        log.error("[stream.lua] please, replace '?' with '#' for options")
        astra.abort()
    end

    if type(parse_addr[module_name]) ~= 'function' then return nil end

    local result = { module_name = module_name }
    parse_addr[module_name](url_addr, result)
    if url_options then parse_options(url_options, result) end

    return result
end

-- oooooooooo  ooooooooooo  oooooooo8 ooooooooooo oooooooooo ooooo  oooo ooooooooooo
--  888    888  888    88  888         888    88   888    888 888    88   888    88
--  888oooo88   888ooo8     888oooooo  888ooo8     888oooo88   888  88    888ooo8
--  888  88o    888    oo          888 888    oo   888  88o     88888     888    oo
-- o888o  88o8 o888ooo8888 o88oooo888 o888ooo8888 o888o  88o8    888     o888ooo8888

function start_reserve(channel_data)
    function set_input()
        for input_id = 1, #channel_data.input do
            local input_data = channel_data.input[input_id]
            if input_data.on_air then
                log.info("[" .. channel_data.config.name .. "] Activate input #" .. input_id)
                channel_data.transmit:set_upstream(input_data.tail:stream())
                return input_id
            end
        end
        return 0
    end

    local active_input_id = set_input()

    if active_input_id == 0 then
        for input_id = 2, #channel_data.input do
            local input_data = channel_data.input[input_id]
            if not input_data.source then
                init_input(channel_data, input_id)
                return nil
            end
        end

        log.error("[" .. channel_data.config.name .. "] No active input")
        return
    end

    for input_id = active_input_id + 1, #channel_data.input do
        local input_data = channel_data.input[input_id]
        if input_data.source then
            kill_input(channel_data, input_id)

            log.debug("[" .. channel_data.config.name .. "] Destroy input #" .. input_id)
            channel_data.input[input_id] = { on_air = false, }
        end
    end
    collectgarbage()
end

function log_analyze_error(channel_data, input_id, analyze_data)
    local bitrate = 0
    local sc_errors = 0
    local pes_errors = 0

    for _,item in pairs(analyze_data.analyze) do
        bitrate = bitrate + item.bitrate
        sc_errors = sc_errors + item.sc_error
        pes_errors = pes_errors + item.pes_error
    end

    local analyze_message = "[" .. channel_data.config.name .. " #" .. input_id .. "] " .. "Bitrate:" .. bitrate .. "Kbit/s"

    if sc_errors > 0 then
        analyze_message = analyze_message .. " Scrambled"
    end

    if pes_errors > 0 then
        analyze_message = analyze_message .. " PES-Error"
    end

    log.error(analyze_message)
end

-- ooooo         ooooooooo  ooooo  oooo oooooooooo
--  888           888    88o 888    88   888    888
--  888 ooooooooo 888    888  888  88    888oooo88
--  888           888    888   88888     888    888
-- o888o         o888ooo88      888     o888ooo888

local dvb_list

local input_list = {}
local kill_input_list = {}

function dvb_tune(dvb_conf)
    if dvb_conf.mac then
        if not dvb_list then
            if dvbls then
                dvb_list = dvbls()
            else
                log.error("module dvbls is required")
                astra.abort()
            end
        end
        dvb_conf.mac = dvb_conf.mac:upper()
        for _, adapter_info in pairs(dvb_list) do
            if dvb_conf.mac and adapter_info.mac == dvb_conf.mac then
                dvb_conf.adapter = adapter_info.adapter
                if adapter_info.device > 0 then
                    dvb_conf.device = adapter_info.device
                else
                    dvb_conf.device = nil
                end
                dvb_conf.mac = nil
                break
            end
        end
        if dvb_conf.mac then
            log.error("[stream.lua] failed to get DVB by MAC")
            astra.abort()
        end
    end

    if dvb_conf.tp then
        local _, _, freq_s, pol_s, srate_s = dvb_conf.tp:find("(%d+):(%a):(%d+)")
        if not freq_s then
            log.error("[stream.lua] DVB wrong \"tp\" format")
            astra.abort()
        end
        dvb_conf.frequency = tonumber(freq_s)
        dvb_conf.polarization = pol_s
        dvb_conf.symbolrate = tonumber(srate_s)
    end

    if dvb_conf.lnb then
        local _, eo, lof1_s, lof2_s, slof_s = dvb_conf.lnb:find("(%d+):(%d+):(%d+)")
        if not lof1_s then
            log.error("[stream.lua] DVB wrong \"lnb\" format")
            astra.abort()
        end
        dvb_conf.lof1 = tonumber(lof1_s)
        dvb_conf.lof2 = tonumber(lof2_s)
        dvb_conf.slof = tonumber(slof_s)
    end

    if dvb_conf.type:lower() == "asi" then
        return asi_input(dvb_conf)
    else
        return dvb_input(dvb_conf)
    end
end

input_list.dvb = function(input_conf)
    if input_conf.dvbcam and input_conf.pnr then
        input_conf._instance:ca_set_pnr(input_conf.pnr, true)
    end

    return { tail = input_conf._instance }
end

kill_input_list.dvb = function(input_conf, input_data)
    if input_conf.dvbcam and input_conf.pnr then
        input_data.source.tail:ca_set_pnr(input_conf.pnr, false)
    end
end

-- ooooo         ooooo  oooo ooooooooo  oooooooooo
--  888           888    88   888    88o 888    888
--  888 ooooooooo 888    88   888    888 888oooo88
--  888           888    88   888    888 888
-- o888o           888oo88   o888ooo88  o888o

local udp_instance_list = {}

input_list.udp = function(input_conf)
    if not input_conf.port then input_conf.port = 1234 end

    local addr = input_conf.addr .. ":" .. input_conf.port
    if input_conf.localaddr then addr = input_conf.localaddr .. "@" .. addr end

    local udp_instance
    if udp_instance_list[addr] then
        udp_instance = udp_instance_list[addr]
        udp_instance.count = udp_instance.count + 1
    else
        udp_instance = {}
        udp_instance.tail = udp_input(input_conf)
        udp_instance.count = 1
        udp_instance_list[addr] = udp_instance
    end

    return udp_instance
end

kill_input_list.udp = function(input_conf, input_data)
    local addr = input_conf.addr .. ":" .. input_conf.port
    if input_conf.localaddr then addr = input_conf.localaddr .. "@" .. addr end

    local udp_instance = udp_instance_list[addr]
    udp_instance.count = udp_instance.count - 1

    if udp_instance.count > 0 then return nil end

    udp_instance.tail = nil
    udp_instance_list[addr] = nil
end

-- ooooo         ooooooooooo ooooo ooooo       ooooooooooo
--  888           888    88   888   888         888    88
--  888 ooooooooo 888oo8      888   888         888ooo8
--  888           888         888   888      o  888    oo
-- o888o         o888o       o888o o888ooooo88 o888ooo8888

input_list.file = function(input_conf)
    return { tail = file_input(input_conf) }
end

kill_input_list.file = function(input_conf, input_data)
    --
end

-- ooooo         ooooo ooooo ooooooooooo ooooooooooo oooooooooo
--  888           888   888  88  888  88 88  888  88  888    888
--  888 ooooooooo 888ooo888      888         888      888oooo88
--  888           888   888      888         888      888
-- o888o         o888o o888o    o888o       o888o    o888o

function http_parse_location(headers)
    for _, header in pairs(headers) do
        local _,_,location = header:find("^Location: http://(.*)")
        if location then
            local port, uri
            local p,_ = location:find("/")
            if p then
                uri = location:sub(p)
                location = location:sub(1, p - 1)
            else
                uri = "/"
            end
            local p,_ = location:find(":")
            if p then
                port = tonumber(location:sub(p + 1))
                location = location:sub(1, p - 1)
            else
                port = 80
            end
            return location, port, uri
        end
    end
    return nil
end

input_list.http = function(input_conf)
    local instance = {}
    local http_conf = {}

    http_conf.host = input_conf.host
    http_conf.port = input_conf.port
    http_conf.uri = input_conf.uri
    http_conf.headers =
    {
        "User-Agent: Astra",
        "Host: " .. input_conf.host .. ":" .. input_conf.port
    }
    http_conf.ts = true
    http_conf.callback = function(self, data)
        if type(data) == 'table' then
            if data.code == 200 then
                if instance.timeout then
                    instance.timeout:close()
                    instance.timeout = nil
                end

                instance.tail:set_upstream(self:stream())

            elseif data.code == 301 or data.code == 302 then
                if instance.timeout then
                    instance.timeout:close()
                    instance.timeout = nil
                end

                local host, port, uri = http_parse_location(data.headers)
                if host then
                    http_conf.host = host
                    http_conf.port = port
                    http_conf.uri = uri
                    http_conf.headers[2] = "Host: " .. host .. ":" .. port

                    instance.request = http_request(http_conf)
                    instance.timeout = timer({
                        interval = 5,
                        callback = function(self)
                            instance.request:close()
                            instance.request = http_request(http_conf)
                        end
                    })
                end
                self:close()
            end
        elseif type(data) == 'nil' then
            if instance.timeout then instance.timeout:close() end
            instance.timeout = timer({
                interval = 5,
                callback = function(self)
                    instance.request:close()
                    instance.request = http_request(http_conf)
                end
            })
        end
    end

    if input_conf.auth then
        table.insert(http_conf.headers, "Authorization: Basic " .. input_conf.auth)
    end

    instance.tail = transmit({})
    instance.request = http_request(http_conf)
    instance.timeout = timer({
        interval = 5,
        callback = function(self)
            instance.request:close()
            instance.request = http_request(http_conf)
        end
    })

    return instance
end

kill_input_list.http = function(input_conf, input_data)
    if input_data.source.timeout then
        input_data.source.timeout:close()
        input_data.source.timeout = nil
    end
    input_data.source.request:close()
    input_data.source.request = nil
end

-- ooooo oooo   oooo oooooooooo ooooo  oooo ooooooooooo
--  888   8888o  88   888    888 888    88  88  888  88
--  888   88 888o88   888oooo88  888    88      888
--  888   88   8888   888        888    88      888
-- o888o o88o    88  o888o        888oo88      o888o

input_module = {}
kill_input_module = {}

function init_input(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]

    if not input_conf.module_name then
        log.error("[stream.lua] option 'module_name' is required for input")
        astra.abort()
    end

    local init_input_type = input_list[input_conf.module_name]
    if not init_input_type then
        log.error("[" .. channel_data.config.name .. " #" .. input_id .. "] Unknown type of input " .. input_id)
        astra.abort()
    end

    local input_data = { }
    input_data.source = init_input_type(input_conf)
    input_data.tail = input_data.source.tail

    if input_conf.pnr or channel_data.config.map then
        local channel_conf =
        {
            name = channel_data.config.name,
            upstream = input_data.tail:stream(),
            pnr = input_conf.pnr,
            sdt = true,
            eit = true,
        }

        if (input_conf.no_sdt or no_sdt) then channel_conf.sdt = nil end
        if (input_conf.no_eit or no_eit) then channel_conf.eit = nil end
        if channel_data.config.map then channel_conf.map = channel_data.config.map end

        input_data.channel = channel(channel_conf)
        input_data.tail = input_data.channel
    end

    if input_conf.biss then
        input_data.decrypt = decrypt({
            upstream = input_data.tail:stream(),
            name = channel_data.config.name,
            biss = input_conf.biss,
        })
        input_data.tail = input_data.decrypt

    elseif input_conf.cam then
        local decrypt_conf = {
            upstream = input_data.tail:stream(),
            name = channel_data.config.name,
            cam = input_conf.cam,
        }
        if input_conf.cas_data then decrypt_conf.cas_data = input_conf.cas_data end
        input_data.decrypt = decrypt(decrypt_conf)
        input_data.tail = input_data.decrypt

    end

    for key,_ in pairs(input_conf) do
        if input_module[key] then
            input_module[key](input_conf, input_data)
        end
    end

    input_data.analyze = analyze({
        upstream = input_data.tail:stream(),
        name = channel_data.config.name,
        callback = function(data)
                on_analyze(channel_data, input_id, data)

                if data.analyze then
                    if data.on_air ~= input_data.on_air then
                        if data.on_air == false then
                            log_analyze_error(channel_data, input_id, data)
                        end
                        input_data.on_air = data.on_air
                        start_reserve(channel_data)
                    end
                end
            end
    })

    channel_data.input[input_id] = input_data
end

function kill_input(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]
    local input_data = channel_data.input[input_id]
    if not input_data.source then return nil end
    kill_input_list[input_conf.module_name](input_conf, input_data)

    for key,_ in pairs(input_conf) do
        if kill_input_module[key] then
            kill_input_module[key](input_conf, input_data)
        end
    end

    input_data.source = nil
    input_data.channel = nil
    input_data.decrypt = nil
    input_data.analyze = nil
    input_data.tail = nil
    channel_data.input[input_id] = nil
end

--   ooooooo            ooooo  oooo ooooooooo  oooooooooo
-- o888   888o           888    88   888    88o 888    888
-- 888     888 ooooooooo 888    88   888    888 888oooo88
-- 888o   o888           888    88   888    888 888
--   88ooo88              888oo88   o888ooo88  o888o

local output_list = {}
local kill_output_list = {}

output_list.udp = function(output_conf)
    if not output_conf.port then output_conf.port = 1234 end
    if not output_conf.ttl then output_conf.ttl = 32 end

    return { tail = udp_output(output_conf) }
end

kill_output_list.udp = function(output_conf, output_data)
    --
end

--   ooooooo            ooooooooooo ooooo ooooo       ooooooooooo
-- o888   888o           888    88   888   888         888    88
-- 888     888 ooooooooo 888oo8      888   888         888ooo8
-- 888o   o888           888         888   888      o  888    oo
--   88ooo88            o888o       o888o o888ooooo88 o888ooo8888

output_list.file = function(output_conf)
    return { tail = file_output(output_conf) }
end

kill_output_list.file = function(output_conf, output_data)
    --
end

--   ooooooo            ooooo ooooo ooooooooooo ooooooooooo oooooooooo
-- o888   888o           888   888  88  888  88 88  888  88  888    888
-- 888     888 ooooooooo 888ooo888      888         888      888oooo88
-- 888o   o888           888   888      888         888      888
--   88ooo88            o888o o888o    o888o       o888o    o888o

local http_instance_list = {}
local http_server_header = "Server: Astra"

function http_server_send_404(server, client)
    local content = "<html>" ..
                    "<center><h1>Not Found</h1></center>" ..
                    "<hr />" ..
                    "<small>Astra</small>" ..
                    "</html>"

    server:send(client, {
        code = 404,
        message = "Not Found",
        headers = {
            http_server_header,
            "Content-Type: text/html; charset=utf-8",
            "Content-Length: " .. #content,
            "Connection: close",
        },
        content = content
    })

    server:close(client)
end

function http_server_send_200(server, client, upstream)
    server:send(client, {
        code = 200,
        message = "OK",
        headers = {
            http_server_header,
            "Content-Type:application/octet-stream",
        },
        upstream = upstream
    })
end

output_list.http = function(output_conf)
    local addr = output_conf.host .. ":" .. output_conf.port

    local http_instance

    if http_instance_list[addr] then
        http_instance = http_instance_list[addr]
        http_instance.uri_list[output_conf.uri] = output_conf.upstream
        return http_instance
    end

    http_instance = { uri_list = {} }
    http_instance.uri_list[output_conf.uri] = output_conf.upstream
    http_instance.tail = http_server({
        addr = output_conf.host,
        port = output_conf.port,
        callback = function(self, client, data)
                if type(data) == 'table' then
                    local http_upstream = http_instance.uri_list[data.uri]
                    if http_upstream then
                        http_server_send_200(self, client, http_upstream)
                    else
                        http_server_send_404(self, client)
                    end
                end
            end
    })

    http_instance_list[addr] = http_instance
    return http_instance
end

kill_output_list.http = function(output_conf, output_data)
    local addr = output_conf.host .. ":" .. output_conf.port

    local http_instance = http_instance_list[addr]

    http_instance.uri_list[output_conf.uri] = nil
    local has_uri = false
    for _ in pairs(http_instance.uri_list) do
        has_uri = true
        break
    end

    if has_uri == true then return nil end

    http_instance.tail:close()
    http_instance.tail = nil
    http_instance_list[addr] = nil
end

--   ooooooo  ooooo  oooo ooooooooooo oooooooooo ooooo  oooo ooooooooooo
-- o888   888o 888    88  88  888  88  888    888 888    88  88  888  88
-- 888     888 888    88      888      888oooo88  888    88      888
-- 888o   o888 888    88      888      888        888    88      888
--   88ooo88    888oo88      o888o    o888o        888oo88      o888o

output_module = {}
kill_output_module = {}

function init_output(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]

    if not output_conf.module_name then
        log.error("[stream.lua] option 'module_name' is required for output")
        astra.abort()
    end

    local init_output_type = output_list[output_conf.module_name]
    if not init_output_type then
        log.error("[" .. channel_data.config.name .. "] Unknown type of output " .. output_id)
        astra.abort()
    end

    local output_data = {}
    output_data.tail = channel_data.tail

    for key,_ in pairs(output_conf) do
        if output_module[key] then
            output_module[key](output_conf, output_data)
        end
    end

    output_conf.upstream = output_data.tail:stream()
    output_data.instance = init_output_type(output_conf)

    channel_data.output[output_id] = output_data
end

function kill_output(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]
    local output_data = channel_data.output[output_id]

    for key,_ in pairs(output_conf) do
        if kill_output_module[key] then
            kill_output_module[key](output_conf, output_data)
        end
    end

    kill_output_list[output_conf.module_name](output_conf, output_data)
    output_data.instance = nil
    output_data.tail = nil
    channel_data.output[output_id] = nil
end

--   oooooooo8 ooooo ooooo      o      oooo   oooo oooo   oooo ooooooooooo ooooo
-- o888     88  888   888      888      8888o  88   8888o  88   888    88   888
-- 888          888ooo888     8  88     88 888o88   88 888o88   888ooo8     888
-- 888o     oo  888   888    8oooo88    88   8888   88   8888   888    oo   888      o
--  888oooo88  o888o o888o o88o  o888o o88o    88  o88o    88  o888ooo8888 o888ooooo88

local channel_list = {}

function make_channel(channel_conf)
    if not channel_conf.name then
        log.error("[stream.lua] option 'name' is required")
        astra.abort()
    end

    if not channel_conf.input or #channel_conf.input == 0 then
        log.error("[stream.lua " .. channel_conf.name .. "] option 'input' is required")
        astra.abort()
    end

    if not channel_conf.output then channel_conf.output = {} end

    if type(channel_conf.enable) == 'nil' then
        channel_conf.enable = true
    end

    if (channel_conf.enable == false) or (channel_conf.enable == 0) then
        log.info("[" ..channel_conf.name .. "] channel is disabled")
        return nil
    end

    function parse_conf(list)
        for key,val in pairs(list) do
            if type(val) == 'string' then
                local conf_tmp = parse_url(val)
                if not conf_tmp then
                    log.error("[stream.lua] wrong URL format: " .. val)
                    astra.abort()
                end
                list[key] = conf_tmp
            end
        end
    end

    parse_conf(channel_conf.input)
    parse_conf(channel_conf.output)

    local channel_data = {}
    channel_data.config = channel_conf
    channel_data.input = {}
    channel_data.output = {}

    for input_id = 1, #channel_conf.input do
        channel_data.input[input_id] = { on_air = false, }
    end
    init_input(channel_data, 1)

    channel_data.transmit = transmit({ upstream = channel_data.input[1].tail:stream() })
    channel_data.tail = channel_data.transmit

    for output_id,_ in pairs(channel_conf.output) do
        init_output(channel_data, output_id)
    end

    table.insert(channel_list, channel_data)
    return channel_data
end

function kill_channel(channel_data)
    local channel_id = 0
    for key, value in pairs(channel_list) do
        if value == channel_data then
            channel_id = key
            break
        end
    end
    if channel_id == 0 then
        log.error("[stream.lua] channel is not found")
        return
    end

    for input_id = 1, #channel_data.config.input do
        kill_input(channel_data, input_id)
    end
    channel_data.input = nil

    for output_id = 1, #channel_data.config.output do
        kill_output(channel_data, output_id)
    end
    channel_data.output = nil

    channel_data.transmit = nil
    channel_data.config = nil
    channel_data.tail = nil

    table.remove(channel_list, channel_id)

    collectgarbage()
end

function find_channel(key, value)
    for _, channel_data in pairs(channel_list) do
        if channel_data.config[key] == value then
            return channel_data
        end
    end
end
