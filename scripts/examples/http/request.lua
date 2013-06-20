
function on_read(self, data)
    if type(data) == 'string' then
        print(data)
    elseif type(data) == 'nil' then
        self:close()
    end
end

log.set({ debug = true })

http_request({
    addr = "93.158.134.3",
    port = 80,
    headers = {
        "User-Agent: Astra",
        "Host: ya.ru"
    },
    callback = on_read
})
