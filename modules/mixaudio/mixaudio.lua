
output_module.mixaudio = function(output_conf, output_data)
    local mixaudio_conf = { upstream = output_data.tail:stream() }
    local b = output_conf.mixaudio:find("/")
    if b then
        mixaudio_conf.pid = tonumber(output_conf.mixaudio:sub(1, b - 1))
        mixaudio_conf.direction = output_conf.mixaudio:sub(b + 1)
    else
        mixaudio_conf.pid = tonumber(output_conf.mixaudio)
    end

    output_data.mixaudio = mixaudio(mixaudio_conf)
    output_data.tail = output_data.mixaudio
end

kill_output_module.mixaudio = function(output_conf, output_data)
    output_data.mixaudio = nil
end
