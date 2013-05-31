server = nil
-- ----------------------------------------------------------------------------------
-- VARS

app_version = "v.1"
localaddr = nil -- for -l option
client_addr = nil
is_rtp = false

log.set({ stdout = true })

rtsp_sessions = {}
-- ----------------------------------------------------------------------------------
-- Aux funcs

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

function setup_vars()
    rtsp_version = "RTSP/1.0"
    rtsp_server = "Server: Astra " .. astra.version
    sdp_proto = (is_rtp == true) and "RTP/AVP" or "UDP"
    sdp = "v=0\r\n" ..
          "s=Astra " .. astra.version .. "\r\n" ..
          "c=IN IP4 0.0.0.0\r\n" ..
          "a=control:*\r\n" ..
          "m=video 11000 " .. sdp_proto .. " 33\r\n" ..
          "a=rtpmap:33 MP2T/90000\r\n"
end

function send_400(client)
    log.debug("Response: 400")
    server:send(client, {
        version = rtsp_version,
        code = 400,
        message = "Bad Request",
        headers = {
            rtsp_server,
            "Connection: close"
        }
    })
end

function send_405(client)
    log.debug("Response: 405")
    server:send(client, {
        version = rtsp_version,
        code = 405,
        message = "Method Not Allowed",
        headers = {
            rtsp_server,
            "Connection: close"
        }
    })
end

function send_200(client, headers, content)
    log.debug("Response: 200 OK")
    for _,h in pairs(headers) do log.debug("    " .. h) end

    server:send(client, {
        version = rtsp_version,
        code = 200,
        message = "OK",
        headers = headers,
        content = content,
    })
end

function get_cseq(data)
    for _,h in pairs(data.headers) do
        local cseq = h:match("CSeq: (%d+)")
        if cseq then return tonumber(cseq) end
    end
    return 0
end

function get_session(data)
    for _,h in pairs(data.headers) do
        local sess = h:match("Session: (%d+)")
        if sess then return tonumber(sess) end
    end
    return nil
end


function get_transport(data)
    for _,h in pairs(data.headers) do
        local t = h:match("Transport: (.*)")
        if t then
            return {
                destination = t:match("destination=([%d.]+)"),
                client_port = t:match("client_port=([%d-]+)")
            }
        end
    end
    return {}
end

function play_start(session_data)
    if session_data.is_active then play_stop(session_data) end
    session_data.src.instance = udp_input({
        addr = session_data.src.addr,
        port = session_data.src.port,
        localaddr = localaddr
    })
    session_data.dst.instance = udp_output({
        addr = session_data.dst.addr,
        port = session_data.dst.port,
        ttl = 32,
        rtp = is_rtp,
        upstream = session_data.src.instance:stream()
    })
    session_data.is_active = true
end

function play_stop(session_data)
    if not session_data.is_active then return end
    session_data.src.instance = nil
    session_data.dst.instance = nil
    collectgarbage()
    session_data.is_active = false
end

-- ----------------------------------------------------------------------------------
-- RTSP Methods 

-- 
-- OPTIONS
--

function rtsp_method_options(client, data)
    local cseq = get_cseq(data)
    send_200(client, {
        rtsp_server,
        "CSeq: " .. tostring(cseq),
        "Public: DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE"
    })
end

-- 
-- DESCRIBE
--

