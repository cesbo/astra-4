
local server_addr = "127.0.0.1"
local server_port = 8000

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

function http_callback_root(server, client, request)
    if not request then return nil end

    server:send(client, {
        code = 200,
        headers =
        {
            "Content-Type: text/html",
            "Connection: close",
        },
        content = index_html,
    })
end

function http_callback_api(server, client, request)
    print(request)
    local client_data = server:data(client)
    if request == "Start Timer" then
        client_data.counter = 0
        client_data.timer = timer({
            interval = 1,
            callback = function()
                    client_data.counter = client_data.counter + 1
                    server:send(client, "Counter: " .. client_data.counter)
                end
        })
    elseif request == "Stop Timer" then
        client_data.timer:close()
        client_data.timer = nil
    end
end

http_server({
    addr = server_addr,
    port = server_port,
    name = "WebSocket test",
    route = {
        { "/",      http_callback_root },
        { "/api",   http_websocket({ callback = http_callback_api }) },
    }
})
