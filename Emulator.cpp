#include "Emulator.h"
#include <iostream>
#include <cstring>

// register flags
#define FLAG_Z 7
#define FLAG_N 6
#define FLAG_H 5
#define FLAG_C 4

// timer defintions
#define TIMA 0xFF05
#define TMA 0xFF06
#define TMC 0xFF07
#define CLOCKSPEED 4194304

Emulator::Emulator() {
    // initializing starting state
    m_ProgramCounter = 0x100;
    m_RegisterAF.reg = 0x01B0;
    m_RegisterBC.reg = 0x0013;
    m_RegisterDE.reg = 0x00D8;
    m_RegisterHL.reg = 0x014D;
    m_StackPointer.reg = 0xFFFE;
    m_Rom[0xFF05] = 0x00;
    m_Rom[0xFF06] = 0x00;
    m_Rom[0xFF07] = 0x00;
    m_Rom[0xFF10] = 0x80;
    m_Rom[0xFF11] = 0xBF;
    m_Rom[0xFF12] = 0xF3;
    m_Rom[0xFF14] = 0xBF;
    m_Rom[0xFF16] = 0x3F;
    m_Rom[0xFF17] = 0x00;
    m_Rom[0xFF19] = 0xBF;
    m_Rom[0xFF1A] = 0x7F;
    m_Rom[0xFF1B] = 0xFF;
    m_Rom[0xFF1C] = 0x9F;
    m_Rom[0xFF1E] = 0xBF;
    m_Rom[0xFF20] = 0xFF;
    m_Rom[0xFF21] = 0x00;
    m_Rom[0xFF22] = 0x00;
    m_Rom[0xFF23] = 0xBF;
    m_Rom[0xFF24] = 0x77;
    m_Rom[0xFF25] = 0xF3;
    m_Rom[0xFF26] = 0xF1;
    m_Rom[0xFF40] = 0x91;
    m_Rom[0xFF42] = 0x00;
    m_Rom[0xFF43] = 0x00;
    m_Rom[0xFF45] = 0x00;
    m_Rom[0xFF47] = 0xFC;
    m_Rom[0xFF48] = 0xFF;
    m_Rom[0xFF49] = 0xFF;
    m_Rom[0xFF4A] = 0x00;
    m_Rom[0xFF4B] = 0x00;
    m_Rom[0xFFFF] = 0x00;

    // load a cartridge into memory
    memset(m_CartridgeMemory, 0, sizeof(m_CartridgeMemory));

    FILE *in;
    in = fopen("Tetris.gb", "rb");
    fread(m_CartridgeMemory, 1, 0x200000, in);
    fclose(in);

    // detect rom bank mode
    m_MBC1 = false;
    m_MBC2 = false;
    m_ROMBanking = false;
    switch(m_CartridgeMemory[0x147]) {
        case 1 : m_MBC1 = true; break;
        case 2 : m_MBC1 = true; break;
        case 3 : m_MBC1 = true; break;
        case 5 : m_MBC2 = true; break;
        case 6 : m_MBC2 = true; break;
        default : break;
    }

    // specify which rom bank is loaded into internal memory
    m_CurrentROMBank = 1;

    // initialize ram banking
    m_EnableRAM = false;
    memset(&m_RAMBanks, 0, sizeof(m_RAMBanks));
    m_CurrentRAMBank = 0;

    // initialize timer
    int frequency = 4096;
    m_TimerCounter = CLOCKSPEED / frequency;
    m_DividerCounter = 0;
    m_DividerRegister = 0;

    // initialize interrupts
    m_InterruptMaster = true;

    // initialize scanline
    m_ScanlineCounter = 0;
}

/**
 * Emulation loop
 */
// void Emulator::Update() {
//     const int MAX_CYCLES = 69905;
//     int cyclesThisUpdate = 0;

