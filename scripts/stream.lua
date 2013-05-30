
--[[
make_channel({
    name = "Channel 1",
    input =
    {
        {
            type = "dvb",
            adapter = 0,
            pnr = 100
        }
    },
    output =
    {
        {
            type = "udp",
            addr = "127.0.0.1",
            port = "10000"
        }
    }
})

make_channel({
    name = "Channel 2",
    input =
    {
        {
            type = "udp",
            addr = "239.255.2.139",
            port = 1234
        }
    },
    output =
    {
        {
            type = "file",
            filename = "/dev/null"
        }
    }
})
]]--

input_list = {}

input_list["dvb"] = function(name, input_conf)
    -- TODO: get dvb adapter from global lists
end

input_list["udp"] = function(name, input_conf)
    if not input_conf.port then input_conf.port = 1234 end
    return { tail = udp_input(input_conf) }
end

input_list["file"] = function(name, input_conf)
    return { tail = file_input(input_conf) }
end

function init_input(input_conf)
    if not input_conf.type then
        log.error("[stream.lua] option 'type' is required for input")
        astra.abort()
    end

    local init_input_type = input_list[input_conf.type:lower()]
    if not init_input_type then
        log.error("[stream.lua] unknown input type")
        astra.abort()
    end

    local input_mods = {}
    input_mods.source = init_input_type(name, input_conf)
    input_mods.tail = input_mods.source.tail

    if input_conf.pnr then
        local channel_conf =
        {
            name = input_conf.name,
            upstream = input_mods.tail:stream(),
            demux = input_mods.tail:demux(),
        }
        if input_conf.pnr then channel_conf.pnr = input_conf.pnr end
        if input_conf.caid then channel_conf.caid = input_conf.caid end
        if input_conf.sdt then channel_conf.sdt = input_conf.sdt end
        if input_conf.eit then channel_conf.eit = input_conf.eit end

        input_mods.channel = channel(channel_conf)
        input_mods.tail = input_mods.channel
    end

    if input_conf.biss then
        local decrypt_conf =
        {
            upstream = input_mods.tail:stream(),
            biss = input_conf.biss
        }

        input_mods.decrypt = decrypt(decrypt_conf)
        input_mods.tail = input_mods.decrypt
    elseif input_conf.cam then
        local  decrypt_conf =
        {
            upstream = input_mods.tail:stream(),
            cam = input_conf.cam
        }

        input_mods.decrypt = decrypt(decrypt_conf)
        input_mods.tail = input_mods.decrypt
    end

    -- TODO: extra modules

    input_mods.analyze = analyze({ upstream = input_mods.tail:stream(), name = input_conf.name })
    input_mods.tail = input_mods.analyze

    return input_mods
end

function make_channel(channel_conf)
    if not channel_conf.name then
        log.error("[stream.lua] option 'name' is required")
        astra.abort()
    end

    if not channel_conf.input or #channel_conf.input == 0 then
        log.error("[stream.lua] option 'input' is required")
        astra.abort()
    end

    local modules =
    {
        input = {},
        output = {}
    }

    for _, input_conf in pairs(channel_conf.input) do
        input_conf.name = channel_conf.name
    end

    local new_input = init_input(channel_conf.input[1])
    table.insert(modules.input, new_input)

    modules.transmit = transmit({ upstream = new_input.tail:stream() })
    modules.tail = modules.transmit

    channel_conf.__modules = modules
end
