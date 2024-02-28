// MCP4725 control code

#ifndef DAC_H_
#define DAC_H_

#define SLAVE_ADDRESS 0b1100000

void setDACVoltage(uint16_t v);

void zeroDACEEPROM();

#endif
