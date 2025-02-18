#include "Config.h"
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
    m_UsingMemoryModel16_8 = true;
    m_EnableRAM = false;
    memset(&m_RAMBanks, 0, sizeof(m_RAMBanks));
    m_CurrentRAMBank = 0;

    // initialize timer
    int frequency = 4096;
    m_TimerCounter = CLOCKSPEED / frequency;
    m_DividerCounter = 0;
    m_DividerRegister = 0;
    m_CurrentClockSpeed = 1024;
    m_CyclesThisUpdate = 0;
    m_Halted = false;

    // initialize interrupts
    m_InterruptMaster = false;
    m_PendingInteruptDisabled = false;
    m_PendingInteruptEnabled = false;

    // initialize scanline
    m_ScanlineCounter = 0;

    // joypad
    m_JoypadState = 0xFF;

}

/**
 * Emulation loop
 */
void Emulator::Update() {
    const int MAX_CYCLES = 69905;
    int cyclesThisUpdate = 0;

    while(cyclesThisUpdate < MAX_CYCLES) {
        int cycles = ExecuteNextOpcode();
        cyclesThisUpdate += cycles;
        UpdateTimers(cycles);
        UpdateGraphics(cycles);
        DoInterrupts();
    }

    // RenderScreen();
}

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
    } else if(0xFF00 == address) {
        return GetJoypadState();
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
 * Process the current interrupt
 */
void Emulator::ServiceInterrupt(int interrupt) {
    m_InterruptMaster = false;
    BYTE req = ReadMemory(0xFF0F);
    req = BitReset(req, interrupt);
    WriteMemory(0xFF0F, req);

    // we must save the current execution address by pushing it onto the stack
    PushWordOntoStack(m_ProgramCounter);

    switch(interrupt) {
        case 0: m_ProgramCounter = 0x40; break;
        case 1: m_ProgramCounter = 0x48; break;
        case 2: m_ProgramCounter = 0x50; break;
        case 4: m_ProgramCounter = 0x60; break;
    }
}

/**
 * Update the graphics
 */
void Emulator::UpdateGraphics(int cycles) {
    SetLCDStatus();

    if(IsLCDEnabled())
        m_ScanlineCounter -= cycles;
    else
        return;

    if(m_ScanlineCounter <= 0) {
        // move onto the next scanline
        m_Rom[0xFF44]++;
        BYTE currentLine = ReadMemory(0xFF44);

        m_ScanlineCounter = 456;

        
        if(currentLine == 144)
            // we have entered vertical blank period
            RequestInterrupt(0);
        else if(currentLine > 153)
            // if gone past scanline 153 reset to 0
            m_Rom[0xFF44] = 0;
        else if(currentLine < 144)
            // draw the current scan line
            DrawScanLine();
    }
}

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

/**
 * Draw a single scanline
 */
void Emulator::DrawScanLine() {
    BYTE lcdControl = ReadMemory(0xFF40);
    if(TestBit(lcdControl, 0))
        RenderTiles(lcdControl);
    if(TestBit(lcdControl, 1))
        RenderSprites(lcdControl);
}

/**
 * Render the tiles from memory
 */
void Emulator::RenderTiles(BYTE lcdControl) {
    WORD tileData = 0;
    WORD backgroundMemory = 0;
    bool unsig = true;

    // where to draw the visual area and the window
    BYTE scrollY = ReadMemory(0xFF42);
    BYTE scrollX = ReadMemory(0xFF43);
    BYTE windowY = ReadMemory(0xFF4A);
    BYTE windowX = ReadMemory(0xFF4B);

    bool usingWindow = false;

    // is the window enabled?
    if(TestBit(lcdControl, 5)) {
        // is the current scanline we're drawing within the Y pos?
        if(windowY <= ReadMemory(0xFF44)) {
            usingWindow = true;
        }
    }

    // which tile data are we using?
    if(TestBit(lcdControl, 4)) {
        tileData = 0x8000;
    } else {
        // this memory region uses signed bytes as tile identifiers
        tileData = 0x8800;
        unsig = true;
    }

    if(false == usingWindow) {
        if(TestBit(lcdControl, 3))
            backgroundMemory = 0x9C00;
        else
            backgroundMemory = 0x9800;
    } else {
        // which window memory?
        if(TestBit(lcdControl, 6))
            backgroundMemory = 0x9C00;
        else
            backgroundMemory = 0x9800;
    }

    BYTE yPos = 0;

    // yPos is used to calculate which of 32 vertical tiles the
    // current scanline is drawing
    if(!usingWindow)
        yPos = scrollY + ReadMemory(0xFF44);
    else
        yPos = ReadMemory(0xFF44) - windowY;
    
    // which of the 8 vertical pixels of the current
    // tile is the scanline on?
    WORD tileRow = (((BYTE) (yPos / 8)) * 32);

    // time to start drawing the 160 horizontal pixels
    // for this scanline
    for(int pixel = 0; pixel < 160; pixel++) {
        BYTE xPos = pixel + scrollX;

        // translate the current x pos to window space if necessary
        if(usingWindow) {
            if(pixel >= windowX) {
                xPos = pixel - windowX;
            }
        }

        // which of the 32 horizontal tiles does this xPos fall within?
        WORD tileCol = (xPos / 8);
        SIGNED_WORD tileNum;

        // get the tile identity number. Remember it can be signed or unsigned
        WORD tileAddress = backgroundMemory + tileRow + tileCol;
        if(unsig)
            tileNum = (BYTE)ReadMemory(tileAddress);
        else
            tileNum = (SIGNED_BYTE)ReadMemory(tileAddress);

        // deduce where this tile identifier is in memory
        WORD tileLocation = tileData;

        if(unsig)
            tileLocation += (tileNum * 16);
        else
            tileLocation += ((tileNum + 128) * 16);

        // find the correct vertical line we're on of the
        // tile to get the tile data from memory
        BYTE line = yPos % 8;
        line *= 2;
        BYTE data1 = ReadMemory(tileLocation + line);
        BYTE data2 = ReadMemory(tileLocation + line + 1);

        int colorBit = xPos % 8;
        colorBit -= 7;
        colorBit *= -1;

        int colorNum = BitGetVal(data2, colorBit);
        colorNum <<= 1;
        colorNum |= BitGetVal(data2, colorBit);

        // now that we have the color id, get the actual
        // color from the pallette 0xFF47
        COLOR col = GetColor(colorNum, 0xFF47);
        int red = 0;
        int green = 0;
        int blue = 0;

        // setup the RGB values
        switch(col) {
            case WHITE: red = 255; green = 255; blue = 255; break;
            case LIGHT_GRAY: red = 0xCC; green = 0xCC; blue = 0xCC; break;
            case DARK_GRAY: red = 0x77; green = 0x77; blue = 0x77; break;
        }

        int finally = ReadMemory(0xFF44);

        // safety boundary check
        if((finally < 0) || (finally > 143) || (pixel < 0) || (pixel > 159)) {
            continue;
        }

        m_ScreenData[pixel][finally][0] = red;
        m_ScreenData[pixel][finally][1] = green;
        m_ScreenData[pixel][finally][2] = blue;
    } 
}

void Emulator::RenderSprites(BYTE lcdControl) {
   bool use8x16 = false;
   if (TestBit(lcdControl,2))
     use8x16 = true;

   for (int sprite = 0; sprite < 40; sprite++) {
     // sprite occupies 4 bytes in the sprite attributes table
     BYTE index = sprite * 4;
     BYTE yPos = ReadMemory(0xFE00 + index) - 16;
     BYTE xPos = ReadMemory(0xFE00 + index + 1) - 8;
     BYTE tileLocation = ReadMemory(0xFE00+index + 2);
     BYTE attributes = ReadMemory(0xFE00+index + 3);

     bool yFlip = TestBit(attributes, 6);
     bool xFlip = TestBit(attributes, 5);

     int scanline = ReadMemory(0xFF44);

     int ysize = 8;
     if (use8x16)
       ysize = 16;

     // does this sprite intercept with the scanline?
     if ((scanline >= yPos) && (scanline < (yPos + ysize))) {
       int line = scanline - yPos;

       // read the sprite in backwards in the y axis
       if (yFlip) {
         line -= ysize;
         line *= -1;
       }

       line *= 2; // same as for tiles
       WORD dataAddress = (0x8000 + (tileLocation * 16)) + line;
       BYTE data1 = ReadMemory(dataAddress);
       BYTE data2 = ReadMemory(dataAddress + 1);

       // its easier to read in from right to left as pixel 0 is
       // bit 7 in the color data, pixel 1 is bit 6 etc...
       for (int tilePixel = 7; tilePixel >= 0; tilePixel--) {
         int colorbit = tilePixel;
         // read the sprite in backwards for the x axis
         if (xFlip) {
           colorbit -= 7;
           colorbit *= -1;
         }

         // the rest is the same as for tiles
         int colorNum = BitGetVal(data2,colorbit);
         colorNum <<= 1;
         colorNum |= BitGetVal(data1,colorbit);

         WORD colorAddress = TestBit(attributes, 4) ? 0xFF49 : 0xFF48;
         COLOR col = GetColor(colorNum, colorAddress);

         // white is transparent for sprites.
         if (col == WHITE)
           continue;

         int red = 0;
         int green = 0;
         int blue = 0;

         switch(col) {
           case WHITE: red = 255; green = 255; blue = 255; break;
           case LIGHT_GRAY: red = 0xCC; green = 0xCC; blue = 0xCC; break;
           case DARK_GRAY: red = 0x77; green = 0x77; blue = 0x77; break;
         }

         int xPix = 0 - tilePixel;
         xPix += 7;

         int pixel = xPos + xPix;

         // sanity check
         if ((scanline < 0) || (scanline > 143) || (pixel < 0) || (pixel > 159)) {
           continue;
         }

         m_ScreenData[pixel][scanline][0] = red;
         m_ScreenData[pixel][scanline][1] = green;
         m_ScreenData[pixel][scanline][2] = blue;
       }
     }
   }
}

Emulator::COLOR Emulator::GetColor(BYTE colorNum, WORD address) const {
   COLOR res = WHITE;
   BYTE palette = ReadMemory(address);
   int hi = 0;
   int lo = 0;

   // which bits of the color palette does the color id map to?
   switch (colorNum)
   {
     case 0: hi = 1; lo = 0; break;
     case 1: hi = 3; lo = 2; break;
     case 2: hi = 5; lo = 4; break;
     case 3: hi = 7; lo = 6; break;
   }

   // use the palette to get the color
   int color = 0;
   color = BitGetVal(palette, hi) << 1;
   color |= BitGetVal(palette, lo);

   // convert the game color to emulator color
   switch (color)
   {
     case 0: res = WHITE; break;
     case 1: res = LIGHT_GRAY; break;
     case 2: res = DARK_GRAY; break;
     case 3: res = BLACK; break;
   }

   return res;
}

void Emulator::KeyPressed(int key) {
    bool previouslyUnset = false;

    // if setting from 1 to 0 we may have to request an interrupt
    if(TestBit(m_JoypadState, key) == false)
        previouslyUnset = true;
    
    // remember if a keypressed its bit is 0 not 1
    m_JoypadState = BitReset(m_JoypadState, key);

    // button pressed
    bool button = true;

    // is this a standard button or a directional button?
    if(key > 3)
        button = true;
    else // directional button
        button = false;

    BYTE keyReq = m_Rom[0xFF00];
    bool requestInterrupt = false;

    // only request interrupt if the button just pressed
    // is the style of the button the game is interested in
    if(button && !TestBit(keyReq, 5))
        requestInterrupt = true;
    else if(!button && !TestBit(keyReq, 4))
        requestInterrupt = true;
    
    // request interrupt
    if(requestInterrupt && !previouslyUnset)
        RequestInterrupt(4);
}

/**
 * Reset joypad state on key released
 */
void Emulator::KeyReleased(int key) {
    m_JoypadState = BitSet(m_JoypadState, key);
}

/**
 * Get current joypad state
 */
BYTE Emulator::GetJoypadState() const {
    BYTE res = m_Rom[0xFF00];
    // flip all the bits
    res ^= 0xFF;

    // are we interested in the standard buttons?
    if(!TestBit(res, 4)) {
        BYTE topJoypad = m_JoypadState >> 4;
        topJoypad |= 0xF0; // turn the top 4 bits on
        res &= topJoypad; // show what buttons are pressed
    } else if(!TestBit(res, 5)) {
        // directional buttons
        BYTE bottomJoypad = m_JoypadState & 0xF;
        bottomJoypad |= 0xF0;
        res &= bottomJoypad;
    }

    return res;
}

/**
 * Execute next opcode in memory
 */
BYTE Emulator::ExecuteNextOpcode( )
{

	BYTE opcode = m_Rom[m_ProgramCounter] ;

	if ((m_ProgramCounter >= 0x4000 && m_ProgramCounter <= 0x7FFF) || (m_ProgramCounter >= 0xA000 && m_ProgramCounter <= 0xBFFF))
		opcode = ReadMemory(m_ProgramCounter) ;

	if (!m_Halted)
	{
		m_ProgramCounter++ ;
		m_TotalOpcodes++ ;

		ExecuteOpcode( opcode ) ;


	}
	else
	{
		m_CyclesThisUpdate += 4;
	}

	// we are trying to disable interupts, however interupts get disabled after the next instruction
	// 0xF3 is the opcode for disabling interupt
	if (m_PendingInteruptDisabled)
	{
		if (ReadMemory(m_ProgramCounter-1) != 0xF3)
		{
			m_PendingInteruptDisabled = false ;
			m_InterruptMaster = false ;
		}
	}

	if (m_PendingInteruptEnabled)
	{
		if (ReadMemory(m_ProgramCounter-1) != 0xFB)
		{
			m_PendingInteruptEnabled = false ;
			m_InterruptMaster = true ;
		}
	}

	return opcode ;

}

/**
 * 8bit loads
 */
void Emulator::CPU_8BIT_LOAD( BYTE& reg ) {
    BYTE n = ReadMemory(m_ProgramCounter);
    m_ProgramCounter++;
    reg = n;
}

/**
 * 8bit adds
 */
void Emulator::CPU_8BIT_ADD(BYTE &reg, BYTE toAdd, int cycles, bool useImmediate, bool addCarry)
{
    BYTE before = reg;
    BYTE adding = 0;

    // are we adding immediate data or the second param?
    if (useImmediate) {
        BYTE n = ReadMemory(m_ProgramCounter);
        m_ProgramCounter++;
        adding = n;
    } else {
        adding = toAdd;
    }

    // are we also adding the carry flag?
    if (addCarry) {
        if (TestBit(m_RegisterAF.lo, FLAG_C))
            adding++;
    }

    reg += adding;

    // set the flags
    m_RegisterAF.lo = 0;

    if (reg == 0)
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_Z);

    WORD htest = (before & 0xF);
    htest += (adding & 0xF);

    if (htest > 0xF)
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_H);

    if ((before + adding) > 0xFF)
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_C);
}

