/*
 *--------------------------------------------------------------------
 * Project:     VM65 - Virtual Machine/CPU emulator programming
 *                     framework.  
 *
 * File:   			MemMapDev.cpp
 *
 * Purpose: 		Implementation of MemMapDev class.
 *							The MemMapDev class implements the highest abstraction
 *							layer for interfacing with emulated devices via memory
 *							addressing space, which is a typical way of
 *							interfacing the CPU with peripherals in the real world
 *							microprocessor systems.
 *							It also implements/emulates the behavior of the
 *							devices. The core implementation of the devices may
 *							be contained in separate classes or inside MemMapDev
 *							class. Note that MemMapDev class always contains
 *							at least partial (highest abstraction layer)
 *							implementation of the emulated device as it defines
 *							the methods triggered by corresponding memory address
 *							accesses.
 *
 * Date:      	8/25/2016
 *
 * Copyright:  (C) by Marek Karcz 2016. All rights reserved.
 *
 * Contact:    makarcz@yahoo.com
 *
 * License Agreement and Warranty:

   This software is provided with No Warranty.
   I (Marek Karcz) will not be held responsible for any damage to
   computer systems, data or user's health resulting from use.
   Please proceed responsibly and apply common sense.
   This software is provided in hope that it will be useful.
   It is free of charge for non-commercial and educational use.
   Distribution of this software in non-commercial and educational
   derivative work is permitted under condition that original
   copyright notices and comments are preserved. Some 3-rd party work
   included with this project may require separate application for
   permission from their respective authors/copyright owners.

 *--------------------------------------------------------------------
 */
#include "MemMapDev.h"
#include "Memory.h"
#include "MKGenException.h"

#include <stdio.h>
#include <iostream>
#include <sstream>
#include <ctype.h>
#if defined(WINDOWS)
#include <conio.h>
#endif

