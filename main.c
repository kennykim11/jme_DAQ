/****************************************************************
* HEADER FILES
***************************************************************/
#include <DAVE.h>                 //Declarations from DAVE Code Generation (includes SFR declaration)

	// GENERAL
		#define PACKET_SIZE 72 			// How large data packet is to be sent out over Ethernet
		uint32_t packet_count = 0; 		// Packet counter to check for lost packets


	// TIMING
		uint32_t millisec = 0;		// Value to capture the amount of milliseconds that have passed since program start
		uint32_t TC_ms = 0;
		uint32_t TC_us = 0;
		uint32_t ADC0_ms = 0;
		uint32_t ADC0_us = 0;
		uint32_t ADC1_ms = 0;
		uint32_t ADC1_us = 0;

	// FLAGS FOR READING
		uint8_t ReadTC = 0x00;		// Flag for Thermocouple read
		uint8_t ReadADC0 = 0x00;	// Flag for ADC0 Read -- Interrupt
		uint8_t ReadADC1 = 0x00;	// Flag for ADC1 Read -- Interrupt

	// FLAGS FOR WRITING
		uint8_t tx_flag = 0; // Ethernet data transmit flag

	// ADC VARIABLES
		uint8_t configArray[56] = {0x00}; // Note: overwritten by each ADC, so put a breakpoint after each ADC config if you want to read the data for an individal ADC

	// THERMOCOUPLE VARIABLES
		uint8_t thermocouple_ss = 0; // Determines which slave select is active for thermocouples

	// FUNCTION PROTOTYPES
		void adc_register_config();
		void xmc_ADC_setup();
		void parseTime(uint8_t data[]);

