
local server_addr = "127.0.0.1"
local server_port = 8000

http_server({
    addr = server_addr,
    port = server_port,
    route = {
        { "/*", http_static({ path = "." }) }
    }
})
