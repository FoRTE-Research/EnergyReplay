// MCP4725 control code

#include <stdint.h>
#include <msp430.h>

// DAC globals
uint8_t highByteDAC, lowByteDAC, txByteCounter;
uint8_t *PTxData;
uint8_t TXByteCtr;
uint8_t TxData[2];

// Command for setting DAC EEPROM to 0
const uint8_t txZero[3] = {0b01100000, 0x0, 0x0};


void setDACVoltage(uint16_t v){
    v &= 0x0FFF;                        // DAC is 12 bits
    TxData[0] = v >> 8;
    TxData[1] = v & 0xFF;
    PTxData = (uint8_t*) TxData;        // TX array start address
    TXByteCtr = sizeof(TxData);         // Load TX byte counter

    while (UCB1CTLW0 & UCTXSTP);         // Ensure stop gets sent
    UCB1CTLW0 |= UCTR | UCTXSTT;         // Send I2C start
}

void zeroDACEEPROM(){
    PTxData = (uint8_t*) txZero;
    TXByteCtr = sizeof(txZero);

    UCB0CTL1 |= UCTR | UCTXSTT;
    while (UCB1CTL1 & UCTXSTP);
}

#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector = EUSCI_B1_VECTOR
__interrupt void USCI_B1_ISR(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(EUSCI_B1_VECTOR))) USCI_B1_ISR (void)
#else
#error Compiler not supported!
#endif
{
    switch(__even_in_range(UCB1IV, USCI_I2C_UCBIT9IFG))
    {
        case USCI_NONE:          break;     // Vector 0: No interrupts
        case USCI_I2C_UCALIFG:   break;     // Vector 2: ALIFG
        case USCI_I2C_UCNACKIFG:            // Vector 4: NACKIFG
            UCB1CTLW0 |= UCTXSTT;           // resend start if NACK
            break;
        case USCI_I2C_UCSTTIFG:  break;     // Vector 6: STTIFG
        case USCI_I2C_UCSTPIFG:  break;     // Vector 8: STPIFG
        case USCI_I2C_UCRXIFG3:  break;     // Vector 10: RXIFG3
        case USCI_I2C_UCTXIFG3:  break;     // Vector 12: TXIFG3
        case USCI_I2C_UCRXIFG2:  break;     // Vector 14: RXIFG2
        case USCI_I2C_UCTXIFG2:  break;     // Vector 16: TXIFG2
        case USCI_I2C_UCRXIFG1:  break;     // Vector 18: RXIFG1
        case USCI_I2C_UCTXIFG1:  break;     // Vector 20: TXIFG1
        case USCI_I2C_UCRXIFG0:  break;     // Vector 22: RXIFG0
        case USCI_I2C_UCTXIFG0:             // Vector 24: TXIFG0
            if (TXByteCtr)                  // Check TX byte counter
            {
                UCB1TXBUF = *PTxData++;     // Load TX buffer
                TXByteCtr--;                // Decrement TX byte counter
            }
            else
            {
                UCB1CTLW0 |= UCTXSTP;       // I2C stop condition
                UCB1IFG &= ~UCTXIFG;        // Clear USCI_B1 TX int flag
            }
            break;
        case USCI_I2C_UCBCNTIFG: break;     // Vector 26: BCNTIFG
        case USCI_I2C_UCCLTOIFG: break;     // Vector 28: clock low timeout
        case USCI_I2C_UCBIT9IFG: break;     // Vector 30: 9th bit
        default: break;
    }
}