// ETHERNET CONFIG ////////////////////////////////////////////////////////////////////////////////////////////////////////
		/****************************************************************
		* ETHERNET MACROS AND DEFINES
		***************************************************************/
		/* Server IP address */
		#define SERVER_IP_ADDR0    192U
		#define SERVER_IP_ADDR1    168U
		#define SERVER_IP_ADDR2    1U
		#define SERVER_IP_ADDR3    5U

		/* Server HTTP port number */
		#define SERVER_HTTP_PORT   8080U

		/* Server domain name */
		#define DOMAIN_NAME "192.168.1.5"

		/* Enable DHCP bounding check for client
		 * (disabled by default) */
		#define DHCP_ENABLED 0U

		/* Client ID
		 * ATTENTION: Don't forget to set a different MAC address
		 * inside ETH_LWIP_0 APP as well.
		 * (0 = Device#1; 1 = Device #2) */
		#define DEVICE_ID 0U

		/****************************************************************
		* PROTOTYPES
		***************************************************************/
		void client_err(void *arg, err_t err);
		void client_init(void);
		err_t client_connected(void *arg, struct tcp_pcb *pcb, err_t err);
		void client_close(struct tcp_pcb *pcb);
		err_t client_sent(void *arg, struct tcp_pcb *pcb, u16_t len);
		void send_data(uint8_t data[64]);

		/****************************************************************
		* LOCAL DATA
		***************************************************************/

		/* Pointer to lwIP network interface structure */
		extern struct netif xnetif;

		/* TCP protocol control block */
		struct tcp_pcb *pcb_send, *pcb_open;

		/* Connection status */
		uint8_t connection_ready=0,pcb_valid=0;


		/****************************************************************
		* API IMPLEMENTATION
		***************************************************************/
		/**
		 * @client_err
		 *
		 * Error callback, called by lwip. Invalidates global protocol control block pointer in case of connection resets.
		 *
		 * @input  : none
		 *
		 * @output : none
		 *
		 * @return : none
		 *
		 * */
		void client_err(void *arg, err_t err)
		{
		  if (err == ERR_RST)
			pcb_valid=0;
		  return;
		}

		/**
		 * @client_init
		 *
		 * Initializes the connection to the server.
		 * Registers callback client_connected, which is called when connecting trials are done
		 * Registers callback client_error, which is called when there is an error on the connection
		 *
		 * @input  : none
		 *
		 * @output : none
		 *
		 * @return : none
		 *
		 * */
		void client_init(void)
		{
		  struct ip_addr dest;
		  IP4_ADDR(&dest, SERVER_IP_ADDR0, SERVER_IP_ADDR1, SERVER_IP_ADDR2, SERVER_IP_ADDR3);

		  if (pcb_open!=0)
			tcp_abort(pcb_open);
		  pcb_open = tcp_new();
		  tcp_err(pcb_open, client_err);
		  tcp_bind(pcb_open, IP_ADDR_ANY, SERVER_HTTP_PORT); //server port for incoming connection
		  tcp_arg(pcb_open, NULL);
		  tcp_connect(pcb_open, &dest, SERVER_HTTP_PORT, client_connected); //server port for incoming connection
		}

		/**
		 * @client_connected
		 *
		 * Confirmation callback, called by lwip. Store global protocol control block pointer in case of successfull connection, otherwise
		 * invalidate
		 *
		 * @input  : none
		 *
		 * @output : none
		 *
		 * @return : err_t error type
		 *
		 * */
		err_t client_connected(void *arg, struct tcp_pcb *pcb, err_t err)
		{
		  /* Connection succeded ?*/
		  if (err==ERR_OK)
		  {
			/* Store protocol control block for next send */
			connection_ready=1;
			pcb_valid=1;
			pcb_send=pcb;
		  }
		  else
		  {
			/* Close/Inavlidate protocol control block */
			client_close(pcb);
			pcb_valid=0;
		  }
		  return err;
		}

		/**
		 * @client_close
		 *
		 * Close connection
		 *
		 * @input  : pcb - pointer to protocol control block
		 *
		 * @output : none
		 *
		 * @return : none
		 *
		 * */
		void client_close(struct tcp_pcb *pcb)
		{
		  tcp_arg(pcb, NULL);
		  tcp_sent(pcb, NULL);
		  tcp_abort(pcb);
		}

		/**
		 * @client_close
		 *
		 * Confirmation callback, called by lwip when data was sent.
		 * Invalidate protocol control block and re-initialize connection for next transfer.
		 *
		 * @input  : pcb - protocl control block
		 *           len - length
		 *
		 * @output : none
		 *
		 * @return : err_t error type
		 *
		 * */
		err_t client_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
		{
		  /* Sending succeeded; Close protocol control block */
		  client_close(pcb);
		  pcb_valid=0;
		  /* Prepare already connection for next transfer */
		  client_init();
		  return ERR_OK;
		}

		/**
		 * @client_close
		 *
		 * Send data header to computer
		 *
		 * @input  : data - data to be sent
		 *
		 * @output : none
		 *
		 * @return : none
		 *
		 * */
		void send_data(uint8_t data[])
		{
		  static uint32_t connect_WD=0;
		  if ((connection_ready==1)&&(pcb_send!=0))
		  {
			connection_ready=0;
			connect_WD=0;
			tcp_sent(pcb_send, client_sent);
			tcp_write(pcb_send, (uint8_t*)data, PACKET_SIZE, 0);
			tcp_output(pcb_send);

		  }
		  else
		  {
			/* Connection watchdog triggered
			 * --> reset Protocol Control Block
			 */
			connect_WD++;
			if (connect_WD==10)
			{
			  client_close(pcb_send);
			  pcb_valid=0;
			}
		  }
		}



		/**
		 * @tim_sys_check_timeouts_wrap
		 *
		 * Timer callback to administrate the timeouts of lwip stack.
		 *
		 * @input  : none
		 *
		 * @output : none
		 *
		 * @return : none
		 *
		 * */
		void tim_sys_check_timeouts_wrap(void *args)
		{
		  sys_check_timeouts();
		}
// END ETHERNET CONFIG ////////////////////////////////////////////////////////////////////////////////////////////////////////

