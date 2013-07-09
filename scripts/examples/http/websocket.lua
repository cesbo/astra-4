
local server_addr = "127.1"
local server_port = 5000

local index_html =
[[<!doctype html>
<html>
<head>
<script src="http://ajax.googleapis.com/ajax/libs/jquery/2.0.1/jquery.min.js"></script>
<style>
body {
    font-family: monospace;
}
hr {
    background-color: #0074c8;
    height: 1px;
    border: 0px;
}
#log {
    border: 1px solid #0074c8;
    font-family: monospace;
}
.buttons>input {
    padding-right: 10px;
}
</style>
</head>
<body>
<h1>Astra WebSocket test</h1>
<hr>
<div class="buttons">
<input type="button" value="Start Timer" id="start_timer" />
<input type="button" value="Stop Timer" id="stop_timer" />
</div>
<textarea id="log" rows="24" cols="80"></textarea>
</body>
<script>
$log = $('#log');
$log.append("Astra WebSocket test is started...\n");

ws = new WebSocket("ws://]] .. server_addr .. ":" .. server_port .. [[/api");
ws.onopen = function(evt) { $log.append("Connection established\n"); };
ws.onmessage = function(evt) { $log.append("Received: " + evt.data + "\n"); };
ws.onclose = function(evt) { $log.append("Connection closed\n"); };
ws.onerror = function(evt) {
    $log.append("Error. See console for more information\n")
    console.log("WebSocket Error: ", evt);
};

$('#start_timer').click(function(obj) {
    ws.send("Start Timer");
});

$('#stop_timer').click(function(obj) {
    ws.send("Stop Timer");
});
</script>
</html>
]]

local server_name = "Server: Astra WebSocket test"

function dump_table(t, i)
    if not i then i = "" end
    for var, val in pairs(t) do
        if type(val) == 'table' then
            print(i .. var .. ":")
            dump_table(val, i .. "    ")
        else
            print(i .. var .. ": " .. tostring(val))
        end
    end
end

function on_server_data(self, client, data)
    if type(data) == "table" then
        -- dump_table(data)

        if data.message then
            log.error("[websock.lua] " .. data.message)
            self:close(client)
            return
        end

        if data.method == "GET" then
            if data.uri == "/" then
                self:send(client, {
                    code = 200,
                    message = "OK",
                    headers =
                    {
                        server_name,
                        "Content-Type: text/html",
                        "Content-Length: " .. #index_html
                    },
                    content = index_html
                })

            elseif data.uri == "/api" then
                local headers =
                {
                    server_name,
                    "Upgrade: websocket",
                    "Connection: Upgrade",
                }

                function get_ws_key(headers)
                    for _, header in pairs(data.headers) do
                        local ws_key = header:match("^Sec%-WebSocket%-Key: ([A-z0-9=+/]+)$")
                        if ws_key then return ws_key end
                    end
                    return nil
                end
                local ws_key = get_ws_key(data.headers)
                if ws_key then
                    local ws_accept_str = ws_key .. "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
                    local ws_accept = utils.base64_encode(utils.sha1(ws_accept_str))
                    table.insert(headers, "Sec-WebSocket-Accept: " .. ws_accept)
                end

                self:send(client, {
                    code = 101,
                    message = "Switching Protocols",
                    headers = headers
                })
            end
        else
            log.error("[websock.lua] method " .. data.method .. " is not supported")
            self:close(client)
        end
    elseif type(data) == "string" then
        local client_data = self:data(client)
        if data == "Start Timer" then
            client_data.counter = 0
            client_data.timer = timer({
                interval = 1,
                callback = function()
                        client_data.counter = client_data.counter + 1
                        self:send(client, "Counter: " .. client_data.counter)
                    end
            })
        elseif data == "Stop Timer" then
            client_data.timer:close()
            client_data.timer = nil
            collectgarbage()
        end
    else
        collectgarbage()
    end
end

http_server({
    addr = server_addr,
    port = server_port,
    callback = on_server_data
})