//#define DBG 1
#if defined (DBG)
#include <iostream>
using namespace std;
#endif

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */

 namespace MKBasic {

/*
 *--------------------------------------------------------------------
 * Method:		MemMapDev()
 * Purpose:		Class constructor.
 * Arguments: n/a
 * Returns:   n/a
 *--------------------------------------------------------------------
 */
MemMapDev::MemMapDev()
{
	mpMem = NULL;
	Initialize();
}

/*
 *--------------------------------------------------------------------
 * Method:		MemMapDev()
 * Purpose:		Custom class constructor.
 * Arguments:	pmem - Pointer to Memory, pointer to Memory object.
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
MemMapDev::MemMapDev(Memory *pmem)
{
	mpMem = pmem;
	Initialize();
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
MemMapDev::~MemMapDev()
{

}

/*
 *--------------------------------------------------------------------
 * Method:		Initialize()
 * Purpose:		Initialize class and all supported devices.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void MemMapDev::Initialize()
{
	mpConsoleIO = new ConsoleIO();
	if (NULL == mpConsoleIO)
		throw MKGenException("MemMapDev::Initialize() : Out of memory - ConsoleIO");
	mInBufDataBegin = mInBufDataEnd = 0;	
	mOutBufDataBegin = mOutBufDataEnd = 0;
	mIOEcho = false;
	mCharIOAddr = CHARIO_ADDR;
	mGraphDispAddr = GRDISP_ADDR;
	mpGraphDisp = NULL;
	mpCharIODisp = NULL;
	mGrDevRegs.mGraphDispChrTbl = CHARTBL_BANK;
	mCharTblAddr = CHARTBL_BANK * ((MAX_8BIT_ADDR+1) / 0x10);
	mGrDevRegs.mGraphDispTxtCurX = 0;
	mGrDevRegs.mGraphDispTxtCurY = 0;
	mGrDevRegs.mGraphDispCrsMode = GRAPHDEVCRSMODE_BLOCK;
	AddrRange addr_range(CHARIO_ADDR, CHARIO_ADDR+1);
	DevPar dev_par("echo", "false");
	MemAddrRanges addr_ranges_chario;
	DevParams dev_params_chario;
	addr_ranges_chario.push_back(addr_range);
	dev_params_chario.push_back(dev_par);
	Device dev_chario(DEVNUM_CHARIO, 
						 				"Character I/O", 
									  addr_ranges_chario, 
									  &MemMapDev::CharIODevice_Read,
									  &MemMapDev::CharIODevice_Write,
									  dev_params_chario);
	mDevices.push_back(dev_chario);

	addr_range.start_addr = GRDISP_ADDR;
	addr_range.end_addr = GRDISP_ADDR + GRAPHDEVREG_END - 1;
	dev_par.name = "nil";
	dev_par.value = "nil";
	MemAddrRanges addr_ranges_grdisp;
	DevParams dev_params_grdisp;
	addr_ranges_grdisp.push_back(addr_range);
	dev_params_grdisp.push_back(dev_par);	
	Device dev_grdisp(DEVNUM_GRDISP, 
									  "Graphics Display", 
									  addr_ranges_grdisp, 
									  &MemMapDev::GraphDispDevice_Read,
									  &MemMapDev::GraphDispDevice_Write,
									  dev_params_grdisp);
	mDevices.push_back(dev_grdisp);	
	mCharIOActive = false;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetDevice()
 * Purpose:		Get device by specified number.
 * Arguments:	devnum - device number
 * Returns:		Device structure value.
 *--------------------------------------------------------------------
 */
Device MemMapDev::GetDevice(int devnum)
{
	Device ret;
	for (MemMappedDevices::iterator devit = mDevices.begin();
		   devit != mDevices.end();
		   ++devit
		  ) {

		if (devit->num == devnum) {
			ret = *devit;
			break;
		}
	}
	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		SetupDevice()
 * Purpose:		Configure device memory ranges and parameters.
 * Arguments:	devnum - device number
 *            memranges - memory address ranges vector
 *            params - parameters vector
 * Returns:		integer, 0 if OK, error number otherwise
 *--------------------------------------------------------------------
 */
int MemMapDev::SetupDevice(int devnum, 
													 MemAddrRanges memranges, 
													 DevParams params)
{
#if defined(DBG)			
	cout << "DBG: MemMapDev::SetupDevice()" << endl;
#endif				
	int ret = 0;
	Device dev = GetDevice(devnum);
	if (dev.num >= 0) {
		dev.addr_ranges = memranges;
		dev.params = params;
		MemMappedDevices devices_new;
		for (MemMappedDevices::iterator it = mDevices.begin();
				 it != mDevices.end();
				 ++it
				) {
			if ((*it).num != devnum) devices_new.push_back(*it);
		}
		devices_new.push_back(dev);
		mDevices = devices_new;
		// device specific post-processing
		if (DEVNUM_CHARIO == devnum) {
			MemAddrRanges::iterator it = memranges.begin();
			mCharIOAddr = (*it).start_addr;
			for (DevParams::iterator it = params.begin();
					 it != params.end();
					 ++it
					) {
				string par_name = (*it).name;
				string par_val = (*it).value;
				if (0 == par_name.compare("echo")) {
					if (0 == par_val.compare("true")) mIOEcho = true;
					else mIOEcho = false;
				}
			}
#if defined(DBG)			
			cout << "DBG: MemMapDev::SetupDevice() mCharIOAddr = $" << hex << mCharIOAddr << endl;
			cout << "DBG: MemMapDev::SetupDevice() mIOEcho = " << (mIOEcho?"true":"false") << endl;
#endif			
		} else if (DEVNUM_GRDISP == devnum) {
			MemAddrRanges::iterator it = memranges.begin();
			mGraphDispAddr = (*it).start_addr;
		}
		// finished device specific post-processing
	} else {
		ret++;	// error
#if defined(DBG)
		cout << "DBG: MemMapDev::SetupDevice() ERROR!" << endl;
#endif		
	}

#if defined(DBG)
	while (!getchar());
	cout << "Press [ENTER]...";
	getchar();
#endif	

	return ret;
}

#if defined(LINUX)

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <ncurses.h>
#endif  // LINUX

/*
 *--------------------------------------------------------------------
 * Method:		SetCurses()
 * Purpose:		Initialize curses screen.
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
 /***
void MemMapDev::SetCurses()
{
	mpConsoleIO->InitCursesScr();
}
***/

/*
 *--------------------------------------------------------------------
 * Method:		ReadCharKb()
 * Purpose:		If char I/O active, read character from console
 *            (non-blocking) and put in an input  FIFO buffer.
 * Arguments: nonblock - if true, works in non-blocking mode
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
unsigned char MemMapDev::ReadCharKb(bool nonblock)
{
	unsigned char ret = 0;
		static int c = ' ';	// static, initializes once, remembers prev.
												// value
		if (mIOEcho && isprint(c)) mpConsoleIO->PrintChar(c);
		if (nonblock) { 

			// get a keystroke only if character is already in buffer	
			if (mpConsoleIO->KbHit()) c = mpConsoleIO->GetChar();
			else c = 0;			

		}	else {

			// wait for a keystroke, then get the character from buffer
			while(!mpConsoleIO->KbHit());
			c = mpConsoleIO->GetChar();
		}
#if defined(LINUX)
		if (c == 3) { // capture CTRL-C in CONIO mode
			mpConsoleIO->CloseCursesScr();
			kill(getpid(),SIGINT);
		} else if (c == 0x0A) {
      c = 0x0D;	// without this conversion EhBasic won't work
    } else if (c == KEY_BACKSPACE) {
      c = 8;		// raw/cbreak modes do not pass the backspace
    }
#endif
		mCharIOBufIn[mInBufDataEnd] = c;
		mInBufDataEnd++;
		if (mInBufDataEnd >= CHARIO_BUF_SIZE) mInBufDataEnd = 0;
		ret = c;

	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetCharIn()
 * Purpose:		Return character from the emulated character I/O FIFO
 *            input buffer or -1 if FIFO empty or char I/O not active.
 * Arguments: n/a
 * Returns:		character
 *--------------------------------------------------------------------
 */
char MemMapDev::GetCharIn()
{
	char ret = -1;
	if (mInBufDataEnd != mInBufDataBegin) {
		ret = mCharIOBufIn[mInBufDataBegin];	
		mInBufDataBegin++;
		if (mInBufDataBegin >= CHARIO_BUF_SIZE) mInBufDataBegin = 0;
	}
	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetCharOut()
 * Purpose:		Return character from the emulated character I/O FIFO
 *            output buffer or -1 if FIFO empty or char I/O not 
 *            active.
 * Arguments: n/a
 * Returns:		character
 *--------------------------------------------------------------------
 */
char MemMapDev::GetCharOut()
{
	char ret = -1;
	if (mOutBufDataEnd != mOutBufDataBegin) {
		ret = mCharIOBufOut[mOutBufDataBegin];	
		mOutBufDataBegin++;
		if (mOutBufDataBegin >= CHARIO_BUF_SIZE) mOutBufDataBegin = 0;
	}
	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:    CharIOFlush()
 * Purpose:   Flush the character I/O FIFO output buffer contents
 *            to the character I/O device's screen buffer.
 * Arguments: 
 * Returns:   
 *--------------------------------------------------------------------
 */
void MemMapDev::CharIOFlush()
{
	char cr = -1;
	while ((cr = GetCharOut()) != -1) {
		mpCharIODisp->PutChar(cr);
	}
}

/*
 *--------------------------------------------------------------------
 * Method:		PutCharIO()
 * Purpose:		Put character in the output char I/O FIFO buffer.
 *            If character I/O device emulation is enabled, print the
 *            character to the standard output, then flush the I/O
 *            FIFO output buffer to the character device screen
 *            buffer.
 * Arguments: c - character
 * Returns:   n/a
 *--------------------------------------------------------------------
 */
void MemMapDev::PutCharIO(char c)
{
	mCharIOBufOut[mOutBufDataEnd] = c;
	mOutBufDataEnd++;
	if (mOutBufDataEnd >= CHARIO_BUF_SIZE) mOutBufDataEnd = 0;
	if (mCharIOActive) {
#if defined(LINUX)
    // because ncurses will remove characters if sequence is
    // CR,NL, I convert CR,NL to NL,CR (NL=0x0A, CR=0x0D)
    static char prevc = 0;
		if (c == 7) mpConsoleIO->Beep();
		else
    if (c == 0x0D && prevc != 0x0A) { prevc = c; c = 0x0A; }
    else if (c == 0x0A && prevc == 0x0D) { 
      prevc = c; c = 0x0D;
      mpConsoleIO->PrintChar(prevc);
      mpConsoleIO->PrintChar(c);
    }
    else { prevc = c; mpConsoleIO->PrintChar(c); }
#else
    mpConsoleIO->PrintChar(c);
#endif
		CharIOFlush();
	}
}

/*
 *--------------------------------------------------------------------
 * Method:		CharIODevice_Write()
 * Purpose:		Write value to Char I/O device register.
 * Arguments: addr - address, val - value (character)
 * Returns:   n/a
 *--------------------------------------------------------------------
 */
void MemMapDev::CharIODevice_Write(int addr, int val)
{
	if ((unsigned int)addr == mCharIOAddr
			|| (unsigned int)addr == mCharIOAddr+1) {
		PutCharIO ((char) val);
	}
}

/*
 *--------------------------------------------------------------------
 * Method:		CharIODevice_Read()
 * Purpose:		Read value from Char I/O device register.
 * Arguments: addr - address
 * Returns:   value
 *--------------------------------------------------------------------
 */
int MemMapDev::CharIODevice_Read(int addr)
{
	int ret = 0;
	if ((unsigned int)addr == mCharIOAddr) {
		ret = ReadCharKb(false);	// blocking mode input
	} else if ((unsigned int)addr == mCharIOAddr+1) {
		ret = ReadCharKb(true);		// non-blocking mode input
	}
	mpMem->Poke8bitImg((unsigned short)addr, (unsigned char)ret);
	return ret;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetCharIOAddr()
 * Purpose:		Returns current address of basic character I/O area.
 * Arguments:	n/a
 * Returns:		address of I/O area
 *--------------------------------------------------------------------
 */
unsigned short MemMapDev::GetCharIOAddr()
{
	return mCharIOAddr;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetCharIOEchoOn()
 * Purpose:		Returns current status of char I/O local echo flag.
 * Arguments:	n/a
 * Returns:		true if local echo is enabled, false if disabled
 *--------------------------------------------------------------------
 */
bool MemMapDev::GetCharIOEchoOn()
{
	return mIOEcho;
}

/*
 *--------------------------------------------------------------------
 * Method:    	IsCharIOActive()
 * Purpose:   
 * Arguments: 
 * Returns:   
 *--------------------------------------------------------------------
 */
bool MemMapDev::IsCharIOActive()
{
	return mCharIOActive;
}

/*
 *--------------------------------------------------------------------
 * Method:    	ActivateCharIO()
 * Purpose:   	Activate character I/O device, create Display object.
 * Arguments: 	n/a
 * Returns:   	Pointer to Display object.
 *--------------------------------------------------------------------
 */
Display *MemMapDev::ActivateCharIO()
{
	if (NULL == mpCharIODisp) {
		mpCharIODisp = new Display();
		if (NULL == mpCharIODisp)
			throw MKGenException(
				"Out of memory while initializing Character I/O Display Device");
	}
	mCharIOActive = true;
	mpCharIODisp->ClrScr();

	return mpCharIODisp;
}

/*
 *--------------------------------------------------------------------
 * Method:    	GetDispPtr()
 * Purpose:   
 * Arguments: 
 * Returns:   	Pointer to Display object.
 *--------------------------------------------------------------------
 */
Display *MemMapDev::GetDispPtr()
{
	return mpCharIODisp;
}

/*
 *--------------------------------------------------------------------
 * Method:    	DeactivateCharIO()
 * Purpose:   
 * Arguments: 
 * Returns:   
 *--------------------------------------------------------------------
 */
void MemMapDev::DeactivateCharIO()
{
	if (NULL != mpCharIODisp) delete mpCharIODisp;
	mpCharIODisp = NULL;
	mCharIOActive = false;
}

/*
 *--------------------------------------------------------------------
 * Method:		GetGraphDispAddrBase()
 * Purpose:		Return base address of graphics display device.
 * Arguments:	n/a
 * Returns:		unsigned short - address ($0000 - $FFFF)
 *--------------------------------------------------------------------
 */
unsigned short MemMapDev::GetGraphDispAddrBase()
{
	return mGraphDispAddr;
}

/*
 *--------------------------------------------------------------------
 * Method:		GraphDispDevice_Read()
 * Purpose:		Read from specified graphics display device register.
 * Arguments:	int - address of the register in memory.
 * Returns:		int - read value.
 *--------------------------------------------------------------------
 */
int MemMapDev::GraphDispDevice_Read(int addr)
{
	// this device has no read registers, return 0
	GraphDisp_ReadEvents();
	GraphDisp_Update();
	return 0;
}

/*
 *--------------------------------------------------------------------
 * Method:		GraphDispDevice_Write()
 * Purpose:		Write a value to specified graphics display device
 *						register.
 * Arguments:	addr - address of the register in memory
 *						val - value to write
 * Returns:		n/a
 *--------------------------------------------------------------------
 */	
void MemMapDev::GraphDispDevice_Write(int addr, int val)
{
	if (NULL != mpGraphDisp) {	// only if device is active
		if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_X) {
			// setup X coordinate of the pixel or beginning of line,
			// less significant part
			mGrDevRegs.mGraphDispLoX = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_X + 1) {
			// setup X coordinate of the pixel, more significant part
			mGrDevRegs.mGraphDispHiX = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_Y) {
			// setup Y coordinate of the pixel
			mGrDevRegs.mGraphDispY = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_PXCOL_R) {
			// setup pixel RGB color Red component
			mGrDevRegs.mGraphDispPixColR = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_PXCOL_G) {
			// setup pixel RGB color Green component
			mGrDevRegs.mGraphDispPixColG = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_PXCOL_B) {
			// setup pixel RGB color Blue component
			mGrDevRegs.mGraphDispPixColB = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_BGCOL_R) {
			// setup background RGB color Red component
			mGrDevRegs.mGraphDispBgColR = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_BGCOL_G) {
			// setup background RGB color Green component
			mGrDevRegs.mGraphDispBgColG = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_BGCOL_B) {
			// setup background RGB color Blue component
			mGrDevRegs.mGraphDispBgColB = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_X2) {
			// setup X coordinate of the end of line, less significant part
			mGrDevRegs.mGraphDispLoX2 = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_X2 + 1) {
			// setup X coordinate of the end of line, more significant part
			mGrDevRegs.mGraphDispHiX2 = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_Y2) {
			// setup Y coordinate of the end of line
			mGrDevRegs.mGraphDispY2 = (unsigned char)val;
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_CHRTBL) {
			// set new address of the character table, 2 kB bank #0-31
			mGrDevRegs.mGraphDispChrTbl = (unsigned char)(val & 0x003F);
			mCharTblAddr = mGrDevRegs.mGraphDispChrTbl * ((MAX_8BIT_ADDR+1) / 0x20);
			unsigned char char_rom[CHROM_8x8_SIZE];
			for (unsigned int i=0; i<CHROM_8x8_SIZE; i++) {
				char_rom[i] = mpMem->Peek8bitImg((unsigned short)((mCharTblAddr + i) & 0xFFFF));
			}
			mpGraphDisp->CopyCharRom8x8(char_rom);
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_TXTCURX) {
			if (val <= TXTCRSR_MAXCOL)
				mGrDevRegs.mGraphDispTxtCurX = (unsigned char) (val & 0x007F);
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_TXTCURY) {
			if (val <= TXTCRSR_MAXROW)
				mGrDevRegs.mGraphDispTxtCurY = (unsigned char) (val & 0x001F);
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_CRSMODE) {
			if (val < GRAPHDEVCRSMODE_END)
				mGrDevRegs.mGraphDispCrsMode = (unsigned char) (val & 0x000F);
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_TXTMODE) {
			if (val < GRAPHDEVTXTMODE_END)
				mGrDevRegs.mGraphDispTxtMode = (unsigned char) (val & 0x000F);			
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_PUTC) {
			if (val <= 0xFF) {
				mpGraphDisp->PrintChar8x8(val, 
																	mGrDevRegs.mGraphDispTxtCurX,
																	mGrDevRegs.mGraphDispTxtCurY,
																	(mGrDevRegs.mGraphDispTxtMode == GRAPHDEVTXTMODE_REVERSE));
				/***
				unsigned int n = val * 8;
				int x = mGrDevRegs.mGraphDispTxtCurX * 8;
				int y = mGrDevRegs.mGraphDispTxtCurY * 8;				
				unsigned char chdef[8];
				for (int i=0; i<8; i++, n++) {
					chdef[i] = mpMem->Peek8bitImg((unsigned short)((mCharTblAddr + n) & 0xFFFF));
				}
				mpGraphDisp->RenderChar8x8(chdef, x, y, (mGrDevRegs.mGraphDispTxtMode == GRAPHDEVTXTMODE_REVERSE));
				***/
			}
		} else if ((unsigned int)addr == mGraphDispAddr + GRAPHDEVREG_CMD) {
			// execute command
			switch (val) {
				
				case GRAPHDEVCMD_CLRSCR:
					mpGraphDisp->ClearScreen();
					break;

				case GRAPHDEVCMD_SETPXL:
					mpGraphDisp->SetPixel(mGrDevRegs.mGraphDispLoX 
																	+ 256 * mGrDevRegs.mGraphDispHiX,
																mGrDevRegs.mGraphDispY);
					break;

				case GRAPHDEVCMD_CLRPXL:
					mpGraphDisp->ErasePixel(mGrDevRegs.mGraphDispLoX
																		+ 256 * mGrDevRegs.mGraphDispHiX,
																	mGrDevRegs.mGraphDispY);
					break;

				case GRAPHDEVCMD_SETBGC:
					mpGraphDisp->SetBgColor(mGrDevRegs.mGraphDispBgColR,
																	mGrDevRegs.mGraphDispBgColG,
																	mGrDevRegs.mGraphDispBgColB);				
					break;

				case GRAPHDEVCMD_SETFGC:
					mpGraphDisp->SetFgColor(mGrDevRegs.mGraphDispPixColR,
																	mGrDevRegs.mGraphDispPixColG,
																	mGrDevRegs.mGraphDispPixColB);				
					break;					

				case GRAPHDEVCMD_DRAWLN:
					mpGraphDisp->DrawLine(mGrDevRegs.mGraphDispLoX 
																		+ 256 * mGrDevRegs.mGraphDispHiX,
																mGrDevRegs.mGraphDispY,
																mGrDevRegs.mGraphDispLoX2
																		+ 256 * mGrDevRegs.mGraphDispHiX2,
																mGrDevRegs.mGraphDispY2);
					break;

				case GRAPHDEVCMD_ERASLN:
					mpGraphDisp->EraseLine(mGrDevRegs.mGraphDispLoX 
																		+ 256 * mGrDevRegs.mGraphDispHiX,
																 mGrDevRegs.mGraphDispY,
																 mGrDevRegs.mGraphDispLoX2
																		+ 256 * mGrDevRegs.mGraphDispHiX2,
																 mGrDevRegs.mGraphDispY2);
					break;					

				default:
					break;

			}
		}
		GraphDisp_ReadEvents();
		GraphDisp_Update();
	}	// if (NULL != mpGraphDisp)
}

/*
 *--------------------------------------------------------------------
 * Method:		ActivateGraphDisp()
 * Purpose:		Activate graphics display.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void MemMapDev::ActivateGraphDisp()
{
	if (NULL == mpGraphDisp) {
		mpGraphDisp = new GraphDisp();
		if (NULL == mpGraphDisp)
			throw MKGenException("Out of memory while initializing Graphics Display Device");
		mGrDevRegs.mGraphDispBgColR = 0;
		mGrDevRegs.mGraphDispBgColG = 0;
		mGrDevRegs.mGraphDispBgColB = 0;
		mGrDevRegs.mGraphDispPixColR = 0xFF;
		mGrDevRegs.mGraphDispPixColG = 0xFF;
		mGrDevRegs.mGraphDispPixColB = 0xFF;
		mpGraphDisp->SetBgColor(mGrDevRegs.mGraphDispBgColR,
														mGrDevRegs.mGraphDispBgColG,
														mGrDevRegs.mGraphDispBgColB);
		mpGraphDisp->SetFgColor(mGrDevRegs.mGraphDispPixColR,
														mGrDevRegs.mGraphDispPixColG,
														mGrDevRegs.mGraphDispPixColB);
		GraphDisp_Update();
	}
}

/*
 *--------------------------------------------------------------------
 * Method:		DeactivateGraphDisp()
 * Purpose:		Inactivate graphics display.
 * Arguments:	n/a
 * Returns:		n/a
 *--------------------------------------------------------------------
 */
void MemMapDev::DeactivateGraphDisp()
{
#if defined(DBG)
	if (false == mpGraphDisp->IsMainLoopActive()) {
		cout << "DBG: ERROR: Main Loop is already inactive in Graphics Display." << endl;
	}
#endif	
	if (NULL != mpGraphDisp) delete mpGraphDisp;
	mpGraphDisp = NULL;
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
void MemMapDev::GraphDisp_ReadEvents()
{
	if (NULL != mpGraphDisp) mpGraphDisp->ReadEvents();
}

/*
 *--------------------------------------------------------------------
 * Method:
 * Purpose:
 * Arguments:
 * Returns:
 *--------------------------------------------------------------------
 */
void MemMapDev::GraphDisp_Update()
{
	if (NULL != mpGraphDisp) mpGraphDisp->Update();
}

} // namespace MKBasic
