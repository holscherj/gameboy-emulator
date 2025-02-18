#include "Emulator.h"
#include <iostream>
#include <cstring>

#define FLAG_Z 7
#define FLAG_N 6
#define FLAG_H 5
#define FLAG_C 4

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

void Emulator::DoChangeROMRAMMode(BYTE data) {
    BYTE newData = data & 0x1;
    m_ROMBanking = (newData == 0) ? true : false;
    if(m_ROMBanking)
        m_CurrentRAMBank = 0;
}