-- Astra Script: IPTV Streaming
-- http://cesbo.com/astra
--
-- Copyright (C) 2013-2014, Andrey Dyldin <and@cesbo.com>
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

http_user_agent = "Astra"

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

    elseif data.analyze and input_id > 0 then
        local input_data = channel_data.input[input_id]

        if data.on_air ~= input_data.on_air then
            if data.on_air == false then
                local analyze_message = "[" .. channel_data.config.name .. " #" .. input_id .. "] " .. "Bitrate:" .. data.total.bitrate .. "Kbit/s"

                if data.total.cc_errors > 0 then
                    analyze_message = analyze_message .. " CC-Error:" .. data.total.cc_errors
                end

                if data.total.pes_errors > 0 then
                    analyze_message = analyze_message .. " PES-Error"
                end

                if data.total.scrambled then
                    analyze_message = analyze_message .. " Scrambled"
                end

                log.error(analyze_message)
            else
                local analyze_message = "[" .. channel_data.config.name .. " #" .. input_id .. "] " .. "Bitrate:" .. data.total.bitrate .. "Kbit/s"
                log.info(analyze_message)
            end

            input_data.on_air = data.on_air

            if channel_data.ri_delay > 0 then
                if data.on_air == true then
                    channel_data.ri_delay = 0
                    start_reserve(channel_data)
                else
                    channel_data.ri_delay = channel_data.ri_delay - 1
                    input_data.on_air = nil
                end
            else
                start_reserve(channel_data)
            end
        end

    end
end

-- ooooo  oooo oooooooooo  ooooo
--  888    88   888    888  888
--  888    88   888oooo88   888
--  888    88   888  88o    888      o
--   888oo88   o888o  88o8 o888ooooo88

parse_option = {}

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

parse_option.pid = function(val, result)
    result.pid = split(val, ',')
end

parse_option.filter = function(val, result)
    result.filter = split(val, ',')
end

parse_option.map = function(val, result)
    if not result.map then result.map = {} end
    for key, val in pairs(val) do
        table.insert(result.map, ("%s=%s"):format(key, val))
    end
end

--

ifaddrs = nil
if utils.ifaddrs then ifaddrs = utils.ifaddrs() end

parse_addr = {}

parse_addr.dvb = function(addr, result)
    if #addr == 0 then return nil end

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

parse_addr.rtp = function(addr, result)
    parse_addr.udp(addr, result)
    result.rtp = true
end

parse_addr.file = function(addr, result)
    result.filename = addr
end

parse_addr.http = function(addr, result)
    local x = addr:find("@")
    if x then
        result.auth = base64.encode(addr:sub(1, x - 1))
        addr = addr:sub(x + 1)
    end
    local x = addr:find("/")
    if x then
        result.path = addr:sub(x)
        addr = addr:sub(1, x - 1)
    else
        result.path = "/"
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
            parse_option[key](val, result)
        else
            if type(result[key]) == 'table' and type(val) == 'table' then
                for _key,_val in pairs(val) do
                    result[key][_key] = _val
                end
            else
                result[key] = val
            end
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

-- ooooo         ooooooooo  ooooo  oooo oooooooooo
--  888           888    88o 888    88   888    888
--  888 ooooooooo 888    888  888  88    888oooo88
--  888           888    888   88888     888    888
-- o888o         o888ooo88      888     o888ooo888

dvb_list = nil

input_list = {}
kill_input_list = {}

function dvb_tune(dvb_conf)
    if dvb_conf.mac then
        if dvb_list == nil then
            if dvbls then
                dvb_list = dvbls()
            else
                dvb_list = {}
            end
        end
        dvb_conf.mac = dvb_conf.mac:upper()
        for _, adapter_info in pairs(dvb_list) do
            if dvb_conf.mac and adapter_info.mac == dvb_conf.mac then
                log.info("[dvb_input " .. adapter_info.adapter ..
                         ":" .. adapter_info.device .. "] " ..
                         "selected by mac:" .. adapter_info.mac)
                dvb_conf.adapter = adapter_info.adapter
                dvb_conf.device = adapter_info.device
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

