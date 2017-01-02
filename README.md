## Wifi amplifier

A multi-channel sound amplifier controllable over wifi.  This can be used for whole-house audio where the amplifier is in a central location and speaker wire in the wall brings the signal into each room, and each room's volume can be controlled with a wifi application.  There are premium systems that do this that can cost thousands of dollars and may not have a web interface, so this project is to build such a system as cheaply as possible.

The approach is to have an ESP8266 wifi controller provide the control interface that sends signals to 8 20W MAX9744 amplifiers that indicate what the volume should be.  The amplifiers then output 8 separate amplified left and right audio signals that can be sent to different rooms.  I'm sure this could be done with a single beefy 160W amplifier and transistors and digital potentiometers selectively sending the output to different channels, but this amplification method is beyond my electrical engineering knowledge and experience.  Also, having separate amplifiers allows for only as many amplifiers as needed for an application, possibly driving down costs if not all 8 channels are needed.  Since the ESP8266 communicates with the amplifiers over i2c and the amplifiers all use the same i2c address, an i2c multiplexer is needed.

The prototype is to construct a prototype unit using 8 Adafruit 20W MAX9744 amplifiers, but eventually the goal is to produce a PCB that can utilize the MAX9744 chips directly.


## References

Adafruit HUZZAH: https://learn.adafruit.com/adafruit-huzzah-esp8266-breakout/
Adafruit i2c multiplexer: https://learn.adafruit.com/adafruit-tca9548a-1-to-8-i2c-multiplexer-breakout/
Adafruit MAX9744: https://learn.adafruit.com/adafruit-20w-stereo-audio-amplifier-class-d-max9744/
MAX9744 datasheet: https://www.maximintegrated.com/en/products/analog/audio/MAX9744.html