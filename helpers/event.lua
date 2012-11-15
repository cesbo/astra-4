
local hostname = utils.hostname()

local json = nil
if event_request then json = require("json") end

function send_json(info)
    local content = json.encode(info)

    local send_json_callback = function(self, data)
        if type(data) == 'table' and data.code ~= 200 then
            log.error(("[event.lua] send_json() failed %d:%s")
                      :format(data.code, data.message))
        end
    end

    local http_request_config = {
        host = event_request.host,
        uri = "/",
        port = 80,
        method = "POST",
        headers = {
            "User-Agent: Astra " .. astra.version(),
            "Host: " .. event_request.host,
            "Content-Type: application/jsonrequest",
            "Content-Length: " .. #content,
            "Connection: close"
        },
        callback = send_json_callback,
        content = content
    }

    if event_request.uri then
        http_request_config.uri = event_request.uri
    end
    if event_request.port then
        http_request_config.port = event_request.port
    end
    if event_request.method then
        http_request_config.method = event_request.method
    end

    http_request(http_request_config)
end

local module_event_list = {}

function module_event_list.analyze(ch, stat)
    local info = {
        type = 'channel',
        server = hostname,
        stream = ch.stream.config.name,
        channel = ch.config.name,
        output = ch.config.output[1],
        ready = stat.ready,
        bitrate = stat.bitrate,
        scrambled = stat.scrambled,
        pes_error = 0,
        cc_error = 0,
    }

    if ch.last_pes_error ~= stat.pes_error then
        info.pes_error = stat.pes_error - ch.last_pes_error
        ch.last_pes_error = stat.pes_error
    end

    if ch.last_cc_error ~= stat.cc_error then
        info.cc_error = stat.cc_error - ch.last_cc_error
        ch.last_cc_error = stat.cc_error
    end

    send_json(info)
end

function module_event_list.decrypt(ch, stat)
    send_json({
        type = 'decrypt',
        server = hostname,
        stream = ch.stream.config.name,
        channel = ch.config.name,
        output = ch.config.output[1],
        keys = stat.keys,
        cam = stat.cam,
    })
end

function module_event_list.dvb_input(stream, stat)
    send_json({
        type = 'dvb',
        server = hostname,
        stream = stream.config.name,
        adapter = stat.adapter,
        bitrate = stat.bitrate,
        lock = stat.lock,
        unc = stat.unc,
        ber = stat.ber,
        snr = stat.snr,
    })
end

function stream_event(obj, ch, stat)
    if not json then return end
    if not stat then stat = obj:status() end
    local obj_name = tostring(obj)
    module_event_list[obj_name](ch, stat)
end

function heartbeat()
    for _, s in pairs(stream_list) do
        if s.input and s.event then s.event(s.input) end
        for _, c in pairs(s.channels) do
            if c.event then
                if c.analyze then c.event(c.analyze) end
                if c.decrypt then c.event(c.decrypt) end
            end
        end
    end
end

function start_event_time()
    if not event_request then return nil end
    if not json then return nil end
    if not event_request.interval then event_request.interval = 30 end
    return timer({
        interval = event_request.interval,
        callback = heartbeat,
    })
end

stat_timer = start_event_time()
