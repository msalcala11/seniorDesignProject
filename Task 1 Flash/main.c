/*
 * Author: Marty Alcala
 * Take Home Lab 2 - Flash
 * main.c
 */
#include "F2802x_Device.h"

// Configure standalone flash operation
#pragma CODE_SECTION(InitFlash, "RamFun")
void InitFlash(void) {
EALLOW;
FlashRegs.FPWR.bit.PWR = 3;
FlashRegs.FBANKWAIT.bit.RANDWAIT = 2;
FlashRegs.FBANKWAIT.bit.PAGEWAIT = 2;
FlashRegs.FOPT.bit.ENPIPE = 1;
EDIS;
asm(" RPT #6 || NOP");
}
#include "string.h"
extern Uint16 RamFun_loadstart;
extern Uint16 RamFun_loadsize;
extern Uint16 RamFun_runstart;

void SysClkInit(); //Clock Initialization
void ADCInit(); // ADC Initialization
void PWMInit(); // PWM Initialization
void TimerInit(); // Timer Initialization
void InterruptInit(); // Interrupt Initialization
void LedInit(); // LED Initializatioin
void ServiceWatchdog(); // Watchdog Servicing Function
void CheckPushButton3(); // Checks the value of PB3 and adjusts counterThreshold accordingly
interrupt void AdcISR(void); // Interrupt routine called on each ADC end of conversion
interrupt void TimerISR(void); // Interrupt routine called at 100kHz

Uint16 counter = 0; // Our global counter incremented with each interrupt
Uint16 measuredValue; // The measured value of the ePWM1A voltage
Uint16 dataArray[1000]; // stores the latest ADC measurement of ePWM1A
Uint16 TBPRD; // TBPRD value used in this program (stored globally for convenient access when setting duty cycle)
Uint16 pushButtonPressed; // If 1, pushbutton 3 is pressed, if 0, pushbutton 3 is not pressed
Uint16 compareValue; // The current value stored in the ePWM compare register

void main(void) {
	// Load our flash based program into RAM
	memcpy(&RamFun_runstart, &RamFun_loadstart, (Uint32) &RamFun_loadsize);
	InitFlash();
	
	// Enable write access protected registers
	EALLOW;

	// Disable the watchdog register so that we can have an
	// arbitrarily long initialization procedure
	SysCtrlRegs.WDCR = 0x0068;

	// Set the leds as GPIO outputs
	LedInit();

	// Set the System Clock Frequency to 10MHz
	SysClkInit();
	TimerInit();
	InterruptInit();
	ADCInit();
	PWMInit();

	// Reenable the watchdog register
	SysCtrlRegs.WDCR = 0x0028;

	// Revoke write access to protected registers
	EDIS;

	// Start the timer
	CpuTimer0Regs.TCR.bit.TSS = 0;
	while(1){
		ServiceWatchdog();
	}

}

void SysClkInit(){
	// Set the system clock to 90MHz

	// Disable failed oscillator detect logic
	SysCtrlRegs.PLLSTS.bit.MCLKOFF = 1;
	// Set the factor fosc is multiplied by
	SysCtrlRegs.PLLCR.bit.DIV = 9;
	//Check if SysCtrlRegs.PLLSTS.bit.PLLLOCKS is 1 (If not, wait for it to lock)
	while(SysCtrlRegs.PLLSTS.bit.PLLLOCKS != 1){
		// wait for the register to lock
	}
	// Re-enable failed oscillator detect logic
	SysCtrlRegs.PLLSTS.bit.MCLKOFF = 0;
	// Set the factor fosc is divided by (3 means divided by 1)
	SysCtrlRegs.PLLSTS.bit.DIVSEL = 3;
}

void TimerInit(){
	//	Formula: ftmr = fclk/((0+1)(PRD.all+1))

	// Set Timer frequency to 100kHz
	Uint32 systemClockFrequency = 90000000;
	Uint32 desiredFrequency = 100000;

	//	Set PRD according to the formula above
	CpuTimer0Regs.PRD.all = (systemClockFrequency/desiredFrequency) - 1;

	//  Set TPR and TPRH to 0 for simplicity so we can just worry about PRD
	CpuTimer0Regs.TPRH.bit.TDDRH = 0;
	CpuTimer0Regs.TPR.bit.TDDR = 0;

	// Stop the timer
	CpuTimer0Regs.TCR.bit.TSS = 1;

	// Load the timer with our newly set pre-scale values above
	CpuTimer0Regs.TCR.bit.TRB = 1;

	// Enable timer interrupts
	CpuTimer0Regs.TCR.bit.TIE = 1;

}

