i = udp_input({addr = "239.255.2.19", socket_size = 0x80000})

o = file_output({filename = "ts01.ts", upstream = i:stream()})
