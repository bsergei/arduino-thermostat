# arduino-thermostat

Simple heating thermostat using DS18B20 and 3-digits 7-segment display (common anode).
It has basic operation with only two buttons.
A0 - DS18B20
A1 - "-" button
A2 - "+" button
A3 - Relay output. High to activate heater.
D6-D13 - Display segments (a, b, c, d, e, f, g and point).
D3-D5 - Digits selector.
Use cases:
1. Press "+" or "-" to view/change target temperature.
2. Press both "+" and "-" to view/change histeresis temperature.
