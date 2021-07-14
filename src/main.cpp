/*

Copyright 2011-2017 Tyler Gilbert

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

 */

#include <cstdio>
#include <sapi/sys.hpp>
#include <sapi/var.hpp>
#include <sapi/hal.hpp>


static volatile bool m_stop;

static void show_usage(const Cli & cli);
static void * process_uart_input(void * args);

int main(int argc, char * argv[]) {
	String input;
	int ret;
	Cli cli(argc, argv);
	cli.set_publisher("Stratify Labs, Inc");
	cli.handle_version();
	UartAttributes uart_attributes;
	Thread input_thread(
				Thread::StackSize(2048),
				Thread::IsDetached(false)
				);

	String action;
	String frequency;
	String rx;
	String tx;
	String stop_bits;
	String parity;
	String port;
	String value;

	action = cli.get_option(
				("action"),
				Cli::Description("specify the action bridge|read|write")
				);

	port = cli.get_option(
				("port"),
				Cli::Description("The UART port number to use 0|1|2...")
				);

	frequency = cli.get_option(
				("frequency"),
				Cli::Description("specify the bitrate in Hz (default is 115200)")
				);

	rx = cli.get_option(
				("rx"),
				Cli::Description("specify the RX pin (default is to use system value)")
				);

	tx = cli.get_option(
				("tx"),
				Cli::Description("specify the TX pin (default is to use system value)")
				);

	stop_bits = cli.get_option(
				("stop"),
				Cli::Description("specify the number of stop bits as 0.5|1|1.5|2 (default is 1)")
				);

	parity = cli.get_option(
				("parity"),
				Cli::Description("specify the number of stop bits as none|odd|even (default is none)")
				);

	value = cli.get_option(
				("value"),
				Cli::Description("specify a string when writing to the UART")
				);

	if( cli.get_option(("help")) == "true" ){
		show_usage(cli);
	}

	if( rx.is_empty() == false ){ uart_attributes->pin_assignment.rx = Pin::from_string(rx); }
	if( tx.is_empty() == false ){ uart_attributes->pin_assignment.tx = Pin::from_string(tx); }
	uart_attributes.set_frequency( frequency.to_integer() );
	if( uart_attributes.frequency() == 0 ){
		uart_attributes.set_frequency(115200);
	}

	if( stop_bits == "0.5" ){ uart_attributes->o_flags |= Uart::IS_STOP0_5; }
	else if( stop_bits == "1" ){ uart_attributes->o_flags |= Uart::IS_STOP1; }
	else if( stop_bits == "1.5" ){ uart_attributes->o_flags |= Uart::IS_STOP1_5; }
	else if( stop_bits == "2" ){ uart_attributes->o_flags |= Uart::IS_STOP2; }

	if( parity == "even" ){ uart_attributes->o_flags |= Uart::IS_PARITY_EVEN; }
	else if( parity == "odd" ){ uart_attributes->o_flags |= Uart::IS_PARITY_ODD; }

	bool is_all_defaults = false;

	if( frequency.is_empty() && rx.is_empty() && tx.is_empty() && stop_bits.is_empty() && parity.is_empty() ){
		is_all_defaults = true;
	}

	uart_attributes.set_port( port.to_integer() );

	Uart uart(uart_attributes.port());
	if( uart.open(fs::OpenFlags::read_write()) < 0 ){
		printf("%s>Failed to open UART port %d\n", cli.name().cstring(), uart_attributes.port());
		perror("Failed");
		exit(1);
	}

	if( is_all_defaults ){
		printf("%s>Starting UART probe on port %d with default system settings\n", cli.name().cstring(), uart_attributes.port());
		if( (ret = uart.set_attributes()) < 0 ){
			printf("%s>UART Failed to init with BSP settings %d (%d)\n", cli.name().cstring(), uart.error_number(), ret);
			exit(1);
		}
	} else {
		printf("%s>Starting UART probe on port %d at %ldbps\n", cli.name().cstring(), uart_attributes.port(), uart_attributes.freq());
		if( (ret = uart.set_attributes(uart_attributes)) < 0 ){
			printf("%s>UART Failed to init with user settings %d (%d)\n", cli.name().cstring(), uart.error_number(), ret);
			exit(1);
		}
	}

	if( action == "bridge" ){

		printf("bridging UART to stdio use enter `exit` to quit\n");

		input_thread.create(
					Thread::Function(process_uart_input),
					Thread::FunctionArgument(&uart)
					);

		input.resize(64);

		do {
			input.clear();
			fgets(input.to_char(), input.length(), stdin);
			if( input != "exit\n" ){ uart.write(input); }
		} while( input != "exit\n" );

		printf("%s>Stopping\n", cli.name().cstring());
		m_stop = true;

		input_thread.kill(
					Signal::CONT
					); //this will interrupt the blocking UART read and cause m_stop to be read

		input_thread.join(); //this will suspend until input_thread is finished

	} else if( action == "write" ){
		if( value.is_empty() ){
			printf("error: specify value with --value=<string>\n");
			show_usage(cli);
		}

		if( uart.write(value) < 0 ){
			printf("error: failed to write %s to uart\n", value.cstring());
		}

	} else {
		printf("error: action must be specified using --action=[bridge|write]\n");
		show_usage(cli);
	}


	printf("%s>Closing UART\n", cli.name().cstring());
	uart.close();

	printf("%s>Exiting\n", cli.name().cstring());

	return 0;
}

void * process_uart_input(void * args){
	Uart * uart = (Uart*)args;
	int result;
	Data input(64);
	do {
		input.clear();
		if( (result = uart->read(input)) > 0 ){
			printf("%s",
					 String(
						 input.to_const_char(),
						 String::Length(result)
						 ).cstring()
					 );
			fflush(stdout);
		}
	} while( !m_stop );

	return 0;
}


void show_usage(const Cli & cli){
	printf("Usage: uarttool --port=<port> --action=<action> [options]\n");
	printf("Examples:\n");
	printf("- Bridge UART to stdio using system settings: uarttool --uart=0 --action=bridge\n");
	printf("- Write a string to the UART: uarttool --uart=0 --action=write --value=Hello\n");
	cli.show_options();
	exit(1);
}