input_list.dvb = function(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]

    if not input_conf._instance then
        input_conf._instance = dvb_tune(input_conf)
        input_conf._instance_single = true
    end

    if input_conf.dvbcam and input_conf.pnr then
        input_conf._instance:ca_set_pnr(input_conf.pnr, true)
    end

    return { tail = input_conf._instance }
end

kill_input_list.dvb = function(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]
    local input_data = channel_data.input[input_id]

    if input_conf.dvbcam and input_conf.pnr then
        input_data.source.tail:ca_set_pnr(input_conf.pnr, false)
    end

    if input_conf._instance_single == true then
        input_conf._instance = nil
        input_conf._instance_single = nil
    end
end

-- ooooo         ooooo  oooo ooooooooo  oooooooooo
--  888           888    88   888    88o 888    888
--  888 ooooooooo 888    88   888    888 888oooo88
--  888           888    88   888    888 888
-- o888o           888oo88   o888ooo88  o888o

udp_instance_list = {}

input_list.udp = function(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]

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

kill_input_list.udp = function(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]

    local addr = input_conf.addr .. ":" .. input_conf.port
    if input_conf.localaddr then addr = input_conf.localaddr .. "@" .. addr end

    local udp_instance = udp_instance_list[addr]
    udp_instance.count = udp_instance.count - 1

    if udp_instance.count > 0 then return nil end

    udp_instance.tail = nil
    udp_instance_list[addr] = nil
end

input_list.rtp = function(channel_data, input_id)
    return input_list.udp(channel_data, input_id)
end

kill_input_list.rtp = function(channel_data, input_id)
    kill_input_list.udp(channel_data, input_id)
end

-- ooooo         ooooooooooo ooooo ooooo       ooooooooooo
--  888           888    88   888   888         888    88
--  888 ooooooooo 888oo8      888   888         888ooo8
--  888           888         888   888      o  888    oo
-- o888o         o888o       o888o o888ooooo88 o888ooo8888

input_list.file = function(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]

    return { tail = file_input(input_conf) }
end

kill_input_list.file = function(channel_data, input_id)
    --
end

-- ooooo         ooooo ooooo ooooooooooo ooooooooooo oooooooooo
--  888           888   888  88  888  88 88  888  88  888    888
--  888 ooooooooo 888ooo888      888         888      888oooo88
--  888           888   888      888         888      888
-- o888o         o888o o888o    o888o       o888o    o888o

http_instance_list = {}

function http_parse_location(self, response)
    if not response.headers then return nil end
    local location = response.headers['location']
    if not location then return nil end

    local host = self.__options.host
    local port = self.__options.port
    local path

    local b = location:find("://")
    if b then
        local p = location:sub(1, b - 1)
        if p ~= "http" then
            return nil
        end
        location = location:sub(b + 3)
        b = location:find("/")
        if b then
            path = location:sub(b)
            location = location:sub(1, b - 1)
        else
            path = "/"
        end
        b = location:find(":")
        if b then
            port = tonumber(location:sub(b + 1))
            host = location:sub(1, b - 1)
        else
            port = 80
            host = location
        end
    else
        path = location
    end

    return host, port, path
end

