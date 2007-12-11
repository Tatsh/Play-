#include <stdio.h>
#include <exception>
#include "PS2VM.h"
#include "INTC.h"
#include "IPU.h"
#include "SIF.h"
#include "VIF.h"
#include "Timer.h"
#include "MA_EE.h"
#include "MA_VU.h"
#include "COP_SCU.h"
#include "COP_FPU.h"
#include "COP_VU.h"
#include "PtrMacro.h"
#include "StdStream.h"
#include "GZipStream.h"
#ifdef WIN32
#include "VolumeStream.h"
#endif
#include "Config.h"
#include "Profiler.h"

#ifdef _DEBUG

//#define		SCREENTICKS		10000
//#define		VBLANKTICKS		1000
//#define		SCREENTICKS		500000
#define		SCREENTICKS		1000000
#define		VBLANKTICKS		10000

#else

#define		SCREENTICKS		1000000
//#define		SCREENTICKS		4833333
//#define		SCREENTICKS		2000000
//#define		SCREENTICKS		1000000
#define		VBLANKTICKS		10000

#endif

using namespace Framework;
using namespace boost;
using namespace std;
using namespace std::tr1;

CPS2VM::CPS2VM() :
m_pRAM(new uint8[RAMSIZE]),
m_pBIOS(new uint8[BIOSSIZE]),
m_pSPR(new uint8[SPRSIZE]),
m_pVUMem0(NULL),
m_pMicroMem0(NULL),
m_pVUMem1(NULL),
m_pMicroMem1(NULL),
m_pThread(NULL),
m_EE(MEMORYMAP_ENDIAN_LSBF, 0x00000000, 0x20000000),
m_VU1(MEMORYMAP_ENDIAN_LSBF, 0x00000000, 0x00008000),
m_executor(m_EE),
m_nStatus(PAUSED),
m_nEnd(false),
m_pGS(NULL),
m_pPad(NULL),
m_singleStep(false),
m_nVBlankTicks(SCREENTICKS),
m_nInVBlank(false),
m_pCDROM0(NULL),
m_dmac(m_pRAM, m_pSPR),
m_gif(m_pGS, m_pRAM, m_pSPR)
{
    m_os = new CPS2OS(m_EE, m_VU1, m_pRAM, m_pBIOS, m_pGS);
}

CPS2VM::~CPS2VM()
{
    delete m_os;
}

//////////////////////////////////////////////////
//Various Message Functions
//////////////////////////////////////////////////

void CPS2VM::CreateGSHandler(GSHANDLERFACTORY pF, void* pParam)
{
	if(m_pGS != NULL) return;

	CREATEGSHANDLERPARAM Param;
    Param.pFactory	= pF;
	Param.pParam	= pParam;

//	SendMessage(PS2VM_MSG_CREATEGS, &Param);
    m_mailBox.SendCall(bind(&CPS2VM::CreateGsImpl, this, &Param), true);
}

CGSHandler* CPS2VM::GetGSHandler()
{
	return m_pGS;
}

void CPS2VM::DestroyGSHandler()
{
	if(m_pGS == NULL) return;
//	SendMessage(PS2VM_MSG_DESTROYGS);
    m_mailBox.SendCall(bind(&CPS2VM::DestroyGsImpl, this), true);
}

void CPS2VM::CreatePadHandler(PADHANDLERFACTORY pF, void* pParam)
{
/*
	CREATEPADHANDLERPARAM Param;
	if(m_pPad != NULL) return;

	Param.pFactory	= pF;
	Param.pParam	= pParam;

	SendMessage(PS2VM_MSG_CREATEPAD, &Param);
*/
}

void CPS2VM::DestroyPadHandler()
{
/*
	if(m_pPad == NULL) return;
	SendMessage(PS2VM_MSG_DESTROYPAD);
*/
}

CVirtualMachine::STATUS CPS2VM::GetStatus() const
{
    return m_nStatus;
}

void CPS2VM::Step()
{
    if(GetStatus() == RUNNING) return;
    m_singleStep = true;
//    SendMessage(PS2VM_MSG_RESUME);
    m_mailBox.SendCall(bind(&CPS2VM::ResumeImpl, this), true);
}

