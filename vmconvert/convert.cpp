/*
 * convert.cpp
 *  dump convert program required by both vmconvert and vmur
 *
 *  Copyright IBM Corp. 2004, 2008.
 *
 *  Author(s): Michael Holzheu
 */

#include "vm_dump.h"
#include "lkcd_dump.h"

int
vm_convert(const char* inputFileName, const char* outputFileName,
	   const char* progName)
{
/* Do the conversion */
	try {
		switch(VMDump::getDumpType(inputFileName)){
			case Dump::DT_VM64_BIG:
			{
				VMDump64Big* vmdump;
				LKCDDump64* lkcddump;

				vmdump = new VMDump64Big(inputFileName);
				vmdump->printInfo();
				lkcddump = new LKCDDump64(vmdump,
						vmdump->getRegisterContent());
				lkcddump->writeDump(outputFileName);
				delete vmdump;
				delete lkcddump;
				break;
			}
			case Dump::DT_VM64:
			{
				VMDump64* vmdump;
				LKCDDump64* lkcddump;

				vmdump = new VMDump64(inputFileName);
				vmdump->printInfo();
				lkcddump = new LKCDDump64(vmdump,
						vmdump->getRegisterContent());
				lkcddump->writeDump(outputFileName);
				delete vmdump;
				delete lkcddump;
				break;
			}
			case Dump::DT_VM32:
			{
				VMDump32* vmdump;
				LKCDDump32* lkcddump;

				vmdump = new VMDump32(inputFileName);
				vmdump->printInfo();
				lkcddump = new LKCDDump32(vmdump,
						vmdump->getRegisterContent());
				lkcddump->writeDump(outputFileName);
				delete vmdump;
				delete lkcddump;
				break;
			}
			default:
				throw DumpException("This is not a vmdump");
		}
	} catch (DumpException ex) {
		printf("%s: %s\n", progName, ex.what());
		fflush(stdout);
		return 1;
	}
	return 0;
}
