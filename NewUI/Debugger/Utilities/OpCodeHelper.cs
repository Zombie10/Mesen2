﻿using Avalonia.Controls;
using Mesen.Debugger.Controls;
using Mesen.Interop;
using Mesen.Localization;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Mesen.Debugger.Utilities;

public static class OpCodeHelper
{
	private static Dictionary<CpuType, CpuDocumentationData> _data = new();

	static OpCodeHelper()
	{
		InitNesDocumentation();
		InitSnesDocumentation();
		InitPceDocumentation();
	}

	public static DynamicTooltip? GetTooltip(CodeSegmentInfo seg)
	{
		if(!_data.TryGetValue(seg.Data.CpuType, out CpuDocumentationData? doc)) {
			return null;
		}

		string opname = (doc.OpComparer != null ? doc.OpComparer(seg.Text) : seg.Text).ToLower();
		if(!doc.OpDesc.TryGetValue(opname, out OpCodeDesc? desc)) {
			return null;
		}

		if(seg.Data.ByteCode.Length <= 0) {
			return null;
		}

		byte opcode = seg.Data.ByteCode[0];

		StackPanel panel = new StackPanel() { MaxWidth = 250 };
		panel.Children.Add(new TextBlock() { Text = desc.Name, FontSize = 14, Margin = new(2), TextWrapping = Avalonia.Media.TextWrapping.Wrap, FontWeight = Avalonia.Media.FontWeight.Bold });
		panel.Children.Add(new TextBlock() { Text = desc.Description, Margin = new(2), TextWrapping = Avalonia.Media.TextWrapping.Wrap });

		TooltipEntries items = new();
		items.AddCustomEntry("OP", panel);
		if(!string.IsNullOrEmpty(seg.Data.ByteCodeStr)) {
			items.AddEntry("Byte Code", seg.Data.ByteCodeStr);
		}
		items.AddEntry("Mode", doc.OpMode[opcode]);
		if(doc.OpCycleCount != null) {
			items.AddEntry("Cycle Count", doc.OpCycleCount[opcode]);
		}
		if(desc.Flags != null) {
			items.AddEntry("Affected Flags", string.Join(", ", desc.Flags));
		}

		return new DynamicTooltip() { Items = items };
	}

	private static void InitDocumentation(CpuType cpuType, DocFileFormat doc, Dictionary<string, OpCodeDesc>? baseDesc = null)
	{
		Dictionary<string, OpCodeDesc> desc = baseDesc ?? new();
		foreach(OpCodeDesc op in doc.Instructions) {
			desc[op.Op] = op;
		}

		Dictionary<int, string> mode = new();
		Dictionary<int, string> cycleCount = new();
		for(int i = 0; i < 256; i++) {
			mode[i] = ResourceHelper.GetEnumText(doc.AddressingModes[i]);

			if(doc.MinCycles != null) {
				string cycles = doc.MinCycles[i].ToString();
				if(doc.MinCycles[i] < doc.MaxCycles?[i]) {
					cycles += "-" + doc.MaxCycles[i];
				}
				cycleCount[i] = cycles;
			}
		}

		_data[cpuType] = new CpuDocumentationData(desc, mode, doc.MinCycles != null ? cycleCount : null);
	}

	private static DocFileFormat Get6502Documentation()
	{
		return ReadDocumentationFile("NesDocumentation.json");
	}

	private static void InitNesDocumentation()
	{
		InitDocumentation(CpuType.Nes, Get6502Documentation());
	}

	private static void InitSnesDocumentation()
	{
		Dictionary<string, OpCodeDesc> baseDesc = new(_data[CpuType.Nes].OpDesc);
		InitDocumentation(CpuType.Snes, ReadDocumentationFile("SnesDocumentation.json"), baseDesc);
	}

	private static void InitPceDocumentation()
	{
		Dictionary<string, OpCodeDesc> baseDesc = new(_data[CpuType.Nes].OpDesc);
		InitDocumentation(CpuType.Pce, ReadDocumentationFile("PceDocumentation.json"), baseDesc);
		_data[CpuType.Pce].OpComparer = x => x.Substring(0, 3);
	}

	private static DocFileFormat ReadDocumentationFile(string filename)
	{
		using StreamReader reader = new StreamReader(Assembly.GetExecutingAssembly().GetManifestResourceStream("Mesen.Debugger.Documentation." + filename)!);
		return JsonSerializer.Deserialize<DocFileFormat>(reader.ReadToEnd(), new JsonSerializerOptions() { Converters = { new JsonStringEnumConverter() }, PropertyNamingPolicy = JsonNamingPolicy.CamelCase })!;
	}

	private enum AddrMode
	{
		//6502
		None, Acc, Imp, Imm, Rel,
		Zero, Abs, ZeroX, ZeroY,
		Ind, IndX, IndY, AbsX, AbsY,

		//65816
		AbsLng, AbsIdxXInd, AbsIdxX,
		AbsIdxY, AbsLngIdxX, AbsIndLng,
		AbsInd,
		BlkMov,
		Dir, DirIdxX, DirInd,
		DirIndLng, DirIndIdxY, DirIdxIndX,
		DirIdxY, DirIndLngIdxY,
		Imm8, Imm16, ImmX, ImmM,
		RelLng,
		Stk, StkRel, StkRelIndIdxY,

		//PCE
		AbsXInd,
		Block,
		ImZero, ImZeroX, ImAbs, ImAbsX,
		ZInd, ZeroRel,
	}

	[Flags]
	private enum CpuFlag { None = 0, Carry = 1, Decimal = 2, Interrupt = 4, Negative = 8, Overflow = 16, Zero = 32, Memory = 64, Index = 128, Emulation = 256 }

	private class DocFileFormat
	{
		public OpCodeDesc[] Instructions { get; set; } = Array.Empty<OpCodeDesc>();
		public AddrMode[] AddressingModes { get; set; } = Array.Empty<AddrMode>();
		public int[]? MinCycles { get; set; }
		public int[]? MaxCycles { get; set; }
	}

	private class OpCodeDesc
	{
		public string Op { get; set; } = "";
		public string Name { get; set; } = "";
		public string Description { get; set; } = "";
		public CpuFlag[]? Flags { get; set; }
	}

	private class CpuDocumentationData
	{
		public Dictionary<string, OpCodeDesc> OpDesc { get; }
		public Dictionary<int, string> OpMode { get; }
		public Dictionary<int, string>? OpCycleCount { get; }
		public Func<string, string>? OpComparer { get; set; }

		public CpuDocumentationData(Dictionary<string, OpCodeDesc> opDesc, Dictionary<int, string> opMode, Dictionary<int, string>? opCycleCount = null)
		{
			OpDesc = opDesc;
			OpMode = opMode;
			OpCycleCount = opCycleCount;
		}
	}
}