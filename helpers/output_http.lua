
function parse_url(url)
    -- proto
    local proto = "http"
    local so, eo = url:find("://")
    if eo then
        proto = url:sub(0, so - 1)
        url = url:sub(eo + 1)
    end

    -- uri
    local uri = "/"
    local so, eo = url:find("/")
    if eo then
        uri = url:sub(so)
        url = url:sub(0, so - 1)
    end

    -- port
    local port = 80
    local so, eo = url:find(":")
    if eo then
        port = tonumber(url:sub(eo + 1))
        url = url:sub(0, so - 1)
    end

    return {
        proto = proto,
        host = url,
        port = port,
        uri = uri
    }
end

local server_list = {}
local server_header = "Server: Astra [output_http.lua]"

function send_404(self)
    local content = "<html>" ..
                    "<center><h1>Not Found</h1></center>" ..
                    "<hr />" ..
                    "<small>Astra</small>" ..
                    "</html>"
    self:send({
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

function send_playlist(self)
    local content = "[playlist]"
    local id = 1
    for addr,server in pairs(server_list) do
        for uri,channel in pairs(server.channels) do
            content = content ..
                      "\nFile" .. id .. "=http://" .. addr .. uri ..
                      "\nTitle" .. id .. "=" .. channel.name ..
                      "\nLength" .. id .. "=-1"
            id = id + 1
        end
    end
    content = content ..
              "\nNumberOfEntries=" .. tostring(id - 1) ..
              "\nVersion=2\n"
    self:send({
        code = 200,
        message = "Ok",
        headers = {
            server_header,
            "Content-Type: audio/x-scpls",
            "Content-Length: " .. #content,
            "Connection: close",
        },
        content = content
    })
end

function send_200(self)
    self:send({
        code = 200,
        message = "OK",
        headers = {
            server_header,
            "Content-Type:application/octet-stream",
        }
    })
end

function instance_cb(self, data, instance)
    if type(data) == 'table' then
        if data.message then
            log.error("[output_http.lua] " .. data.message)
            self:close()
            return
        end
        -- check channel
        local channel = instance.channels[data.uri]
        if not channel then
            if data.uri == "/playlist.pls" then
                send_playlist(self)
            else
                send_404(self)
            end
            self:close()
            return
        end
        -- start channel
        send_200(self)
        instance.clients[self] = channel
        channel.parent:attach(self)
    elseif type(data) == 'string' then
        -- do nothing
    else
        -- close connection
        local channel = instance.clients[self]
        if channel then
            channel.parent:detach(self)
            instance.clients[self] = nil
        end
    end
end

function output_list.http(output)
    local url = parse_url(output.dst)

    local addr = url.host .. ":" .. tostring(url.port)
    local server = server_list[addr]
    if not server then
        server = {}
        server_list[addr] = server
        server.clients = {}
        server.channels = {}
        server.instance = http_server({
            addr = url.host,
            port = url.port,
            callback = function(self, data) instance_cb(self, data, server) end
        })
    end

    if server.channels[url.uri] then
        log.error("[output_http.lua] channel " .. url.uri ..
                  " already defined")
        return
    end

    local channel = {}
    server.channels[url.uri] = channel
    channel.parent = output.modules[1]
    channel.name = output.config.name
end
