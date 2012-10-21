#!/usr/bin/env astra

log.set({ stdout = true })

local argc = #argv
ch = {}
pes_error = {}
cc_error = {}

function check_analyze()
    local s = ch.a:status()

    log.info("ready:" .. tostring(s.ready) .. " " ..
             "bitrate:" .. tostring(s.bitrate) .. "Kbit/s")

    local cc_error_msg = ""
    local pes_error_msg = ""

    for pid,ss in pairs(s.stream) do
        if cc_error[pid] ~= ss.cc_error then
            cc_error_msg = cc_error_msg .. pid .. ":" .. ss.cc_error .. " "
            cc_error[pid] = ss.cc_error
        end
        if pes_error[pid] ~= ss.pes_error then
            pes_error_msg = pes_error_msg .. pid .. ":" .. ss.pes_error .. " "
            pes_error[pid] = ss.pes_error
        end
    end
    if #cc_error_msg > 0 then
        log.warning("[CC error] " .. cc_error_msg)
    end
    if #pes_error_msg > 0 then
        log.warning("[PES error] " .. pes_error_msg)
    end
end

if argc == 0 then
    print("Usage: astra analyze.lua ADDRESS [PORT]")
    astra.exit()
else
    local udp_input_conf = { addr = argv[1], port = 1234 }
    if argc >= 2 then
        udp_input_conf.port = argv[2]
    end
    if argc >= 3 then
        udp_input_conf.localaddr = argv[3]
    end
    ch.i = udp_input(udp_input_conf)
    ch.a = analyze({ name = "Test" })
    ch.i:attach(ch.a)
    ch.t = timer({ interval = 5, callback = check_analyze })
end
