#ifndef EMULATOR_H
#define EMULATOR_H

// Type definitions for the Gameboy's data types
typedef unsigned char BYTE ;
typedef char SIGNED_BYTE ;
typedef unsigned short WORD ;
typedef signed short SIGNED_WORD ;

#define FLAG_MASK_Z 128
#define FLAG_MASK_N 64
#define FLAG_MASK_H 32
#define FLAG_MASK_C 16
#define FLAG_Z 7
#define FLAG_N 6
#define FLAG_H 5
#define FLAG_C 4

class Emulator {
    public:
        // color
        enum COLOR {
            WHITE,
            LIGHT_GRAY,
            DARK_GRAY,
            BLACK
        };

        // methods
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
        void RequestInterrupt(int id);
        void DoInterrupts();
        void ServiceInterrupt(int interrupt);
        void UpdateGraphics(int cycles);
        void SetLCDStatus();
        bool IsLCDEnabled() const;
        void DoDMATransfer(BYTE data);
        void DrawScanLine();
        void RenderTiles(BYTE lcdControl);
        void RenderSprites(BYTE lcdControl);
        COLOR GetColor(BYTE colorNum, WORD address) const;
        void KeyPressed(int key);
        void KeyReleased(int key);
        BYTE GetJoypadState() const;
        BYTE ExecuteNextOpcode();
        void ExecuteOpcode(BYTE opcode);
        void ExecuteExtendedOpcode();
        void CPU_8BIT_LOAD(BYTE &reg);
        void CPU_16BIT_LOAD(WORD &reg);
        void CPU_REG_LOAD(BYTE &reg, BYTE load, int cycles);
        void CPU_REG_LOAD_ROM(BYTE &reg, WORD address);
        void CPU_8BIT_ADD(BYTE &reg, BYTE toAdd, int cycles, bool useImmediate, bool addCarry);
        void CPU_8BIT_SUB(BYTE &reg, BYTE toSubtract, int cycles, bool useImmediate, bool subCarry);
        void CPU_8BIT_AND(BYTE &reg, BYTE toAnd, int cycles, bool useImmediate);
        void CPU_8BIT_OR(BYTE &reg, BYTE toOr, int cycles, bool useImmediate);
        void CPU_8BIT_XOR(BYTE &reg, BYTE toXOr, int cycles, bool useImmediate);
        void CPU_8BIT_COMPARE(BYTE reg, BYTE toSubtract, int cycles, bool useImmediate); // dont pass a reference
        void CPU_8BIT_INC(BYTE &reg, int cycles);
        void CPU_8BIT_DEC(BYTE &reg, int cycles);
        void CPU_8BIT_MEMORY_INC(WORD address, int cycles);
        void CPU_8BIT_MEMORY_DEC(WORD address, int cycles);
        void CPU_RESTARTS(BYTE n);

        void CPU_16BIT_DEC(WORD &word, int cycles);
        void CPU_16BIT_INC(WORD &word, int cycles);
        void CPU_16BIT_ADD(WORD &reg, WORD toAdd, int cycles);

        void CPU_JUMP(bool useCondition, int flag, bool condition);
        void CPU_JUMP_IMMEDIATE(bool useCondition, int flag, bool condition);
        void CPU_CALL(bool useCondition, int flag, bool condition);
        void CPU_RETURN(bool useCondition, int flag, bool condition);

        void CPU_SWAP_NIBBLES(BYTE &reg);
        void CPU_SWAP_NIB_MEM(WORD address);
        void CPU_SHIFT_LEFT_CARRY(BYTE &reg);
        void CPU_SHIFT_LEFT_CARRY_MEMORY(WORD address);
        void CPU_SHIFT_RIGHT_CARRY(BYTE &reg, bool resetMSB);
        void CPU_SHIFT_RIGHT_CARRY_MEMORY(WORD address, bool resetMSB);

        void CPU_RESET_BIT(BYTE &reg, int bit);
        void CPU_RESET_BIT_MEMORY(WORD address, int bit);
        void CPU_TEST_BIT(BYTE reg, int bit, int cycles);
        void CPU_SET_BIT(BYTE &reg, int bit);
        void CPU_SET_BIT_MEMORY(WORD address, int bit);

        void CPU_DAA();

        void CPU_RLC(BYTE &reg);
        void CPU_RLC_MEMORY(WORD address);
        void CPU_RRC(BYTE &reg);
        void CPU_RRC_MEMORY(WORD address);
        void CPU_RL(BYTE &reg);
        void CPU_RL_MEMORY(WORD address);
        void CPU_RR(BYTE &reg);
        void CPU_RR_MEMORY(WORD address);

        void CPU_SLA(BYTE &reg);
        void CPU_SLA_MEMORY(WORD address);
        void CPU_SRA(BYTE &reg);
        void CPU_SRA_MEMORY(WORD address);
        void CPU_SRL(BYTE &reg);
        void CPU_SRL_MEMORY(WORD address);
        void WriteByte(WORD address, BYTE data);
        void PushWordOntoStack(WORD word);
        WORD ReadWord() const;
        WORD PopWordOffStack();
        ~Emulator() = default;

        // game cartridge memory
        BYTE m_CartridgeMemory[0x200000];

        // screen resolution emulation
        BYTE m_ScreenData[160][140][3];

        // main memory
        BYTE m_Rom[0x10000];
        bool m_UsingMemoryModel16_8 ;
        unsigned long long m_TotalOpcodes;
        bool m_Halted;

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
        int m_CurrentClockSpeed;
        int m_CyclesThisUpdate;

        // interrupts
        bool m_InterruptMaster;
        bool m_PendingInteruptDisabled;
		bool m_PendingInteruptEnabled;

        // scanlines
        int m_ScanlineCounter;

        // joypad
        BYTE m_JoypadState;
};

#endif