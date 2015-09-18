<h1><a id="user-content-rfid2ln" class="anchor" href="#rfid2ln" aria-hidden="true"><span class="octicon octicon-link"></span></a>RFID to Loconet (r) interface</h1>

Combining the mrrwa (http://mrrwa.org/) and MFRC522 (https://github.com/miguelbalboa/rfid) projects to create a RFID over Loconet (r) interface for the roling stock localisation in model railroad control programs (main target is Rocrail - www.rocrail.net, as this is the program I'm using).

Requirements:
- LocoNet library (http://mrrwa.org/, https://github.com/mrrwa/LocoNet);
- MFRC522 library (https://github.com/miguelbalboa/rfid)

The RFID data is sent over Loconet using a variable length message type (0xEx), with 12 bytes length:

`
0xE4 0x0C 0x41 <ADDR_H> <ADDR_L> <UID0_LSB> <UID1_LSB> <UID2_LSB> <UID3_LSB> <UID4_LSB> <UID_MSBS> <CHK_SUMM>
`

where the UIDX_LSB contains the bite b6..b0 of the corresponding UIDx, and the UID_MSBS contains the MSBits of 
UID0..UID4 as b0..b4.

<a name="hardware"></a>
<h2><a id="hardware" class="anchor" href="#hardware" aria-hidden="true"><span class="octicon octicon-link"></span></a>Hardware</h2>

To connect the Arduino board to the Loconet (R) bus, use the LocoNet interface presented on mrrwa.org (http://mrrwa.org/loconet-interface/).

The connections between the Arduino and the MFRC522 board are described in the code (and on MFRC522 library's site  at https://github.com/miguelbalboa/rfid).

<a name="to-do"></a>
<h2><a id="to-do" class="anchor" href="#to-do" aria-hidden="true"><span class="octicon octicon-link"></span></a>TO DO's</h2>
- implement of answering to the Loconet Query command (similar to the LocoIO - http://users.telenet.be/deloof/page6.htmlLocoIO)
- implement the (board) address change (similar to the LocoIO)
<a name="user-content-license"></a>
<h2><a id="user-content-license" class="anchor" href="#license" aria-hidden="true"><span class="octicon octicon-link"></span></a>License</h2>

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this software dedicate any and all copyright interest in the software to the public domain. We make this dedication for the benefit of the public at large and to the detriment of our heirs and successors. We intend this dedication to be an overt act of relinquishment in perpetuity of all present and future rights to this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to http://unlicense.org/
