#ifndef EMULATOR_H
#define EMULATOR_H

// Type definitions for the Gameboy's data types
typedef unsigned char BYTE;
typedef char SIGNED_BYTE;
typedef unsigned short WORD;
typedef signed short SIGNED_WORD;

class Emulator {
    public:
        Emulator();
        void Update();
        void WriteMemory(WORD address, BYTE data);
        BYTE ReadMemory(WORD address) const;
        void HandleBanking(WORD address, BYTE data);
        void DoRAMBankEnable(WORD address, BYTE data);
        void DoChangeLoROMBank(BYTE data);
        void DoChangeHiRomBank(BYTE data);
        void DoRAMBankChange(BYTE data);
        void DoChangeROMRAMMode(BYTE data);
        void UpdateTimers(int cylces);
        bool IsClockEnabled() const;
        BYTE GetClockFreq() const;
        void SetClockFreq();
        void DoDividerRegister(int cycles);
        ~Emulator() = default;

        // game cartridge memory
        BYTE m_CartridgeMemory[0x200000];

        // screen resolution emulation
        BYTE m_ScreenData[160][140][3];

        // main memory
        BYTE m_Rom[0x10000];

        // registers
        union Register {
            WORD reg;
            struct {
                BYTE lo;
                BYTE hi;
            };
        };
        
        Register m_RegisterAF;
        Register m_RegisterBC;
        Register m_RegisterDE;
        Register m_RegisterHL;

        // program counter and stack pointer
        WORD m_ProgramCounter;
        Register m_StackPointer;

        // rom bank modes
        bool m_MBC1;
        bool m_MBC2;
        bool m_ROMBanking;
        BYTE m_CurrentROMBank;

        // ram banks
        bool m_EnableRAM;
        BYTE m_RAMBanks[0x8000];
        BYTE m_CurrentRAMBank;

        // timer
        int m_TimerCounter;
        int m_DividerCounter;
        int m_DividerRegister;

    private:
};

#endif