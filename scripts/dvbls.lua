#!/usr/bin/env astra

local dvb_list = dvbls()

for _,dvb_info in pairs(dvb_list) do
    log.info("adapter = " .. dvb_info.adapter .. ", device = " .. dvb_info.device)
    if dvb_info.error then
        log.error("    " .. dvb_info.error)
    else
        if dvb_info.busy == true then
            log.info("    adapter in use")
        end
        log.info("    mac = " .. dvb_info.mac)
        log.info("    frontend = " .. dvb_info.frontend)
    end
end

astra.exit()
