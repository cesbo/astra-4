
output_module.biss = function(output_conf, output_data)
    output_data.biss = biss_encrypt({
        upstream = output_data.tail:stream(),
        key = output_conf.biss
    })
    output_data.tail = output_data.biss
end

kill_output_module.biss = function(output_conf, output_data)
    output_data.biss = nil
end