//     while(cyclesThisUpdate < MAX_CYCLES) {
//         int cycles = ExecuteNextOpcode();
//         cyclesThisUpdate += cycles;
//         UpdateTimers(cycles);
//         UpdateGraphics(cycles);
//         DoInterrupts();
//     }

//     RenderScreen();
// }

/**
 * Safely write to available memory
 */
void Emulator::WriteMemory(WORD address, BYTE data) {
    
    if(address < 0x8000) {
        // don't allow memory writing to the read only memory
        HandleBanking(address, data);
    } else if((address >= 0xA000) && (address < 0xC000)) {
        if(m_EnableRAM) {
            WORD newAddress = address - 0xA000;
            m_RAMBanks[newAddress + (m_CurrentRAMBank * 0x2000)] = data;
        }
    } else if((address >= 0xE000) && (address < 0xFE00)) {
        // writing to ECHO ram also writes in RAM
        m_Rom[address] = data;
        WriteMemory(address - 0x2000, data);
    } else if((address >= 0xFEA0) && (address < 0xFEFF)) {
        // this area is restricted
    } else if(TMC == address) {
        BYTE currentFreq = GetClockFreq();
        m_CartridgeMemory[TMC] = data;
        BYTE newFreq = GetClockFreq();

        if(currentFreq != newFreq) {
            SetClockFreq();
        }
    } else if(0xFF04 == address) {
        // trap the divider register
        m_Rom[0xFF04] = 0;
    } else if(address == 0xFF44) {
        m_Rom[address] = 0;
    } else if(address == 0xFF46) {
        DoDMATransfer(data);
    } else {
        m_Rom[address] = data;
    }

}

/**
 * Safely read from available memory
 */
BYTE Emulator::ReadMemory(WORD address) const {
    if((address >= 0x4000) && (address <= 0x7FFF)) {
        // reading from rom memory bank
        WORD newAddress = address - 0x4000;
        return m_CartridgeMemory[newAddress + (m_CurrentROMBank * 0x4000)];
    } else if((address >= 0xA000) && (address <= 0xBFFF)) {
        // reading from ram memory bank
        WORD newAddress = address - 0xA000;
        return m_RAMBanks[newAddress + (m_CurrentRAMBank * 0x2000)];
    }

    // else, return memory
    return m_Rom[address];
}

/**
 * Handle banking
 */
void Emulator::HandleBanking(WORD address, BYTE data) {
    if(address < 0x2000) {
        // do ram enabling
        if(m_MBC1 || m_MBC2) {
            DoRAMBankEnable(address, data);
        }
    } else if((address >= 0x2000) && (address < 0x4000)) {
        // do rom bank change
        if(m_MBC1 || m_MBC2) {
            DoChangeLoROMBank(data);
        }
    } else if((address >= 0x4000) && (address < 0x6000)) {
        // do rom or ram bank change
        // no ram bank in mbc2 so always use ram bank 0
        if(m_MBC1) {
            if(m_ROMBanking) {
                DoChangeHiRomBank(data);
            } else {
                DoRAMBankChange(data);
            }
        }
    } else if((address >= 0x6000) && (address < 0x8000)) {
        if(m_MBC1) {
            DoChangeROMRAMMode(data);
        }
    }
}

/**
 * Helper function to test bit at a given position
 */
bool TestBit(int number, int position) {
    return (number & (1 << position)) != 0;
}

/**
 * Enable ram banking
 */
void Emulator::DoRAMBankEnable(WORD address, BYTE data) {
    if(m_MBC2) {
        if (TestBit(address, 4)) return;
    }

    BYTE testData = data & 0xF;
    if(testData == 0xA)
        m_EnableRAM = true;
    else if(testData == 0x0)
        m_EnableRAM = false;
}

/**
 * Changing LO rom bank
 */
void Emulator::DoChangeLoROMBank(BYTE data) {
    if(m_MBC2) {
        m_CurrentROMBank = data & 0xF;
        if(m_CurrentROMBank == 0) m_CurrentROMBank++;
        return;
    }

    BYTE lower5 = data & 31;
    m_CurrentROMBank &= 224;
    m_CurrentROMBank |= lower5;
    if(m_CurrentROMBank == 0) m_CurrentROMBank++;
}

