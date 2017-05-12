/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2016 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/*! @file
 *  This file contains an ISA-portable cache simulator
 *  cache hierarchies
 */
/* Modified by weil0ng:
 *   Add support for seperate icache/dcache access.
 *   Add support for dcache hierarchies.
 *   Add support for instrumentation control.
 */


#include "pin.H"
#include "instlib.H"
#include "control_manager.H"

#include <iostream>
#include <fstream>

#include "cache.H"
#include "pin_profile.H"

using namespace INSTLIB;
using namespace CONTROLLER;

ICOUNT icount;

CONTROL_MANAGER control("controller_");

BOOL do_trace;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,    "pintool",
    "o", "cache.out", "specify dcache file name");
KNOB<BOOL>   KnobTrackInsts(KNOB_MODE_WRITEONCE,    "pintool",
    "ti", "0", "track individual instructions -- increases profiling time");
KNOB<BOOL>   KnobTrackLoads(KNOB_MODE_WRITEONCE,    "pintool",
    "tl", "0", "track individual loads -- increases profiling time");
KNOB<BOOL>   KnobTrackStores(KNOB_MODE_WRITEONCE,   "pintool",
   "ts", "0", "track individual stores -- increases profiling time");
KNOB<UINT32> KnobThresholdHit(KNOB_MODE_WRITEONCE , "pintool",
   "rh", "100", "only report memops with hit count above threshold");
KNOB<UINT32> KnobThresholdMiss(KNOB_MODE_WRITEONCE, "pintool",
   "rm","100", "only report memops with miss count above threshold");
KNOB<UINT32> KnobIL1CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "il1-size","64", "icache size in kilobytes");
KNOB<UINT32> KnobDL1CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "dl1-size","64", "dcache size in kilobytes");
KNOB<UINT32> KnobDL2CacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "dl2-size","64", "dcache size in kilobytes");
KNOB<UINT32> KnobLineSize(KNOB_MODE_WRITEONCE, "pintool",
    "line-size","64", "cache block size in bytes");
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
    "a","4", "cache associativity (1 for direct mapped)");

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr <<
        "This tool represents a cache simulator.\n"
        "\n";

    cerr << KNOB_BASE::StringKnobSummary() << endl; 
    return -1;
}

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

// wrap configuation constants into their own name space to avoid name clashes

namespace ICACHE
{
    const UINT32 max_sets = KILO;
    const CACHE_ALLOC::STORE_ALLOCATION allocation = CACHE_ALLOC::STORE_ALLOCATE;

    typedef CACHE_DIRECT_MAPPED(max_sets, allocation) ICACHE;
}

namespace DCACHE
{
    const UINT32 max_sets = KILO; // cacheSize / (lineSize * associativity);
    const UINT32 max_associativity = 256; // associativity;
    const CACHE_ALLOC::STORE_ALLOCATION allocation = CACHE_ALLOC::STORE_ALLOCATE;

    typedef CACHE_ROUND_ROBIN(max_sets, max_associativity, allocation) DCACHE;
}

ICACHE::ICACHE* il1 = NULL;
DCACHE::DCACHE* dl1 = NULL;
DCACHE::DCACHE* dl2 = NULL;

typedef enum
{
    COUNTER_MISS = 0,
    COUNTER_HIT = 1,
    COUNTER_NUM
} COUNTER;



typedef  COUNTER_ARRAY<UINT64, COUNTER_NUM> COUNTER_HIT_MISS;


// holds the counters with misses and hits
// conceptually this is an array indexed by instruction address
COMPRESSOR_COUNTER<ADDRINT, UINT32, COUNTER_HIT_MISS> dprofile;
COMPRESSOR_COUNTER<ADDRINT, UINT32, COUNTER_HIT_MISS> iprofile;

/* I-cache access functions. */

VOID InstLoadMulti(ADDRINT addr, UINT32 size, UINT32 instId) {
    // Access IL1.
    const BOOL il1Hit = il1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_LOAD);

    if (do_trace) {
        const COUNTER counter = il1Hit ? COUNTER_HIT : COUNTER_MISS;
        ++iprofile[instId][counter];
    }
}

VOID InstLoadSingle(ADDRINT addr, UINT32 instId) {
    // Access IL1.
    const BOOL il1Hit = il1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);

    if (do_trace) {
        const COUNTER counter = il1Hit ? COUNTER_HIT : COUNTER_MISS;
        ++iprofile[instId][counter];
    }
}

