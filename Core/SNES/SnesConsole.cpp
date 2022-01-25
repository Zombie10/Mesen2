#include "stdafx.h"
#include "SNES/SnesConsole.h"
#include "SNES/SnesCpu.h"
#include "SNES/SnesPpu.h"
#include "SNES/Spc.h"
#include "SNES/InternalRegisters.h"
#include "SNES/SnesControlManager.h"
#include "SNES/SnesMemoryManager.h"
#include "SNES/SnesDmaController.h"
#include "SNES/BaseCartridge.h"
#include "SNES/RamHandler.h"
#include "SNES/CartTypes.h"
#include "SNES/SpcFileData.h"
#include "SNES/SnesDefaultVideoFilter.h"
#include "SNES/SnesNtscFilter.h"
#include "Gameboy/Gameboy.h"
#include "Gameboy/GbPpu.h"
#include "Debugger/Debugger.h"
#include "Debugger/DebugTypes.h"
#include "SNES/SnesState.h"
#include "SNES/Coprocessors/DSP/NecDsp.h"
#include "SNES/Coprocessors/MSU1/Msu1.h"
#include "SNES/Coprocessors/SA1/Sa1.h"
#include "SNES/Coprocessors/GSU/Gsu.h"
#include "SNES/Coprocessors/CX4/Cx4.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/Interfaces/IControlManager.h"
#include "Utilities/Serializer.h"
#include "Utilities/Timer.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/PlatformUtilities.h"
#include "Utilities/FolderUtilities.h"
#include "EventType.h"
#include "SNES/RegisterHandlerA.h"
#include "SNES/RegisterHandlerB.h"

SnesConsole::SnesConsole(Emulator* emu)
{
	_emu = emu;
	_settings = emu->GetSettings();
}

SnesConsole::~SnesConsole()
{
}

void SnesConsole::Initialize()
{
}

void SnesConsole::Release()
{
}

void SnesConsole::RunFrame()
{
	_frameRunning = true;

	while(_frameRunning) {
		_cpu->Exec();
	}
}

void SnesConsole::OnBeforeRun()
{
	_memoryManager->IncMasterClockStartup();

	//TODO?
	//_controlManager->UpdateInputState();
}

void SnesConsole::ProcessEndOfFrame()
{
#ifndef LIBRETRO
	_cart->RunCoprocessors();
	if(_cart->GetCoprocessor()) {
		_cart->GetCoprocessor()->ProcessEndOfFrame();
	}

	_emu->ProcessEndOfFrame();

	_controlManager->UpdateControlDevices();
	_controlManager->UpdateInputState();
	_internalRegisters->ProcessAutoJoypadRead();
#endif
	_frameRunning = false;
}

void SnesConsole::RunSingleFrame()
{
	//Used by Libretro
	/*_emulationThreadId = std::this_thread::get_id();
	_isRunAheadFrame = false;

	_controlManager->UpdateInputState();
	_internalRegisters->ProcessAutoJoypadRead();

	RunFrame();

	_cart->RunCoprocessors();
	if(_cart->GetCoprocessor()) {
		_cart->GetCoprocessor()->ProcessEndOfFrame();
	}

	_controlManager->UpdateControlDevices();*/
}

void SnesConsole::Stop()
{
}

void SnesConsole::Reset()
{
	_dmaController->Reset();
	_internalRegisters->Reset();
	_memoryManager->Reset();
	_spc->Reset();
	_ppu->Reset();
	_cart->Reset();
	//_controlManager->Reset();

	//Reset cart before CPU to ensure correct memory mappings when fetching reset vector
	_cpu->Reset();
}