/**
 * Changing HI rom bank
 */
void Emulator::DoChangeHiRomBank(BYTE data) {
    // turn off the upper 3 bits of the current rom
    m_CurrentROMBank &= 31;

    // turn off the lower 5 bits of the data
    data &= 224;
    m_CurrentROMBank |= data;
    if(m_CurrentROMBank == 0) m_CurrentROMBank++;
}

/**
 * Changing ram banks
 */
void Emulator::DoRAMBankChange(BYTE data) {
    m_CurrentRAMBank = data & 0x3;
}

/**
 * Selecting rom or ram banking mode
 */
void Emulator::DoChangeROMRAMMode(BYTE data) {
    BYTE newData = data & 0x1;
    m_ROMBanking = (newData == 0) ? true : false;
    if(m_ROMBanking)
        m_CurrentRAMBank = 0;
}

/**
 * Update the timers
 */
void Emulator::UpdateTimers(int cycles) {
    DoDividerRegister(cycles);

    // the clock must be enabled to update the clock
    if(IsClockEnabled()) {
        m_TimerCounter -= cycles;

        // enough cpu clock cycles have happened to update the timer
        if(m_TimerCounter <= 0) {
            // reset m_TimerTracer to the correct value
            SetClockFreq();

            // timer about to overflow
            if(ReadMemory(TIMA) == 255) {
                WriteMemory(TIMA, ReadMemory(TMA));
                // RequestInterupt(2);
            } else {
                WriteMemory(TIMA, ReadMemory(TIMA) + 1);
            }
        }
    }
}

/**
 * Check if timer is enabled
 */
bool Emulator::IsClockEnabled() const {
    return TestBit(ReadMemory(TMC), 2) ? true : false;
}

/**
 * Getter for the clock's frequency
 */
BYTE Emulator::GetClockFreq() const {
    return ReadMemory(TMC) & 0x3;
}

/**
 * Setter for the clock's frequency
 */
void Emulator::SetClockFreq() {
    BYTE freq = GetClockFreq();
    switch(freq) {
        case 0: m_TimerCounter = 1024; break; // freq 4096
        case 1: m_TimerCounter = 16; break; // freq 262144
        case 2: m_TimerCounter = 64; break; // freq 65536
        case 3: m_TimerCounter = 256; break; // freq 16382
    }
}

/**
 * Divider register
 */
void Emulator::DoDividerRegister(int cycles) {
    m_DividerRegister += cycles;
    if(m_DividerCounter >= 255) {
        m_DividerCounter = 0;
        m_Rom[0xFF04]++;
    }
}

/**
 * Helper function for setting a bit
 */
BYTE BitSet(BYTE byte, int position) {
    if(position < 0 || position > 7) {
        return byte;
    }
    return byte | (1 << position);
}

/**
 * Requesting an interrupt
 */
void Emulator::RequestInterrupt(int id) {
    BYTE req = ReadMemory(0xFF0F);
    req = BitSet(req, id);
    WriteMemory(0xFF0F, id);
}

/**
 * Iterate through interrupts in memory and service them
 */
void Emulator::DoInterrupts() {
    if(m_InterruptMaster == true) {
        BYTE req = ReadMemory(0xFF0F);
        BYTE enabled = ReadMemory(0xFFFF);
        if(req > 0) {
            for(int i = 0; i < 5; i++) {
                if(TestBit(req, i) == true) {
                    if(TestBit(enabled, i))
                        ServiceInterrupt(i);
                }
            }
        }
    }
}

/**
 * Helper function for resetting a bit
 */
BYTE BitReset(BYTE byte, int position) {
    if(position < 0 || position > 7) {
        return byte;
    }
    return byte & ~(1 << position);
}

