# sw_ladspa

OBS Studio filters ported to LADSPA with the addition of Lookahead

## Example usage in pipewire

As default source:
```
context.modules = [
    {
        name = libpipewire-module-filter-chain
        args = {
            node.description = "Default Source"
            media.name = "default_source"
            filter.graph = {
                nodes = [
                    {
                        type = builtin
                        name = low_cut
                        label = bq_highpass
                        control = { "Freq" = 150.0 "Q" = 1.0 }
                    }
                    {
                        type = builtin
                        name = high_cut
                        label = bq_lowpass
                        control = { "Freq" = 15000.0 "Q" = 1.0 }
                    }
                    {
                        type = builtin
                        name = gain
                        label = bq_highshelf
                        control = { "Freq" = 0.0 "Q" = 1.0 "Gain" = 12.0 }
                    }
                    {
                        type = ladspa
                        name = gate
                        plugin = sw_noise_gate
                        label = sw_noise_gate_mono
                        control = {
                            "Close Threshold (dB)" = -32.0
                            "Open Threshold (dB)" = -26.0
                            "Attack (ms)" = 25.0
                            "Hold (ms)" = 200.0
                            "Release (ms)" = 150.0
                            "Lookahead (ms)" = 25.0
                        }
                    }
                    {
                        type = ladspa
                        name = comp
                        plugin = sw_compressor
                        label = sw_compressor_mono
                        control = {
                            "Ratio (:1)" = 10.0
                            "Threshold (dB)" = -18.0
                            "Attack (ms)" = 6.0
                            "Release (ms)" = 60.0
                            "Output Gain (dB)" = 0.0
                            "Lookahead (ms)" = 6.0
                        }
                    }
                ]
                links = [
                    { output = "low_cut:Out"  input = "high_cut:In" }
                    { output = "high_cut:Out" input = "gain:In" }
                    { output = "gain:Out"     input = "gate:In" }
                    { output = "gate:Out"     input = "comp:In" }
                ]
            }
            audio.channels = 1
            audio.position = [ MONO ]
            audio.rate = 48000
            capture.props = {
                node.name = "effect_input.default_source"
                target.object = "alsa_input.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-mono"
            }
            playback.props = {
                node.name = "effect_output.default_source"
                media.class = Audio/Source
            }
        }
    }
]
```

As default sink:
```
context.modules = [
    {
        name = libpipewire-module-filter-chain
        args = {
            node.description = "Default Sink"
            media.name = "default_sink"
            filter.graph = {
                nodes = [
                    {
                        type = builtin
                        name = low_cut_L
                        label = bq_highpass
                        control = { "Freq" = 20.0 "Q" = 1.0 }
                    }
                    {
                        type = builtin
                        name = low_cut_R
                        label = bq_highpass
                        control = { "Freq" = 20.0 "Q" = 1.0 }
                    }
                    {
                        type = builtin
                        name = high_cut_L
                        label = bq_lowpass
                        control = { "Freq" = 20000.0 "Q" = 1.0 }
                    }
                    {
                        type = builtin
                        name = high_cut_R
                        label = bq_lowpass
                        control = { "Freq" = 20000.0 "Q" = 1.0 }
                    }
                    {
                        type = ladspa
                        name = comp
                        plugin = sw_compressor
                        label = sw_compressor
                        control = {
                            "Ratio (:1)" = 10.0
                            "Threshold (dB)" = -18.0
                            "Attack (ms)" = 6.0
                            "Release (ms)" = 60.0
                            "Output Gain (dB)" = 0.0
                            "Lookahead (ms)" = 6.0
                        }
                    }
                ]
                links = [
                    { output = "low_cut_L:Out"  input = "high_cut_L:In" }
                    { output = "low_cut_R:Out"  input = "high_cut_R:In" }
                    { output = "high_cut_L:Out" input = "comp:In_L" }
                    { output = "high_cut_R:Out" input = "comp:In_R" }
                ]
                inputs  = [ "low_cut_L:In" "low_cut_R:In" ]
                outputs = [ "comp:Out_L" "comp:Out_R" ]
            }
            audio.channels = 2
            audio.position = [ FL FR ]
            audio.rate = 48000
            capture.props = {
                node.name = "capture.default_sink"
                media.class = Audio/Sink
            }
            playback.props = {
                node.name = "playback.default_sink"
                target.object = "alsa_output.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo-output"
            }
        }
    }
]
```

## Building

```sh
CFLAGS="-O2 -fPIC -shared"

gcc $CFLAGS sw_noise_gate.c -lm -o sw_noise_gate.so
gcc $CFLAGS sw_compressor.c -lm -o sw_compressor.so
```
