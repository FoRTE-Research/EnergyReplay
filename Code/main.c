// Code for MSP430FR5994 controlling energy replay board V1.2

#include <msp430.h> 
#include <stdint.h>
#include <stddef.h>

#include "dac.h"

#define SENSE_RESISTOR_VAL 0.5f
#define LOAD_VOLTAGE_LIMIT 3.6f
#define LOAD_ADC_REF 3.3f
#define SHUNT_ADC_REF 2.5f

// LFMODCLK which sources ACLK is about 37.42 kHz
// Timestep in milliseconds
#define TIMER_PERIOD (TIMESTEP * 37420 / 1000)
#if TIMER_PERIOD > 65535
#error "Timer period overflows register, reduce timestep"
#endif

#define greenOn() P3OUT |= BIT3
#define greenOff() P3OUT &= ~BIT3
#define redOn() P3OUT |= BIT4
#define redOff() P3OUT &= ~BIT4

// Buttons pull pin to ground when pressed
#define s1Pressed() !(P3IN & BIT5)
#define s2Pressed() !(P3IN & BIT6)

// Board pins:
// P1.3, P1.4: ADC inputs (A3, A4). P1.3 must be manually connected to load
// P3.3, P3.4: green, red LEDs
// P3.5, P3.6: S1, S2. Buttons are normally-open connection to ground
// P5.0, P5.1: SDA, SCL for I2C (UCB1)

// ADC globals
uint8_t newAdcReading = 0;
uint16_t adcDiffAmp, adcLoad;

// Power globals
size_t powerIndex = 0;

void boardInit(){
    WDTCTL = WDTPW | WDTHOLD;   // Stop WDT
    PM5CTL0 &= ~LOCKLPM5;       // Clear GPIO high-z

    // ----- Init pins -----
    P3OUT &= ~(BIT3 | BIT4);        // Clear LED pins for init
    P3OUT |= BIT0 | BIT1 | BIT2;    // These are active low
    P3DIR |= (BIT0 | BIT1 | BIT2 | BIT3 | BIT4);     // LED pins to output
    P3DIR &= ~(BIT5 | BIT6);    // Button pins to input with pull up resistors
    P3REN |= (BIT5 | BIT6);
    P3OUT |= (BIT5 | BIT6);

    P5SEL0 |= (BIT0 | BIT1);    // P5.0, P5.1 controlled by I2C module
    P5SEL1 &= ~(BIT0 | BIT1);

    P1SEL0 |= BIT3 | BIT4;      // P1.3 input A3, P1.4 input A4 for ADC
    P1SEL1 |= BIT3 | BIT4;

    // ----- Init clocks -----
    FRCTL0 = FRCTLPW | NWAITS_1;                            // One FRAM wait state
    CSCTL0_H = CSKEY_H;                                     // Unlock CS registers
    CSCTL1 = DCOFSEL_0;                                     // Set DCO to 1MHz
    CSCTL2 = SELA__LFMODCLK | SELS__DCOCLK | SELM__DCOCLK;  // Set SMCLK = MCLK = DCO, ACLK = LFMODCLK (~39 kHz)
    CSCTL3 = DIVA__4 | DIVS__4 | DIVM__4;                   // Divide by 4 for errata
    CSCTL1 = DCOFSEL_4 | DCORSEL;                           // Set DCO to 16MHz
    __delay_cycles(60);                                     // Delay to let DCO settle
    CSCTL3 = DIVA__1 | DIVS__16 | DIVM__1;                  // MCLK = DC0 = 16 MHz, SMCLK = DCO/16 = 1 MHz
    CSCTL0_H = 0;                                           // Lock CS registers

    // ----- Init I2C -----
    UCB1CTLW0 = UCSWRST;                                    // Enable SW reset
    UCB1CTLW0 |= UCMODE_3 | UCMST | UCSSEL__SMCLK | UCSYNC; // I2C master mode, SMCLK
    UCB1BRW = 8;                                            // fSCL = SMCLK/4 = ~125 kHz
    UCB1I2CSA = SLAVE_ADDRESS;                              // Slave Address
    UCB1CTLW0 &= ~UCSWRST;                                  // Clear SW reset, resume operation
    UCB1IE |= UCTXIE0;                                      // Enable TX interrupt

    // ----- Init REF -----
    while(REFCTL0 & REFGENBUSY);                            // Let ref settle
    REFCTL0 |= REFVSEL_2 | REFON;                           // 2.5V ref on

    // ----- Init ADC -----
    // ADC uses ADC12OSC - default is fine
    ADC12CTL0 = ADC12SHT0_2 | ADC12ON;
    ADC12CTL1 = ADC12SHP | ADC12CONSEQ_1;                   // Sampling timer, sequence-of-channels
    ADC12CTL2 |= ADC12RES_2;                                // 12-bit conversion results
    ADC12IER0 |= ADC12IE0 | ADC12IE1;                       // Enable ADC conv complete interrupt
    ADC12MCTL0 |= ADC12INCH_3 | ADC12VRSEL_1;               // Mem0: A3 ADC input select, Vref = 2.5v from above
    ADC12MCTL1 |= ADC12INCH_4 | ADC12VRSEL_0 | ADC12EOS;    // Mem1: A4 ADC input select, Vref = 3.3v, end of conversion sequence
    while(!(REFCTL0 & REFGENRDY));                          // Let ref settle

    // ----- Init timer -----
    TA0CTL = TASSEL__ACLK | MC__UP | TACLR;
    TA0CCR0 = TIMER_PERIOD;
    TA0CCTL0 = CCIE;

    __bis_SR_register(GIE);
}