input_list.http = function(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]
    local input_data = channel_data.input[input_id]

    local addr = input_conf.host .. ":" .. input_conf.port .. input_conf.path
    local instance = http_instance_list[addr]
    if instance then
        instance.count = instance.count + 1
        return instance
    end

    instance = { count = 1, }
    http_instance_list[addr] = instance

    local http_conf = {}

    http_conf.host = input_conf.host
    http_conf.port = input_conf.port
    http_conf.path = input_conf.path

    http_conf.headers =
    {
        "User-Agent: " .. http_user_agent,
        "Host: " .. input_conf.host .. ":" .. input_conf.port,
        "Connection: close",
    }
    if input_conf.auth then
        table.insert(http_conf.headers, "Authorization: Basic " .. input_conf.auth)
    end

    http_conf.stream = true
    if input_conf.sync then http_conf.sync = input_conf.sync end
    if input_conf.buffer_size then http_conf.buffer_size = input_conf.buffer_size end
    if input_conf.timeout then http_conf.timeout = input_conf.timeout end

    local timer_conf =
    {
        interval = 5,
        callback = function(self)
            instance.timeout:close()
            instance.timeout = nil

            if instance.request then instance.request:close() end
            instance.request = http_request(http_conf)
        end
    }

    http_conf.callback = function(self, response)
            if not response then
                instance.request:close()
                instance.request = nil
                instance.timeout = timer(timer_conf)

            elseif response.code == 200 then
                if instance.timeout then
                    instance.timeout:close()
                    instance.timeout = nil
                end

                instance.tail:set_upstream(self:stream())

            elseif response.code == 301 or response.code == 302 then
                if instance.timeout then
                    instance.timeout:close()
                    instance.timeout = nil
                end

                instance.request:close()
                instance.request = nil

                local host, port, path = http_parse_location(self, response)
                if host then
                    http_conf.host = host
                    http_conf.port = port
                    http_conf.path = path
                    http_conf.headers[2] = "Host: " .. host .. ":" .. port

                    log.info("[" .. input_data.name .. "] Redirect to " ..
                             "http://" .. host .. ":" .. port .. path)
                    instance.request = http_request(http_conf)
                else
                    log.error("[" .. input_data.name .. "] Redirect failed")
                    instance.timeout = timer(timer_conf)
                end

            else
                log.error("[" .. input_data.name .. "] " ..
                          "HTTP Error " .. response.code .. ":" .. response.message)

                instance.request:close()
                instance.request = nil
                instance.timeout = timer(timer_conf)
            end
        end

    instance.tail = transmit()
    instance.request = http_request(http_conf)

    return instance
end

kill_input_list.http = function(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]

    local addr = input_conf.host .. ":" .. input_conf.port .. input_conf.path
    local instance = http_instance_list[addr]

    instance.count = instance.count - 1
    if instance.count > 0 then return nil end

    if instance.timeout then
        instance.timeout:close()
        instance.timeout = nil
    end

    if instance.request then
        instance.request:close()
        instance.request = nil
    end

    http_instance_list[addr] = nil
end

-- ooooo oooo   oooo oooooooooo ooooo  oooo ooooooooooo
--  888   8888o  88   888    888 888    88  88  888  88
--  888   88 888o88   888oooo88  888    88      888
--  888   88   8888   888        888    88      888
-- o888o o88o    88  o888o        888oo88      o888o

input_module = {}
kill_input_module = {}

