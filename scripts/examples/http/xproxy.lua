#!/usr/bin/env astra

function split(s,d)
    local p = 1
    local t = {}
    while true do
        b = s:find(d,p)
        if not b then table.insert(t, s:sub(p)) return t end
        table.insert(t, s:sub(p,b - 1))
        p = b + 1
    end
end

server_header = "Server: xproxy"

function send_404(self, client)
    local content = "<html>" ..
                    "<center><h1>Not Found</h1></center>" ..
                    "<hr />" ..
                    "<small>Astra</small>" ..
                    "</html>"
    self:send(client, {
        code = 404,
        message = "Not Found",
        headers = {
            server_header,
            "Content-Type: text/html; charset=utf-8",
            "Content-Length: " .. #content,
            "Connection: close",
        },
        content = content
    })
end

localaddr = nil -- for -l option

function on_http_read(self, client, data)
    local client_data = self:data(client)

    if type(data) == 'table' then
        -- connected
        if data.message then
            log.error("[xproxy.lua] " .. data.message)
            self:close()
            return
        end

        local u = split(data.uri, "/") --> { "", "udp", "239.255.1.1:1234" }
        if #u < 3 or u[2] ~= 'udp' then
            send_404(self)
            return
        end

        local a = split(u[3], ":") --> { "239.255.1.1", "1234" }
        local udp_input_conf = { addr = a[1], port = 1234 }
        if #a == 2 then udp_input_conf.port = tonumber(a[2]) end
        if localaddr then udp_input_conf.localaddr = localaddr end

        client_data.input = udp_input(udp_input_conf)
        self:send(client, {
            code = 200,
            message = "OK",
            headers = {
                server_header,
                "Content-Type:application/octet-stream",
            },
            upstream = client_data.input:stream()
        })
    elseif type(data) == 'nil' then
        -- close connection
        client_data.input = nil
        collectgarbage()
    end
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

i = 1
while i <= #argv do
    if not options[argv[i]] then
        print("unknown option: " .. argv[i])
        usage()
        astra.exit()
    end
    i = options[argv[i]](i)
end

http_server({
    addr = http_addr,
    port = http_port,
    callback = on_http_read
})