/**
 * 8bit subs
 */
void Emulator::CPU_8BIT_SUB(BYTE &reg, BYTE subtracting, int cycles, bool useImmediate, bool subCarry)
{
    BYTE before = reg;
    BYTE toSubtract = 0;

    if (useImmediate) {
        BYTE n = ReadMemory(m_ProgramCounter);
        m_ProgramCounter++;
        toSubtract = n;
    } else {
        toSubtract = subtracting;
    }

    if (subCarry) {
        if (TestBit(m_RegisterAF.lo, FLAG_C))
            toSubtract++;
    }

    reg -= toSubtract;
    m_RegisterAF.lo = 0;

    if (reg == 0)
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_Z);

    m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_N);

    // set if no borrow
    if (before < toSubtract)
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_C);

    SIGNED_WORD htest = (before & 0xF);
    htest -= (toSubtract & 0xF);

    if (htest < 0)
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_H);
}

/**
 * 8bit xor
 */
void Emulator::CPU_8BIT_XOR(BYTE& reg, BYTE toXOr, int cycles, bool useImmediate) {
    BYTE myxor = 0;

    if(useImmediate) {
        BYTE n = ReadMemory(m_ProgramCounter);
        m_ProgramCounter++;
        myxor = n;
    } else {
        myxor = toXOr;
    }

    reg ^= myxor;
    m_RegisterAF.lo = 0;

    if(reg == 0)
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_Z);
}

