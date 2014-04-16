
function on_read(self, response)
    print("Status: " .. response.code .. " " .. response.message)
    if response.headers then
        print("Headers:")
        for k,v in pairs(response.headers) do
            print("    \"" .. k .. "\" = " .. v)
        end
    end

    if response.content then
        print("")
        print(response.content)
    end
    astra.exit()
end

log.set({ debug = true })

http_request({
    host = "ya.ru",
    port = 80,
    headers = {
        "User-Agent: Astra",
        "Host: ya.ru",
        "Connection: close"
    },
    callback = on_read
})
