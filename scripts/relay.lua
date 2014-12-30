-- Astra Relay
-- https://cesbo.com/astra/
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

function xproxy_init_client(server, client, request, path)
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

    if request.query then
        local query_path = ""
        for k,v in pairs(request.query) do
            query_path = query_path .. "&" .. k .. "=" .. v
        end
        path = path .. "?" .. query_path:sub(2)
    end

    client_list[client_id] = {
        client = client,
        addr = client_addr,
        path = path,
        st   = os.time(),
    }

    client_data.client_id = client_id
end

function xproxy_kill_client(server, client)
    local client_data = server:data(client)
    if not client_data.client_id then return nil end

    client_list[client_data.client_id] = nil
    client_data.client_id = nil
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
    <title>Astra Relay : Statistics</title>
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
                <td class="brand">Astra Relay</td>
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

function on_request_stat(server, client, request)
    if not request then return nil end

    if request.query then
        if request.query.close then
            local client_id = tonumber(request.query.close)
            if client_list[client_id] then
                server:close(client_list[client_id].client)
            end
            server:redirect(client, "/stat/")
            return nil
        end
    end

    if xproxy_pass then
        if request.headers['authorization'] ~= xproxy_pass then
            server:send(client, {
                code = 401,
                headers = {
                    "WWW-Authenticate: Basic realm=\"Astra Relay\"",
                    "Content-Length: 0",
                    "Connection: close",
                }
            })
            return nil
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

function on_request_playlist(server, client, request)
    if not request then return nil end

    if not playlist_request then
        server:abort(client, 404)
        return nil
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

function on_request_udp(server, client, request)
    local client_data = server:data(client)

    if not request then -- on_close
        kill_input(client_data.input)
        xproxy_kill_client(server, client)
        collectgarbage()
        return nil
    end

    local format = request.path:sub(2, 4)
    local url = format .. "://" .. request.path:sub(6)
    local conf = parse_url(url)
    if not conf then
        server:abort(client, 404)
        return nil
    end

    xproxy_init_client(server, client, request, url)

    local allow_channel = function()
        conf.name = "Relay " .. client_data.client_id
        conf.socket_size = 0x80000
        if localaddr then conf.localaddr = localaddr end
        client_data.input = init_input(conf)
        server:send(client, client_data.input.tail:stream())
    end

    allow_channel()
end

-- ooooo ooooo ooooooooooo ooooooooooo oooooooooo
--  888   888  88  888  88 88  888  88  888    888
--  888ooo888      888         888      888oooo88
--  888   888      888         888      888
-- o888o o888o    o888o       o888o    o888o

function on_request_http(server, client, request)
    local client_data = server:data(client)

    if not request then -- on_close
        kill_input(client_data.input)
        xproxy_kill_client(server, client)
        collectgarbage()
        return nil
    end

    local url = "http://" .. request.path:sub(7)
    local conf = parse_url(url)
    if not conf then
        server:abort(client, 404)
        return nil
    end

    xproxy_init_client(server, client, request, url)

    local allow_channel = function()
        conf.name = "Relay " .. client_data.client_id
        client_data.input = init_input(conf)
        server:send(client, client_data.input.tail:stream())
    end

    allow_channel()
end

--  oo    oo
--   88oo88
-- o88888888o
--   oo88oo
--  o88  88o

function on_request_channel(server, client, request)
    local client_data = server:data(client)

    if not request then -- on_close
        kill_input(client_data.input)
        xproxy_kill_client(server, client)
        collectgarbage()
        return nil
    end

    if not channels then
        server:abort(client, 404)
        return nil
    end

    local path = request.path:sub(2) -- skip '/'
    local channel = channels[path]

    if not channel then
        channel = channels["*"]
    end

    if not channel then
        server:abort(client, 404)
        return nil
    end

    local conf = parse_url(channel)
    if not conf then
        server:abort(client, 404)
        return nil
    end

    xproxy_init_client(server, client, request, request.path)

    local allow_channel = function()
        conf.name = "Relay " .. client_data.client_id
        client_data.input = init_input(conf)
        server:send(client, client_data.input.tail:stream())
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

xproxy_buffer_size = nil
xproxy_buffer_fill = nil

xproxy_allow_udp = true
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
    --buffer-size       buffer size in Kb (default: 1024)
    --buffer-fill       minimal packet size in Kb (default: 128)
    --no-udp            disable direct access the to UDP/RTP source
    --no-http           disable direct access the to HTTP source
    --pass              basic authentication for statistics. login:password
    FILE                full path to the Lua-script
]]

options = {
    ["-a"] = function(idx)
        xproxy_addr = argv[idx + 1]
        return 1
    end,
    ["-p"] = function(idx)
        xproxy_port = tonumber(argv[idx + 1])
        if not xproxy_port then
            log.error("[Relay] wrong port value")
            astra.abort()
        end
        return 1
    end,
    ["-l"] = function(idx)
        localaddr = argv[idx + 1]
        return 1
    end,
    ["--buffer-size"] =  function(idx)
        xproxy_buffer_size = tonumber(argv[idx + 1])
        return 1
    end,
    ["--buffer-fill"] =  function(idx)
        xproxy_buffer_fill = tonumber(argv[idx + 1])
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
        log.error("--no-rtp option is deprecated")
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
    if argv[1] == "--xproxy" then
        log.error("--xproxy option is deprecated. use --relay instead")
    end

    log.info("Starting Astra " .. astra.version)
    log.info("Astra Relay started on " .. xproxy_addr .. ":" .. xproxy_port)

    local route = {
        { "/stat/", on_request_stat },
        { "/stat", http_redirect({ location = "/stat/" }) },
    }

    local function init_http_upstream(callback)
        return http_upstream({
            callback = callback,
            buffer_size = xproxy_buffer_size,
            buffer_fill = xproxy_buffer_fill,
        })
    end

    if xproxy_allow_udp then
        table.insert(route, { "/udp/*", init_http_upstream(on_request_udp) })
        table.insert(route, { "/rtp/*", init_http_upstream(on_request_udp) })
    end

    if xproxy_allow_http then
        table.insert(route, { "/http/*", init_http_upstream(on_request_http) })
    end

    if playlist_request then
        table.insert(route, { "/playlist*", on_request_playlist })
    end

    if xproxy_route then
        for _,r in ipairs(xproxy_route) do table.insert(route, r) end
    end

    table.insert(route, { "/*", init_http_upstream(on_request_channel) })

    http_server({
        addr = xproxy_addr,
        port = xproxy_port,
        server_name = "Astra Relay",
        route = route
    })
end
