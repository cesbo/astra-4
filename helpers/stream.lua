
options = {
    dvb_input = {
        adapter = true,
        device = true,
        diseqc = true,
        type = true,
        tp = true,
        lnb = true,
        lnb_sharing = true,
        budget = true,
        buffer_size = true,
        modulation = true,
        fec = true,
        rolloff = true,
        symbolrate = true,
        frequency = true,
        bandwidth = true,
        guardinterval = true,
        transmitmode = true,
        hierarchy = true,
    },
    demux = {
    },
    channel = {
        name = true,
        pid = true,
        pnr = true,
        sdt = true,
        filter = true,
        map = true,
    },
    analyze = {
        name = true,
    },
    udp_input = {
        addr = true,
        port = true,
        localaddr = true,
        rtp = true,
    },
    udp_output = {
        addr = true,
        port = true,
        ttl = true,
        localaddr = true,
        socket_size = true,
        rtp = true,
    },
    file_input = {
        filename = true,
    },
    file_output = {
        filename = true,
        m2ts = true,
    },
    decrypt = {
        name = true,
        cam = true,
        real_pnr = true,
        ecm_pid = true,
        fake = true,
    },
    newcamd = {
        name = true,
        host = true,
        port = true,
        user = true,
        pass = true,
        key = true,
        disable_emm = true,
        cas_data = true,
    },
}

function module_options(module, config, default)
    local ret = {}
    local template = options[tostring(module)]

    if default then
        for var,val in pairs(default) do
            if template[var] then ret[var] = val end
        end
    end

    for var,val in pairs(config) do
        if template[var] then ret[var] = val end
    end

    return ret
end

function module_attach(obj, m)
    if #obj.modules > 0 then
        local tail = obj.modules[#obj.modules]
        tail:attach(m)
    end
    table.insert(obj.modules, m)
    return m
end

-----------
-- INPUT --
-----------

input_list = {}

function input_list.dvb(stream)
    local dvb_input_config = module_options(dvb_input, stream.config.input)
    stream.input = dvb_input(dvb_input_config)

    stream.event = function(self)
        local stat = self:status()
        stream_event(self, stream, stat)
    end
    if stream_event then stream.input:event(stream.event) end
end

function input_list.udp(stream)
    local udp_input_default = { port = 1234 }
    local udp_input_config = module_options(udp_input, stream.config.input, udp_input_default)

    stream.input = udp_input(udp_input_config)
end

function input_list.file(stream)
    local file_input_config = module_options(file_input, stream.config.input)
    stream.input = file_input(file_input_config)
end

------------
-- OUTPUT --
------------

output_list = {}

function output_list.udp(output, is_rtp)
    local udp_output_default = { port = 1234, ttl = 32 }
    if is_rtp then udp_output_default.rtp = true end

    -- parse dst
    local addr = output.dst
    -- localaddr
    local so, eo = addr:find("@")
    if eo then
        if so > 1 then
            udp_output_default.localaddr = addr:sub(0, so - 1)
        end
        addr = addr:sub(eo + 1)
    end
    -- port
    local so, eo = addr:find(":")
    if eo then
        udp_output_default.port = tonumber(addr:sub(eo + 1))
        addr = addr:sub(0, so - 1)
    end
    -- addr
    udp_output_default.addr = addr

    local udp_output_config = module_options(udp_output, output.config, udp_output_default)
    module_attach(output, udp_output(udp_output_config))
end

function output_list.rtp(output)
    output_list.udp(output, true)
end

function output_list.file(output)
    module_attach(output, file_output({ filename = output.dst }))
end

-----------
-- EXTRA --
-----------

extra_list = {}

function extra_list.mixaudio(obj) -- obj is channel or output
    local mixaudio_config = {}
    mixaudio_config.pid = obj.config.mixaudio[1]
    if type(obj.config.mixaudio[2]) == 'string' then
        mixaudio_config.direction = obj.config.mixaudio[2]
    end
    return module_attach(obj, mixaudio(mixaudio_config))
end

-------------
-- CHANNEL --
-------------

function make_channel(parent, config)
    local ch = { config = config, modules = {}, outputs = {} }

    local tail = module_attach(ch, parent)

    -- channel
    if config.pnr then
        local channel_config = module_options(channel, config)
        tail = module_attach(ch, channel(channel_config))
    end

    -- event
    if config.event and stream_event then
        if type(config.event) == 'string' then
            ch.event_name = config.event
        end
        ch.event = function(self) stream_event(self, ch) end
    end

    -- decrypt
    if type(config.cam) == 'userdata' then
        local decrypt_config = module_options(decrypt, config)
        tail = module_attach(ch, decrypt(decrypt_config))
        ch.decrypt = tail
        if ch.event then ch.decrypt:event(ch.event) end

    elseif type(config.cam) == 'table' then
        local decrypt_config = module_options(decrypt, config)
        decrypt_config.cam = nil
        tail = module_attach(ch, decrypt(decrypt_config))
        ch.decrypt = tail
        camgroup_channel(ch)
    end

    -- extra
    for extra, _ in pairs(config) do
        if extra_list[extra] then
            tail = extra_list[extra](ch)
        end
    end

    -- analyze
    if config.analyze ~= false then
        local analyze_config = module_options(analyze, config)
        ch.analyze = analyze(analyze_config)
        tail:attach(ch.analyze)

        if ch.event then
            ch.last_cc_error = 0
            ch.last_pes_error = 0
            ch.analyze:event(ch.event)
        end
    end

    -- outputs
    if not config.output then return ch end

    if type(config.event) == 'boolean' then
        ch.event_name = config.output[1]
    end

    local oi = 1
    while config.output[oi] do
        local output = { config = {}, modules = { tail } }
        output.config.name = config.name
        output.config.dst = config.output[oi]
        local ofunc = nil

        -- output dst
        local dst = output.config.dst
        local so, eo = dst:find("://")
        if eo then
            local dst_type = dst:sub(0, so - 1)
            output.dst = dst:sub(eo + 1)
            ofunc = output_list[dst_type]
            if not ofunc then
                log.warning("[stream.lua] unknown output type: " .. dst_type)
            end
        else
            log.warning("[stream.lua] wrong output format: " .. dst)
        end
        oi = oi + 1

        -- output config
        if type(config.output[oi]) == 'table' then
            for var, val in pairs(config.output[oi]) do
                output.config[var] = val
            end
            oi = oi + 1
        end

        -- start output
        if ofunc then
            -- extra
            for extra, _ in pairs(output.config) do
                if extra_list[extra] then
                    extra_list[extra](output)
                end
            end

            -- output
            ofunc(output)
        end

        table.insert(ch.outputs, output)
    end

    return ch
end

------------
-- STREAM --
------------

stream_list = {}

function make_stream(config, channels)
    local stream = { config = config, channels = {} }

    if type(config.input) == 'table' then
        local input_type = config.input[1]
        if not input_list[input_type] then
            log.error("[stream.lua] make_stream() wrong input type")
            return
        end
        input_list[input_type](stream)
    elseif type(config.input) == 'userdata' then
        stream.input = config.input
    else
        log.error("[stream.lua] make_stream() input is not defined")
        return
    end

    local tail = stream.input

    if type(channels) == 'table' then
        if config.demux then
            stream.demux = demux()
            tail:attach(stream.demux)
            tail = stream.demux
        end

        for _, cconf in pairs(channels) do
            local ch = make_channel(tail, cconf)
            table.insert(stream.channels, ch)
        end
    else
        stream.analyze = analyze({ name = "Stream" })
        tail:attach(stream.analyze)
    end

    table.insert(stream_list, stream)
    return stream
end
