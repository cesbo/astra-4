
function module_attach(obj, m)
    if #obj.modules > 0 then
        local tail = obj.modules[#obj.modules]
        tail:attach(m)
    end
    table.insert(obj.modules, m)
    return m
end

input_list = {}

function input_list.dvb(stream)
    function parse_tp(tp)
        local _, _, freq_s, pol_s, srate_s = tp:find("(%d+):(%a):(%d+)")
        if not freq_s then return nil end
        return tonumber(freq_s), pol_s, tonumber(srate_s)
    end
    if stream.config.dvb.tp then
        local freq, pol, srate = parse_tp(stream.config.dvb.tp)
        if not freq then
            log.error("[stream.lua] failed to parse \"tp\"")
            astra.abort()
        end
        stream.config.dvb.frequency = freq
        stream.config.dvb.polarization = pol
        stream.config.dvb.symbolrate = srate
    end

    function parse_lnb(lnb)
        local _, eo, lof1_s = lnb:find("(%d+):*")
        if not lof1_s then return nil end
        local lof2, slof = 0, 0
        if #lnb > eo then
            local _, _, lof2_s, slof_s = lnb:find("(%d+):(%d+)", eo + 1)
            if not lof2_s then return nil end
            lof2 = tonumber(lof2_s)
            slof = tonumber(slof_s)
        end
        return tonumber(lof1_s), lof2, slof
    end
    if stream.config.dvb.lnb then
        local lof1, lof2, slof = parse_lnb(stream.config.dvb.lnb)
        if not lof1 then
            log.error("[stream.lua] failed to parse \"lnb\"")
            astra.abort()
        end
        stream.config.dvb.lof1 = lof1
        stream.config.dvb.lof2 = lof2
        stream.config.dvb.slof = slof
    end

    stream.input = dvb_input(stream.config.dvb)

    if stream_event then
        stream.event = function(self)
            stream_event(self, stream, self:status())
        end
        stream.input:event(stream.event)
    end
end

function input_list.udp(stream)
    stream.input = udp_input(stream.config.udp)
end

function input_list.file(stream)
    stream.input = file_input(stream.config.file)
end

function input_list.module(stream)
    stream.input = stream.config.module[1]
end

output_list = {}

function output_list.udp(output, is_rtp)
    if not output.config.ttl then output.config.ttl = 32 end
    if is_rtp then output.config.rtp = true end

    -- parse dst
    local addr = output.dst
    -- localaddr
    local so, eo = addr:find("@")
    if eo then
        if so > 1 then
            output.config.localaddr = addr:sub(0, so - 1)
        end
        addr = addr:sub(eo + 1)
    end
    -- port
    local so, eo = addr:find(":")
    if eo then
        output.config.port = tonumber(addr:sub(eo + 1))
        addr = addr:sub(0, so - 1)
    end
    -- addr
    output.config.addr = addr

    module_attach(output, udp_output(output.config))
end

function output_list.rtp(output)
    output_list.udp(output, true)
end

function output_list.file(output)
    output.config.filename = output.dst
    module_attach(output, file_output(output.config))
end

extra_list = {}

function extra_list.mixaudio(obj)
    local mixaudio_config = {}
    mixaudio_config.pid = obj.config.mixaudio[1]
    if type(obj.config.mixaudio[2]) == 'string' then
        mixaudio_config.direction = obj.config.mixaudio[2]
    end
    return module_attach(obj, mixaudio(mixaudio_config))
end

function make_channel(parent, stream, config)
    local ch = {
        stream = stream,
        config = config,
        modules = {},
        output = {},
    }

    local tail = module_attach(ch, parent)

    local check_demux = false
    -- channel
    if config.pnr then
        tail = module_attach(ch, channel(config))
        check_demux = true
    end

    if stream.demux and not check_demux then
        log.warning("[stream.lua] channel:" .. config.name
                    .. " pnr option is required")
    end

    -- event
    if config.event and event_request and stream_event then
        ch.event = function(self) stream_event(self, ch) end
    end

    -- decrypt
    if type(config.cam) == 'userdata' then
        ch.decrypt = module_attach(ch, decrypt(config))
        tail = ch.decrypt
        if ch.event then tail:event(ch.event) end
    elseif type(config.cam) == 'table' then
        ch.camgroup = { config = config.cam }
        config.cam = nil
        ch.decrypt = module_attach(ch, decrypt(config))
        tail = ch.decrypt
        camgroup_channel(ch)
    end

    -- extra
    for extra, _ in pairs(config) do
        if extra_list[extra] then tail = extra_list[extra](ch) end
    end

    -- analyze
    if config.analyze ~= false then
        ch.analyze = analyze(config)
        tail:attach(ch.analyze)

        if ch.event then
            ch.last_cc_error = 0
            ch.last_pes_error = 0
            ch.analyze:event(ch.event)
        end
    end

    -- outputs
    if not config.output then return ch end

    function get_dst(dst)
        if type(dst) ~= 'string' then return nil, nil end
        local so, eo = dst:find("://")
        if not eo then return nil, nil end
        local dst_type = dst:sub(0, so - 1)
        return output_list[dst_type], dst:sub(eo + 1)
    end

    local oi = 1
    while config.output[oi] do
        local output_func, dst_addr = get_dst(config.output[oi])
        if not output_func then
            log.error("[stream.lua] unknown output type"
                      .. " stream:" .. stream.name
                      .. " channel:" .. config.name)
            astra.abort()
        end
        oi = oi + 1

        local output = {
            channel = ch,
            dst = dst_addr,
            config = {},
            modules = { tail },
        }

        if type(config.output[oi]) == 'table' then
            output.config = config.output[oi]
            oi = oi + 1

            for extra, _ in pairs(output.config) do
                if extra_list[extra] then extra_list[extra](output) end
            end
        end

        output_func(output)
        table.insert(ch.output, output)
    end

    return ch
end

stream_list = {}

function make_stream(config, channels)
    local stream = {
        config = config,
    }

    function get_input()
        for name, method in pairs(input_list) do
            if config[name] then return method end
        end
        log.error("[stream.lua] make_stream() wrong input type")
        astra.abort()
    end

    if not config.name then
        config.name = "Stream #" .. tostring(#stream_list + 1)
        log.warning("[stream.lua] name of stream is undefined")
    end

    if type(config.input) == 'table' then
        log.warning("[stream.lua] option \"input\" is deprecated")
        local input_type = config.input[1]
        table.remove(config.input, 1)
        config[input_type] = config.input
        config.input = nil
    end

    local input_func = get_input()
    input_func(stream)

    local tail = stream.input

    if type(channels) == 'table' then
        if config.demux then
            stream.demux = demux()
            tail:attach(stream.demux)
            tail = stream.demux
        end

        stream.channels = {}
        for _, channel_config in pairs(channels) do
            local c = make_channel(tail, stream, channel_config)
            table.insert(stream.channels, c)
        end
    else
        stream.analyze = analyze({ name = "Stream" })
        tail:attach(stream.analyze)
    end

    table.insert(stream_list, stream)
    return stream
end
