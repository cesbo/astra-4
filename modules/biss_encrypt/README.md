# Description

BISS Encrypt module for Astra v4

# Usage

    require("stream")
    require("biss_encrypt")
    make_channel({
        name = "Channel Name",
        input = { ... },
        output = { "module://address#biss=1122330044556600" },
    })

# Key format

Fourth and eighth bytes in the key is a control sum.
In example key is 1122330044556600.
The module will calculating control sum self.

    11 + 22 + 33 = 66
    44 + 55 + 66 = FF

The encryption key will be 11223366445566FF.