void CPS2VM::Resume()
{
    if(m_nStatus == RUNNING) return;
//	SendMessage(PS2VM_MSG_RESUME);
    m_mailBox.SendCall(bind(&CPS2VM::ResumeImpl, this), true);
    m_OnRunningStateChange();
}

void CPS2VM::Pause()
{
	if(m_nStatus == PAUSED) return;
//	SendMessage(PS2VM_MSG_PAUSE);
    m_mailBox.SendCall(bind(&CPS2VM::PauseImpl, this), true);
    m_OnMachineStateChange();
    m_OnRunningStateChange();
}

void CPS2VM::Reset()
{
    assert(m_nStatus == PAUSED);
    ResetVM();
//	SendMessage(PS2VM_MSG_RESET);
//    m_mailBox.SendCall(bind(&CPS2VM::ResetVM, this), true);
}

void CPS2VM::DumpEEThreadSchedule()
{
//	if(m_pOS == NULL) return;
	if(m_nStatus != PAUSED) return;
	m_os->DumpThreadSchedule();
}

void CPS2VM::DumpEEIntcHandlers()
{
//	if(m_pOS == NULL) return;
	if(m_nStatus != PAUSED) return;
	m_os->DumpIntcHandlers();
}

void CPS2VM::DumpEEDmacHandlers()
{
//	if(m_pOS == NULL) return;
	if(m_nStatus != PAUSED) return;
	m_os->DumpDmacHandlers();
}

void CPS2VM::Initialize()
{
	CreateVM();
    m_pThread = new thread(bind(&CPS2VM::EmuThread, this));
}

void CPS2VM::Destroy()
{
    m_mailBox.SendCall(bind(&CPS2VM::DestroyImpl, this));
//	SendMessage(PS2VM_MSG_DESTROY);
	m_pThread->join();
	DELETEPTR(m_pThread);
	DestroyVM();
}

unsigned int CPS2VM::SaveState(const char* sPath)
{
    throw runtime_error("Not implemented.");
//	return SendMessage(PS2VM_MSG_SAVESTATE, (void*)sPath);
}

unsigned int CPS2VM::LoadState(const char* sPath)
{
    throw runtime_error("Not implemented.");
//    return SendMessage(PS2VM_MSG_LOADSTATE, (void*)sPath);
}

//unsigned int CPS2VM::SendMessage(PS2VM_MSG nMsg, void* pParam)
//{
//	return m_MsgBox.SendMessage(nMsg, pParam);
//}

//////////////////////////////////////////////////
//Non extern callable methods
//////////////////////////////////////////////////