function rtsp_method_describe(client, data)
    local cseq = get_cseq(data)

    send_200(client, {
        rtsp_server,
        "CSeq: " .. tostring(cseq),
        "Content-Type: application/sdp",
        "Content-Length: " .. tostring(#sdp)
    }, sdp)
end

-- 
-- SETUP
--

function rtsp_method_setup(client, data)
    -- create new session
    local session_data = {}    
    
    repeat
        session_data.session = math.random(10000000, 99000000)
    until not rtsp_sessions[session_data.session] -- ensure that new random value is unique
    
    
    session_data.src = {}
    session_data.dst = {}
    
    log.debug("Creating session " .. session_data.session)
    
    rtsp_sessions[session_data.session] = session_data
    
    local cseq = get_cseq(data)

    local src, tail = data.uri:match("/udp/([%d:.]+)(.*)")

    if not src then
        send_400(client)
        return
    end

    local so,eo = src:find(":")
    if eo then
        session_data.src.addr = src:sub(0, so - 1)
        session_data.src.port = src:sub(eo + 1)
    else
        session_data.src.addr = src
        session_data.src.port = "1234"
    end

    local transport = get_transport(data)
    if transport.destination then
        session_data.dst.addr = transport.destination
    else
        if client_addr then
            -- get address from -c option
            session_data.dst.addr = client_addr
        else
            -- get address from connection
            session_data.dst.addr = data.addr
        end
    end

    if transport.client_port then
        session_data.dst.port = tonumber(transport.client_port:match("%d+"))
    else
        session_data.dst.port = 11000
    end

    local tstring = "Transport: " .. sdp_proto ..
                    ";unicast;destination=" ..
                    session_data.dst.addr ..
                    ";client_port=" .. tostring(session_data.dst.port)

    send_200(client, {
        rtsp_server,
        "CSeq: " .. tostring(cseq),
        "Session: " .. tostring(session_data.session),
        tstring
    })
end

-- 
-- PLAY
--

function rtsp_method_play(client, data)
    local session = get_session(data)
    if not session then
        log.error("Session id not found it request")
        send_405(client)
        return
    end
    
    local session_data = rtsp_sessions[session] 
    if not session_data then
        log.error("Session not found by id " .. session)
        send_405(client)
        return
    end

    local cseq = get_cseq(data)
    local headers = {
        rtsp_server,
        "CSeq: " .. tostring(cseq),
        "Session: " .. tostring(session_data.session)
    }
    send_200(client, headers)

    play_start(session_data)
end

function rtsp_method_get_parameter(client, data)
    local session = get_session(data)
    if not session then
        log.error("Session id not found it request")
        send_405(client)
        return
    end
    
    local session_data = rtsp_sessions[session] 
    if not session_data then
        log.error("Session not found by id " .. session)
        send_405(client)
        return
    end


    local cseq = get_cseq(data)
    send_200(client, {
        rtsp_server,
        "CSeq: " .. tostring(cseq)
    })
end

-- 
-- TEARDOWN
--

function rtsp_method_teardown(client, data)
    local session = get_session(data)
    if not session then
        log.error("Session id not found it request")
        send_405(client)
        return
    end
    
    local session_data = rtsp_sessions[session] 
    if not session_data then
        log.error("Session not found by id " .. session)
        send_405(client)
        return
    end

    play_stop(session_data)

    local cseq = get_cseq(data)
    send_200(client, {
        rtsp_server,
        "CSeq: " .. tostring(cseq)
    })
    rtsp_sessions[session] = nil

    collectgarbage()
end

-- --------------------------------------
-- Processing HTTP callback
--

rtsp_methods = {
    ["OPTIONS"] = rtsp_method_options,
    ["DESCRIBE"] = rtsp_method_describe,
    ["SETUP"] = rtsp_method_setup,
    ["PLAY"] = rtsp_method_play,
    ["GET_PARAMETER"] = rtsp_method_get_parameter,
    ["TEARDOWN"] = rtsp_method_teardown,
}

function cb(client, data)
     
    if type(data) == 'table' then
        if not data.method then
            log.error("[rtsp.lua] " .. data.message)
            server:close(client)
            return
        end

        log.debug("Request: " .. data.method .. " " .. data.uri)
        for _,h in pairs(data.headers) do log.debug("    " .. h) end

        if rtsp_methods[data.method] then
            rtsp_methods[data.method](client, data)
        else
            send_405(client)
        end
    elseif type(data) == 'string' then
        log.debug("data reseived")
        print(data)
    elseif type(data) == 'nil' then
        collectgarbage()
    end
end

-- --------------------------------------
-- Usage and MAIN
--


function usage()
    print("Usage: rtspproxy [OPTIONS]\n" ..
          "    -h                  help\n" ..
          "    -v                  version\n" ..
          "    -a ADDR             local addres to listen\n" ..
          "    -p PORT             local port to listen\n" ..
          "    -l ADDR             source interface address\n" ..
          "    -c ADDR             client address\n" ..
          "    --rtp               use RTP instead of UDP\n" ..
          "    --debug             print debug messages"
          )
    astra.exit()
end

function version()
    print("rtspproxy: " .. app_version ..
          " [Astra: " .. astra.version .. "]")
    astra.exit()
end

rtsp_addr = "0.0.0.0"
rtsp_port = 554

options = {
    ["-h"] = function(i) usage() return i + 1 end,
    ["-v"] = function(i) version() return i + 1 end,
    ["-a"] = function(i) rtsp_addr = argv[i + 1] return i + 2 end,
    ["-p"] = function(i) rtsp_port = tonumber(argv[i + 1]) return i + 2 end,
    ["-l"] = function(i) localaddr = argv[i + 1] return i + 2 end,
    ["-c"] = function(i) client_addr = argv[i + 1] return i + 2 end,
    ["--rtp"] = function(i) is_rtp = true return i + 1 end,
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
    setup_vars()
    return http_server({
        addr = rtsp_addr,
        port = rtsp_port,
        callback = cb
    })
end

server = main()
