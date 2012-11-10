
function camgroup_event(self, channel, decrypt_id)
    local s = self:status()

    function set_active_cam(id)
        if id > 0 then
            local item = channel.camgroup.config.items[id]
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
            local active_cam_item = channel.camgroup.config.items[active_id]

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
            log.warning("[camgroup.lua] failed cam: "
                        .. channel.camgroup.config.items[decrypt_id]
                           .cam_config.name)

            for id, item in pairs(channel.camgroup.items) do
                if item.is_active then
                    set_active_cam(id)
                    return
                end
            end

            for id, item in pairs(channel.camgroup.config.items) do
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

    for id = 1, #items do
        local item = items[1]
        table.remove(items, 1)
        local cam_name = item[1]
        if _G[cam_name] then items[cam_name .. "_" .. id] = item end
    end

    function parse_name(name)
        local so, eo = name:find("_")
        if not eo then return nil end
        return name:sub(0, so - 1)
    end

    for item_id,item_conf in pairs(items) do
        local cam_name = parse_name(item_id)
        local cam = _G[cam_name]
        if cam then
            local item = {
                cam_mod = cam,
                cam_config = item_conf,
                mode = item_conf.mode,
            }

            if item_conf.mode == 1 then
                item.cam = cam(item.cam_config)
            end

            table.insert(group.items, item)
        end
    end

    return group
end

function camgroup_channel(channel)
    channel.camgroup.active_id = 0
    channel.camgroup.items = {}

    -- get a parent module of the decrypt module
    local parent = channel.modules[#channel.modules - 1]

    for id,cam_item in pairs(channel.camgroup.config.items) do
        local decrypt_config = {
            name = channel.config.name .. ":" .. tostring(id),
            cam = nil,
            fake = true,
        }

        local item = {
            is_active = false,
            decrypt = decrypt(decrypt_config),
            event = function(self)
                camgroup_event(self, channel, id)
            end
        }
        item.decrypt:event(item.event)
        parent:attach(item.decrypt)

        if cam_item.cam then item.decrypt:cam(cam_item.cam) end

        table.insert(channel.camgroup.items, item)
    end
end
