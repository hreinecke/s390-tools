/*
 * register_content.cpp
 *  register content classes
 * 
 *  Copyright IBM Corp. 2004, 2006.
 *   
 *  Author(s): Michael Holzheu
 */

#include <string.h>
#include "register_content.h"
#include "dump.h"

RegisterContent32::RegisterContent32(void)
	: regSets(), nrCpus(0)
{
}

RegisterContent32::RegisterContent32(const RegisterContent32& r)
{
	nrCpus = r.nrCpus;
	memcpy(&regSets,&r.regSets,sizeof(regSets));
}

void
RegisterContent32::addRegisterSet(const RegisterSet32& rs)
{
	if(nrCpus < MAX_CPUS){
		regSets[nrCpus++] = rs;
	} else {
		throw(DumpException("RegisterContent32::addRegisterSet - "\
					"No more register sets available"));
	}
}

RegisterSet32
RegisterContent32::getRegisterSet(int cpu){
	if(cpu <= nrCpus){
		return regSets[cpu];
	} else {
		throw(DumpException("RegisterContent32::getRegisterSet - "\
					"No register set for cpu"));
	}
}

RegisterContent64::RegisterContent64(void)
	: regSets(), nrCpus(0)
{
}

RegisterContent64::RegisterContent64(const RegisterContent64& r)
{
	nrCpus = r.nrCpus;
	memcpy(&regSets,&r.regSets,sizeof(regSets));
}

void
RegisterContent64::addRegisterSet(const RegisterSet64& rs)
{
	if(nrCpus < MAX_CPUS){
		regSets[nrCpus++] = rs;
	} else {
		throw(DumpException("RegisterContent64::addRegisterSet - "\
					"No more register sets available"));
	}
}

RegisterSet64
RegisterContent64::getRegisterSet(int cpu)
{
	if(cpu <= nrCpus){
		return regSets[cpu];
	} else {
		throw(DumpException("RegisterContent64::getRegisterSet - "\
					"No register set for cpu"));
	}
}
