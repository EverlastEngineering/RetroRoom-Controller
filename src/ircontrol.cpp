#include "ircontrol.h"

const uint16_t kIrLed = IR_CONTROL_PIN;
IRsend irsend(kIrLed);

void ir_control_init() { irsend.begin(); }

void sendSonyPower() {
	/**
	 * This turns out to be harder that I thought to get sony codes and convert
	them for use with the IRRemote library.

	I found some codes here.
	https://tasmota.github.io/docs/Codes-for-IR-Remotes/#ir-remote-codes-sony-kdl-ex540

	I reverse engineered the "hex code" to the integer code found elsewhere as
	the following.

	To convert a sony code to the "hex code"
	Find the code you want:
	https://www.sbprojects.net/knowledge/ir/sirc.php
	http://www.hifi-remote.com/sony/Sony_tv.htm

	Then go here:
	https://www.rapidtables.com/convert/number/decimal-to-binary.html
	Convert the code to binary
	eg 21 becomes 10101
	represented as 7 bit = 0010101
	reverse the order of the bit = 1010100
	these are the 7 command bits

	the address bits are also found on the sirc link above
	for tv, the address is 1, and you'd perform the same procedure but as 5 bit.
	easy: 10000

	So, the binary bit stream is 1010100 and 10000 together, or, 101010010000.

	Finally, convert that to hex.
	https://www.rapidtables.com/convert/number/binary-to-hex.html
	That comes out as A90, which is confirmed as the demo "hex code" for power
	on a sony.

	750 = discrete power on
	430 = discrete video 3 <-- confirmed on the first link! Yay!
	E30 = discrete video 4
	130 = discrete video 5 (yuv)

	If these are right, they could be set to the definitions for the inputs.
	*/
	irsend.sendSony(0xa90, 12, 2); // 12 bits & 2 repeats
	Serial.print("Sony Power Cycle IR Sent");
}