LoadRomResult SnesConsole::LoadRom(VirtualFile& romFile)
{
	unique_ptr<BaseCartridge> cart = BaseCartridge::CreateCartridge(this, romFile);
	if(cart) {
		_cart.swap(cart);
		
		UpdateRegion();

		_internalRegisters.reset(new InternalRegisters());
		_memoryManager.reset(new SnesMemoryManager());
		_ppu.reset(new SnesPpu(_emu, this));
		_controlManager.reset(new SnesControlManager(this));
		_dmaController.reset(new SnesDmaController(_memoryManager.get()));
		_spc.reset(new Spc(this));

		_msu1.reset(Msu1::Init(romFile, _spc.get()));

		if(_cart->GetSpcData()) {
			//TODO
    		_spc->LoadSpcFile(_cart->GetSpcData());
			_spcPlaylist = FolderUtilities::GetFilesInFolder(romFile.GetFolderPath(), { ".spc" }, false);
			std::sort(_spcPlaylist.begin(), _spcPlaylist.end());
			auto result = std::find(_spcPlaylist.begin(), _spcPlaylist.end(), romFile.GetFilePath());
			_spcTrackNumber = (uint32_t)std::distance(_spcPlaylist.begin(), result);
		}

		_cpu.reset(new SnesCpu(this));
		_memoryManager->Initialize(this);
		_internalRegisters->Initialize(this);

		_ppu->PowerOn();
		_cpu->PowerOn();

		_controlManager->UpdateControlDevices();
				
		UpdateRegion();

		return LoadRomResult::Success;
	}

	return LoadRomResult::UnknownType;
}

void SnesConsole::Init()
{
}

uint64_t SnesConsole::GetMasterClock()
{
	return _memoryManager->GetMasterClock();
}

uint32_t SnesConsole::GetMasterClockRate()
{
	return _masterClockRate;
}

ConsoleRegion SnesConsole::GetRegion()
{
	return _region;
}

ConsoleType SnesConsole::GetConsoleType()
{
	return ConsoleType::Snes;
}

void SnesConsole::UpdateRegion()
{
	switch(_settings->GetSnesConfig().Region) {
		case ConsoleRegion::Auto: _region = _cart->GetRegion(); break;

		default:
		case ConsoleRegion::Ntsc: _region = ConsoleRegion::Ntsc; break;
		case ConsoleRegion::Pal: _region = ConsoleRegion::Pal; break;
	}

	_masterClockRate = _region == ConsoleRegion::Pal ? 21281370 : 21477270;
}

double SnesConsole::GetFps()
{
	UpdateRegion();
	if(_region == ConsoleRegion::Ntsc) {
		return _settings->GetVideoConfig().IntegerFpsMode ? 60.0 : 60.0988118623484;
	} else {
		return _settings->GetVideoConfig().IntegerFpsMode ? 50.0 : 50.0069789081886;
	}
}

PpuFrameInfo SnesConsole::GetPpuFrame()
{
	//TODO null checks
	PpuFrameInfo frame = {};
	frame.FrameBuffer = (uint8_t*)_ppu->GetScreenBuffer();
	frame.Width = 256;
	frame.Height = 239;
	frame.FrameCount = _ppu->GetFrameCount();
	frame.FirstScanline = 0;
	frame.ScanlineCount = _ppu->GetVblankEndScanline() + 1;
	frame.CycleCount = 341;
	return frame;
}

vector<CpuType> SnesConsole::GetCpuTypes()
{
	vector<CpuType> cpuTypes = { CpuType::Snes, CpuType::Spc };
	if(_cart->GetGsu()) {
		cpuTypes.push_back(CpuType::Gsu);
	} else if(_cart->GetDsp()) {
		cpuTypes.push_back(CpuType::NecDsp);
	} else if(_cart->GetCx4()) {
		cpuTypes.push_back(CpuType::Cx4);
	} else if(_cart->GetGameboy()) {
		cpuTypes.push_back(CpuType::Gameboy);
	} else if(_cart->GetSa1()) {
		cpuTypes.push_back(CpuType::Sa1);
	}
	return cpuTypes;
}

void SnesConsole::SaveBattery()
{
	if(_cart) {
		_cart->SaveBattery();
	}
}

BaseVideoFilter* SnesConsole::GetVideoFilter()
{
	VideoFilterType filterType = _emu->GetSettings()->GetVideoConfig().VideoFilter;
	if(filterType == VideoFilterType::NTSC && GetRomFormat() != RomFormat::Spc) {
		return new SnesNtscFilter(_emu);
	} else {
		return new SnesDefaultVideoFilter(_emu);
	}
}