void CPS2VM::CreateVM()
{
	printf("PS2VM: Virtual Machine Memory Usage: RAM: %i MBs, BIOS: %i MBs, SPR: %i KBs.\r\n", RAMSIZE / 0x100000, BIOSSIZE / 0x100000, SPRSIZE / 0x1000);
	
//	m_pRAM			= (uint8*)malloc(RAMSIZE);
//	m_pBIOS			= (uint8*)malloc(BIOSSIZE);
//	m_pSPR			= (uint8*)malloc(SPRSIZE);
	m_pVUMem1		= (uint8*)malloc(VUMEM1SIZE);
	m_pMicroMem1	= (uint8*)malloc(MICROMEM1SIZE);

	//EmotionEngine context setup
	m_EE.m_pMemoryMap->InsertReadMap(0x00000000, 0x01FFFFFF, m_pRAM,				                        0x00);
	m_EE.m_pMemoryMap->InsertReadMap(0x02000000, 0x02003FFF, m_pSPR,				                        0x01);
    m_EE.m_pMemoryMap->InsertReadMap(0x10000000, 0x10FFFFFF, bind(&CPS2VM::IOPortReadHandler, this, _1),    0x02);
    m_EE.m_pMemoryMap->InsertReadMap(0x12000000, 0x12FFFFFF, bind(&CPS2VM::IOPortReadHandler, this, _1),    0x03);
	m_EE.m_pMemoryMap->InsertReadMap(0x1FC00000, 0x1FFFFFFF, m_pBIOS,				                        0x04);

	m_EE.m_pMemoryMap->InsertWriteMap(0x00000000, 0x01FFFFFF, m_pRAM,				                            0x00);
	m_EE.m_pMemoryMap->InsertWriteMap(0x02000000, 0x02003FFF, m_pSPR,				                            0x01);
    m_EE.m_pMemoryMap->InsertWriteMap(0x10000000, 0x10FFFFFF, bind(&CPS2VM::IOPortWriteHandler, this, _1, _2),	0x02);
    m_EE.m_pMemoryMap->InsertWriteMap(0x12000000, 0x12FFFFFF, bind(&CPS2VM::IOPortWriteHandler,	this, _1, _2),  0x03);

    m_EE.m_pMemoryMap->SetWriteNotifyHandler(bind(&CPS2VM::EEMemWriteHandler, this, _1));

	m_EE.m_pArch			= &g_MAEE;
	m_EE.m_pCOP[0]			= &g_COPSCU;
	m_EE.m_pCOP[1]			= &g_COPFPU;
	m_EE.m_pCOP[2]			= &g_COPVU;

    m_EE.m_handlerParam     = this;
    m_EE.m_pAddrTranslator	= CPS2OS::TranslateAddress;
    m_EE.m_pSysCallHandler  = EESysCallHandlerStub;
#ifdef DEBUGGER_INCLUDED
    m_EE.m_pTickFunction	= EETickFunctionStub;
#else
	m_EE.m_pTickFunction	= NULL;
#endif

	//Vector Unit 1 context setup
	m_VU1.m_pMemoryMap->InsertReadMap(0x00000000, 0x00003FFF, m_pVUMem1,	0x00);
	m_VU1.m_pMemoryMap->InsertReadMap(0x00004000, 0x00007FFF, m_pMicroMem1,	0x01);

	m_VU1.m_pMemoryMap->InsertWriteMap(0x00000000, 0x00003FFF, m_pVUMem1,	0x00);

	m_VU1.m_pArch			= &g_MAVU;
	m_VU1.m_pAddrTranslator	= CMIPS::TranslateAddress64;

#ifdef DEBUGGER_INCLUDED
	m_VU1.m_pTickFunction	= VU1TickFunctionStub;
#else
	m_VU1.m_pTickFunction	= NULL;
#endif

    m_dmac.SetChannelTransferFunction(2, bind(&CGIF::ReceiveDMA, &m_gif, _1, _2, _3));

	CDROM0_Initialize();

	ResetVM();

	printf("PS2VM: Created PS2 Virtual Machine.\r\n");
}

void CPS2VM::ResetVM()
{
	memset(m_pRAM,			0, RAMSIZE);
	memset(m_pSPR,			0, SPRSIZE);
	memset(m_pVUMem1,		0, VUMEM1SIZE);
	memset(m_pMicroMem1,	0, MICROMEM1SIZE);

	//LoadBIOS();

	//Reset Context
	memset(&m_EE.m_State, 0, sizeof(MIPSSTATE));
	memset(&m_VU1.m_State, 0, sizeof(MIPSSTATE));
    m_EE.m_State.nDelayedJumpAddr = MIPS_INVALID_PC;
    m_VU1.m_State.nDelayedJumpAddr = MIPS_INVALID_PC;

	//Set VF0[w] to 1.0
	m_EE.m_State.nCOP2[0].nV3	= 0x3F800000;
	m_VU1.m_State.nCOP2[0].nV3	= 0x3F800000;

	m_VU1.m_State.nPC		= 0x4000;

	m_nStatus = PAUSED;
	
	//Reset subunits
	CDROM0_Reset();
//	CSIF::Reset();
//	CIPU::Reset();
    m_gif.Reset();
//	CVIF::Reset();
    m_dmac.Reset();
//	CINTC::Reset();
//	CTimer::Reset();

	if(m_pGS != NULL)
	{
		m_pGS->Reset();
	}

//	DELETEPTR(m_pOS);
//	m_pOS = new CPS2OS(m_EE, m_VU1, m_pRAM, m_pBIOS, m_pGS);
    m_os->Release();
    m_os->Initialize();

	RegisterModulesInPadHandler();
}

void CPS2VM::DestroyVM()
{
	CDROM0_Destroy();

	FREEPTR(m_pRAM);
	FREEPTR(m_pBIOS);
}