void ADCInit(){

	// Conifugre and initialize the ADC module

	//Set the ADC clock to 45MHz
	SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 1; // Enable the ADC module to be clocked
	asm(" NOP");
	asm(" NOP"); // wait 2 clock cycles to let the change stabilize
	AdcRegs.ADCCTL2.bit.CLKDIV2EN = 1; // Divide the system clock by 2 (making the result 45MHz)
	AdcRegs.ADCCTL2.bit.ADCNONOVERLAP = 1; // Do not allow overlap of samples

	//Power up and enable the ADC Module
	AdcRegs.ADCCTL1.bit.ADCPWDN = 1; // Power up this bad boy
	AdcRegs.ADCCTL1.bit.ADCBGPWD = 1; // Power up buffer circuitry inside the core
	AdcRegs.ADCCTL1.bit.ADCREFPWD = 1; // Power up reference buffers
	AdcRegs.ADCCTL1.bit.ADCENABLE = 1; // Enable the ADC

	// Wait for 1 ms before requesting a conversion to ensure accuracy
	Uint32 i = 0;
	for(i = 0; i < (.001*10000000); i++){
		asm(" NOP");
	}

	//Configure the conversions
	AdcRegs.ADCSOC0CTL.bit.CHSEL = 0x0; // Set the first �start of conversion� sample pin to be ADC-A0
	AdcRegs.ADCSOC0CTL.bit.ACQPS = 0x6; // Set sampling hold time. Since we need to wait at least 80ns to sample, at a 5MHz clock the minimum of 7 clock cycles should be more than enough to satisfy an 80ns hold time
	AdcRegs.ADCSOC0CTL.bit.TRIGSEL = 0x0; // Set the conversion start trigger to software only

	//CONFIGURE ADC INTERRUPTS
	AdcRegs.INTSEL1N2.bit.INT1SEL = 0; //Connect ADCINT1 to EOC0
	AdcRegs.INTSEL1N2.bit.INT1E = 1; //Enable ADCINT1
	AdcRegs.ADCCTL1.bit.INTPULSEPOS = 1; // Ensure INT pulse generation occurs 1 cycle prior to ADC result latching into its result register
}

void PWMInit(){

	//Initialize the PWM registers

	//Enable the module clock
	SysCtrlRegs.PCLKCR1.bit.EPWM1ENCLK = 1; // Clock ePWM1
	asm(" NOP");
	asm(" NOP"); // wait 2 clock cycles to let the change stabilize
	//Configure the Time Base Clock and Counter
	EPwm1Regs.TBCTL.bit.CTRMODE = 2; // Set the count to up-down mode

	//Formula: fpwm = fclk/(2 � TBPRD)(HSPCLKDIV)(CLKDIV)
	Uint32 systemClock = 90000000; // 90MHz
	Uint32 desiredPWMfrequency = 1000; //1kHz

	TBPRD = systemClock/(2*desiredPWMfrequency);
	EPwm1Regs.TBPRD = TBPRD; // Set fpwm to 1 kHz based on a 90MHz clock
	EPwm1Regs.TBCTL.bit.HSPCLKDIV = 0; //divide by 1
	EPwm1Regs.TBCTL.bit.CLKDIV = 0; // divide by 1

	//Set the Output Actions
	EPwm1Regs.AQCTLA.bit.CAU = 1; //Set EPWMA to low when the counter equals the active CMPA register and the counter is incrementing.
	EPwm1Regs.AQCTLA.bit.CAD = 2; //Set EPWMA to high when the time-base counter equals the active CMPA register and the counter is decrementing.
	EPwm1Regs.AQCTLB.bit.CAU = 2; //Set EPWMB to high when the counter equals the active CMPB register and the counter is incrementing.
	EPwm1Regs.AQCTLB.bit.CAD = 1; //Set EPWMB to low when the time-base counter equals the active CMPB register and the counter is decrementing.

	//Set the Event Triggers
	EPwm1Regs.ETSEL.bit.SOCASEL = 2; // Enable event time-base counter equal to period (TBCTR = TBPRD)
	EPwm1Regs.ETSEL.bit.SOCAEN = 1; //Enable EPWM1SOCA pulse
	EPwm1Regs.ETPS.bit.SOCAPRD  = 1; //These bits determine how many selected ETSEL[SOCASEL] events need to occur before an EPWMxSOCA pulse is generated. To be generated, the pulse must be enabled (ETSEL[SOCAEN] = 1). The SOCA pulse will be generated even if the status flag is set from a previous start of conversion (ETFLG[SOCA] = 1). Once the SOCA pulse is generated, the ETPS[SOCACNT] bits will automatically be cleared.

	//Enable the Time Base Clock
	SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 1;
	asm(" NOP");
	asm(" NOP"); // wait 2 clock cycles to let the change stabilize

	//Utilize the PWM Pins
	//Map and Set the Duty Cycle
	EPwm1Regs.CMPA.half.CMPA = (Uint16)TBPRD*.9; // Compare A = 350 TBCLK counts

	GpioCtrlRegs.GPAMUX1.bit.GPIO0 = 1; //Set the 0 pin to ePWM1 output A

	GpioCtrlRegs.GPBMUX1.bit.GPIO32 = 0; // Mux pin to be general purpose I/0
	GpioCtrlRegs.GPBDIR.bit.GPIO32 = 1; // Set pin to be an output
	GpioDataRegs.GPBSET.bit.GPIO32 = 1; // Send the PWM reset signal

}

