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
    local client_data = server:data(client)

    local client_id = client_data.client_id
    if client_id then
        client_list[client_id].path = path
        return
    end

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

    client_data.client_id = client_id
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

    if xproxy_pass then
        if request.headers['authorization'] ~= xproxy_pass then
            server:send(client, {
                code = 401,
                headers = {
                    "WWW-Authenticate: Basic realm=\"xProxy\"",
                    "Content-Length: 0",
                    "Connection: close",
                }
            })
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

-- oooooooooo ooooo            o   ooooo  oooo ooooo       ooooo  oooooooo8 ooooooooooo
--  888    888 888            888    888  88    888         888  888        88  888  88
--  888oooo88  888           8  88     888      888         888   888oooooo     888
--  888        888      o   8oooo88    888      888      o  888          888    888
-- o888o      o888ooooo88 o88o  o888o o888o    o888ooooo88 o888o o88oooo888    o888o

function on_http_playlist(server, client, request)
    if not request then return nil end

    if not playlist_request then
        server:abort(client, 404)
        return
    end

    local playlist_callback = function(content_type, content)
        if content_type then
            server:send(client, {
                code = 200,
                headers = {
                    "Content-Type: " .. content_type,
                    "Connection: close",
                },
                content = content,
            })
        else
            server:abort(client, 404)
        end
    end

    playlist_request(request, playlist_callback)
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
                    collectgarbage()
                end
            end
            client_list[client_data.client_id] = nil
            client_data.client_id = nil
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

    make_client_id(server, client, request, format .. "://" .. fpath)

    local allow_channel = function()
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
                server:abort(client, 404)
                return
            end
            udp_input_conf.addr = path:sub(1, b - 1)
        else
            udp_input_conf.port = 1234
            udp_input_conf.addr = path
        end

        local _,_,o1,o2,o3,o4 = udp_input_conf.addr:find("(%d+)%.(%d+)%.(%d+)%.(%d+)")
        if not o4 then
            server:abort(client, 404)
            return
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

    allow_channel()
end

-- ooooo ooooo ooooooooooo ooooooooooo oooooooooo
--  888   888  88  888  88 88  888  88  888    888
--  888ooo888      888         888      888oooo88
--  888   888      888         888      888
-- o888o o888o    o888o       o888o    o888o

function http_parse_url(url)
    local host, port, path, auth
    local b = url:find("://")
    local p = url:sub(1, b - 1)
    if p ~= "http" then
        return nil
    end
    url = url:sub(b + 3)
    b = url:find("@")
    if b then
        auth = base64.encode(url:sub(1, b - 1))
        url = url:sub(b + 1)
    end
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
    return host, port, path, auth
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

        instance.transmit:set_upstream(instance.request:stream())

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
                instance.clients = instance.clients - 1
                if instance.clients == 0 then
                    if instance.request then
                        instance.request:close()
                        instance.request = nil
                    end
                    if instance.timeout then
                        instance.timeout:close()
                        instance.timeout = nil
                    end
                    instance.transmit = nil
                    instance_list[client_data.input_id] = nil
                    collectgarbage()
                end
            end
            client_list[client_data.client_id] = nil
            client_data.client_id = nil
        end
        return
    end

    local url = "http://" .. request.path:sub(7)
    make_client_id(server, client, request, url)

    local allow_channel = function()
        local http_input_conf = {}
        local host, port, path, auth = http_parse_url(url)

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
            if auth then
                table.insert(http_input_conf.headers, "Authorization: Basic " .. auth)
            end
            http_input_conf.stream = true
            http_input_conf.callback = on_http_http_callback

            instance = {}
            http_input_conf.instance = instance

            instance.clients = 0
            instance.transmit = transmit()
            instance.request = http_request(http_input_conf)

            instance_list[instance_id] = instance
        end

        client_data.input_id = instance_id

        instance.clients = instance.clients + 1

        server:send(client, instance.transmit:stream())
    end

    allow_channel()
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
            client_data.callback(server, client, nil)
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

    local allow_channel = function()
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

    allow_channel()
end

-- oooo     oooo      o      ooooo oooo   oooo
--  8888o   888      888      888   8888o  88
--  88 888o8 88     8  88     888   88 888o88
--  88  888  88    8oooo88    888   88   8888
-- o88o  8  o88o o88o  o888o o888o o88o    88

xproxy_addr = "0.0.0.0"
xproxy_port = 8000

xproxy_allow_udp = true
xproxy_allow_rtp = true
xproxy_allow_http = true

xproxy_pass = nil

xproxy_script = nil

function on_sighup()
    if xproxy_script then dofile(xproxy_script) end
end

options_usage = [[
    -a ADDR             local address to listen
    -p PORT             local port for incoming connections
    -l ADDR             source interface for UDP/RTP streams
    --no-udp            disable direct access the to UDP source
    --no-rtp            disable direct access the to RTP source
    --no-http           disable direct access the to HTTP source
    --pass              basic authentication for statistics. login:password
    FILE                xProxy configuration file
]]

options = {
    ["-a"] = function(idx)
        xproxy_addr = argv[idx + 1]
        return 1
    end,
    ["-p"] = function(idx)
        xproxy_port = tonumber(argv[idx + 1])
        if not xproxy_port then
            log.error("[xProxy] wrong port value")
            astra.abort()
        end
        return 1
    end,
    ["-l"] = function(idx)
        localaddr = argv[idx + 1]
        return 1
    end,
    ["--channels"] = function(idx)
        xproxy_script = argv[idx + 1]
        on_sighup()
        return 1
    end,
    ["--no-udp"] = function(idx)
        xproxy_allow_udp = false
        return 0
    end,
    ["--no-rtp"] = function(idx)
        xproxy_allow_rtp = false
        return 0
    end,
    ["--no-http"] = function(idx)
        xproxy_allow_http = false
        return 0
    end,
    ["--pass"] = function(idx)
        xproxy_pass = "Basic " .. base64.encode(argv[idx + 1])
        return 1
    end,
    ["*"] = function(idx)
        xproxy_script = argv[idx]
        if utils.stat(xproxy_script).type ~= 'file' then
            return -1
        end
        on_sighup()
        return 0
    end,
}

function main()
    log.info("Starting Astra " .. astra.version)
    log.info("xProxy started on " .. xproxy_addr .. ":" .. xproxy_port)

    local route = {
        { "/stat/", on_http_stat },
        { "/stat", http_redirect({ location = "/stat/" }) },
    }

    if xproxy_allow_udp then
        table.insert(route, { "/udp/*", http_upstream({ callback = on_http_udp }) })
    end

    if xproxy_allow_rtp then
        table.insert(route, { "/rtp/*", http_upstream({ callback = on_http_udp }) })
    end

    if xproxy_allow_http then
        table.insert(route, { "/http/*", http_upstream({ callback = on_http_http }) })
    end

    if playlist_request then
        table.insert(route, { "/playlist*", on_http_playlist })
    end

    table.insert(route, { "/*", http_upstream({ callback = on_http_channels }) })

    http_server({
        addr = xproxy_addr,
        port = xproxy_port,
        server_name = "xProxy",
        route = route
    })
end