unsigned int CPS2VM::SaveVMState(const char* sPath)
{
	CStream* pS;

	if(m_pGS == NULL)
	{
		printf("PS2VM: GS Handler was not instancied. Cannot save state.\r\n");
		return 1;
	}

	try
	{
		pS = new CGZipStream(sPath, "wb");
	}
	catch(...)
	{
		return 1;
	}

	pS->Write(&CPS2VM::m_EE.m_State, sizeof(MIPSSTATE));
	pS->Write(&CPS2VM::m_VU1.m_State, sizeof(MIPSSTATE));

	pS->Write(CPS2VM::m_pRAM,		RAMSIZE);
	pS->Write(CPS2VM::m_pSPR,		SPRSIZE);
	pS->Write(CPS2VM::m_pVUMem1,	VUMEM1SIZE);
	pS->Write(CPS2VM::m_pMicroMem1, MICROMEM1SIZE);

	m_pGS->SaveState(pS);
//	CINTC::SaveState(pS);
	m_dmac.SaveState(pS);
//	CSIF::SaveState(pS);
//	CVIF::SaveState(pS);

	delete pS;

	printf("PS2VM: Saved state to file '%s'.\r\n", sPath);

	return 0;
}

unsigned int CPS2VM::LoadVMState(const char* sPath)
{
	CStream* pS;

	if(m_pGS == NULL)
	{
		printf("PS2VM: GS Handler was not instancied. Cannot load state.\r\n");
		return 1;
	}

	try
	{
		pS = new CGZipStream(sPath, "rb");
	}
	catch(...)
	{
		return 1;
	}

	pS->Read(&CPS2VM::m_EE.m_State, sizeof(MIPSSTATE));
	pS->Read(&CPS2VM::m_VU1.m_State, sizeof(MIPSSTATE));

	pS->Read(CPS2VM::m_pRAM,		RAMSIZE);
	pS->Read(CPS2VM::m_pSPR,		SPRSIZE);
	pS->Read(CPS2VM::m_pVUMem1,		VUMEM1SIZE);
	pS->Read(CPS2VM::m_pMicroMem1,	MICROMEM1SIZE);

	m_pGS->LoadState(pS);
//	CINTC::LoadState(pS);
	m_dmac.LoadState(pS);
//	CSIF::LoadState(pS);
//	CVIF::LoadState(pS);

	delete pS;

	printf("PS2VM: Loaded state from file '%s'.\r\n", sPath);

	m_OnMachineStateChange();

	return 0;
}

void CPS2VM::PauseImpl()
{
    m_nStatus = PAUSED;
//    printf("PS2VM: Virtual Machine paused.\r\n");
}

void CPS2VM::ResumeImpl()
{
    m_nStatus = RUNNING;
//    printf("PS2VM: Virtual Machine started.\r\n");
}

void CPS2VM::DestroyImpl()
{
    DELETEPTR(m_pGS);
    m_nEnd = true;
}

void CPS2VM::CreateGsImpl(CREATEGSHANDLERPARAM* param)
{
    m_pGS = param->pFactory(param->pParam);
    m_pGS->Initialize();
}

void CPS2VM::DestroyGsImpl()
{
    DELETEPTR(m_pGS);
}

void CPS2VM::CDROM0_Initialize()
{
	CConfig::GetInstance()->RegisterPreferenceString("ps2.cdrom0.path", "");
	m_pCDROM0 = NULL;
}

void CPS2VM::CDROM0_Reset()
{
	DELETEPTR(m_pCDROM0);
	CDROM0_Mount(CConfig::GetInstance()->GetPreferenceString("ps2.cdrom0.path"));
}

void CPS2VM::CDROM0_Mount(const char* sPath)
{
#ifdef WIN32
	CStream* pStream;

	//Check if there's an m_pCDROM0 already
	//Check if files are linked to this m_pCDROM0 too and do something with them

	if(strlen(sPath) != 0)
	{
		try
		{
			//Gotta think of something better than that...
			if(sPath[0] == '\\')
			{
				pStream = new Win32::CVolumeStream(sPath[4]);
			}
			else
			{
				pStream = new CStdStream(fopen(sPath, "rb"));
			}
			m_pCDROM0 = new CISO9660(pStream);
		}
		catch(const exception& Exception)
		{
			printf("PS2VM: Error mounting cdrom0 device: %s\r\n", Exception.what());
		}
	}

	CConfig::GetInstance()->SetPreferenceString("ps2.cdrom0.path", sPath);
#else
//	throw runtime_error("Not implemented.");
#endif
}