function init_input(channel_data, input_id)
    channel_data.input[input_id] = {}

    local input_conf = channel_data.config.input[input_id]
    local input_data = channel_data.input[input_id]

    input_data.name = channel_data.config.name .. " #" .. input_id
    input_data.config = input_conf
    input_data.source = input_list[input_conf.module_name](channel_data, input_id)
    input_data.tail = input_data.source.tail

    if input_conf.pnr == nil and input_conf.pid == nil then
        if  input_conf.set_pnr or
            input_conf.map or channel_data.config.map or
            input_conf.filter
        then
            input_conf.pnr = 0
        end
    end

    if input_conf.pnr ~= nil or input_conf.pid ~= nil then
        local channel_conf = {
            name = input_data.name,
            upstream = input_data.tail:stream(),
        }

        if input_conf.pnr ~= nil then
            channel_conf.pnr = input_conf.pnr
            if input_conf.set_pnr then channel_conf.set_pnr = input_conf.set_pnr end

            if input_conf.no_sdt ~= true and _G.no_sdt ~= true then channel_conf.sdt = true end
            if input_conf.no_eit ~= true and _G.no_eit ~= true then channel_conf.eit = true end

            if channel_conf.sdt and input_conf.pass_sdt then channel_conf.pass_sdt = true end
            if channel_conf.eit and input_conf.pass_eit then channel_conf.pass_eit = true end
        end

        if input_conf.pid then channel_conf.pid = input_conf.pid end

        if input_conf.map then
            channel_conf.map = input_conf.map
        elseif channel_data.config.map then
            channel_conf.map = channel_data.config.map
        end

        if input_conf.filter then channel_conf.filter = input_conf.filter end
        if input_conf.cam or input_conf.cas == true then channel_conf.cas = true end

        input_data.channel = channel(channel_conf)
        input_data.tail = input_data.channel
    end

    if input_conf.biss then
        input_data.decrypt = decrypt({
            upstream = input_data.tail:stream(),
            name = input_data.name,
            biss = input_conf.biss,
        })
        input_data.tail = input_data.decrypt

    elseif input_conf.cam then
        local decrypt_conf = {
            upstream = input_data.tail:stream(),
            name = input_data.name,
            cam = input_conf.cam,
        }
        if input_conf.cas_data then decrypt_conf.cas_data = input_conf.cas_data end
        if input_conf.ecm_pid then decrypt_conf.ecm_pid = input_conf.ecm_pid end
        if input_conf.pnr and input_conf.set_pnr then decrypt_conf.cas_pnr = input_conf.pnr end
        if input_conf.disable_emm == true then decrypt_conf.disable_emm = true end
        if input_conf.shift then decrypt_conf.shift = input_conf.shift end
        input_data.decrypt = decrypt(decrypt_conf)
        input_data.tail = input_data.decrypt

    end

    for key,_ in pairs(input_conf) do
        if input_module[key] then
            input_module[key](channel_data, input_id)
        end
    end

    if input_conf.no_analyze == true then
        if channel_data.transmit then
            channel_data.transmit:set_upstream(input_data.tail:stream())
        end
    else
        local cc_limit = nil
        if input_conf.cc_limit then
            cc_limit = input_conf.cc_limit
        else
            if channel_data.config.cc_limit then
                cc_limit = channel_data.config.cc_limit
            end
        end
        input_data.analyze = analyze({
            upstream = input_data.tail:stream(),
            name = input_data.name,
            cc_limit = cc_limit,
            callback = function(data)
                on_analyze(channel_data, input_id, data)
            end
        })
    end
end

function kill_input(channel_data, input_id)
    local input_conf = channel_data.config.input[input_id]
    local input_data = channel_data.input[input_id]

    if not input_data or not input_data.source then return nil end

    kill_input_list[input_conf.module_name](channel_data, input_id)

    for key,_ in pairs(input_conf) do
        if kill_input_module[key] then
            kill_input_module[key](channel_data, input_id)
        end
    end

    input_data.source = nil
    input_data.analyze = nil
    input_data.channel = nil
    input_data.decrypt = nil
    input_data.tail = nil
    channel_data.input[input_id] = nil

    collectgarbage()
end

--   ooooooo            ooooo  oooo ooooooooo  oooooooooo
-- o888   888o           888    88   888    88o 888    888
-- 888     888 ooooooooo 888    88   888    888 888oooo88
-- 888o   o888           888    88   888    888 888
--   88ooo88              888oo88   o888ooo88  o888o

output_list = {}
kill_output_list = {}

output_list.udp = function(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]

    if not output_conf.port then output_conf.port = 1234 end
    if not output_conf.ttl then output_conf.ttl = 32 end

    return { tail = udp_output(output_conf) }
end

kill_output_list.udp = function(channel_data, output_id)
    --
end

output_list.rtp = function(channel_data, output_id)
    return output_list.udp(channel_data, output_id)
end

kill_output_list.rtp = function(channel_data, output_id)
    --
end

--   ooooooo            ooooooooooo ooooo ooooo       ooooooooooo
-- o888   888o           888    88   888   888         888    88
-- 888     888 ooooooooo 888oo8      888   888         888ooo8
-- 888o   o888           888         888   888      o  888    oo
--   88ooo88            o888o       o888o o888ooooo88 o888ooo8888

