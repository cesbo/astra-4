
function main()
    log.info("Starting Astra " .. astra.version)
    log.set({ color = true })

    if not dvbls then
        log.error("dvbls module is not found")
        astra.exit()
    end

    local dvb_list = dvbls()

    for _,dvb_info in pairs(dvb_list) do
        if dvb_info.error then
            log.error("adapter = " .. dvb_info.adapter .. ", device = " .. dvb_info.device)
            log.error("    " .. dvb_info.error)
        else
            if dvb_info.busy == true then
                log.warning("adapter = " .. dvb_info.adapter .. ", device = " .. dvb_info.device)
                log.warning("    adapter in use")
                log.warning("    mac = " .. dvb_info.mac)
                log.warning("    frontend = " .. dvb_info.frontend)
                log.warning("    type = " .. dvb_info.type)
            else
                log.info("adapter = " .. dvb_info.adapter .. ", device = " .. dvb_info.device)
                log.info("    mac = " .. dvb_info.mac)
                log.info("    frontend = " .. dvb_info.frontend)
                log.info("    type = " .. dvb_info.type)
            end
        end
    end

    astra.exit()
end