/**
 * 8bit jumps
 */

void Emulator::CPU_JUMP_IMMEDIATE(bool useCondition, int flag, bool condition)
{
    SIGNED_BYTE n = (SIGNED_BYTE)ReadMemory(m_ProgramCounter);

    if (!useCondition) {
        m_ProgramCounter += n;
    }
    else if (TestBit(m_RegisterAF.lo, flag) == condition) {
        m_ProgramCounter += n;
    }

    m_ProgramCounter++;
}

/**
 * Calls
 */
void Emulator::CPU_CALL(bool useCondition, int flag, bool condition)
{
   WORD nn = ReadWord( ) ;
   m_ProgramCounter += 2;

   if (!useCondition)
   {
     PushWordOntoStack(m_ProgramCounter) ;
     m_ProgramCounter = nn ;
     return ;
   }

   if (TestBit(m_RegisterAF.lo, flag)==condition)
   {
     PushWordOntoStack(m_ProgramCounter) ;
     m_ProgramCounter = nn ;
   }
}

WORD Emulator::PopWordOffStack( )
{
	WORD word = ReadMemory(m_StackPointer.reg+1) << 8 ;
	word |= ReadMemory(m_StackPointer.reg) ;
	m_StackPointer.reg+=2 ;

	return word ;
}

