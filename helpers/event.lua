
local hostname = utils.hostname()

json = nil

if event_request then
    json = require("json")
end

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

function module_event_list.analyze(group, stat)
    local info = {}
    info.type = 'channel'
    info.server = hostname
    info.channel = group.event_name
    info.onair = stat.ready
    info.ready = stat.ready
    info.bitrate = stat.bitrate * 1024
    info.scrambled = stat.scrambled

    info.pes_error = 0
    if group.last_pes_error ~= stat.pes_error then
        info.pes_error = stat.pes_error - group.last_pes_error
        group.last_pes_error = stat.pes_error
    end

    info.cc_error = 0
    if group.last_cc_error ~= stat.cc_error then
        info.cc_error = stat.cc_error - group.last_cc_error
        group.last_cc_error = stat.cc_error
    end

    send_json(info)
end

function module_event_list.decrypt(group, stat)
    local info = {}
    info.type = 'decrypt'
    info.server = hostname
    info.channel = group.event_name
    info.keys = stat.keys
    info.cam = stat.cam
    send_json(info)
end

function module_event_list.dvb_input(group, stat)
    local info = {}
    info.type = 'dvb'
    info.server = hostname
    info.adapter = group.config.input.adapter
    info.bitrate = stat.bitrate * 1024
    info.lock = stat.lock
    info.unc = stat.unc
    info.ber = stat.ber
    info.snr = stat.snr

    send_json(info)
end

function stream_event(obj, group, stat)
    if not json then return end
    if not stat then stat = obj:status() end
    local obj_name = tostring(obj)
    module_event_list[obj_name](group, stat)
end

function heartbeat()
    for _, s in pairs(stream_list) do
        if type(s.config.input) == 'table' and s.event then
            s.event(s.input)
        end
        for _, c in pairs(s.channels) do
            if c.event then
                if c.analyze then c.event(c.analyze) end
                if c.decrypt then c.event(c.decrypt) end
            end
        end
    end
end

stat_timer = nil
if event_request then
    if not event_request.interval then event_request.interval = 30 end
    stat_timer = timer({
        interval = event_request.interval,
        callback = heartbeat
    })
end