output_list.file = function(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]

    return { tail = file_output(output_conf) }
end

kill_output_list.file = function(channel_data, output_id)
    --
end

--   ooooooo            ooooo ooooo ooooooooooo ooooooooooo oooooooooo
-- o888   888o           888   888  88  888  88 88  888  88  888    888
-- 888     888 ooooooooo 888ooo888      888         888      888oooo88
-- 888o   o888           888   888      888         888      888
--   88ooo88            o888o o888o    o888o       o888o    o888o

http_client_list = {}
http_instance_list = {}

function make_client_id(server, client, request, url)
    local client_id
    repeat
        client_id = math.random(10000000, 99000000)
    until not http_client_list[client_id]

    local client_addr = request.headers['x-real-ip']
    if not client_addr then client_addr = request.addr end

    http_client_list[client_id] = {
        server = server,
        client = client,
        addr   = client_addr,
        url   = url,
        st     = os.time(),
    }

    return client_id
end

function on_http_request(server, client, request)
    local client_data = server:data(client)

    if not request then
        if not client_data.client_id then
            return
        end

        if client_data.channel_data then
            local channel_data = client_data.channel_data
            channel_data.clients = channel_data.clients - 1
            if channel_data.clients == 0 then
                for input_id = 1, #channel_data.config.input do
                    kill_input(channel_data, input_id)
                end
                channel_data.input = {}
                collectgarbage()
            end
            client_data.channel_data = nil
        end

        http_client_list[client_data.client_id] = nil
        client_data.client_id = nil
        return
    end

    local http_output_info = server.__options.channel_list[request.path]

    if not http_output_info then
        server:abort(client, 404)
        return
    end

    local channel_data = http_output_info[1]
    local output_id = http_output_info[2]
    local output_conf = channel_data.config.output[output_id]

    local url = "http://" .. server.__options.addr .. ":"
                          .. server.__options.port
                          .. request.path

    client_data.client_id = make_client_id(server, client, request, url)

    local http_allow_client = function()
        client_data.channel_data = channel_data

        if channel_data.clients == 0 then init_input(channel_data, 1) end
        channel_data.clients = channel_data.clients + 1
        channel_data.http_client_list[client_data.client_id] = true

        server:send(client, output_conf.upstream)
    end

    http_allow_client()
end

output_list.http = function(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]
    local addr = output_conf.host .. ":" .. output_conf.port
    local http_instance = nil

    if not channel_data.http_client_list then channel_data.http_client_list = {} end

    local http_output_info = { channel_data, output_id }

    if http_instance_list[addr] then
        http_instance = http_instance_list[addr]
        http_instance.channel_list[output_conf.path] = http_output_info

        return http_instance
    end

    http_instance = { channel_list = {} }
    http_instance.channel_list[output_conf.path] = http_output_info
    http_instance.tail = http_server({
        addr = output_conf.host,
        port = output_conf.port,
        buffer_size = output_conf.buffer_size,
        buffer_fill = output_conf.buffer_fill,
        channel_list = http_instance.channel_list,
        route = {
            { "*", http_upstream({ callback = on_http_request }) },
        },
    })

    http_instance_list[addr] = http_instance

    return http_instance
end

kill_output_list.http = function(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]

    local addr = output_conf.host .. ":" .. output_conf.port

    local http_instance = http_instance_list[addr]

    for client_id,_ in pairs(channel_data.http_client_list) do
        local client = http_client_list[client_id]
        client.server:close(client.client)
    end

    http_instance.channel_list[output_conf.path] = nil

    for _ in pairs(http_instance.channel_list) do
        return nil
    end

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