/**
 * Returns
 */
void Emulator::CPU_RETURN(bool useCondition, int flag, bool condition) {
    if(!useCondition) {
        m_ProgramCounter = PopWordOffStack();
        return;
    }

    if(TestBit(m_RegisterAF.lo, flag) == condition) {
        m_ProgramCounter = PopWordOffStack();
    }
}

/**
 * Rotate right through carry
 */
void Emulator::CPU_RRC(BYTE &reg)
{
    bool isLSBSet = TestBit(reg, 0);

    m_RegisterAF.lo = 0;

    reg >>= 1;

    if (isLSBSet)
    {
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_C);
        reg = BitSet(reg, 7);
    }
    if (reg == 0)
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_Z);
}

/**
 * CPU test bit
 */
void Emulator::CPU_TEST_BIT(BYTE reg, int bit, int cycles)
{
    if (TestBit(reg, bit))
        m_RegisterAF.lo = BitReset(m_RegisterAF.lo, FLAG_Z);
    else
        m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_Z);

    m_RegisterAF.lo = BitReset(m_RegisterAF.lo, FLAG_N);
    m_RegisterAF.lo = BitSet(m_RegisterAF.lo, FLAG_H);
}

void Emulator::WriteByte(WORD address, BYTE data)
{
	// writing to memory address 0x0 to 0x1FFF this disables writing to the ram bank. 0 disables, 0xA enables
	if (address <= 0x1FFF)
	{
	    if (m_MBC1)
	    {
            if ((data & 0xF) == 0xA)
                m_EnableRAM = true ;
            else if (data == 0x0)
                m_EnableRAM = false ;
	    }
	    else if (m_MBC2)
	    {
        //bit 0 of upper byte must be 0
	     if (false == TestBit(address,8))
	     {
	         if ((data & 0xF) == 0xA)
                m_EnableRAM = true ;
            else if (data == 0x0)
                m_EnableRAM = false ;
	     }
	    }

	}

	// if writing to a memory address between 2000 and 3FFF then we need to change rom bank
	else if ( (address >= 0x2000) && (address <= 0x3FFF) )
	{
		if (m_MBC1)
		{
			if (data == 0x00)
				data++;

			data &= 31;

			// Turn off the lower 5-bits.
			m_CurrentROMBank &= 224;

			// Combine the written data with the register.
			m_CurrentROMBank |= data;

		}
		else if (m_MBC2)
		{
            data &= 0xF ;
            m_CurrentROMBank = data ;
		}
	}

	// writing to address 0x4000 to 0x5FFF switches ram banks (if enabled of course)
	else if ( (address >= 0x4000) && (address <= 0x5FFF))
	{
		if (m_MBC1)
		{
			// are we using memory model 16/8
			if (m_UsingMemoryModel16_8)
			{
				// in this mode we can only use Ram Bank 0
				m_CurrentRAMBank = 0 ;

				data &= 3;
				data <<= 5;

				if ((m_CurrentROMBank & 31) == 0)
				{
					data++;
				}

				// Turn off bits 5 and 6, and 7 if it somehow got turned on.
				m_CurrentROMBank &= 31;

				// Combine the written data with the register.
				m_CurrentROMBank |= data;
			}
			else
			{
				m_CurrentRAMBank = data & 0x3 ;
			}
		}
	}

	// writing to address 0x6000 to 0x7FFF switches memory model
	else if ( (address >= 0x6000) && (address <= 0x7FFF))
	{
		if (m_MBC1)
		{
			// we're only interested in the first bit
			data &= 1 ;
			if (data == 1)
			{
				m_CurrentRAMBank = 0 ;
				m_UsingMemoryModel16_8 = false ;
			}
			else
				m_UsingMemoryModel16_8 = true ;
		}
	}

	// from now on we're writing to RAM

 	else if ((address >= 0xA000) && (address <= 0xBFFF))
 	{
 		if (m_EnableRAM)
 		{
 		    if (m_MBC1)
 		    {
                WORD newAddress = address - 0xA000 ;
                m_RAMBanks[newAddress + (m_CurrentRAMBank * 0x2000)] = data;
 		    }
 		}
 		else if (m_MBC2 && (address < 0xA200))
 		{
 		    WORD newAddress = address - 0xA000 ;
            m_RAMBanks[newAddress + (m_CurrentRAMBank * 0x2000)] = data;
 		}

 	}


	// we're right to internal RAM, remember that it needs to echo it
	else if ( (address >= 0xC000) && (address <= 0xDFFF) )
	{
		m_Rom[address] = data ;
	}

	// echo memory. Writes here and into the internal ram. Same as above
	else if ( (address >= 0xE000) && (address <= 0xFDFF) )
	{
		m_Rom[address] = data ;
		m_Rom[address -0x2000] = data ; // echo data into ram address
	}

	// This area is restricted.
 	else if ((address >= 0xFEA0) && (address <= 0xFEFF))
 	{
 	}

	// reset the divider register
	else if (address == 0xFF04)
	{
		m_Rom[0xFF04] = 0 ;
		m_DividerCounter = 0 ;
	}

	// not sure if this is correct
	else if (address == 0xFF07)
	{
		m_Rom[address] = data ;

		int timerVal = data & 0x03 ;

		int clockSpeed = 0 ;

		switch(timerVal)
		{
			case 0: clockSpeed = 1024 ; break ;
			case 1: clockSpeed = 16; break ;
			case 2: clockSpeed = 64 ;break ;
			case 3: clockSpeed = 256 ;break ; // 256
			default: assert(false); break ; // weird timer val
		}

		if (clockSpeed != m_CurrentClockSpeed)
		{
			m_TimerCounter = 0 ;
			m_CurrentClockSpeed = clockSpeed ;
		}
	}


	// FF44 shows which horizontal scanline is currently being draw. Writing here resets it
	else if (address == 0xFF44)
	{
		m_Rom[0xFF44] = 0 ;
	}

	else if (address == 0xFF45)
	{
		m_Rom[address] = data ;
	}
	// DMA transfer
	else if (address == 0xFF46)
	{
	    WORD newAddress = (data << 8) ;
		for (int i = 0; i < 0xA0; i++)
		{
			m_Rom[0xFE00 + i] = ReadMemory(newAddress + i);
		}
	}

	// This area is restricted.
 	else if ((address >= 0xFF4C) && (address <= 0xFF7F))
 	{
 	}


	// I guess we're ok to write to memory... gulp
	else
	{
		m_Rom[address] = data ;
	}
}

void Emulator::PushWordOntoStack(WORD word)
{
	BYTE hi = word >> 8 ;
	BYTE lo = word & 0xFF;
	m_StackPointer.reg-- ;
	WriteByte(m_StackPointer.reg, hi) ;
	m_StackPointer.reg-- ;
	WriteByte(m_StackPointer.reg, lo) ;
}

WORD Emulator::ReadWord() const
{
    WORD res = ReadMemory(m_ProgramCounter + 1);
    res = res << 8;
    res |= ReadMemory(m_ProgramCounter);
    return res;
}