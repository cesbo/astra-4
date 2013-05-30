#!/usr/bin/env astra


function on_read(self, data)
    if type(data) == 'string' then
        print(data)
    elseif type(data) == 'nil' then
        self:close()
    end
end


log.set({ debug = true })

r = http_request({
   addr = "172.16.155.192",
   port = 8000, 
   ts = true, 
   callback = on_read,
   uri = "/udp/239.255.2.19:1234"
   })


o = udp_output({ upstream = r:stream(), addr = "239.255.9.9", port = 9876 })