void CPS2VM::CDROM0_Destroy()
{
	DELETEPTR(m_pCDROM0);
}

void CPS2VM::LoadBIOS()
{
	CStdStream BiosStream(fopen("./vfs/rom0/scph10000.bin", "rb"));
	BiosStream.Read(m_pBIOS, BIOSSIZE);
}

void CPS2VM::RegisterModulesInPadHandler()
{
	if(m_pPad == NULL) return;

	m_pPad->RemoveAllListeners();
//	m_pPad->InsertListener(CSIF::GetPadMan());
//	m_pPad->InsertListener(CSIF::GetDbcMan());
}

uint32 CPS2VM::IOPortReadHandler(uint32 nAddress)
{
	uint32 nReturn;

#ifdef PROFILE
	CProfiler::GetInstance().EndZone();
#endif

	nReturn = 0;
	if(nAddress >= 0x10000000 && nAddress <= 0x1000183F)
	{
//		nReturn = CTimer::GetRegister(nAddress);
	}
	else if(nAddress >= 0x10002000 && nAddress <= 0x1000203F)
	{
//		nReturn = CIPU::GetRegister(nAddress);
	}
	else if(nAddress >= 0x10008000 && nAddress <= 0x1000EFFC)
	{
		nReturn = m_dmac.GetRegister(nAddress);
	}
	else if(nAddress >= 0x1000F000 && nAddress <= 0x1000F01C)
	{
//		nReturn = CINTC::GetRegister(nAddress);
	}
	else if(nAddress >= 0x1000F520 && nAddress <= 0x1000F59C)
	{
		nReturn = m_dmac.GetRegister(nAddress);
	}
	else if(nAddress >= 0x12000000 && nAddress <= 0x1200108C)
	{
		if(m_pGS != NULL)
		{
			nReturn = m_pGS->ReadPrivRegister(nAddress);		
		}
	}
	else
	{
		printf("PS2VM: Read an unhandled IO port (0x%0.8X).\r\n", nAddress);
	}

#ifdef PROFILE
	CProfiler::GetInstance().BeginZone(PROFILE_EEZONE);
#endif

	return nReturn;
}

uint32 CPS2VM::IOPortWriteHandler(uint32 nAddress, uint32 nData)
{
#ifdef PROFILE
	CProfiler::GetInstance().EndZone();
#endif

	if(nAddress >= 0x10000000 && nAddress <= 0x1000183F)
	{
//		CTimer::SetRegister(nAddress, nData);
	}
	else if(nAddress >= 0x10002000 && nAddress <= 0x1000203F)
	{
//		CIPU::SetRegister(nAddress, nData);
	}
	else if(nAddress >= 0x10007000 && nAddress <= 0x1000702F)
	{
//		CIPU::SetRegister(nAddress, nData);
	}
	else if(nAddress >= 0x10008000 && nAddress <= 0x1000EFFC)
	{
		m_dmac.SetRegister(nAddress, nData);
    }
	else if(nAddress >= 0x1000F000 && nAddress <= 0x1000F01C)
	{
//		CINTC::SetRegister(nAddress, nData);
	}
	else if(nAddress == 0x1000F180)
	{
		//stdout data
//		CSIF::GetFileIO()->Write(1, 1, &nData);
	}
	else if(nAddress >= 0x1000F520 && nAddress <= 0x1000F59C)
	{
		m_dmac.SetRegister(nAddress, nData);
	}
	else if(nAddress >= 0x12000000 && nAddress <= 0x1200108C)
	{
		if(m_pGS != NULL)
		{
			m_pGS->WritePrivRegister(nAddress, nData);
		}
	}
	else
	{
		printf("PS2VM: Wrote to an unhandled IO port (0x%0.8X, 0x%0.8X, PC: 0x%0.8X).\r\n", nAddress, nData, m_EE.m_State.nPC);
	}

#ifdef PROFILE
	CProfiler::GetInstance().BeginZone(PROFILE_EEZONE);
#endif

    return 0;
}