RomFormat SnesConsole::GetRomFormat()
{
	return _cart->GetSpcData() ? RomFormat::Spc : RomFormat::Sfc;
}

AudioTrackInfo SnesConsole::GetAudioTrackInfo()
{
	AudioTrackInfo track = {};
	SpcFileData* spc = _cart->GetSpcData();
	if(spc) {
		track.Artist = spc->Artist;
		track.Comment = spc->Comment;
		track.GameTitle = spc->GameTitle;
		track.SongTitle = spc->SongTitle;
		track.Position = _ppu->GetFrameCount() / GetFps();
		track.Length = spc->TrackLength + (spc->FadeLength / 1000.0);
		track.FadeLength = spc->FadeLength / 1000.0;
		track.TrackNumber = _spcTrackNumber + 1;
		track.TrackCount = (uint32_t)_spcPlaylist.size();

	}
	return track;
}

void SnesConsole::ProcessAudioPlayerAction(AudioPlayerActionParams p)
{
	if(_spcTrackNumber >= 0) {
		int i = (int)_spcTrackNumber;
		switch(p.Action) {
			case AudioPlayerAction::PrevTrack:
				if(GetAudioTrackInfo().Position < 2) {
					i--;
				}
				break;
			case AudioPlayerAction::NextTrack: i++; break;
		}

		if(i < 0) {
			i = (int)_spcPlaylist.size() - 1;
		} else if(i >= _spcPlaylist.size()) {
			i = 0;
		}

		//Asynchronously move to the next file
		//Can't do this in the current thread in some contexts (e.g when track reaches end)
		//because this is called from the emulation thread.
		thread switchTrackTask([this, i]() {
			_emu->LoadRom(VirtualFile(_spcPlaylist[i]), VirtualFile());
		});
		switchTrackTask.detach();
	}
}

void SnesConsole::Serialize(Serializer& s)
{
	s.Stream(_cpu.get());
	s.Stream(_memoryManager.get());
	s.Stream(_ppu.get());
	s.Stream(_dmaController.get());
	s.Stream(_internalRegisters.get());
	s.Stream(_cart.get());
	s.Stream(_controlManager.get());
	s.Stream(_spc.get());
	if(_msu1) {
		s.Stream(_msu1.get());
	}
}

SnesCpu* SnesConsole::GetCpu()
{
	return _cpu.get();
}

SnesPpu* SnesConsole::GetPpu()
{
	return _ppu.get();
}

Spc* SnesConsole::GetSpc()
{
	return _spc.get();
}

BaseCartridge* SnesConsole::GetCartridge()
{
	return _cart.get();
}

SnesMemoryManager* SnesConsole::GetMemoryManager()
{
	return _memoryManager.get();
}

InternalRegisters* SnesConsole::GetInternalRegisters()
{
	return _internalRegisters.get();
}

IControlManager* SnesConsole::GetControlManager()
{
	return _controlManager.get();
}

SnesDmaController* SnesConsole::GetDmaController()
{
	return _dmaController.get();
}

Msu1* SnesConsole::GetMsu1()
{
	return _msu1.get();
}

Emulator* SnesConsole::GetEmulator()
{
	return _emu;
}

bool SnesConsole::IsRunning()
{
	return _cpu != nullptr;
}

AddressInfo SnesConsole::GetAbsoluteAddress(AddressInfo& relAddress)
{
	switch(relAddress.Type) {
		case MemoryType::SnesMemory:
			if(_memoryManager->IsRegister(relAddress.Address)) {
				return { relAddress.Address & 0xFFFF, MemoryType::Register };
			} else {
				return _memoryManager->GetMemoryMappings()->GetAbsoluteAddress(relAddress.Address);
			}
		
		case MemoryType::SpcMemory: return _spc->GetAbsoluteAddress(relAddress.Address);
		case MemoryType::Sa1Memory: return _cart->GetSa1()->GetMemoryMappings()->GetAbsoluteAddress(relAddress.Address);
		case MemoryType::GsuMemory: return _cart->GetGsu()->GetMemoryMappings()->GetAbsoluteAddress(relAddress.Address);
		case MemoryType::Cx4Memory: return _cart->GetCx4()->GetMemoryMappings()->GetAbsoluteAddress(relAddress.Address);
		case MemoryType::NecDspMemory: return { relAddress.Address, MemoryType::DspProgramRom };
		case MemoryType::GameboyMemory: return _cart->GetGameboy()->GetAbsoluteAddress(relAddress.Address);
		default: throw std::runtime_error("Unsupported address type");
	}
}

