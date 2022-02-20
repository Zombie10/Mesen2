#include "stdafx.h"
#include "SNES/Debugger/GsuDebugger.h"
#include "SNES/SnesMemoryManager.h"
#include "SNES/SnesConsole.h"
#include "SNES/BaseCartridge.h"
#include "SNES/Coprocessors/GSU/Gsu.h"
#include "SNES/Debugger/TraceLogger/GsuTraceLogger.h"
#include "Debugger/DisassemblyInfo.h"
#include "Debugger/Disassembler.h"
#include "Debugger/CallstackManager.h"
#include "Debugger/BreakpointManager.h"
#include "Debugger/ExpressionEvaluator.h"
#include "Debugger/Debugger.h"
#include "Debugger/MemoryAccessCounter.h"
#include "Debugger/CodeDataLogger.h"
#include "MemoryOperationType.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"

GsuDebugger::GsuDebugger(Debugger* debugger)
{
	SnesConsole* console = (SnesConsole*)debugger->GetConsole();

	_debugger = debugger;
	_codeDataLogger = debugger->GetCodeDataLogger(CpuType::Snes);
	_disassembler = debugger->GetDisassembler();
	_memoryAccessCounter = debugger->GetMemoryAccessCounter();
	_gsu = console->GetCartridge()->GetGsu();
	_memoryManager = console->GetMemoryManager();
	_settings = debugger->GetEmulator()->GetSettings();
	
	_traceLogger.reset(new GsuTraceLogger(debugger, console->GetPpu(), _memoryManager));

	_breakpointManager.reset(new BreakpointManager(debugger, CpuType::Gsu, debugger->GetEventManager(CpuType::Snes)));
	_step.reset(new StepRequest());
}

void GsuDebugger::Reset()
{
	_prevOpCode = 0xFF;
}

void GsuDebugger::ProcessRead(uint32_t addr, uint8_t value, MemoryOperationType type)
{
	if(type == MemoryOperationType::DummyRead) {
		//Ignore all dummy reads for now
		return;
	}

	AddressInfo addressInfo = _gsu->GetMemoryMappings()->GetAbsoluteAddress(addr);
	MemoryOperationInfo operation(addr, value, type, MemoryType::GsuMemory);

	if(type == MemoryOperationType::ExecOpCode) {
		if(addressInfo.Type == MemoryType::SnesPrgRom) {
			_codeDataLogger->SetFlags(addressInfo.Address, CdlFlags::Code | CdlFlags::Gsu);
		}

		if(_traceLogger->IsEnabled() || _settings->CheckDebuggerFlag(DebuggerFlags::GsuDebuggerEnabled)) {
			GsuState gsuState = _gsu->GetState();
			_disassembler->BuildCache(addressInfo, gsuState.SFR.GetFlagsHigh() & 0x13, CpuType::Gsu);

			if(_traceLogger->IsEnabled()) {
				gsuState.R[15] = addr;

				DisassemblyInfo disInfo = _disassembler->GetDisassemblyInfo(addressInfo, addr, 0, CpuType::Gsu);
				_traceLogger->Log(gsuState, disInfo, operation);
			}
		}

		_prevOpCode = value;
		_prevProgramCounter = addr;

		_step->ProcessCpuExec();

		_memoryAccessCounter->ProcessMemoryExec(addressInfo, _memoryManager->GetMasterClock());
	} else {
		if(addressInfo.Type == MemoryType::SnesPrgRom) {
			_codeDataLogger->SetFlags(addressInfo.Address, CdlFlags::Data | CdlFlags::Gsu);
		}
		if(_traceLogger->IsEnabled()) {
			_traceLogger->LogNonExec(operation);
		}
		_memoryAccessCounter->ProcessMemoryRead(addressInfo, _memoryManager->GetMasterClock());
	}

	_debugger->ProcessBreakConditions(CpuType::Gsu, *_step.get(), _breakpointManager.get(), operation, addressInfo);
}

void GsuDebugger::ProcessWrite(uint32_t addr, uint8_t value, MemoryOperationType type)
{
	AddressInfo addressInfo = _gsu->GetMemoryMappings()->GetAbsoluteAddress(addr);
	MemoryOperationInfo operation(addr, value, type, MemoryType::GsuMemory);
	_debugger->ProcessBreakConditions(CpuType::Gsu, *_step.get(), _breakpointManager.get(), operation, addressInfo);

	if(_traceLogger->IsEnabled()) {
		_traceLogger->LogNonExec(operation);
	}

	_disassembler->InvalidateCache(addressInfo, CpuType::Gsu);
	_memoryAccessCounter->ProcessMemoryWrite(addressInfo, _memoryManager->GetMasterClock());
}

void GsuDebugger::Run()
{
	_step.reset(new StepRequest());
}

void GsuDebugger::Step(int32_t stepCount, StepType type)
{
	StepRequest step;

	switch(type) {
		case StepType::StepOut:
		case StepType::StepOver:
		case StepType::Step: step.StepCount = stepCount; break;
		
		case StepType::SpecificScanline:
		case StepType::PpuStep:
			break;
	}

	_step.reset(new StepRequest(step));
}

BreakpointManager* GsuDebugger::GetBreakpointManager()
{
	return _breakpointManager.get();
}

CallstackManager* GsuDebugger::GetCallstackManager()
{
	return nullptr;
}

IAssembler* GsuDebugger::GetAssembler()
{
	throw std::runtime_error("Assembler not supported for GSU");
}

BaseEventManager* GsuDebugger::GetEventManager()
{
	throw std::runtime_error("Event manager not supported for GSU");
}

CodeDataLogger* GsuDebugger::GetCodeDataLogger()
{
	return nullptr;
}

BaseState& GsuDebugger::GetState()
{
	return _gsu->GetState();
}

ITraceLogger* GsuDebugger::GetTraceLogger()
{
	return _traceLogger.get();
}