/**
 * Process the current interrupt
 */
// void Emulator::ServiceInterrupt(int interrupt) {
//     m_InterruptMaster = false;
//     BYTE req = ReadMemory(0xFF0F);
//     req = BitReset(req, interrupt);
//     WriteMemory(0xFF0F, req);

//     // we must save the current execution address by pushing it onto the stack
//     PushWordOntoStack(m_ProgramCounter);

//     switch(interrupt) {
//         case 0: m_ProgramCounter = 0x40; break;
//         case 1: m_ProgramCounter = 0x48; break;
//         case 2: m_ProgramCounter = 0x50; break;
//         case 4: m_ProgramCounter = 0x60; break;
//     }
// }

/**
 * Update the graphics
 */
// void Emulator::UpdateGraphics(int cycles) {
//     SetLCDStatus();

//     if(IsLCDEnabled())
//         m_ScanlineCounter -= cycles;
//     else
//         return;

//     if(m_ScanlineCounter <= 0) {
//         // move onto the next scanline
//         m_Rom[0xFF44]++;
//         BYTE currentLine = ReadMemory(0xFF44);

//         m_ScanlineCounter = 456;

        
//         if(currentLine == 144)
//             // we have entered vertical blank period
//             RequestInterrupt(0);
//         else if(currentLine > 153)
//             // if gone past scanline 153 reset to 0
//             m_Rom[0xFF44] = 0;
//         else if(currentLine < 144)
//             // draw the current scan line
//             DrawScanLine();
//     }
// }

/**
 * Set LCD status
 */
void Emulator::SetLCDStatus() {
    BYTE status = ReadMemory(0xFF41);
    if(false == IsClockEnabled()) {
        // set the mode to 1 during lcd disabled and reset scanline
        m_ScanlineCounter = 456;
        m_Rom[0xFF44] = 0;
        status &= 252;
        status = BitSet(status, 0);
        WriteMemory(0xFF41, status);
        return;
    }

    BYTE currentLine = ReadMemory(0xFF44);
    BYTE currentMode = status & 0x3;

    BYTE mode = 0;
    bool reqInt = false;

    // in vblank so set mode to 1
    if(currentLine >= 144) {
        mode = 1;
        status = BitSet(status, 0);
        status = BitReset(status, 1);
        reqInt = TestBit(status, 4);
    } else {
        int mode2bounds = 456 - 80;
        int mode3bounds = mode2bounds - 172;

        if(m_ScanlineCounter >= mode2bounds) {
            // mode 2
            mode = 2;
            status = BitSet(status, 1);
            status = BitReset(status, 0);
            reqInt = TestBit(status, 5);
        } else if(m_ScanlineCounter >= mode3bounds) {
            // mode 3
            mode = 3;
            status = BitSet(status, 1);
            status = BitSet(status, 0);
        } else {
            // mode 0
            mode = 0;
            status = BitReset(status, 1);
            status = BitReset(status, 0);
            reqInt = TestBit(status, 3);
        }
    }

    // just entered a mnew mode so request interrupt
    if(reqInt && (mode != currentMode))
        RequestInterrupt(1);
    
    // check coincidence flag
    BYTE ly = ReadMemory(0xFF44);
    if(ly == ReadMemory(0xFF45)) {
        status = BitSet(status, 2);
        if(TestBit(status, 6))
            RequestInterrupt(1);
    } else {
        status = BitReset(status, 2);
    }

    WriteMemory(0xFF41, status);
}

/**
 * Check if LCD is enabled
 */
bool Emulator::IsLCDEnabled() const {
    return TestBit(ReadMemory(0xFF40), 7);
}

/**
 * DMA transfer
 */
void Emulator::DoDMATransfer(BYTE data) {
    WORD address = data << 8; // source address is data * 100
    for(int i = 0; i < 0xA0; i++) {
        WriteMemory(0xFE00 + i, ReadMemory(address + i));
    }
}