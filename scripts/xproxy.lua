#!/usr/bin/astra

-- xProxy
-- https://cesbo.com/solutions/xproxy/
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

client_list = {}
localaddr = nil -- for -l option

instance_list = {}

function make_client_id(server, client, request, path)
    local client_id
    repeat
        client_id = math.random(10000000, 99000000)
    until not client_list[client_id]

    local client_addr = request.headers['x-real-ip']
    if not client_addr then client_addr = request.addr end

    client_list[client_id] = {
        client = client,
        addr = client_addr,
        path = path,
        st   = os.time(),
    }

    return client_id
end

--  oooooooo8 ooooooooooo   o   ooooooooooo
-- 888        88  888  88  888  88  888  88
--  888oooooo     888     8  88     888
--         888    888    8oooo88    888
-- o88oooo888    o888o o88o  o888o o888o

st = os.time()

function render_stat_html()
    local table_content = ""
    local i = 1
    local ct = os.time()
    for client_id, client_stat in pairs(client_list) do
        local dt = ct - client_stat.st
        local uptime = string.format("%02d:%02d", (dt / 3600), (dt / 60) % 60)
        table_content = table_content .. "<tr>" ..
                        "<td>" .. i .. "</td>" ..
                        "<td>" .. client_stat.addr .. "</td>" ..
                        "<td>" .. client_stat.path .. "</td>" ..
                        "<td>" .. uptime .. "</td>" ..
                        "<td><a href=\"/stat/?close=" .. client_id .. "\">Disconnect</a></td>" ..
                        "</tr>\r\n"
        i = i + 1
    end

    return [[<!DOCTYPE html>

<html lang="en">
<head>
    <meta charset="utf-8" />
    <title>xProxy : Statistics</title>
    <style type="text/css">
body { font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif; color: #333333; }
table { width: 600px; margin: auto; }
.brand { text-align: left; font-size: 18px; line-height: 20px; }
.version { text-align: right; font-size: 14px; line-height: 20px; color: #888; }
    </style>
</head>
<body>
    <table border="0">
        <tbody>
            <tr>
                <td class="brand">xProxy</td>
                <td class="version">Astra v.]] .. astra.version .. [[</td>
            </tr>
        </tbody>
    </table>
    <table border="1">
        <thead>
            <tr>
                <th>#</th>
                <th>IP</th>
                <th>Source</th>
                <th>Uptime</th>
                <th></th>
            </tr>
        </thead>
        <tbody>
]] .. table_content .. [[
        </tbody>
    </table>
</body>
</html>
]]
end

function on_http_stat(server, client, request)
    if not request then return nil end

    if request.query then
        if request.query.close then
            local client_id = tonumber(request.query.close)
            if client_list[client_id] then
                server:close(client_list[client_id].client)
            end
            server:redirect(client, "/stat/")
            return
        end
    end

    server:send(client, {
        code = 200,
        headers = {
            "Content-Type: text/html; charset=utf-8",
            "Connection: close",
        },
        content = render_stat_html(),
    })
end

-- ooooo  oooo ooooooooo  oooooooooo
--  888    88   888    88o 888    888
--  888    88   888    888 888oooo88
--  888    88   888    888 888
--   888oo88   o888ooo88  o888o

function on_http_udp(server, client, request)
    local client_data = server:data(client)

    if not request then -- on_close
        if client_data.client_id then
            if client_data.input_id then
                local udp_instance = instance_list[client_data.input_id]
                udp_instance.clients = udp_instance.clients - 1
                if udp_instance.clients == 0 then
                    udp_instance.instance = nil
                    instance_list[client_data.input_id] = nil
                end
            end
            client_list[client_data.client_id] = nil
        end
        return
    end

    local format = request.path:sub(2, 4)
    local path = request.path:sub(6) -- skip '/udp/'

    -- full path
    local fpath = path
    if request.query then
        local i = 0
        for k,v in pairs(request.query) do
            if i == 0 then
                fpath = fpath .. "?" .. k .. "=" .. v
            else
                fpath = fpath .. "&" .. k .. "=" .. v
            end
            i = i + 1
        end
    end

    local client_id = make_client_id(server, client, request, format .. "://" .. fpath)
    client_data.client_id = client_id

    local udp_input_conf = { socket_size = 0x80000 }

    if format == 'rtp' then udp_input_conf.rtp = true end

    -- trim trailing slash
    local b = path:find("/")
    if b then
        path = path:sub(1, b - 1)
    end

    local b = path:find("@")
    if b then
        udp_input_conf.localaddr = path:sub(1, b - 1)
        path = path:sub(b + 1)
    else
        if localaddr then
            udp_input_conf.localaddr = localaddr
        end
    end

    local b = path:find(":")
    if b then
        udp_input_conf.port = tonumber(path:sub(b + 1))
        if not udp_input_conf.port then
            server:abort(client, 400)
            return
        end
        udp_input_conf.addr = path:sub(1, b - 1)
    else
        udp_input_conf.port = 1234
        udp_input_conf.addr = path
    end

    local instance_id = udp_input_conf.addr .. ":" .. udp_input_conf.port
    local udp_instance = instance_list[instance_id]
    if not udp_instance then
        udp_instance = {}
        udp_instance.clients = 0
        udp_instance.instance = udp_input(udp_input_conf)
        instance_list[instance_id] = udp_instance
    end

    client_data.input_id = instance_id

    udp_instance.clients = udp_instance.clients + 1

    server:send(client, udp_instance.instance:stream())
end

-- ooooo ooooo ooooooooooo ooooooooooo oooooooooo
--  888   888  88  888  88 88  888  88  888    888
--  888ooo888      888         888      888oooo88
--  888   888      888         888      888
-- o888o o888o    o888o       o888o    o888o

function http_parse_url(url)
    local host, port , path
    local b = url:find("://")
    local p = url:sub(1, b - 1)
    if p ~= "http" then
        return nil
    end
    url = url:sub(b + 3)
    b = url:find("/")
    if b then
        path = url:sub(b)
        url = url:sub(1, b - 1)
    else
        path = "/"
    end
    b = url:find(":")
    if b then
        port = tonumber(url:sub(b + 1))
        host = url:sub(1, b - 1)
    else
        port = 80
        host = url
    end
    return host, port, path
end

function http_parse_location(self, response)
    if not response.headers then return nil end
    local location = response.headers['location']
    if not location then return nil end

    local host = self.__options.host
    local port = self.__options.port
    local path

    if location:find("://") then
        host, port, path = http_parse_url(location)
    else
        path = location
    end

    return host, port, path
end

function on_http_http_callback(self, response)
    local instance = self.__options.instance
    local http_conf = self.__options

    local timer_conf = {
        interval = 5,
        callback = function(self)
                instance.timeout:close()
                instance.timeout = nil

                if instance.request then instance.request:close() end
                instance.request = http_request(http_conf)

                collectgarbage()
            end
    }

    if not response then
        instance.request:close()
        instance.request = nil
        instance.timeout = timer(timer_conf)

    elseif response.code == 200 then
        if instance.timeout then
            instance.timeout:close()
            instance.timeout = nil
        end

        for _,v in pairs(instance.client_list) do
            local server = v[1]
            local client = v[2]
            local client_data = server:data(client)
            client_data.transmit:set_upstream(instance.request:stream())
        end

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

            log.info("[xProxy] Redirect to http://" .. host .. ":" .. port .. path)
            instance.request = http_request(http_conf)
        else
            log.error("[xProxy] Redirect failed")
            instance.timeout = timer(timer_conf)
        end

    else
        log.error("[xProxy] HTTP Error " .. response.code .. ":" .. response.message)

        instance.request:close()
        instance.request = nil
        instance.timeout = timer(timer_conf)
    end
end

function on_http_http(server, client, request)
    local client_data = server:data(client)

    if not request then -- on_close
        if client_data.client_id then
            if client_data.input_id then
                local instance = instance_list[client_data.input_id]
                instance.client_list[client_data.client_id] = nil
                instance.clients = instance.clients - 1
                if instance.clients == 0 then
                    instance.request:close()
                    instance.request = nil
                    instance_list[client_data.input_id] = nil
                end
            end
            client_list[client_data.client_id] = nil
        end
        return
    end

    local url = "http://" .. request.path:sub(7)

    local client_id = make_client_id(server, client, request, url)
    client_data.client_id = client_id

    local http_input_conf = {}
    local host, port, path = http_parse_url(url)

    if not port then
        server:abort(client, 400)
        return
    end
    http_input_conf.host = host
    http_input_conf.port = port
    http_input_conf.path = path

    local instance_id = host .. ":" .. port .. path
    local instance = instance_list[instance_id]
    if not instance then
        http_input_conf.headers =
        {
            "User-Agent: xProxy",
            "Host: " .. host .. ":" .. port,
            "Connection: close",
        }
        http_input_conf.stream = true
        http_input_conf.callback = on_http_http_callback

        instance = {}
        http_input_conf.instance = instance

        instance.clients = 0
        instance.client_list = {}
        instance.request = http_request(http_input_conf)

        instance_list[instance_id] = instance
    end

    client_data.transmit = transmit()
    client_data.input_id = instance_id

    instance.clients = instance.clients + 1
    instance.client_list[client_id] = { server, client }

    server:send(client, client_data.transmit:stream())
end

--  oo    oo
--   88oo88
-- o88888888o
--   oo88oo
--  o88  88o

function on_http_channels(server, client, request)
    local client_data = server:data(client)

    if not request then -- on_close
        if client_data.callback then
            client_data.callback(server, client, request)
            client_data.callback = nil
        end
        return
    end

    if not channels then
        server:abort(client, 404)
        return
    end

    local path = request.path:sub(2) -- skip '/'
    local channel = channels[path]

    if not channel then
        server:abort(client, 404)
        return
    end

    local b = channel:find("://")
    if not b then
        server:abort(client, 500)
        return
    end

    local proto = channel:sub(1, b - 1)
    local url = channel:sub(b + 3)

    request.path = "/" .. proto .. "/" .. url
    if proto == "udp" or proto == "rtp" then
        client_data.callback = on_http_udp
        on_http_udp(server, client, request)
    elseif proto == "http" then
        client_data.callback = on_http_http
        on_http_http(server, client, request)
    else
        server:abort(client, 404)
    end
end

-- oooo     oooo      o      ooooo oooo   oooo
--  8888o   888      888      888   8888o  88
--  88 888o8 88     8  88     888   88 888o88
--  88  888  88    8oooo88    888   88   8888
-- o88o  8  o88o o88o  o888o o888o o88o    88

function usage()
    print("Usage: astra xproxy.lua [OPTIONS]\n" ..
          "    -h                  help\n" ..
          "    -a ADDR             local addres to listen\n" ..
          "    -p PORT             local port to listen\n" ..
          "    -l ADDR             source interface address\n" ..
          "    --channels FILE     file with the channel names\n" ..
          "    --debug             print debug messages"
          )
    astra.exit()
end

http_addr = "0.0.0.0"
http_port = 8000

options = {
    ["-h"] = function() usage() return 1 end,
    ["--help"] = function() usage() return 1 end,
    ["-a"] = function(i) http_addr = argv[i + 1] return 2 end,
    ["-p"] = function(i) http_port = tonumber(argv[i + 1]) return 2 end,
    ["-l"] = function(i) localaddr = argv[i + 1] return 2 end,
    ["--channels"] = function(i) dofile(argv[i + 1]) return 2 end,
    ["--debug"] = function () log.set({ debug = true }) return 1 end,
}

i = 2
while i <= #argv do
    if not options[argv[i]] then
        print("unknown option: " .. argv[i])
        usage()
    end
    i = i + options[argv[i]](i)
end

http_server({
    addr = http_addr,
    port = http_port,
    server_name = "xProxy",
    route = {
        { "/stat/", on_http_stat },
        { "/stat", http_redirect({ location = "/stat/" }) },
        { "/udp/*", http_upstream({ callback = on_http_udp }) },
        { "/rtp/*", http_upstream({ callback = on_http_udp }) },
        { "/http/*", http_upstream({ callback = on_http_http }) },
        { "/*", http_upstream({ callback = on_http_channels }) },
    }
})

log.info("xProxy started on " .. http_addr .. ":" .. http_port)