/**

* @brief main() - Application entry point
*
* <b>Details of function</b><br>
* This routine is the application entry point. It is invoked by the device startup code. It is responsible for
* invoking the APP initialization dispatcher routine - DAVE_Init() and hosting the place-holder for user application
* code.
*/
int main(void) {
	// VARIABLES
	DAVE_STATUS_t status;
	uint32_t timer_systimer_lwip; 				// Timer for Ethernet Checkouts???
	uint8_t dataArray[PACKET_SIZE] = {0x00}; 	// Create data packet
	uint8_t null[18] = {0x00}; 					// Null packet to "send" during ADC Transfers

	//DAVE STARTUP
		status = DAVE_Init(); /* Initialization of DAVE APPs  */
		if(status != DAVE_STATUS_SUCCESS) {
			/* Placeholder for error handler code.
			* The while loop below can be replaced with an user error handler. */
			XMC_DEBUG("DAVE APPs initialization failed\n");
			while(1U) {
			}
		}

	//Initialize ADCs
		//Unlock / Config
			// ADC0
				SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_ADC,  SPI_MASTER_SS_SIGNAL_0); // Change slave
				for (int i = 0; i <9000; i++){} // Dumb Delay (Remove?)
				adc_register_config();
			// ADC 1
				SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_ADC,  SPI_MASTER_SS_SIGNAL_1); // Change slave
				for (int i = 0; i <9000; i++){} // Dumb Delay (Remove?)
				adc_register_config();

		// Turn on ADCs
			// ADC0
				SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_ADC,  SPI_MASTER_SS_SIGNAL_0); // Change slave
				for (int i = 0; i <9000; i++){} // Dumb Delay (Remove?)
				xmc_ADC_setup();
			// ADC 1
				SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_ADC,  SPI_MASTER_SS_SIGNAL_1); // Change slave
				for (int i = 0; i <9000; i++){} // Dumb Delay (Remove?)
				xmc_ADC_setup();

		// RETURN TO ADC0
			SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_ADC,  SPI_MASTER_SS_SIGNAL_0); // Change slave
			for (int i = 0; i <9000; i++){} // Dumb Delay (Remove?)


	// Initialize and start lwip system timer
		timer_systimer_lwip = SYSTIMER_CreateTimer(10000, SYSTIMER_MODE_PERIODIC, tim_sys_check_timeouts_wrap,0); // WAS  //1000000
		SYSTIMER_StartTimer(timer_systimer_lwip);



	// Define out packet to be sent -- dummy data to see if it's changing with our code
		for (int i = 0; i < PACKET_SIZE; i++) {
			dataArray[i] = i;
		}

	// Enable interrupts once configuration complete
		PIN_INTERRUPT_Enable(&PIN_INTERRUPT_ADC0); 	// ADC0 DRDY Interrupt
		PIN_INTERRUPT_Enable(&PIN_INTERRUPT_ADC1); 	// ADC1 DRDY Interrupt
		INTERRUPT_Enable(&INTERRUPT_TC);			// Thermocouple Timer Interrupt
		INTERRUPT_Enable(&INTERRUPT_TIMESTAMP);		// Millisecond Timestamping Interrupt Enabled

		INTERRUPT_Enable(&INTERRUPT_ETH);			// Ethernet Timer Interrupt -- NOT INTENDED FOR FINAL CODE XXXXXXXXXXXXXXXXXXXXXXXXXX

	while(1) {

		// Thermocouple SPI Transfers
			if(ReadTC == 1){ // Does timer say we should transfer?
				if ( !SPI_MASTER_IsRxBusy(&SPI_MASTER_TC) ) { // Check if SPI is not busy
						switch(thermocouple_ss) { // Slave selection

							case 0: // Slave 0
								SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_TC, SPI_MASTER_SS_SIGNAL_0);
								SPI_MASTER_Receive(&SPI_MASTER_TC, dataArray, 4U);
							break;

							case 1: // Slave 1
								SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_TC, SPI_MASTER_SS_SIGNAL_1);
								SPI_MASTER_Receive(&SPI_MASTER_TC, dataArray+4, 4U);
							break;

							case 2: // Slave 2
								SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_TC, SPI_MASTER_SS_SIGNAL_2);
								SPI_MASTER_Receive(&SPI_MASTER_TC, dataArray+8, 4U);
							break;

							case 3: // Slave 3
								SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_TC, SPI_MASTER_SS_SIGNAL_3);
								SPI_MASTER_Receive(&SPI_MASTER_TC, dataArray+12, 4U);
							break;

							default : // We should never get here
								thermocouple_ss = 0;
								SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_TC, SPI_MASTER_SS_SIGNAL_0);
								SPI_MASTER_Receive(&SPI_MASTER_TC, dataArray, 4U);

						} // End switch

					thermocouple_ss++; // Increment slave

					if (thermocouple_ss > 3){ // All slaves read
						thermocouple_ss = 0; // reset to slave 0
						ReadTC = 0; //Reset Flag
						// NEED TO DO SOMETHING HERE ABOUT NEW DATA (NOT STALE)
					}

				} // End if !busy
			} // End TC Read

		// ADC SPI Transfers
			// NEED TO SET SOME SORT OF PRIORITY HERE, WHERE WE NEED TO HAVE ADC1 HAPPEN, EVEN IF ADC1 IS READY -- NOT SURE IF THIS IS A REAL PROBLEM ONCE WE ACTUALLY HAVE INTERRUPTS INSTEAD OF READ0 and READ1 AUTO-SET TO 1 AT BEGINNING OF LOOP
			// NOTE: I HAD TO MAKE THE FIFO IN THE DAVE APP 32, NOT 16, BECAUSE 16 WOULD NOT HOLD ENOUGH DATA AND THE SPI TRANSFER WOULD SPLIT

			// ADC0 SPI Transfers
				if (ReadADC0 == 1) { // Flag set
					if ( !SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC) ) { // SPI not already in transaction
						SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_ADC, SPI_MASTER_SS_SIGNAL_0); // Change to ADC0
						SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, dataArray+16, 18U);
						ReadADC0 = 0; // Reset Flag
					}
				}

			// ADC1 SPI Transfers
				if (ReadADC1 == 1) {
					if ( !SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC) ) { // SPI not already in transaction
						SPI_MASTER_EnableSlaveSelectSignal(&SPI_MASTER_ADC, SPI_MASTER_SS_SIGNAL_1); // Change to ADC1
						SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, dataArray+34, 18U);
						ReadADC1 = 0; // Reset Flag
					}
				}

		// Ethernet Transactions
			if (tx_flag == 1) { // if hardware timer reached period -- send data
				// Parse Times into respective bytes
					parseTime(dataArray);

				// Packet count split
					dataArray[67] = (packet_count >> 24) 	& 0xff; // MSB
					dataArray[68] = (packet_count >> 16) 	& 0xff;
					dataArray[69] = (packet_count >> 8) 	& 0xff; // Nice
					dataArray[70] = (packet_count >> 0)		& 0xff; // LSB

				// Transmit
					if ((connection_ready==0)&&(pcb_valid==0)) { // Connection already/still active?
						client_init(); // Re-Initialize TCP/IP connection
					}
					else {
						// Send data out
							send_data(dataArray);

						// Increment Packet
							packet_count++;

					}
					// Reset flag
					tx_flag = 0; //
			}

	} // End While Loop
} // End main


