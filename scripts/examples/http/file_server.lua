#!/usr/bin/env astra

mime_types =
{
    ["html"] = "text/html",
    ["js"]   = "application/javascript",
    ["gif"]  = "image/gif",
    ["png"]  = "image/png",
    ["jpeg"] = "image/jpeg",
    ["jpg"]  = "image/jpeg",
    ["css"]  = "text/css"
}

function get_mime_type(uri)
    local ext = string.match(uri, ".+%.(%a+)$")
    if ext then
        local mime = mime_list[ext]
        if mime then
            return mime_list[ext]
        end
    end
    return "text/plain"
end

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
            "Server: Astra",
            "Content-Type: text/html; charset=utf-8",
            "Content-Length: " .. #content,
        },
        content = content
    })
end

function on_http_read(self, client, data)
    local client_data = self:data(client)

    if type(data) == 'table' then
        client_data.file = io.open("." .. data.uri, "rb")

        if not client_data.file then
            send_404(self, client)
            self:close(client)
            return
        end

        local file_size = client_data.file:seek("end")
        client_data.file:seek("set")

        self:send(client, {
            headers = {
                "Server: Astra",
                "Content-Type: " .. get_mime_type(data.uri),
                "Content-Length: " .. file_size,
            },
            file = client_data.file
        })
    elseif type(data) == 'nil' then
        if client_data.file then client_data.file:close() end
    end
end

http_server({
    addr = "127.0.0.1",
    port = 5000,
    callback = on_http_read
})