output_module.mixaudio = function(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]
    local output_data = channel_data.output[output_id]

    if mixaudio == nil then
        log.error("[" .. channel_data.config.name .. " #" .. output_id .. "] " ..
                  "mixaudio module is not found")
        return
    end

    local mixaudio_conf = { upstream = output_data.tail:stream() }
    local b = output_conf.mixaudio:find("/")
    if b then
        mixaudio_conf.pid = tonumber(output_conf.mixaudio:sub(1, b - 1))
        mixaudio_conf.direction = output_conf.mixaudio:sub(b + 1)
    else
        mixaudio_conf.pid = tonumber(output_conf.mixaudio)
    end

    output_data.mixaudio = mixaudio(mixaudio_conf)
    output_data.tail = output_data.mixaudio
end

kill_output_module.mixaudio = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    output_data.mixaudio = nil
end

output_module.biss = function(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]
    local output_data = channel_data.output[output_id]

    if biss_encrypt == nil then
        log.error("[" .. channel_data.config.name .. " #" .. output_id .. "] " ..
                  "biss_encrypt module is not found")
        return
    end

    output_data.biss = biss_encrypt({
        upstream = output_data.tail:stream(),
        key = output_conf.biss
    })
    output_data.tail = output_data.biss
end

kill_output_module.biss = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    output_data.biss = nil
end

function init_output(channel_data, output_id)
    channel_data.output[output_id] = {}

    local output_conf = channel_data.config.output[output_id]
    local output_data = channel_data.output[output_id]

    output_data.tail = channel_data.tail

    for key,_ in pairs(output_conf) do
        if output_module[key] then
            output_module[key](channel_data, output_id)
        end
    end

    output_conf.upstream = output_data.tail:stream()
    output_data.instance = output_list[output_conf.module_name](channel_data, output_id)
end

function kill_output(channel_data, output_id)
    local output_conf = channel_data.config.output[output_id]
    local output_data = channel_data.output[output_id]

    for key,_ in pairs(output_conf) do
        if kill_output_module[key] then
            kill_output_module[key](channel_data, output_id)
        end
    end

    kill_output_list[output_conf.module_name](channel_data, output_id)
    output_data.instance = nil
    output_data.tail = nil
    channel_data.output[output_id] = nil
end

--   oooooooo8 ooooo ooooo      o      oooo   oooo oooo   oooo ooooooooooo ooooo
-- o888     88  888   888      888      8888o  88   8888o  88   888    88   888
-- 888          888ooo888     8  88     88 888o88   88 888o88   888ooo8     888
-- 888o     oo  888   888    8oooo88    88   8888   88   8888   888    oo   888      o
--  888oooo88  o888o o888o o88o  o888o o88o    88  o88o    88  o888ooo8888 o888ooooo88

channel_list = {}

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
    channel_data.ri_delay = 3 -- the delay for reserve

    channel_data.clients = 0
    if #channel_conf.output == 0 then
        channel_data.clients = 1
    else
        for _,output_conf in pairs(channel_conf.output) do
            if output_conf.module_name ~= "http" or
               output_conf.keep_active == true
            then
                channel_data.clients = channel_data.clients + 1
            end
        end
    end

    for input_id = 1, #channel_conf.input do
        channel_data.input[input_id] = { on_air = false, }
    end

    channel_data.transmit = transmit()
    channel_data.tail = channel_data.transmit

    if channel_data.clients > 0 then
        init_input(channel_data, 1)
    end

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

--  oooooooo8 ooooooooooo oooooooooo  ooooooooooo      o      oooo     oooo
-- 888        88  888  88  888    888  888    88      888      8888o   888
--  888oooooo     888      888oooo88   888ooo8       8  88     88 888o8 88
--         888    888      888  88o    888    oo    8oooo88    88  888  88
-- o88oooo888    o888o    o888o  88o8 o888ooo8888 o88o  o888o o88o  8  o88o

options_usage = [[
    FILE                stream configuration file
]]

options = {
    ["*"] = function(idx)
        local filename = argv[idx]
        if utils.stat(filename).type == 'file' then
            dofile(filename)
            return 0
        end
        return -1
    end,
}

function main()
    log.info("Starting Astra " .. astra.version)
end