volatile float actualPower, desiredPower, loadVoltage, shuntVoltage;

int main(void)
{
    boardInit();

    __delay_cycles(100000);

//    while(1){
////        P3OUT ^= BIT3;
////        P3OUT ^= (BIT0 | BIT1 | BIT2);
//        __delay_cycles(5000000);
//        setDACVoltage(2000);
////        P3OUT ^= BIT4;
//        __delay_cycles(5000000);
//        setDACVoltage(1000);
//    }

    // Output ACLK on P2.0 for trim
//    P2DIR |= BIT0;
//    P2SEL0 |= BIT0;
//    P2SEL1 |= BIT0;

    setDACVoltage(0);

    __delay_cycles(100000);

    uint16_t dacLevel = 0;
    uint8_t clampDac = 0;

    while(1){
        ADC12CTL0 |= ADC12ENC | ADC12SC;    // Begin ADC conversion

        // Op amps may produce high voltages at the ADC pins which could damage the chip
        // None of the ADC channels should come near saturating
        // If they have, something wrong - don't increase DAC voltage any more
        if(adcDiffAmp > 4050 || adcLoad > 4050){
            clampDac = 1;
            redOn();
        }else{
            clampDac = 0;
        }

        // Do we have more trace to process?
        // If not, turn off DAC, set red LED, and sit
        if(sizeof(float) * powerIndex >= sizeof(powerArray)){
            setDACVoltage(0);
            __delay_cycles(25000);
            __bic_SR_register(GIE);
            greenOff();
            while(1);
        }

        // Calculate actual voltages from ADC readings
        loadVoltage = ((float) adcLoad / 4095) * LOAD_ADC_REF;          // V1.2: load channel measures load through
        loadVoltage *= 2;                                               // 0.5x voltage divider
        __no_operation();
        shuntVoltage = ((float) adcDiffAmp / 4095) * SHUNT_ADC_REF;     // Shunt channel sees shunt voltage through
        __no_operation();                                               // through 150x amplifier
        shuntVoltage /= 150;

        __no_operation();

        // If load voltage is too high, clamp DAC
        if(loadVoltage > LOAD_VOLTAGE_LIMIT){
            clampDac = 1;
            redOn();
        }

//        actualPower = (shuntVoltage / SENSE_RESISTOR_VAL) * loadVoltage;
        // Let's get actual power into normal terms and then see what we can do about precision
//        actualPower = ((((adcDiffAmp / 4095) * SHUNT_ADC_REF) / 150) / SENSE_RESISTOR_VAL) * loadVoltage;
        __no_operation();
        actualPower = ((float) adcDiffAmp * SHUNT_ADC_REF * loadVoltage) / ((float) 4095 * (float) 150 * SENSE_RESISTOR_VAL);
        __no_operation();
        desiredPower = POWER_COEFFICIENT * powerArray[powerIndex];
        if(actualPower < desiredPower && dacLevel < 4095 && !clampDac){
            redOff();
            setDACVoltage(dacLevel++);
        }else if(actualPower > desiredPower && dacLevel > 0){
            redOff();
            setDACVoltage(dacLevel--);
        }else if(dacLevel == 0 || dacLevel == 4095){
            redOn();
        }

        __no_operation(); // For debugger

        // Decide whether to increase or decrease DAC output

//        if(s1Pressed()){
//            greenOn();
//        }else{
//            greenOff();
//        }
//        if(s2Pressed()){
//            redOn();
//        }else{
//            redOff();
//        }
    }
}