// INTERRUPTS /////////////////////////////////////////////////////////////////////////////////////

	// Timer configured with 1000us period = 1ms
		void TimeStampIRQ(void) {
			TIMER_ClearEvent(&TIMER_TIMESTAMP); // Clear Event Flag
			millisec++; 						// New device uptime
		}

	// Thermocouple trigger -- Timer configured with 100000us period = 100ms = 10Hz
		void TCIRQ(void) {
			TIMER_ClearEvent(&TIMER_TC);		// Clear Event Flag
			TC_us = TIMER_GetTime(&TIMER_TIMESTAMP) / 100; // Get microseconds from timer -- DAVE TIMER APP returns (us * 100);
			TC_ms = millisec;	// Grab milliseconds for capture time
			ReadTC = 1;		// Set flag to read Thermocouples
		}


	// Data Ready Interrupt for ADC0
		void ADC0_DRDY_INT(){
			ADC0_us = TIMER_GetTime(&TIMER_TIMESTAMP) / 100; // Get microseconds from timer -- DAVE TIMER APP returns (us * 100);
			ADC0_ms = millisec;	// Grab milliseconds for capture time
			ReadADC0 = 1; // Set flag to read ADC0
		}

	// Data Ready Interrupt for ADC1
		void ADC1_DRDY_INT(){
			ADC1_us = TIMER_GetTime(&TIMER_TIMESTAMP) / 100; // Get microseconds from timer -- DAVE TIMER APP returns (us * 100);
			ADC1_ms = millisec;	// Grab milliseconds for capture time
			ReadADC1 = 1; // Set flag to read ADC1
		}

	// Ethernet Timer (NOT USED IN FINAL PRODUCT)
		void ETHIRQ(){
			tx_flag = 1;
			DIGITAL_IO_ToggleOutput(&LED_INDICATOR); // LED Toggle for speed check on scope
		}


