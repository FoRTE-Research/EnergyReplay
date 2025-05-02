## Overview

This is a hardware system for replaying arbitrary energy harvesting traces to drive experimentation
on batteryless systems. It does so by measuring the voltage at the load and the current across a shunt
resistor to determine instantaneous power input into the load and comparing this actual power input to
a desired time-varying setpoint; if the actual power is below (above) the setpoint, it increases
(decreases) supply voltage produced by a DAC to approach the setpoint.

#### Project Status

V1.2 is available as part of this Github repo, but the PCBs have not been ordered. The only difference
between V1.1 and V1.2 is the addition of the voltage divider (R18 and R19) to reduce measured load
voltage. Because V1.1 measures load voltage directly, it cannot produce a load voltage higher than
~3.3V (software will stop voltage from increasing further if load voltage approaches this point to
avoid damaging the hardware).

## Hardware

Schematic and PCB designs are generated in KiCad version 9.

The central microcontroller is an MSP430FR5994, which drives an MCP4725 12-bit DAC. Both the MSP430 and
DAC operate at 3.3V; the output of the DAC is fed into an amplifier to increase its dynamic range to 5V.
Current flows across a 0.5 ohm shunt resistor through an optional diode and into the load. Load current
is measured by taking the voltage across the shunt resistor using a 150x differential amplifier into one
ADC pin of the MSP430. Load voltage is divided by 2 using a resistive voltage divider and fed into
another ADC pin of the MSP430. The short-circuit current of the OPA2837 op amps used is roughly 50 mA,
so at 5V the nominal maximum output power is about 250 mW.

LEDs and switches are available for basic debugging and interactive use.

#### Jumpers

**J1:** Debug header for programming the MSP430FR5994.

**J2:** Power select jumper for replay system. The center pin is connected to the system supply voltage;
it draw power either from the debug header or from the 3V3 pin on J5.

**J3/J4:** Jumpers to connect and disconnect the green and red LEDs.

**J5:** GPIO breakout header. Also provides access to ground and 3V3 pins for supplying the system from
a non-debugger source.

**J6:** Jumper for optional diode use. RAW is connected to the load side of the shunt resistor and can
be connected to the load if the diode is not desired. Closing this jumper connects the load side of the
shunt resistor to an LM66100 ideal diode to prevent current backflow from the load. When J6 is closed,
the load should be connected on the OUT pin of J7.

**J7:** Jumper providing access to output pin for connecting load and ground pin for convenience. Do not
close this jumper.

## Software

Code is generated as a Code Composer Studio version 11 project.

The energy trace to replay is defined in the [dac.h](Code/dac.h) file as a `const float powerArray[]`
containing the setpoints and a `#define TIMESTEP` macro to set the time (in milliseconds) that power
should stay at each setpoint. This timestep is used to calculate a period value for a hardware timer
that tracks the time spent at each setpoint; when the timer rolls over, the setpoint increments.

The main application loop reads samples from the shunt and load ADC pins, converts those readings to
an actual power measurement, and compares them to the desired setpoint. The output voltage is
incremented/decremented by 1 on each iteration of the main loop; this is basically a PID controller
with K<sub>P</sub> = 1 and K<sub>I</sub> = K<sub>D</sub> = 0. Improving the design of this control
loop is one avenue for improvement in future iterations, although it currently works "well enough".
ADC, timer, and software control are all decoupled - increasing software execution speed (e.g., by
doing the actual power calculation in hardware using some clever op-amp circuits) should only have
the effect of reducing the response time of the control loop and not mess with the timing of the
power trace. If you do significantly increase the software execution speed, you should double check
that the ADC measurements are blocking so that it doesn't overshoot based on stale ADC measurements.
