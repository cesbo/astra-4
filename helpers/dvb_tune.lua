#!/usr/bin/env astra

dvb = nil

function dvb_tune_configure_html()
    local dvb_list = ""
    for adapter in utils.readdir("/dev/dvb") do
        local adapter_id = adapter:sub(8, #adapter)
        dvb_list = dvb_list .. "<option value=\"" .. adapter_id .. "\">" .. adapter_id .. "</option>"
    end

    return [[
<html>
<head>
    <title>dvb_tune: setup</title>
</head>
<body>
    <form method="get">
        <input type="hidden" name="action" value="tune" />
        <table border="0">
            <tr><th align="right">Adapter</th><td><select name="adapter">]] .. dvb_list .. [[</select></td></tr>
            <tr><th align="right">Type</th><td><select name="type"><option value="S" selected>S</option><option value="S2">S2</option></select></td></tr>
            <tr><th align="right">Transpoder</th><td><input type="text" name="tp" value="12303:L:27500" /></td></tr>
            <tr><th align="right">LNB</th><td><input type="text" name="lnb" value="10750:10750:10750" /></td></tr>
            <tr><td colspan="2" align="center"><input type="submit" value="Tune" /></td></tr>
        </table>
    </form>
</body>
</html>
]]
end

function dvb_tune_status_html()
    local s = dvb:status()
    local status_list =
        "<tr><th align=\"right\">Lock</th><td>" .. tostring(s.lock) .. "</td></tr>" ..
        "<tr><th align=\"right\">Signal</th><td>" .. tostring(s.signal) .. "</td></tr>" ..
        "<tr><th align=\"right\">SNR</th><td>" .. tostring(s.snr) .. "</td></tr>" ..
        "<tr><th align=\"right\">ber</th><td>" .. tostring(s.ber) .. "</td></tr>" ..
        "<tr><th align=\"right\">unc</th><td>" .. tostring(s.unc) .. "</td></tr>"

    return [[
<html>
<head>
    <title>dvb_tune: status</title>
</head>
<body>
    <table>
        ]] .. status_list .. [[
        <tr>
            <td colspan="2" align="center">
                <form method="get">
                    <input type="hidden" name="action" value="stop" />
                    <input type="submit" value="Stop" />
                </form>
            </td>
        </tr>
    </table>
</body>
</html>
]]
end

server_header = "Server: Astra [dvb_tune.lua]"

function send_404(self)
    local content = "<html>" ..
                    "<center><h1>Not Found</h1></center>" ..
                    "<hr />" ..
                    "<small>Astra [dvb_tune.lua]</small>" ..
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

function send_200(self)
    local content = ""
    if dvb then
        content = dvb_tune_status_html()
    else
        content = dvb_tune_configure_html()
    end
    self:send({
        code = 200,
        message = "OK",
        headers = {
            server_header,
            "Pragma: no-cache",
            "Cache-control: no-cache",
            "Content-Type: text/html; charset=utf-8",
            "Content-Length: " .. #content,
            "Connection: close",
        },
        content = content
    })
end

function send_302(self, where)
    self:send({
        code = 302,
        message = "Found",
        headers = {
            server_header,
            "Location: " .. where,
            "Connection: close",
        }
    })
    self:close()
end

function split(s,d)
    local p = 1
    local t = {}
    while true do
        b = s:find(d, p)
        if not b then table.insert(t, s:sub(p)) return t end
        table.insert(t, s:sub(p, b - 1))
        p = b + 1
    end
end

function headers_get(headers, name)
    local name_l = name:lower()
    for _,val in pairs(headers) do
        local hb,he = val:find(": ")
        if hb then
            local name_hl = (val:sub(1, hb - 1)):lower()
            if name_l == name_hl then
                return val:sub(he + 1, #val)
            end
        end
    end
end

function url_decode(url)
    local p = 1
    local nurl = ""
    while true do
        b = url:find("%%", p)
        if not b then nurl = nurl .. url:sub(p) return nurl end
        nurl = nurl .. url:sub(p, b - 1) ..
               string.char(tonumber("0x".. url:sub(b + 1, b + 2)))
        p = b + 3
    end
    return nurl
end

function parse_args(args)
    local a = split(args, "&")
    local al = {}
    for _, val in pairs(a) do
        local vv = split(val, "=")
        if #vv == 1 then
            vv[2] = ""
        end
        al[vv[1]] = url_decode(vv[2])
    end

    if not al.action then
        return
    elseif al.action == 'stop' then
        dvb = nil
        collectgarbage()
    elseif al.action == 'tune' then
        if dvb then
            dvb = nil
            collectgarbage()
        end
        dvb = dvb_input({
            adapter = al.adapter,
            tp = al.tp,
            lnb = al.lnb,
            type = al.type,
        })
    end
end

function cb(self, data)
    if type(data) == 'table' then
        -- connected
        local target = data.uri:sub(2, 4)
        if target ~= 'dvb' then
            send_404(self)
        else
            local args_b = data.uri:find("?", 4)
            if args_b then
                parse_args(data.uri:sub(args_b + 1, #data.uri))
                local hostaddr = headers_get(data.headers, "host")
                send_302(self, "http://" .. hostaddr .. "/dvb")
            else
                send_200(self)
            end
        end
    end
end

function usage()
    print("Usage: astra dvb_tune.lua [OPTIONS]\n" ..
          "    -h                  help\n" ..
          "    -a ADDR             local addres to listen\n" ..
          "    -p PORT             local port to listen\n" ..
          "    --debug             print debug messages"
          )
    astra.exit()
end

http_addr = "0.0.0.0"
http_port = 8000

options = {
    ["-h"] = function(i) usage() return i + 1 end,
    ["-a"] = function(i) http_addr = argv[i + 1] return i + 2 end,
    ["-p"] = function(i) http_port = tonumber(argv[i + 1]) return i + 2 end,
    ["--debug"] = function (i) log.set({ debug = true }) return i + 1 end,
}

function set_options()
    i = 1
    while i <= #argv do
        if not options[argv[i]] then
            print("option " .. argv[i] .. " isn't found")
            return false
        end
        i = options[argv[i]](i)
    end
    return true
end

function main()
    if not set_options() then
        return
    end
    return http_server({
        addr = http_addr,
        port = http_port,
        callback = cb
    })
end

s = main()