// FUNCIONS ///////////////////////////////////////////////////////////////////////////////////////

void adc_register_config(){
	// Register Configurations
		uint8_t unlock[3] = {0x06, 0x55, 0x0}; 				// Unlocks ADC
		uint8_t null[18] = {0x00};							// Sends null for reads
		uint8_t write_A_SYS_CFG[3] = {0x4B, 0x68, 0x00};	// b(01101000) -- Neg Charge Pump Powered Down | High-Res | 2.442 Internal Reference | Internal Voltage Enabled | 5/95% Comparator Threshold
		uint8_t write_D_SYS_CFG[3] = {0x4C, 0x3C, 0x00};	// b(00111100) -- Watchdog Disabled | No CRC | 12ns delay for DONE (not used) | 12ns delay for Hi-Z on DOUT | Fixed Frame Size (6 frames) | CRC disabled
		uint8_t write_CLK1[3] = {0x4D, 0x02, 0x00};			// b(00000010) -- XTAL CLK Source | CLKIN /2
		uint8_t write_CLK2_43kHz[3] = {0x4E, 0x4E, 0x00};	// b(01001110) -- ICLK / 4 | OSR = fMOD / 48
		uint8_t write_CLK2_8kHz[3] = {0x4E, 0x48, 0x00};	// b(01001000) -- ICLK / 4 | OSR = fMOD / 256
		// NOTE -- write_CLK2_43kHz gives a final sample rate of 42.667kHz
		// NOTE -- write_CLK2_8kHz gives a final sample rate of 8kHz


	// Clear configArray for debug
		for (uint8_t i = 0; i<  56; i++){
			configArray[i] = 0x00;
		}

	// Transfer Null
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, configArray, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	//  Unlock ADC for configuration
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, unlock, configArray, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Transfer Null
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, configArray+3, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Write to A_SYS_CFG (See Above)
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, write_A_SYS_CFG, configArray+6, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion
		// Transfer Null
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, configArray+9, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Write to D_SYS_CFG (See Above)
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, write_D_SYS_CFG, configArray+12, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Transfer Null
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, configArray+15, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Write to CLK1 (See Above)
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, write_CLK1, configArray+18, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Transfer Null
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, configArray+21, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Write to CLK2 (See Above)
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, write_CLK2_43kHz, configArray+24, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Transfer Null
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, configArray+27, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

}

void xmc_ADC_setup(){
	uint8_t null[18] = {0x00};						// Sends null for reads
	uint8_t write_ADC_ENA[3] = {0x4F, 0x0F, 0x00};	// b(00001111) -- Enables all ADC channels (note: no option to enable certain channels, all or nothing)
	uint8_t wakeup[3] = {0x00, 0x33, 0x00};			// b(00110011) -- Bring ADC out of standby (start collection)

	// Write to ADC_ENA (See Above)
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, write_ADC_ENA, configArray, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Transfer Null
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, configArray+30, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Wakeup ADC and start conversions
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, wakeup, configArray, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Transfer Null
		SPI_MASTER_Transfer(&SPI_MASTER_ADC, null, configArray+33, 3U);
		while(SPI_MASTER_IsRxBusy(&SPI_MASTER_ADC)){} // Wait for completion

	// Set to "infinite" frame length
		XMC_SPI_CH_SetFrameLength(XMC_SPI2_CH0, 64); // When set to 64, frame does not end based on DAVE App Configuration -- this allows us to grab all 144 bits of data out of the ADC during data collection

}


void parseTime(uint8_t data[]) {
	// TC ms
		data[52] = (TC_ms >> 16) 	& 0xff; // MSB
		data[53] = (TC_ms >> 8) 	& 0xff;
		data[54] = (TC_ms >> 0) 	& 0xff;

	// TC us
		data[55] = (TC_us >> 8) 	& 0xff; // MSB
		data[56] = (TC_us >> 0) 	& 0xff;

	// ADCO0 ms
		data[57] = (ADC0_ms >> 16) 	& 0xff; // MSB
		data[58] = (ADC0_ms >> 8) 	& 0xff;
		data[59] = (ADC0_ms >> 0) 	& 0xff;

	// ADCO0 us
		data[60] = (ADC0_us >> 8) 	& 0xff; // MSB
		data[61] = (ADC0_us >> 0) 	& 0xff;

	// ADC1 ms
		data[62] = (ADC1_ms >> 16) 	& 0xff; // MSB
		data[63] = (ADC1_ms >> 8) 	& 0xff;
		data[64] = (ADC1_ms >> 0) 	& 0xff;

	// ADC1 us
		data[65] = (ADC1_us >> 8) 	& 0xff;	// MSB
		data[66] = (ADC1_us >> 0) 	& 0xff;
}