AddressInfo SnesConsole::GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType)
{
	MemoryMappings* mappings = nullptr;
	switch(cpuType) {
		case CpuType::Snes: mappings = _memoryManager->GetMemoryMappings(); break;
		case CpuType::Spc: break;
		case CpuType::NecDsp: break;
		case CpuType::Sa1: mappings = _cart->GetSa1()->GetMemoryMappings(); break;
		case CpuType::Gsu: mappings = _cart->GetGsu()->GetMemoryMappings(); break;
		case CpuType::Cx4: mappings = _cart->GetCx4()->GetMemoryMappings(); break;
		case CpuType::Gameboy: break;
		default: throw std::runtime_error("Unsupported cpu type");
	}

	switch(absAddress.Type) {
		case MemoryType::SnesPrgRom:
		case MemoryType::SnesWorkRam:
		case MemoryType::SnesSaveRam:
		{
			if(!mappings) {
				throw std::runtime_error("Unsupported cpu type");
			}

			uint8_t startBank = 0;
			//Try to find a mirror close to where the PC is
			if(cpuType == CpuType::Snes) {
				if(absAddress.Type == MemoryType::SnesWorkRam) {
					startBank = 0x7E;
				} else {
					startBank = _cpu->GetState().K & 0xC0;
				}
			} else if(cpuType == CpuType::Sa1) {
				startBank = (_cart->GetSa1()->GetCpuState().K & 0xC0);
			} else if(cpuType == CpuType::Gsu) {
				startBank = (_cart->GetGsu()->GetState().ProgramBank & 0xC0);
			}

			return { mappings->GetRelativeAddress(absAddress, startBank), DebugUtilities::GetCpuMemoryType(cpuType) };
		}

		case MemoryType::SpcRam:
		case MemoryType::SpcRom:
			return { _spc->GetRelativeAddress(absAddress), MemoryType::SpcMemory };

		case MemoryType::GbPrgRom:
		case MemoryType::GbWorkRam:
		case MemoryType::GbCartRam:
		case MemoryType::GbHighRam:
		case MemoryType::GbBootRom:
			return { _cart->GetGameboy()->GetRelativeAddress(absAddress), MemoryType::GameboyMemory };

		case MemoryType::DspProgramRom:
			return { absAddress.Address, MemoryType::NecDspMemory };

		case MemoryType::Register:
			return { absAddress.Address & 0xFFFF, MemoryType::Register };

		default:
			return { -1, MemoryType::Register };
	}
}

void SnesConsole::GetConsoleState(BaseState& baseState, ConsoleType consoleType)
{
	if(consoleType == ConsoleType::Gameboy || consoleType == ConsoleType::GameboyColor) {
		_cart->GetGameboy()->GetConsoleState(baseState, consoleType);
		return;
	}

	SnesState& state = (SnesState&)baseState;
	state.MasterClock = GetMasterClock();
	state.Snes = _cpu->GetState();
	_ppu->GetState(state.Ppu, false);
	state.Spc = _spc->GetState();
	state.Dsp = _spc->GetDspState();

	for(int i = 0; i < 8; i++) {
		state.DmaChannels[i] = _dmaController->GetChannelConfig(i);
	}
	state.InternalRegs = _internalRegisters->GetState();
	state.Alu = _internalRegisters->GetAluState();

	if(_cart->GetDsp()) {
		state.NecDsp = _cart->GetDsp()->GetState();
	}
	if(_cart->GetSa1()) {
		state.Sa1 = _cart->GetSa1()->GetState();
	}
	if(_cart->GetGsu()) {
		state.Gsu = _cart->GetGsu()->GetState();
	}
	if(_cart->GetCx4()) {
		state.Cx4 = _cart->GetCx4()->GetState();
	}
}