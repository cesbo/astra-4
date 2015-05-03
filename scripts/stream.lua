-- Astra Stream
-- https://cesbo.com/astra/
--
-- Copyright (C) 2013-2015, Andrey Dyldin <and@cesbo.com>
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

function on_analyze_spts(channel_data, input_id, data)
    local input_data = channel_data.input[input_id]

    if data.error then
        log.error("[" .. input_data.config.name .. "] Error: " .. data.error)

    elseif data.psi then
        if dump_psi_info[data.psi] then
            dump_psi_info[data.psi]("[" .. input_data.config.name .. "] ", data)
        else
            log.error("[" .. input_data.config.name .. "] Unknown PSI: " .. data.psi)
        end

    elseif data.analyze then

        if data.on_air ~= input_data.on_air then
            local analyze_message = "[" .. input_data.config.name .. "] Bitrate:" .. data.total.bitrate .. "Kbit/s"

            if data.on_air == false then
                local m = nil
                if data.total.scrambled then
                    m = " Scrambled"
                else
                    m = " PES:" .. data.total.pes_errors .. " CC:" .. data.total.cc_errors
                end
                log.error(analyze_message .. m)
            else
                log.info(analyze_message)
            end

            input_data.on_air = data.on_air

            if channel_data.delay > 0 then
                if input_data.on_air == true and channel_data.active_input_id == 0 then
                    start_reserve(channel_data)
                else
                    channel_data.delay = channel_data.delay - 1
                    input_data.on_air = nil
                end
            else
                start_reserve(channel_data)
            end
        end
    end
end

-- oooooooooo  ooooooooooo  oooooooo8 ooooooooooo oooooooooo ooooo  oooo ooooooooooo
--  888    888  888    88  888         888    88   888    888 888    88   888    88
--  888oooo88   888ooo8     888oooooo  888ooo8     888oooo88   888  88    888ooo8
--  888  88o    888    oo          888 888    oo   888  88o     88888     888    oo
-- o888o  88o8 o888ooo8888 o88oooo888 o888ooo8888 o888o  88o8    888     o888ooo8888

function start_reserve(channel_data)
    local active_input_id = 0
    for input_id, input_data in ipairs(channel_data.input) do
        if input_data.on_air == true then
            channel_data.transmit:set_upstream(input_data.input.tail:stream())
            log.info("[" .. channel_data.config.name .. "] Active input #" .. input_id)
            active_input_id = input_id
            break
        end
    end

    if active_input_id == 0 then
        local next_input_id = 0
        for input_id, input_data in ipairs(channel_data.input) do
            if not input_data.input then
                next_input_id = input_id
                break
            end
        end
        if next_input_id == 0 then
            log.error("[" .. channel_data.config.name .. "] Failed to switch to reserve")
        else
            channel_init_input(channel_data, next_input_id)
        end
    else
        channel_data.active_input_id = active_input_id
        channel_data.delay = channel_data.config.timeout

        for input_id, input_data in ipairs(channel_data.input) do
            if input_data.input and input_id > active_input_id then
                channel_kill_input(channel_data, input_id)
                log.debug("[" .. channel_data.config.name .. "] Destroy input #" .. input_id)
                input_data.on_air = nil
            end
        end
        collectgarbage()
    end
end

-- ooooo oooo   oooo oooooooooo ooooo  oooo ooooooooooo
--  888   8888o  88   888    888 888    88  88  888  88
--  888   88 888o88   888oooo88  888    88      888
--  888   88   8888   888        888    88      888
-- o888o o88o    88  o888o        888oo88      o888o

function channel_init_input(channel_data, input_id)
    local input_data = channel_data.input[input_id]
    input_data.input = init_input(input_data.config)

    if input_data.config.no_analyze ~= true then
        input_data.analyze = analyze({
            upstream = input_data.input.tail:stream(),
            name = input_data.config.name,
            cc_limit = input_data.config.cc_limit,
            bitrate_limit = input_data.config.bitrate_limit,
            callback = function(data)
                on_analyze_spts(channel_data, input_id, data)
            end,
        })
    end

    -- TODO: init additional modules

    channel_data.transmit:set_upstream(input_data.input.tail:stream())
end

function channel_kill_input(channel_data, input_id)
    local input_data = channel_data.input[input_id]

    -- TODO: kill additional modules

    input_data.analyze = nil
    input_data.on_air = nil

    kill_input(input_data.input)
    input_data.input = nil

    if input_id == 1 then channel_data.delay = 3 end
    channel_data.input[input_id] = { config = input_data.config, }
end

--   ooooooo  ooooo  oooo ooooooooooo oooooooooo ooooo  oooo ooooooooooo
-- o888   888o 888    88  88  888  88  888    888 888    88  88  888  88
-- 888     888 888    88      888      888oooo88  888    88      888
-- 888o   o888 888    88      888      888        888    88      888
--   88ooo88    888oo88      o888o    o888o        888oo88      o888o