/* NOTES:
ADC0 = IEPE
	IEPE0 = ADC0-CH1
	IEPE1 = ADC0-CH2
	IEPE2 = ADC0-CH3
	IEPE3 = ADC0-CH4

ADC1 = FB/CL
	FB0 = ADC1-CH1
	FB1 = ADC1-CH2
	CL0 = ADC1-CH3
	CL1 = ADC1-CH4

*/


/* FORMAT OF dataArray ////////////////////////////////////////////////////////////////////////////
Bytes:		| 	Data:
=======================================================
0 - 3		|	Thermocouple 0 Data (32 bits = 4 bytes)
4 - 7		|	Thermocouple 1 Data (32 bits = 4 bytes)
8 - 11		|	Thermocouple 2 Data (32 bits = 4 bytes)
12 - 15		|	Thermocouple 3 Data (32 bits = 4 bytes)
16 - 18		|	ADC0 Status 		(24 bits = 3 bytes)
19 - 21		|	ADC0 CH1 Data 		(24 bits = 3 bytes) -- 	IEPE0 Data
22 - 24		|	ADC0 CH2 Data 		(24 bits = 3 bytes) -- 	IEPE1 Data
25 - 27		|	ADC0 CH3 Data 		(24 bits = 3 bytes) -- 	IEPE2 Data
28 - 30		|	ADC0 CH4 Data 		(24 bits = 3 bytes) -- 	IEPE3 Data
31 - 33		|	ADC0 Zeros			(24 bits = 3 bytes)
34 - 36		|	ADC1 Status 		(24 bits = 3 bytes)
37 - 39		|	ADC1 CH0 Data 		(24 bits = 3 bytes) -- 	FB0 Data
40 - 42		|	ADC1 CH1 Data 		(24 bits = 3 bytes) -- 	FB1 Data
43 - 45		|	ADC1 CH2 Data 		(24 bits = 3 bytes) -- 	CL0 Data
46 - 48		|	ADC1 CH3 Data 		(24 bits = 3 bytes) -- 	CL1 Data
49 - 51		|	ADC1 Zeros			(24 bits = 3 bytes)
52 - 54		|	TC Time ms 			(24 bits = 3 bytes) --  Thermocouple Milliseconds Timestamp (24 bits = 4.66 hours of capture time)
55 - 56		|  	TC Time us 			(16 bits = 2 bytes) --  Thermocouple Microseconds Timestamp (16 bits = holds at least 1000us -- timer should keep this under 1000 anyway)
57 - 59		|	ADC0 Time ms 		(24 bits = 3 bytes) --  ADC0 Milliseconds Timestamp (24 bits = 4.66 hours of capture time)
60 - 61		|  	ADC0 Time us 		(16 bits = 2 bytes) --  ADC0 Microseconds Timestamp (16 bits = holds at least 1000us -- timer should keep this under 1000 anyway)
62 - 64		|	ADC1 Time ms 		(24 bits = 3 bytes) --  ADC1 Milliseconds Timestamp (24 bits = 4.66 hours of capture time)
65 - 66		|  	ADC1 Time us 		(16 bits = 2 bytes) --  ADC1 Microseconds Timestamp (16 bits = holds at least 1000us -- timer should keep this under 1000 anyway)
67 - 70		|	Packet Counter		(32 bits = 4 bytes) -- 	Packet counter to check for lost packets
71			|	Faults/Fresh/Stale	(8 	bits = 1 byte ) --	Faults / Flags if we want to add any | Identifies data is new -- mainly for Thermocouples as ADC data is pumped out all the time


// NOTES: ADC outputs 6 frames -- Status | CH1 | CH2 | CH3 | CH4 | Zeros
	- In the future we can write over the zeros

/*/////////////////////////////////////////////////////////////////////////////////////////////////
