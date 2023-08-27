# atarist-sidecart-pico
AtariST cartridge emulator based on Raspberry Pi Pico and RP2040

# Signals

The RP2040 runs at 125MHz, so each clock cycle is 8ns. Every character of the logic analyzer output is 8ns. 

The Atari ST runs at 8MHz, so each clock cycle is 125ns. But the system clock cycle is 4 times the CPU clock cycle, 
so the CPU clock cycle is 500ns. The CPU clock cycle is 62.5 characters of the logic analyzer output.

## !ROM3, !ROM4, !LDS and !UDS

Any external signal in the cartridge buses of the Atari ST can be read or write when the !ROM4 or !ROM3 signals are low. During
this time, the access to the read address bus and specialy the write data bus are allowed and there will not be any contention
conflict with the Atari ST memory bus.

The !LDS and !UDS signals are used to select the upper or lower byte of the data bus. The !LDS signal is active when the lower
and the !UDS signal is active when the upper byte of the data bus is selected. !LDS and !UDS are directly connected to the 
Motorola 68000 signals, so it's not worth to use them to control the RP2040, unless you want to use them jointly with the
!ROM3 and !ROM4 signals.

Hence, when the !ROM4 or !ROM3 signals are low, we will proceed as follows:
1. Read the address bus and wait until the address information is stable. This should take 1 Atari ST clock cycles (125ns).
2. Send the information to the data bus and wait until the data is stable. This should take 1 Atari ST clock cycles (125ns).
3. Wait until the !ROM4 or !ROM3 signals are high again and repeat the process.

ns    0                                                      SCycle 1 (500ns)                                              SCycle 2 (500ns)
------|-------------------------------------------------------------|--------------------------------------------------------------|

ns    0        80       160       240       320       400       480       560       640       720       800       880       960
------|---------|---------|---------|---------|---------|---------|---------|---------|---------|---------|---------|---------|----
!ROMx: ------------------------______________________________________------------------------______________________________________
!UDS:  ----------------------_______________________________________------------------------_______________________________________
!LDS:  ----------------------_______________________________________------------------------_______________________________________

## A1 to A15 address bus without ROMx signals

The address bus is read when the !ROM4 or !ROM3 signals are low. The address bus is 16 bits wide, so it will take 2 clock cycles

This is an example of a long word access to the address &FAF0F0. To retrieve these long word the 68000 will perform two consecutive
word accesses to the address &FAF0F0 and &FAF0F2. The first word access will retrieve the lower word and the second word access will
retrieve the upper word. 

As we can see in the logic analyzer output, the address is stable 56ns (8ns x 7 characters) before the !ROM4 signal is low. Considering
that the address bus signals also have to cross the level shifter, the address bus signals will be stable way before the !ROM4 signal
is low. So it's safe to read the address bus as soon as any !ROMx signas are low.

This diagram shows the address bus signals crossing the 75LVS245 level shifter with the !OE signal in low state. When the !OE signal
is low the level shifter is enabled and the signals are allowed to cross from one side to the other. So all the address bus signals
will propagate from the Atari ST side to the RP2040 side.

ns    0                                                      SCycle 1 (500ns)                                              SCycle 2 (500ns)
------|-------------------------------------------------------------|--------------------------------------------------------------|

ns    0        80       160       240       320       400       480       560       640       720       800       880       960
------|---------|---------|---------|---------|---------|---------|---------|---------|---------|---------|---------|---------|----
!ROMx: _______________________________________------------------------______________________________________-----------------------
!UDS:  ______________________________________-----------------------_______________________________________-----------------------_
!LDS:  ______________________________________-----------------------_______________________________________-----------------------_
A1:    ______________________________________________________---------------------------------------------------------------_______
A2:    _____________________________________________________________________________________________________________________-______
A3:    ____________________________________________________________________________________________________________________--------
A4:    ---------------------------------------------------------------------------------------------------------------------_______
A5:    ----------------------------------------------------------------------------------------------------------------------------
A6:    ---------------------------------------------------------------------------------------------------------------------_______
A7:    ---------------------------------------------------------------------------------------------------------------------_______
A8:    _____________________________________________________________________________________________________________________-------
A9:    _____________________________________________________________________________________________________________________-------
A10:   _____________________________________________________________________________________________________________________-______
A11:   _____________________________________________________________________________________________________________________-------
A12:   ----------------------------------------------------------------------------------------------------------------------------
A13:   ---------------------------------------------------------------------------------------------------------------------_______
A14:   ----------------------------------------------------------------------------------------------------------------------------
A15:   ----------------------------------------------------------------------------------------------------------------------------

## A1 to A15 address bus with ROMx signals control

To avoid unnecessary reads to the address bus, we can use the !ROM4 and !ROM3 signals to control when the address bus is read. The
 access to bus is allowed when the !ROM4 or !ROM3 signals are low. The !OE signal of the level shifter is controlled by the !ROM4
 and !ROM3 signals, so the address bus signals will be propagated to the RP2040 side only when the !ROM4 or !ROM3 signals are low.

Striclty speaking, the level shifter !OE signal could be connected always to GND and the address bus signals will be propagated 
always to the RP2040 side. In the RP2040 side we can use the !ROM4 and !ROM3 signals to control when the address bus is read.
 
