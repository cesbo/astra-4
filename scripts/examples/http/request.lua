
function on_read(self, data)
    if type(data) == 'string' then
        if #data == 0 then
            self:close()
            astra.exit()
            return
        end
        print(data)
    elseif type(data) == 'nil' then
        self:close()
        astra.exit()
    end
end

log.set({ debug = true })

http_request({
    host = "93.158.134.3",
    port = 80,
    headers = {
        "User-Agent: Astra",
        "Host: ya.ru"
    },
    callback = on_read
})
