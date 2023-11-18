# ESP-01_HVAC_Zone_Valve
HVAC Bedroom zone valve. Monitors temperature of air in duct/plenum and also the room. If the duct temperature is helping to achieve the setpoint, open the valve, if it is moving the room temperature away from the set point, close the valve.

Hardware Requirements
* ESP32
* 5V Hobby servo to drive a damper in the air duct.
* 2 x DS18S20 temperature sensors
* 5V power for servo
* 3.3V power for ESP32
  
Wiring
* Both temperature sensor data lines are wired to ESP32 pin GP2
* Servo PWM line is wired to ESP32 pin GP0 (may need to disconnect during programming)

Boot strap
* Find WiFi SSID and password in code and set appropriately

Usage
* Wire, compile, upload, power on.
* Use your router's DHCP settings to find the new device and record it's ip address a.b.c.d
* Browse to one of the following paths:
* a.b.c.d/
  * Default. Returns a simple string of data. Use it for diagnostics, recording, whatever.
  * TS=<set point>,TR=<room temp>,TP=<plenum temp>,D={O|C}
* a.b.c.d/favicon
  * returns a quick 404.
* a.b.c.d/scan
  * Scans for DS18S20 devices on GP2. Returns their ID and current temperature.
  * Also provides means to update configuration. Produces a simple html web form where sensor IDs, setpoint and damper open and close angle (0-255) can be set.
* a.b.c.d/get?...
  * Where the /scan <submit> goes.
  * Has spr skrt wifi hax to switch your device to a different WiFi network:
    * ?ssid=
    * ?pwd=