VOID InstLoadMultiFast(ADDRINT addr, UINT32 size) {
    // Access IL1.
    il1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_LOAD);
}

VOID InstLoadSingleFast(ADDRINT addr) {
    // Access IL1.
    il1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);
}

/* ===================================================================== */

VOID LoadMulti(ADDRINT addr, UINT32 size, UINT32 instId) {
    // first level D-cache
    const BOOL dl1Hit = dl1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_LOAD);
    
    if (do_trace) {
        const COUNTER counter = dl1Hit ? COUNTER_HIT : COUNTER_MISS;
        dprofile[instId][counter]++;
    }
}

/* ===================================================================== */

VOID StoreMulti(ADDRINT addr, UINT32 size, UINT32 instId) {
    // first level D-cache
    const BOOL dl1Hit = dl1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_STORE);

    if (do_trace) {
        const COUNTER counter = dl1Hit ? COUNTER_HIT : COUNTER_MISS;
        dprofile[instId][counter]++;
    }
}

/* ===================================================================== */

VOID LoadSingle(ADDRINT addr, UINT32 instId) {
    // @todo we may access several cache lines for 
    // first level D-cache
    const BOOL dl1Hit = dl1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);

    if (do_trace) {
        const COUNTER counter = dl1Hit ? COUNTER_HIT : COUNTER_MISS;
        dprofile[instId][counter]++;
    }
}
/* ===================================================================== */

VOID StoreSingle(ADDRINT addr, UINT32 instId) {
    // @todo we may access several cache lines for 
    // first level D-cache
    const BOOL dl1Hit = dl1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_STORE);

    if (do_trace) {
        const COUNTER counter = dl1Hit ? COUNTER_HIT : COUNTER_MISS;
        dprofile[instId][counter]++;
    }
}

/* ===================================================================== */

VOID LoadMultiFast(ADDRINT addr, UINT32 size) {
    dl1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_LOAD);
}

/* ===================================================================== */

VOID StoreMultiFast(ADDRINT addr, UINT32 size) {
    dl1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_STORE);
}

/* ===================================================================== */

VOID LoadSingleFast(ADDRINT addr) {
    dl1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);    
}

/* ===================================================================== */

VOID StoreSingleFast(ADDRINT addr) {
    dl1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_STORE);    
}



/* ===================================================================== */

VOID Instruction(INS ins, void * v) {
    // map sparse INS addresses to dense IDs
    const ADDRINT iaddr = INS_Address(ins);
    const UINT32 instId = iprofile.Map(iaddr);
    const UINT32 instSize = INS_Size(ins);
    const BOOL   single = (instSize <= 4);
    
    // Do instruction cache access first.
    if (KnobTrackInsts) {
        if (single) {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) InstLoadSingle,
                                    IARG_UINT32, iaddr,
                                    IARG_UINT32, instId,
                                    IARG_END);
        } else {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) InstLoadMulti,
                                    IARG_UINT32, iaddr,
                                    IARG_UINT32, instSize,
                                    IARG_UINT32, instId,
                                    IARG_END);
        }
    } else {
        if (single) {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) InstLoadSingleFast,
                                    IARG_UINT32, iaddr,
                                    IARG_END);
        } else {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) InstLoadMultiFast,
                                    IARG_UINT32, iaddr,
                                    IARG_UINT32, instSize,
                                    IARG_END);
        }
    }

    // Then do data cache access if needed.
    if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) {
        // map sparse INS addresses to dense IDs
        const ADDRINT iaddr = INS_Address(ins);
        const UINT32 instId = dprofile.Map(iaddr);
        const UINT32 size = INS_MemoryReadSize(ins);
        const BOOL   single = (size <= 4);
                
        if( KnobTrackLoads ) {
            if( single ) {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE, (AFUNPTR) LoadSingle,
                    IARG_MEMORYREAD_EA,
                    IARG_UINT32, instId,
                    IARG_END);
            } else {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE,  (AFUNPTR) LoadMulti,
                    IARG_MEMORYREAD_EA,
                    IARG_MEMORYREAD_SIZE,
                    IARG_UINT32, instId,
                    IARG_END);
            }
        } else {
            if( single ) {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE,  (AFUNPTR) LoadSingleFast,
                    IARG_MEMORYREAD_EA,
                    IARG_END);
                        
            } else {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE,  (AFUNPTR) LoadMultiFast,
                    IARG_MEMORYREAD_EA,
                    IARG_MEMORYREAD_SIZE,
                    IARG_END);
            }
        }
    }
        
    if ( INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins)) {
        // map sparse INS addresses to dense IDs
        const ADDRINT iaddr = INS_Address(ins);
        const UINT32 instId = dprofile.Map(iaddr);
        const UINT32 size = INS_MemoryWriteSize(ins);
        const BOOL   single = (size <= 4);
                
        if( KnobTrackStores ) {
            if( single ) {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE,  (AFUNPTR) StoreSingle,
                    IARG_MEMORYWRITE_EA,
                    IARG_UINT32, instId,
                    IARG_END);
            } else {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE,  (AFUNPTR) StoreMulti,
                    IARG_MEMORYWRITE_EA,
                    IARG_MEMORYWRITE_SIZE,
                    IARG_UINT32, instId,
                    IARG_END);
            }
                
        } else {
            if( single ) {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE,  (AFUNPTR) StoreSingleFast,
                    IARG_MEMORYWRITE_EA,
                    IARG_END);
            } else {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE,  (AFUNPTR) StoreMultiFast,
                    IARG_MEMORYWRITE_EA,
                    IARG_MEMORYWRITE_SIZE,
                    IARG_END);
            }
        }     
    }
}