#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector = ADC12_B_VECTOR
__interrupt void ADC12_ISR(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(ADC12_B_VECTOR))) ADC12_ISR (void)
#else
#error Compiler not supported!
#endif
{
    switch (__even_in_range(ADC12IV, ADC12IV__ADC12RDYIFG))
    {
        case ADC12IV__NONE:        break;   // Vector  0:  No interrupt
        case ADC12IV__ADC12OVIFG:  break;   // Vector  2:  ADC12MEMx Overflow
        case ADC12IV__ADC12TOVIFG: break;   // Vector  4:  Conversion time overflow
        case ADC12IV__ADC12HIIFG:  break;   // Vector  6:  ADC12BHI
        case ADC12IV__ADC12LOIFG:  break;   // Vector  8:  ADC12BLO
        case ADC12IV__ADC12INIFG:  break;   // Vector 10:  ADC12BIN
        case ADC12IV__ADC12IFG0:            // Vector 12:  ADC12MEM0 Interrupt
            adcDiffAmp = ADC12MEM0;
            break;
        case ADC12IV__ADC12IFG1:            // Vector 14:  ADC12MEM1
            adcLoad = ADC12MEM1;
            break;
        case ADC12IV__ADC12IFG2:   break;   // Vector 16:  ADC12MEM2
        case ADC12IV__ADC12IFG3:   break;   // Vector 18:  ADC12MEM3
        case ADC12IV__ADC12IFG4:   break;   // Vector 20:  ADC12MEM4
        case ADC12IV__ADC12IFG5:   break;   // Vector 22:  ADC12MEM5
        case ADC12IV__ADC12IFG6:   break;   // Vector 24:  ADC12MEM6
        case ADC12IV__ADC12IFG7:   break;   // Vector 26:  ADC12MEM7
        case ADC12IV__ADC12IFG8:   break;   // Vector 28:  ADC12MEM8
        case ADC12IV__ADC12IFG9:   break;   // Vector 30:  ADC12MEM9
        case ADC12IV__ADC12IFG10:  break;   // Vector 32:  ADC12MEM10
        case ADC12IV__ADC12IFG11:  break;   // Vector 34:  ADC12MEM11
        case ADC12IV__ADC12IFG12:  break;   // Vector 36:  ADC12MEM12
        case ADC12IV__ADC12IFG13:  break;   // Vector 38:  ADC12MEM13
        case ADC12IV__ADC12IFG14:  break;   // Vector 40:  ADC12MEM14
        case ADC12IV__ADC12IFG15:  break;   // Vector 42:  ADC12MEM15
        case ADC12IV__ADC12IFG16:  break;   // Vector 44:  ADC12MEM16
        case ADC12IV__ADC12IFG17:  break;   // Vector 46:  ADC12MEM17
        case ADC12IV__ADC12IFG18:  break;   // Vector 48:  ADC12MEM18
        case ADC12IV__ADC12IFG19:  break;   // Vector 50:  ADC12MEM19
        case ADC12IV__ADC12IFG20:  break;   // Vector 52:  ADC12MEM20
        case ADC12IV__ADC12IFG21:  break;   // Vector 54:  ADC12MEM21
        case ADC12IV__ADC12IFG22:  break;   // Vector 56:  ADC12MEM22
        case ADC12IV__ADC12IFG23:  break;   // Vector 58:  ADC12MEM23
        case ADC12IV__ADC12IFG24:  break;   // Vector 60:  ADC12MEM24
        case ADC12IV__ADC12IFG25:  break;   // Vector 62:  ADC12MEM25
        case ADC12IV__ADC12IFG26:  break;   // Vector 64:  ADC12MEM26
        case ADC12IV__ADC12IFG27:  break;   // Vector 66:  ADC12MEM27
        case ADC12IV__ADC12IFG28:  break;   // Vector 68:  ADC12MEM28
        case ADC12IV__ADC12IFG29:  break;   // Vector 70:  ADC12MEM29
        case ADC12IV__ADC12IFG30:  break;   // Vector 72:  ADC12MEM30
        case ADC12IV__ADC12IFG31:  break;   // Vector 74:  ADC12MEM31
        case ADC12IV__ADC12RDYIFG: break;   // Vector 76:  ADC12RDY
        default: break;
    }
}

#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer0_A0_ISR (void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(TIMER0_A0_VECTOR))) Timer0_A0_ISR (void)
#else
#error Compiler not supported!
#endif
{
    powerIndex++;
    P3OUT ^= BIT3;
}