void CPS2VM::EEMemWriteHandler(uint32 nAddress)
{
	if(nAddress < RAMSIZE)
	{
		//Check if the block we're about to invalidate is the same
		//as the one we're executing in

		CCacheBlock* pBlock;
		pBlock = m_EE.m_pExecMap->FindBlock(nAddress);
		if(m_EE.m_pExecMap->FindBlock(m_EE.m_State.nPC) != pBlock)
		{
			m_EE.m_pExecMap->InvalidateBlock(nAddress);
		}
		else
		{
#ifdef _DEBUG
//			printf("PS2VM: Warning. Writing to the same cache block as the one we're currently executing in. PC: 0x%0.8X\r\n",
//				m_EE.m_State.nPC);
#endif
		}
	}
}

void CPS2VM::EESysCallHandlerStub(CMIPS* context)
{
//    CPS2VM& vm = *reinterpret_cast<CPS2VM*>(context->m_handlerParam);
//    vm.m_os.SysCallHandler();
}

unsigned int CPS2VM::EETickFunctionStub(unsigned int ticks, CMIPS* context)
{
    return reinterpret_cast<CPS2VM*>(context->m_handlerParam)->EETickFunction(ticks);
}

unsigned int CPS2VM::VU1TickFunctionStub(unsigned int ticks, CMIPS* context)
{
    return reinterpret_cast<CPS2VM*>(context->m_handlerParam)->VU1TickFunction(ticks);
}

unsigned int CPS2VM::EETickFunction(unsigned int nTicks)
{

#ifdef DEBUGGER_INCLUDED

	if(m_EE.m_nIllOpcode != MIPS_INVALID_PC)
	{
		printf("PS2VM: (EmotionEngine) Illegal opcode encountered at 0x%0.8X.\r\n", m_EE.m_nIllOpcode);
		m_EE.m_nIllOpcode = MIPS_INVALID_PC;
		assert(0);
	}

	if(m_EE.MustBreak())
	{
		return 1;
	}

	//TODO: Check if we can remove this if there's no debugger around
//	if(m_MsgBox.IsMessagePending())
    if(m_mailBox.IsPending())
	{
		return 1;
	}

#endif

	return 0;
}

unsigned int CPS2VM::VU1TickFunction(unsigned int nTicks)
{
#ifdef DEBUGGER_INCLUDED

	if(m_VU1.m_nIllOpcode != MIPS_INVALID_PC)
	{
		printf("PS2VM: (VectorUnit1) Illegal/unimplemented instruction encountered at 0x%0.8X.\r\n", m_VU1.m_nIllOpcode);
		m_VU1.m_nIllOpcode = MIPS_INVALID_PC;
	}

	if(m_VU1.MustBreak())
	{
		return 1;
	}

	//TODO: Check if we can remove this if there's no debugger around
//	if(m_MsgBox.IsMessagePending())
    if(m_mailBox.IsPending())
    {
		return 1;
	}

#endif

	return 0;
}

