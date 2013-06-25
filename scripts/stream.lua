
function dump_table(t, p, i)
    if not p then p = print end
    if not i then
        p("{")
        dump_table(t, p, "    ")
        p("}")
        return
    end

    for key,val in pairs(t) do
        if type(val) == 'table' then
            p(i .. tostring(key) .. " = {")
            dump_table(val, p, i .. "    ")
            p(i .. "}")
        elseif type(val) == 'string' then
            p(i .. tostring(key) .. " = \"" .. val .. "\"")
        else
            p(i .. tostring(key) .. " = " .. tostring(val))
        end
    end
end

-- ooooo  oooo oooooooooo  ooooo
--  888    88   888    888  888
--  888    88   888oooo88   888
--  888    88   888  88o    888      o
--   888oo88   o888o  88o8 o888ooooo88

local parse_addr = {}

parse_addr.udp = function(addr, result)
    local x = addr:find("@")
    if x then
        if x > 1 then
            result.localaddr = addr:sub(1, x - 1)
        end
        addr = addr:sub(x + 1)
    end
    local x = addr:find(":")
    if x then
        result.addr = addr:sub(1, x - 1)
        result.port = tonumber(addr:sub(x + 1))
    else
        result.addr = addr
        result.port = 1234
    end
end

parse_addr.file = function(addr, result)
    result.filename = addr
end

function parse_options(options, result)
    local x = options:find("?")
    if x ~= 1 then
        return
    end
    options = options:sub(2)

    function parse_option(option)
        local x = option:find("=")
        if not x then return nil end
        local key = option:sub(1, x - 1)
        result[key] = option:sub(x + 1)
    end

    local pos = 1
    while true do
        local x = options:find("&", pos)
        if x then
            parse_option(options:sub(pos, x - 1))
            pos = x + 1
        else
            parse_option(options:sub(pos))
            return nil
        end
    end
end

function parse_url(url)
    local x,_,module_name,url_addr,url_options = url:find("^(%a+)://([%a%d%.:@_/%-]+)(.*)$" )
    if not module_name then
        return nil
    end
    if type(parse_addr[module_name]) ~= 'function' then return nil end

    local result = { module_name = module_name }
    parse_addr[module_name](url_addr, result)
    if url_options then parse_options(url_options, result) end

    return result
end

-- ooooo oooo   oooo oooooooooo ooooo  oooo ooooooooooo
--  888   8888o  88   888    888 888    88  88  888  88
--  888   88 888o88   888oooo88  888    88      888
--  888   88   8888   888        888    88      888
-- o888o o88o    88  o888o        888oo88      o888o

local input_list = {}

local dvb_instance_list = {}

input_list.dvb = function(input_conf)
    -- TODO:
end

local udp_instance_list = {}

input_list.udp = function(input_conf)
    if not input_conf.port then input_conf.port = 1234 end

    local addr = input_conf.addr .. ":" .. input_conf.port
    if input_conf.localaddr then addr = input_conf.localaddr .. "@" .. addr end

    local udp_instance
    if udp_instance_list[addr] then
        udp_instance = udp_instance_list[addr]
    else
        udp_instance = udp_input(input_conf)
        udp_instance_list[addr] = udp_instance
    end

    return { tail = udp_instance }
end

input_list.file = function(input_conf)
    return { tail = file_input(input_conf) }
end

