
function camgroup_event(self, channel, decrypt_id)
    local s = self:status()
    local isok = s.keys and s.cam
    channel.camgroup.items[decrypt_id].is_active = isok

    function set_active_cam(id)
        if id > 0 then
            local item = channel.camgroup.config.items[id]
            log.info("[camgroup.lua] cam activated: " .. item.cam_config.name)
            channel.camgroup.active_id = id
            channel.decrypt:cam(item.cam)
        else
            channel.camgroup.active_id = 0
            channel.decrypt:cam(nil)
        end
    end

    if isok then
        local active_id = channel.camgroup.active_id
        if active_id == 0 then
            set_active_cam(decrypt_id)
            return
        end

        local active_cam = channel.camgroup.config.items[active_id]

        if active_cam.mode == 2 then
            set_active_cam(decrypt_id)

            local active_group = channel.camgroup.items[active_id]
            active_group.decrypt:cam(nil)
            active_group.is_active = false
            active_cam.cam = nil
            collectgarbage()
        end
    else
        if decrypt_id == channel.camgroup.active_id
           or channel.camgroup.active_id == 0
        then
            log.warning("[camgroup.lua] cam failed: "
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

    local item_id = 1
    for item_name,item_conf in pairs(items) do
        local cam_name = parse_name(item_name)
        local cam = _G[cam_name]
        if cam then
            local item = {
                cam_mod = cam,
                cam_config = item_conf,
                mode = item_conf.mode,
                count = 0, -- decrypts that use this cam
            }

            if item_conf.mode == 1 then
                item.cam = cam(item.cam_config)
            end

            group.items[item_id] = item
        end
        item_id = item_id + 1
    end

    return group
end

function camgroup_channel(channel)
    channel.camgroup.active_id = 0
    channel.camgroup.items = {}

    -- get a parent module of the decrypt module
    local parent = channel.modules[#channel.modules - 1]

    for cam_id,cam_item in pairs(channel.camgroup.config.items) do
        local decrypt_config = {
            name = channel.config.name .. ":" .. tostring(cam_id),
            cam = nil,
            fake = true,
        }

        local item = {
            is_active = false,
            decrypt = decrypt(decrypt_config),
            event = function(self)
                camgroup_event(self, channel, cam_id)
            end
        }
        item.decrypt:event(item.event)
        parent:attach(item.decrypt)

        if cam_item.cam then
            item.decrypt:cam(cam_item.cam)
        end

        table.insert(channel.camgroup.items, item)
    end
end
