server = nil
-- ----------------------------------------------------------------------------------
-- VARS

xhttpserver_addr = "127.0.0.1"
xhttpserver_port = 554
rtsp_addr = "0.0.0.0"
rtsp_port = 554
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

function get_session(headers)
    for _,h in pairs(headers) do
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

function get_session_data(client, data)
    local session = get_session(data.headers)
    if not session then
        log.error("Session id not found it request")
        send_405(client)
        return nil
    end
    
    local session_data = rtsp_sessions[session] 
    if not session_data then
        log.error("Session not found by id " .. session)
        send_405(client)
        return nil
    end
    return session_data
end
-- ----------------------------------------------------------------------------------
-- RTSP Methods 

-- 
-- proxy method
--

function rtsp_method_proxy(client, data, cb_pretable)
    local client_data = server:data(client)
    client_data.resp = {}
    client_data.resp.content = ""
    client_data.resp.bytes_to_close = 0
    table.insert(data.headers, "x-httprtp: true")
    client_data.req = http_request({
        addr = xhttpserver_addr,
        port = xhttpserver_port,
        method = data.method,
        uri = data.uri,
        version = data.version,
        headers = data.headers,
        callback = 
            function(req, resp_data)
                if type(resp_data) == 'table' then
                    if resp_data.code == 0 then
                        log.error("Remote XHTTPRTSP Server request failed: " .. resp_data.message)
                        server:close(client)
                        req:close()
                        client_data.req = nil
                        return
                    end
                    if (cb_pretable) then
                        resp_data = cb_pretable(client, resp_data)
                    end
                    log.debug("PX Response: " .. resp_data.code .. "  " .. resp_data.message)
                    for _,h in pairs(resp_data.headers) do log.debug("PX    " .. h) end
                    server:send(client, {
                            version = rtsp_version,
                            code = resp_data.code,
                            message = resp_data.message,
                            headers = resp_data.headers
                    })
                    for _,h in pairs(resp_data.headers) do
                        local len = h:lower():match("content%-length: (%d+)") 
                        if len then
                            client_data.resp.bytes_to_close = tonumber(len)
                        end
                    end
                elseif type(resp_data) == 'string' then
                    log.debug("PX content... ") --  .. resp_data)
                    server:send(client, resp_data)
                    client_data.resp.bytes_to_close = client_data.resp.bytes_to_close - #resp_data
                elseif type(resp_data) == 'nil' then
                    client_data.resp.bytes_to_close = 0
                end
                log.debug("Toclose: " .. client_data.resp.bytes_to_close)
                if client_data.resp.bytes_to_close <= 0 then
                    req:close()
                    client_data.req = nil
                end
            end
        })
end

-- 
-- SETUP
--

function rtsp_method_setup(client, data, cb_unused)
    
    rtsp_method_proxy(client, data,
    
        function(client2, resp_data)
            -- create new session
            local session_data = {}    
            
            session_data.src = {}
            session_data.dst = {}
            session_data.session = get_session(resp_data.headers)

            rtsp_sessions[session_data.session] = session_data

            log.debug("Setup done for session " .. session_data.session)

            local cseq = get_cseq(data)

            local transport = get_transport(data)
            if transport.destination and dst_source == false then
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

            resp_data.headers = {
                rtsp_server,
                "CSeq: " .. tostring(cseq),
                "Session: " .. tostring(session_data.session),
                tstring
            }

            session_data.src.instance = http_request({
                addr = xhttpserver_addr, 
                port = xhttpserver_port, 
                method = "GETSTREAM",
                uri = data.uri,
                version = data.version,
                headers = {
                    rtsp_server, 
                    "Session: " .. tostring(session_data.session)
                    },
                ts = true,
                callback = function(self, data)
                        if type(data) == 'nil' then
                            self:close()
                        end
                    end
            })
            
            session_data.dst.instance = udp_output({
                upstream = session_data.src.instance:stream(),
                addr = session_data.dst.addr,
                port = session_data.dst.port,
                ttl = 32,
                rtp = is_rtp
            })
            
            return resp_data
        end
    )
end

-- 
-- TEARDOWN
--

function rtsp_method_teardown(client, data, cb_unused)
    local session_data = get_session_data(client, data)
    if not session_data then
        return
    end
    
    if session_data.src.instance then
        session_data.src.instance:close()
    end
    session_data.src.instance = nil
    session_data.dst.instance = nil
    rtsp_sessions[session_data.session] = nil

    log.debug("Destroy session " .. session_data.session)

    collectgarbage()
    
    rtsp_method_proxy(client, data, cb_unused)
end

-- --------------------------------------
-- Processing HTTP callback
--

rtsp_methods = {
    ["OPTIONS"] = rtsp_method_proxy,
    ["DESCRIBE"] = rtsp_method_proxy,
    ["SETUP"] = rtsp_method_setup,
    ["PLAY"] = rtsp_method_proxy,
    ["PAUSE"] = rtsp_method_proxy,
    ["GET_PARAMETER"] = rtsp_method_proxy,
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
            rtsp_methods[data.method](client, data, nil)
        else
            send_405(client)
        end
    elseif type(data) == 'string' then
        log.debug("data received")
        print(data)
    elseif type(data) == 'nil' then
        log.debug("Connection to client closed")
        local client_data = server:data(client)
        if client_data.req then -- need to cleanup ongoing request to upstream server
          client_data.req:close()
          client_data.req = nil
        end
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
          "    -s ADDR             XHTTPRTP server address\n" ..
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



options = {
    ["-h"] = function(i) usage() return i + 1 end,
    ["-v"] = function(i) version() return i + 1 end,
    ["-a"] = function(i) rtsp_addr = argv[i + 1] return i + 2 end,
    ["-p"] = function(i) rtsp_port = tonumber(argv[i + 1]) return i + 2 end,
    ["-l"] = function(i) localaddr = argv[i + 1] return i + 2 end,
    ["-c"] = function(i) client_addr = argv[i + 1] return i + 2 end,
    ["-s"] = function(i) xhttpserver_addr  = argv[i + 1] return i + 2 end,
    ["--sp"] = function(i) xhttpserver_port = argv[i + 1] return i + 2 end,
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