init_output_option = {}
kill_output_option = {}

init_output_option.biss = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    if biss_encrypt == nil then
        log.error("[" .. output_data.config.name .. "] biss_encrypt module is not found")
        return nil
    end

    output_data.biss = biss_encrypt({
        upstream = channel_data.tail:stream(),
        key = output_data.config.biss,
    })
    channel_data.tail = output_data.biss
end

kill_output_option.biss = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.biss = nil
end

init_output_module = {}
kill_output_module = {}

function channel_init_output(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    for key,_ in pairs(output_data.config) do
        if init_output_option[key] then
            init_output_option[key](channel_data, output_id)
        end
    end

    init_output_module[output_data.config.format](channel_data, output_id)
end

function channel_kill_output(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    for key,_ in pairs(output_data.config) do
        if kill_output_option[key] then
            kill_output_option[key](channel_data, output_id)
        end
    end

    kill_output_module[output_data.config.format](channel_data, output_id)
    channel_data.output[output_id] = { config = output_data.config, }
end

--   ooooooo            ooooo  oooo ooooooooo  oooooooooo
-- o888   888o           888    88   888    88o 888    888
-- 888     888 ooooooooo 888    88   888    888 888oooo88
-- 888o   o888           888    88   888    888 888
--   88ooo88              888oo88   o888ooo88  o888o

init_output_module.udp = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    local localaddr = output_data.config.localaddr
    if localaddr and ifaddr_list then
        local ifaddr = ifaddr_list[localaddr]
        if ifaddr and ifaddr.ipv4 then localaddr = ifaddr.ipv4[1] end
    end
    output_data.output = udp_output({
        upstream = channel_data.tail:stream(),
        addr = output_data.config.addr,
        port = output_data.config.port,
        ttl = output_data.config.ttl,
        localaddr = localaddr,
        socket_size = output_data.config.socket_size,
        rtp = (output_data.config.format == "rtp"),
        sync = output_data.config.sync,
        cbr = output_data.config.cbr,
    })
end

kill_output_module.udp = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.output = nil
end

init_output_module.rtp = function(channel_data, output_id)
    init_output_module.udp(channel_data, output_id)
end

kill_output_module.rtp = function(channel_data, output_id)
    kill_output_module.udp(channel_data, output_id)
end

--   ooooooo            ooooooooooo ooooo ooooo       ooooooooooo
-- o888   888o           888    88   888   888         888    88
-- 888     888 ooooooooo 888oo8      888   888         888ooo8
-- 888o   o888           888         888   888      o  888    oo
--   88ooo88            o888o       o888o o888ooooo88 o888ooo8888

init_output_module.file = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.output = file_output({
        upstream = channel_data.tail:stream(),
        filename = output_data.config.filename,
        m2ts = output_data.config.m2ts,
        buffer_size = output_data.config.buffer_size,
        aio = output_data.config.aio,
        directio = output_data.config.directio,
    })
end

kill_output_module.file = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    output_data.output = nil
end

--   ooooooo            ooooo ooooo ooooooooooo ooooooooooo oooooooooo
-- o888   888o           888   888  88  888  88 88  888  88  888    888
-- 888     888 ooooooooo 888ooo888      888         888      888oooo88
-- 888o   o888           888   888      888         888      888
--   88ooo88            o888o o888o    o888o       o888o    o888o

http_output_client_list = {}
http_output_instance_list = {}

function http_output_client(server, client, request)
    local client_data = server:data(client)

    if not request then
        http_output_client_list[client_data.client_id] = nil
        client_data.client_id = nil
        return nil
    end

    local function get_unique_client_id()
        local _id = math.random(10000000, 99000000)
        if http_output_client_list[_id] ~= nil then
            return nil
        end
        return _id
    end

    repeat
        client_data.client_id = get_unique_client_id()
    until client_data.client_id ~= nil

    http_output_client_list[client_data.client_id] = {
        server = server,
        client = client,
        request = request,
        st   = os.time(),
    }
end

function http_output_on_request(server, client, request)
    local client_data = server:data(client)

    if not request then
        if client_data.client_id then
            local channel_data = client_data.output_data.channel_data
            channel_data.clients = channel_data.clients - 1
            if channel_data.clients == 0 and channel_data.input[1].input ~= nil then
                for input_id, input_data in ipairs(channel_data.input) do
                    if input_data.input then
                        channel_kill_input(channel_data, input_id)
                    end
                end
                channel_data.active_input_id = 0
            end

            http_output_client(server, client, nil)
            collectgarbage()
        end
        return nil
    end

    client_data.output_data = server.__options.channel_list[request.path]
    if not client_data.output_data then
        server:abort(client, 404)
        return nil
    end

    http_output_client(server, client, request)

    local channel_data = client_data.output_data.channel_data
    channel_data.clients = channel_data.clients + 1

    local allow_channel = function()
        if not channel_data.input[1].input then
            channel_init_input(channel_data, 1)
        end

        server:send(client, {
            upstream = channel_data.tail:stream(),
            buffer_size = client_data.output_data.config.buffer_size,
            buffer_fill = client_data.output_data.config.buffer_fill,
        })
    end

    allow_channel()
end

init_output_module.http = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    local instance_id = output_data.config.host .. ":" .. output_data.config.port
    local instance = http_output_instance_list[instance_id]

    if not instance then
        instance = http_server({
            addr = output_data.config.host,
            port = output_data.config.port,
            sctp = output_data.config.sctp,
            route = {
                { "/*", http_upstream({ callback = http_output_on_request }) },
            },
            channel_list = {},
        })
        http_output_instance_list[instance_id] = instance
    end

    output_data.instance = instance
    output_data.instance_id = instance_id
    output_data.channel_data = channel_data

    instance.__options.channel_list[output_data.config.path] = output_data
end

kill_output_module.http = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]

    local instance = output_data.instance
    local instance_id = output_data.instance_id

    for _, client in pairs(http_output_client_list) do
        if client.server == instance then
            instance:close(client.client)
        end
    end

    instance.__options.channel_list[output_data.config.path] = nil

    local is_instance_empty = true
    for _ in pairs(instance.__options.channel_list) do
        is_instance_empty = false
        break
    end

    if is_instance_empty then
        instance:close()
        http_output_instance_list[instance_id] = nil
    end

    output_data.instance = nil
    output_data.instance_id = nil
    output_data.channel_data = nil
end

--   ooooooo            oooo   oooo oooooooooo
-- o888   888o           8888o  88   888    888
-- 888     888 ooooooooo 88 888o88   888oooo88
-- 888o   o888           88   8888   888
--   88ooo88            o88o    88  o888o

init_output_module.np = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    local conf = output_data.config

    local http_conf = {
        host = conf.host,
        port = conf.port,
        path = conf.path,
        upstream = channel_data.tail:stream(),
        buffer_size = conf.buffer_size,
        buffer_fill = conf.buffer_size,
        timeout = conf.timeout,
        sctp = conf.sctp,
        headers = {
            "User-Agent: " .. http_user_agent,
            "Host: " .. conf.host,
            "Connection: keep-alive",
        },
    }

    local timer_conf = {
        interval = 5,
        callback = function(self)
            output_data.timeout:close()
            output_data.timeout = nil

            if output_data.request then output_data.request:close() end
            output_data.request = http_request(http_conf)
        end
    }

    http_conf.callback = function(self, response)
        if not response then
            output_data.request:close()
            output_data.request = nil
            output_data.timeout = timer(timer_conf)

        elseif response.code == 200 then
            if output_data.timeout then
                output_data.timeout:close()
                output_data.timeout = nil
            end

        elseif response.code == 301 or response.code == 302 then
            if output_data.timeout then
                output_data.timeout:close()
                output_data.timeout = nil
            end

            output_data.request:close()
            output_data.request = nil

            local o = parse_url(response.headers["location"])
            if o then
                http_conf.host = o.host
                http_conf.port = o.port
                http_conf.path = o.path
                http_conf.headers[2] = "Host: " .. o.host

                log.info("[" .. conf.name .. "] Redirect to http://" .. o.host .. ":" .. o.port .. o.path)
                output_data.request = http_request(http_conf)
            else
                log.error("[" .. conf.name .. "] NP Error: Redirect failed")
                output_data.timeout = timer(timer_conf)
            end

        else
            output_data.request:close()
            output_data.request = nil
            log.error("[" .. conf.name .. "] NP Error: " .. response.code .. ":" .. response.message)
            output_data.timeout = timer(timer_conf)
        end
    end

    output_data.request = http_request(http_conf)
end

kill_output_module.np = function(channel_data, output_id)
    local output_data = channel_data.output[output_id]
    if output_data.timeout then
        output_data.timeout:close()
        output_data.timeout = nil
    end
    if output_data.request then
        output_data.request:close()
        output_data.request = nil
    end
end

--   oooooooo8 ooooo ooooo      o      oooo   oooo oooo   oooo ooooooooooo ooooo
-- o888     88  888   888      888      8888o  88   8888o  88   888    88   888
-- 888          888ooo888     8  88     88 888o88   88 888o88   888ooo8     888
-- 888o     oo  888   888    8oooo88    88   8888   88   8888   888    oo   888      o
--  888oooo88  o888o o888o o88o  o888o o88o    88  o88o    88  o888ooo8888 o888ooooo88

channel_list = {}

function make_channel(channel_config)
    if not channel_config.name then
        log.error("[make_channel] option 'name' is required")
        return nil
    end

    if not channel_config.input or #channel_config.input == 0 then
        log.error("[" .. channel_config.name .. "] option 'input' is required")
        return nil
    end

    if channel_config.output == nil then channel_config.output = {} end
    if channel_config.timeout == nil then channel_config.timeout = 0 end
    if channel_config.enable == nil then channel_config.enable = true end

    if channel_config.enable == false then
        log.info("[" .. channel_config.name .. "] channel is disabled")
        return nil
    end

    local channel_data = {
        config = channel_config,
        input = {},
        output = {},
        delay = 3,
        clients = 0,
    }

    local function check_url_format(obj)
        local url_list = channel_config[obj]
        local config_list = channel_data[obj]
        local module_list = _G["init_" .. obj .. "_module"]
        local function check_module(config)
            if not config then return false end
            if not config.format then return false end
            if not module_list[config.format] then return false end
            return true
        end
        for n, url in ipairs(url_list) do
            local item = {}
            if type(url) == "string" then
                item.config = parse_url(url)
            elseif type(url) == "table" then
                if url.url then
                    local u = parse_url(url.url)
                    for k,v in pairs(u) do url[k] = v end
                end
                item.config = url
            end
            if not check_module(item.config) then
                log.error("[" .. channel_config.name .. "] wrong " .. obj .. " #" .. n .. " format")
                return false
            end
            item.config.name = channel_config.name .. " #" .. n
            table.insert(config_list, item)
        end
        return true
    end

    if not check_url_format("input") then return nil end
    if not check_url_format("output") then return nil end

    if channel_config.map then
        local o = channel_config.map
        if type(o) == "string" then o = o:gsub("%s+", ""):split(",") end
        if type(o) ~= "table" then
            log.error("[" .. channel_config.name .. "] option 'map' has wrong format")
            astra.exit()
        end

        local map = {}
        for _,v in ipairs(o) do table.insert(map, v:split("=")) end
        for _,v in ipairs(channel_data.input) do
            if v.config.map then
                for _,vv in ipairs(map) do table.insert(v.config.map, vv) end
            else
                v.config.map = map
            end
        end
    end

    if channel_config.set_pnr then
        for _,v in ipairs(channel_data.input) do
            if v.config.set_pnr == nil then v.config.set_pnr = channel_config.set_pnr end
        end
    end

    if #channel_data.output == 0 then
        channel_data.clients = 1
    else
        for _, o in pairs(channel_data.output) do
            if o.config.format ~= "http" or o.config.keep_active == true then
                channel_data.clients = channel_data.clients + 1
            end
        end
    end

    channel_data.active_input_id = 0
    channel_data.transmit = transmit()
    channel_data.tail = channel_data.transmit

    if channel_data.clients > 0 then
        channel_init_input(channel_data, 1)
    end

    for output_id in ipairs(channel_data.output) do
        channel_init_output(channel_data, output_id)
    end

    table.insert(channel_list, channel_data)
    return channel_data
end

function kill_channel(channel_data)
    if not channel_data then return nil end

    local channel_id = 0
    for key, value in pairs(channel_list) do
        if value == channel_data then
            channel_id = key
            break
        end
    end

    if channel_id == 0 then
        log.error("[kill_channel] channel is not found")
        return nil
    end

    while #channel_data.input > 0 do
        channel_kill_input(channel_data, 1)
        table.remove(channel_data.input, 1)
    end
    channel_data.input = nil

    while #channel_data.output > 0 do
        channel_kill_output(channel_data, 1)
        table.remove(channel_data.output, 1)
    end
    channel_data.output = nil

    channel_data.tail = nil
    channel_data.transmit = nil
    channel_data.config = nil

    table.remove(channel_list, channel_id)
    collectgarbage()
end

function find_channel(key, value)
    for _, channel_data in pairs(channel_list) do
        if channel_data.config[key] == value then
            return channel_data
        end
    end
    return nil
end

--  oooooooo8 ooooooooooo oooooooooo  ooooooooooo      o      oooo     oooo
-- 888        88  888  88  888    888  888    88      888      8888o   888
--  888oooooo     888      888oooo88   888ooo8       8  88     88 888o8 88
--         888    888      888  88o    888    oo    8oooo88    88  888  88
-- o88oooo888    o888o    o888o  88o8 o888ooo8888 o88o  o888o o88o  8  o88o

options_usage = [[
    FILE                Astra script
]]

options = {
    ["*"] = function(idx)
        local filename = argv[idx]
        if utils.stat(filename).type == "file" then
            dofile(filename)
            return 0
        end
        return -1
    end,
}

function main()
    log.info("Starting Astra " .. astra.version)
end
