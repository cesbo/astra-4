
function camgroup_event(self, channel, decrypt_id)
    local s = self:status()

    function set_active_cam(id)
        if id > 0 then
            local item = channel.config.cam.items[id]
            log.info("[camgroup.lua] active cam: " .. item.cam_config.name)
            channel.camgroup.active_id = id
            channel.decrypt:cam(item.cam)
        else
            channel.camgroup.active_id = 0
            channel.decrypt:cam(nil)
        end
    end

    local isok = s.keys and s.cam
    channel.camgroup.items[decrypt_id].is_active = isok

    if isok then
        if channel.camgroup.active_id == 0 then
            set_active_cam(decrypt_id)
        else
            local active_id = channel.camgroup.active_id
            local active_group_item = channel.camgroup.items[active_id]
            local active_cam_item = channel.config.cam.items[active_id]

            if active_cam_item.mode == 2 then
                set_active_cam(decrypt_id)

                active_group_item.decrypt:cam(nil)
                active_group_item.is_active = false
                active_cam_item.cam = nil
                collectgarbage()
            end
        end
    else
        if decrypt_id == channel.camgroup.active_id
           or channel.camgroup.active_id == 0
        then
            log.warning("[camgroup.lua] failed cam: " ..
                        channel.config.cam.items[decrypt_id].cam_config.name)

            for id, item in pairs(channel.camgroup.items) do
                if item.is_active then
                    set_active_cam(id)
                    return
                end
            end

            for id, item in pairs(channel.config.cam.items) do
                if item.mode == 2 and not item.cam then
                    item.cam = item.cam_mod(item.cam_config)
                    channel.camgroup.items[id].decrypt:cam(item.cam)

                    set_active_cam(0)
                    return
                end
            end

            set_active_cam(0)
            log.error("[camgroup.lua] all cam modules is down")
            if channel.event then channel.event(channel.decrypt) end
        end
    end
end

function camgroup(items)
    local group = { items = {} }

    for _,itemconf in pairs(items) do
        local cam = _G[itemconf[1]]
        if cam then
            local item = {
                cam_mod = cam,
                mode = itemconf.mode,
                cam_config = module_options(cam, itemconf)
            }

            if itemconf.mode == 1 then
                item.cam = cam(item.cam_config)
            end

            table.insert(group.items, item)
        end
    end

    return group
end

function camgroup_channel(channel)
    local group = channel.config.cam
    channel.camgroup = { active_id = 0, items = {} }

    -- get parent module (of the channel decrypt)
    local parent = channel.modules[#channel.modules - 1]

    for id,cam_item in pairs(group.items) do
        local decrypt_config = {}
        decrypt_config.name = channel.config.name .. ":" .. tostring(id)
        decrypt_config.cam = nil
        decrypt_config.fake = true

        local item = {}
        item.is_active = false
        item.decrypt = decrypt(decrypt_config)
        item.event = function(self)
            camgroup_event(self, channel, id)
        end
        item.decrypt:event(item.event)
        parent:attach(item.decrypt)

        if cam_item.cam then item.decrypt:cam(cam_item.cam) end

        table.insert(channel.camgroup.items, item)
    end
end
