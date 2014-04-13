#!/usr/bin/env astra

client_list = {}
localaddr = nil -- for -l option

udp_input_list = {}

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
                        "<td>" .. client_stat.port .. "</td>" ..
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
    <title>xProxy: stat</title>
    <style type="text/css">
table {
    width: 600px;
    margin: auto;
}
    </style>
</head>
<body>
    <table border="1">
        <thead>
            <tr>
                <th>#</th>
                <th>IP</th>
                <th>Port</th>
                <th>Path</th>
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

function on_http_udp(server, client, request)
    local client_data = server:data(client)

    if not request then -- on_close
        if client_data.client_id then
            local udp_instance = udp_input_list[client_data.input_id]
            udp_instance.clients = udp_instance.clients - 1
            if udp_instance.clients == 0 then
                udp_instance.instance = nil
                udp_input_list[udp_instance_id] = nil
            end
            client_list[client_data.client_id] = nil
            collectgarbage()
        end
        return
    end

    local client_id
    repeat
        client_id = math.random(10000000, 99000000)
    until not client_list[client_id]
    client_data.client_id = client_id

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

    client_list[client_id] = {
        client = client,
        addr = request['addr'],
        port = request['port'],
        path = fpath,
        st   = os.time(),
    }

    local udp_input_conf = { socket_size = 0x80000 }

    -- trim trailing slash
    local b = path:find("/")
    if b then
        path = path:sub(1, b - 1)
    end

    local b = path:find(":")
    if b then
        udp_input_conf.port = tonumber(path:sub(b + 1))
        udp_input_conf.addr = path:sub(1, b - 1)
    else
        udp_input_conf.port = 1234
        udp_input_conf.addr = path
    end

    if localaddr then udp_input_conf.localaddr = localaddr end

    local udp_instance_id = udp_input_conf.addr .. ":" .. udp_input_conf.port
    local udp_instance = udp_input_list[udp_instance_id]
    if not udp_instance then
        udp_instance = {}
        udp_instance.clients = 1
        udp_instance.instance = udp_input(udp_input_conf)
        udp_input_list[udp_instance_id] = udp_instance
    end

    client_data.input_id = udp_instance_id

    udp_instance.clients = udp_instance.clients + 1
    server:send(client, udp_instance.instance:stream())
end

function usage()
    print("Usage: astra xproxy.lua [OPTIONS]\n" ..
          "    -h                  help\n" ..
          "    -a ADDR             local addres to listen\n" ..
          "    -p PORT             local port to listen\n" ..
          "    -l ADDR             source interface address\n" ..
          "    --debug             print debug messages"
          )
    astra.exit()
end

http_addr = "0.0.0.0"
http_port = 8000

options = {
    ["-h"] = function(i) usage() return i + 1 end,
    ["--help"] = function(i) usage() return i + 1 end,
    ["-a"] = function(i) http_addr = argv[i + 1] return i + 2 end,
    ["-p"] = function(i) http_port = tonumber(argv[i + 1]) return i + 2 end,
    ["-l"] = function(i) localaddr = argv[i + 1] return i + 2 end,
    ["--debug"] = function (i) log.set({ debug = true }) return i + 1 end,
}

i = 2
while i <= #argv do
    if not options[argv[i]] then
        print("unknown option: " .. argv[i])
        usage()
    end
    i = options[argv[i]](i)
end

http_server({
    addr = http_addr,
    port = http_port,
    server_name = "xProxy",
    route = {
        { "/stat/", on_http_stat },
        { "/stat", http_redirect({ location = "/stat/" }) },
        { "/udp/*", http_upstream({ callback = on_http_udp }) },
    }
})

log.info("xProxy started on " .. http_addr .. ":" .. http_port)