function init_input(channel_data, input_conf)
    if not input_conf.module_name then
        log.error("[stream.lua] option 'module_name' is required for input")
        astra.abort()
    end

    local init_input_type = input_list[input_conf.module_name]
    if not init_input_type then
        log.error("[stream.lua] unknown input type")
        astra.abort()
    end

    local input_mods = {}
    input_mods.source = init_input_type(input_conf)
    input_mods.tail = input_mods.source.tail

    if input_conf.pnr then
        local channel_conf =
        {
            name = channel_data.config.name,
            upstream = input_mods.tail:stream(),
            pnr = input_conf.pnr
        }
        if input_conf.caid then channel_conf.caid = input_conf.caid end
        if input_conf.sdt then channel_conf.sdt = input_conf.sdt end
        if input_conf.eit then channel_conf.eit = input_conf.eit end

        input_mods.channel = channel(channel_conf)
        input_mods.tail = input_mods.channel
    end

    if input_conf.biss then
        input_mods.decrypt = decrypt({
            upstream = input_mods.tail:stream(),
            biss = input_conf.biss
        })
        input_mods.tail = input_mods.decrypt
    end

    -- TODO: extra modules

    input_mods.analyze = analyze({
        upstream = input_mods.tail:stream(),
        name = channel_data.config.name,
        callback = function(data)
                if data.analyze then
                    local bitrate = 0
                    for _,item in pairs(data.analyze) do
                        bitrate = bitrate + item.bitrate
                    end
                    log.info(channel_data.config.name .. ' Bitrate: ' .. bitrate .. ' Kbit/s')

                    if data.on_air ~= input_mods.last_state then
                        input_mods.last_state = data.on_air
                        log.info(channel_data.config.name .. ' Input state: ' .. tostring(data.on_air))
                    end
                end
            end
    })

    return input_mods
end

--   ooooooo  ooooo  oooo ooooooooooo oooooooooo ooooo  oooo ooooooooooo
-- o888   888o 888    88  88  888  88  888    888 888    88  88  888  88
-- 888     888 888    88      888      888oooo88  888    88      888
-- 888o   o888 888    88      888      888        888    88      888
--   88ooo88    888oo88      o888o    o888o        888oo88      o888o

local output_list = {}

output_list.udp = function(output_conf)
    if not output_conf.port then output_conf.port = 1234 end
    if not output_conf.ttl then output_conf.ttl = 32 end

    return { tail = udp_output(output_conf) }
end

output_list.file = function(output_conf)
    return { tail = file_output(output_conf) }
end

function init_output(channel_data, output_conf)
    if not output_conf.module_name then
        log.error("[stream.lua] option 'module_name' is required for output")
        astra.abort()
    end

    local init_output_type = output_list[output_conf.module_name]
    if not init_output_type then
        log.error("[stream.lua] unknown output type")
        astra.abort()
    end

    -- TODO: extra modules

    output_conf.upstream = channel_data.tail:stream()

    local output_mods = {}
    output_mods.source = init_output_type(output_conf)
    output_mods.tail = output_mods.source.tail

    return output_mods
end

--   oooooooo8 ooooo ooooo      o      oooo   oooo oooo   oooo ooooooooooo ooooo
-- o888     88  888   888      888      8888o  88   8888o  88   888    88   888
-- 888          888ooo888     8  88     88 888o88   88 888o88   888ooo8     888
-- 888o     oo  888   888    8oooo88    88   8888   88   8888   888    oo   888      o
--  888oooo88  o888o o888o o88o  o888o o88o    88  o88o    88  o888ooo8888 o888ooooo88

function make_channel(channel_conf)
    if not channel_conf.name then
        log.error("[stream.lua] option 'name' is required")
        astra.abort()
    end

    if not channel_conf.input or #channel_conf.input == 0 then
        log.error("[stream.lua] option 'input' is required")
        astra.abort()
    end

    if not channel_conf.output then channel_conf.output = {} end

    function parse_conf(list)
        for key,val in pairs(list) do
            if type(val) == 'string' then
                local conf_tmp = parse_url(val)
                if not conf_tmp then
                    log.error("[stream.lua] wrong URL format: " .. val)
                    astra.abort()
                end
                list[key] = conf_tmp
            end
        end
    end

    parse_conf(channel_conf.input)
    parse_conf(channel_conf.output)

    local channel_data = {}
    channel_data.config = channel_conf
    channel_data.input = {}
    channel_data.output = {}

    -- Start first input
    local new_input = init_input(channel_data, channel_conf.input[1])
    table.insert(channel_data.input, new_input)

    channel_data.transmit = transmit({ upstream = new_input.tail:stream() })
    channel_data.tail = channel_data.transmit

    for _,output_conf in pairs(channel_conf.output) do
        local new_output = init_output(channel_data, output_conf)
        table.insert(channel_data.output, new_output)
    end
end
