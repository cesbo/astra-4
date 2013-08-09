# Description

MixAudio is a module to replace the left or the right channel to
the content of opposite channel.

# Usage

    require("stream")
    require("mixaudio")
    make_channel({
        name = "Channel Name",
        input = { ... },
        output = {
            "module://address#mixaudio=100",
            "module://address#mixaudio=100/LL",
            "module://address#mixaudio=100/RR",
        },
    })

# Option format

100 is a PID of the audio stream.
LL or RR is an option to set direction if option is not preset
then used LL by default.

*   LL - copy the left channel to the right channel
*   RR - copy the right channel to the left channel
