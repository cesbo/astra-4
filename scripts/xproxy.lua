#!/usr/bin/env astra

client_list = {}
localaddr = nil -- for -l option

function render_stat_html()
    local table_content = ""
    local i = 1
    for _, client_stat in pairs(client_list) do
        table_content = table_content .. "<tr>" ..
                        "<td>" .. i .. "</td>" ..
                        "<td>" .. client_stat.addr .. "</td>" ..
                        "<td>" .. client_stat.port .. "</td>" ..
                        "<td>" .. client_stat.path .. "</td>" ..
                        "</tr>"
        i = i + 1
    end

    return [[<!DOCTYPE html>

<html lang="en">
<head>
    <meta charset="utf-8" />
    <title>XProxy: stat</title>
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
                <th>Addr</th>
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
        if client_data.input then
            client_list[client_data] = nil
            client_data.input = nil
            collectgarbage()
        end
        return
    end

    local path = request.path:sub(6) -- skip '/udp/'

    client_list[client_data] = {
        addr = request['addr'],
        port = request['port'],
        path = path,
    }

    local udp_input_conf = { socket_size = 0x80000 }

    local b = path:find("?")
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

    client_data.input = udp_input(udp_input_conf)

    server:send(client, client_data.input:stream())
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
        { "/udp/*", http_upstream({ callback = on_http_udp }) },
    }
})

log.info("xProxy started on " .. http_addr .. ":" .. http_port)