void CPS2VM::EmuThread()
{
//	CThreadMsg::MESSAGE Msg;
//	unsigned int nRetValue;

	m_nEnd = false;

	while(1)
	{
/*
        if(m_MsgBox.GetMessage(&Msg))
		{
			nRetValue = 0;

			switch(Msg.nMsg)
			{
			case PS2VM_MSG_PAUSE:
				break;
			case PS2VM_MSG_RESUME:
				break;
			case PS2VM_MSG_DESTROY:
				break;
			case PS2VM_MSG_CREATEGS:
				break;
			case PS2VM_MSG_DESTROYGS:
				break;
			case PS2VM_MSG_CREATEPAD:
				CREATEPADHANDLERPARAM* pCreatePadParam;
				pCreatePadParam = (CREATEPADHANDLERPARAM*)Msg.pParam;
				m_pPad = pCreatePadParam->pFactory(pCreatePadParam->pParam);
				RegisterModulesInPadHandler();
				break;
			case PS2VM_MSG_DESTROYPAD:
				DELETEPTR(m_pPad);
				break;
			case PS2VM_MSG_SAVESTATE:
				nRetValue = SaveVMState((const char*)Msg.pParam);
				break;
			case PS2VM_MSG_LOADSTATE:
				nRetValue = LoadVMState((const char*)Msg.pParam);
				break;
			case PS2VM_MSG_RESET:
				ResetVM();
				break;
			}

			m_MsgBox.FlushMessage(nRetValue);
		}
*/
        while(m_mailBox.IsPending())
        {
            m_mailBox.ReceiveCall();
        }
		if(m_nEnd) break;
		if(m_nStatus == PAUSED)
		{
            //Sleep during 100ms
            xtime xt;
            xtime_get(&xt, boost::TIME_UTC);
            xt.nsec += 100 * 1000000;
			thread::sleep(xt);
		}
		if(m_nStatus == RUNNING)
        {
            //Synchonization methods
            //1984.elf - CSR = CSR & 0x08; while(CSR & 0x08 == 0);
			if(static_cast<int>(m_nVBlankTicks) <= 0)
			{
				m_nInVBlank = !m_nInVBlank;
				if(m_nInVBlank)
				{
					m_nVBlankTicks += VBLANKTICKS;
//                    if(m_pGS != NULL)
//                    {
//					    m_pGS->SetVBlank();
//                    }

					//Old Flipping Method
					//m_pGS->Flip();
					m_OnNewFrame();
                    //////

                    if(m_pPad != NULL)
                    {
                        m_pPad->Update();
                    }
				}
				else
				{
					m_nVBlankTicks += SCREENTICKS;
					if(m_pGS != NULL)
					{
						m_pGS->ResetVBlank();
					}
				}
			}
            if(!m_EE.m_State.nHasException)
            {
                int executeQuota = m_singleStep ? 1 : 5000;
				m_nVBlankTicks -= (executeQuota - m_executor.Execute(executeQuota));
            }
            if(m_EE.m_State.nHasException)
            {
                m_os->SysCallHandler();
                assert(!m_EE.m_State.nHasException);
            }
            if(m_executor.MustBreak() || m_singleStep)
            {
                m_nStatus = PAUSED;
                m_singleStep = false;
                m_OnRunningStateChange();
                m_OnMachineStateChange();
            }
/*
			RET_CODE nRet;

#ifdef PROFILE
			CProfiler::GetInstance().BeginIteration();
			CProfiler::GetInstance().BeginZone(PROFILE_EEZONE);
#endif

#if (DEBUGGER_INCLUDED && VU_DEBUG)
			if(CVIF::IsVU1Running())
			{
				nRet = m_VU1.Execute(100000);
				if(nRet == RET_CODE_BREAKPOINT)
				{
					printf("PS2VM: (VectorUnit1) Breakpoint encountered at 0x%0.8X.\r\n", m_VU1.m_State.nPC);
					m_nStatus = PS2VM_STATUS_PAUSED;
					m_OnMachineStateChange.Notify(0);
					m_OnRunningStateChange.Notify(0);
				}
			}
			else
#endif
			{
				if(m_EE.MustBreak())
				{
					nRet = RET_CODE_BREAKPOINT;
				}
				else
				{
					nRet = m_EE.Execute(5000);
					m_nVBlankTicks -= (5000 - m_EE.m_nQuota);
//					CTimer::Count(5000 - m_EE.m_nQuota);
					if((int)m_nVBlankTicks <= 0)
					{
						m_nInVBlank = !m_nInVBlank;
						if(m_nInVBlank)
						{
							m_nVBlankTicks = VBLANKTICKS;
							m_pGS->SetVBlank();

							//Old Flipping Method
							//m_pGS->Flip();
							//m_OnNewFrame.Notify(NULL);
							//////

							m_pPad->Update();
						}
						else
						{
							m_nVBlankTicks = SCREENTICKS;
							m_pGS->ResetVBlank();
						}
					}
				}

#ifdef PROFILE
			CProfiler::GetInstance().EndZone();
			CProfiler::GetInstance().EndIteration();
#endif

				if(nRet == RET_CODE_BREAKPOINT)
				{
					printf("PS2VM: (EmotionEngine) Breakpoint encountered at 0x%0.8X.\r\n", m_EE.m_State.nPC);
					m_nStatus = PAUSED;
					m_OnMachineStateChange();
					m_OnRunningStateChange();
					continue;
				}
			}
*/
		}
	}
}
