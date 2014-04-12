
local server_addr = "127.0.0.1"
local server_port = 8000

mime =
{
    ["html"] = "text/html",
    ["js"]   = "application/javascript",
    ["gif"]  = "image/gif",
    ["png"]  = "image/png",
    ["jpeg"] = "image/jpeg",
    ["jpg"]  = "image/jpeg",
    ["css"]  = "text/css",
}

http_server({
    addr = server_addr,
    port = server_port,
    route = {
        { "/*", http_static({ path = "." }) }
    }
})