VOID Handler(EVENT_TYPE ev, VOID *v, CONTEXT *ctxt, VOID *ip, THREADID tid, BOOL bcast) {
    switch(ev) {
        case EVENT_START:
	    std::cout << "START TRACING" << std::endl;
	    do_trace = true;
	    break;
	case EVENT_STOP:
	    std::cout << "STOP TRACING" << std::endl;
	    do_trace = false;
	    break;
	default:
	    ASSERTX(false);
	    break;
    }
}

/* ===================================================================== */

VOID Fini(int code, VOID * v) {
    // print cache profile
    // @todo what does this print
    std::ofstream outFile(KnobOutputFile.Value().c_str());
    
    outFile << "PIN:MEMLATENCIES 1.0. 0x0\n";
            
    if (KnobTrackInsts) {
        outFile << "#\n"
            "# ICACHE config\n"
            "# ";
        outFile << "size =  " << il1->CacheSize() / 1024 << "KB, "
                << "line =  " << il1->LineSize() << "B, "
                << "assoc = " << il1->Associativity() << std::endl;
        outFile <<
            "#\n"
            "# IL1 stats\n"
            "#\n";
        outFile << il1->StatsLong("# ", CACHE_BASE::CACHE_TYPE_ICACHE);
        outFile <<
            "#\n"
            "# LOAD stats\n"
            "#\n";
        outFile << iprofile.StringLong();
    }

    outFile << "#\n"
            "# DL1 config\n"
            "# ";
    outFile << "size =  " << dl1->CacheSize() / 1024 << "KB, "
                << "line =  " << dl1->LineSize() << "B, "
                << "assoc = " << dl1->Associativity() << std::endl;
    outFile <<
        "#\n"
        "# DL1 stats\n"
        "#\n";
    
    outFile << dl1->StatsLong("# ", CACHE_BASE::CACHE_TYPE_DCACHE);

    if( KnobTrackLoads || KnobTrackStores ) {
        outFile <<
            "#\n"
            "# LOAD stats\n"
            "#\n";
        
        outFile << dprofile.StringLong();
    }
    outFile.close();
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[]) {
    if( PIN_Init(argc,argv) ) {
        return Usage();
    }

    PIN_InitSymbols();

    do_trace = false;
    icount.Activate();
    control.RegisterHandler(Handler, 0, FALSE);
    control.Activate();

    il1 = new ICACHE::ICACHE("L1 Inst Cache",
		    	            KnobIL1CacheSize.Value() * KILO,
			                64, // Linesize.
                            1); // Associativity.

    dl1 = new DCACHE::DCACHE("L1 Data Cache", 
                            KnobDL1CacheSize.Value() * KILO,
                            KnobLineSize.Value(),
                            KnobAssociativity.Value());
    
    iprofile.SetKeyName("iaddr          ");
    iprofile.SetCounterName("icache:miss        icache:hit");
    dprofile.SetKeyName("iaddr          ");
    dprofile.SetCounterName("dcache:miss        dcache:hit");

    COUNTER_HIT_MISS threshold;

    threshold[COUNTER_HIT] = KnobThresholdHit.Value();
    threshold[COUNTER_MISS] = KnobThresholdMiss.Value();
    
    iprofile.SetThreshold( threshold );
    dprofile.SetThreshold(threshold);
    
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns

    PIN_StartProgram();
    
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