void InterruptInit(){
	//Initialize the Interrupt System Registers:

	// Configure Timer Interrupts

	// Enable fetching from the PIE vector table
	PieCtrlRegs.PIECTRL.bit.ENPIE = 1;

	// Load our defined interrupt routine into the PIE Vect Table
	PieVectTable.TINT0 = &TimerISR;

	// Enable interrupts to be triggered by CPU Timer 0 (row 1, col 7 of thie pie vector table)
	PieCtrlRegs.PIEIER1.bit.INTx7 = 1;

	// Enable Interrupts at the CPU Level:
	// Enables level INT1 at the CPU level, disables all other levels - (INT1 contains CPU Timer 0)
	IER = 0x0001;
	// Enable cpu interrupts
	EINT;

	// CONFIGURE ADC INTERRUPTS

	// Register the ISR that will run after we are notified of the conversion completion in the PieVect table
	PieVectTable.ADCINT1 = &AdcISR;

	// Enable the ADC interrupt at the pie level
	PieCtrlRegs.PIEIER1.bit.INTx1 = 1;
}

void LedInit(){
	// Disable software based pullup resistors for inputs that already have
	// such resistors implemented on the external hardware (outputs dont need a pullup)
	GpioCtrlRegs.GPAPUD.bit.GPIO0 = 1;
	GpioCtrlRegs.GPAPUD.bit.GPIO1 = 1;
	GpioCtrlRegs.GPAPUD.bit.GPIO2 = 1;
	GpioCtrlRegs.GPAPUD.bit.GPIO3 = 1;
	GpioCtrlRegs.GPAPUD.bit.GPIO12 = 1;

	// Set the LED fields to be outputs
	GpioCtrlRegs.GPADIR.bit.GPIO0 = 1; //LED 1
	GpioCtrlRegs.GPADIR.bit.GPIO1 = 1; //LED 2
	GpioCtrlRegs.GPADIR.bit.GPIO2 = 1; //LED 4
	GpioCtrlRegs.GPADIR.bit.GPIO3 = 1; //LED 3

	// First lets turn off all LEDs (they are active low)
	GpioDataRegs.GPASET.bit.GPIO0 = 1;
	GpioDataRegs.GPASET.bit.GPIO1 = 1;
	GpioDataRegs.GPASET.bit.GPIO2 = 1;
	GpioDataRegs.GPASET.bit.GPIO3 = 1;

	// Set pushbutton 3 to be an input
	GpioCtrlRegs.GPADIR.bit.GPIO12 = 0;
}

void CheckPushButton3(){
	pushButtonPressed = GpioDataRegs.GPADAT.bit.GPIO12;
}

void ServiceWatchdog(){

	EALLOW;
	SysCtrlRegs.WDKEY = 0x55;
	SysCtrlRegs.WDKEY = 0xAA;
	EDIS;

}

interrupt void AdcISR(void){

	// Read and store the integer value of the conversion of SOC0
	measuredValue = AdcResult.ADCRESULT0;

	// Clear ADCINT1 to show we have read the result
	AdcRegs.ADCINTFLGCLR.bit.ADCINT1 = 1;

	// Acknowledge Interrupt
	PieCtrlRegs.PIEACK.bit.ACK1 = 1;

}

interrupt void TimerISR(void){

	// Check to see if PB3 is pressed
	CheckPushButton3();

	// Let's store the current measuredValue of the PWM pin voltage in our array
	dataArray[counter] = measuredValue;
	counter++;

	// Reset the counter if we have reached the bounds of our data Array
	if(counter > 999)
		counter = 0;

	if(pushButtonPressed)
		// set duty cycle to .1;
		compareValue = (Uint16)(TBPRD*0.1);
	else
		// set duty cycle to .9;
		compareValue = (Uint16)(TBPRD*0.9);

	EPwm1Regs.CMPA.half.CMPA = compareValue;

	// Request new conversions
	AdcRegs.ADCSOCFRC1.bit.SOC0 = 1; // Request a conversion on SOC0

	// Show that the interrupt has been serviced and re-enable future interrupts
	// from category 1 (a category including CPU Timer 0)
	PieCtrlRegs.PIEACK.bit.ACK1 = 1;
}

