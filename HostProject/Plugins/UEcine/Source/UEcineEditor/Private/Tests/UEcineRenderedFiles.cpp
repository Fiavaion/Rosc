#include "UEcineRenderedFiles.h"

#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "Math/Float16.h"
#include "Misc/AutomationTest.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Serialization/Archive.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Reads from a block of bytes, and says when it has been asked for more than there is. */
	struct FRenderedFileBytes
	{
		explicit FRenderedFileBytes(TConstArrayView<uint8> InBytes) : Bytes(InBytes) {}

		bool Has(int64 Count) const { return Count >= 0 && Offset + Count <= Bytes.Num(); }

		/** A run of characters ended by a zero byte. */
		FString ReadName()
		{
			FString Name;
			while (Has(1) && Bytes[Offset] != 0)
			{
				Name.AppendChar(static_cast<TCHAR>(Bytes[Offset++]));
			}
			++Offset;
			return Name;
		}

		int32 ReadInt32LittleEndian()
		{
			if (!Has(4))
			{
				Offset = Bytes.Num() + 1;
				return 0;
			}
			const uint32 Value = Bytes[Offset] | (Bytes[Offset + 1] << 8) | (Bytes[Offset + 2] << 16) | (static_cast<uint32>(Bytes[Offset + 3]) << 24);
			Offset += 4;
			return static_cast<int32>(Value);
		}

		bool IsPastEnd() const { return Offset > Bytes.Num(); }

		TConstArrayView<uint8> Bytes;
		int64 Offset = 0;
	};

	// How OpenEXR numbers its ways of packing rows, and its kinds of number.
	constexpr int32 ExrCompressionNone = 0;
	constexpr int32 ExrCompressionZipOneRow = 2;
	constexpr int32 ExrCompressionZip = 3;
	constexpr int32 ExrPixelHalf = 1;
	constexpr int32 ExrPixelFloat = 2;

	struct FExrChannel
	{
		/** "layer.channel" or just "channel"; in a file in parts, a part's plain channels take the part's name as their layer. */
		FString Name;
		int32 PixelType = 0;
	};

	struct FExrPart
	{
		TArray<FExrChannel> Channels;
		FIntRect DataWindow;
		int32 Compression = 0;
	};

	struct FExrFile
	{
		TArray<FExrPart> Parts;
		TMap<FString, FString> Text;
		bool bInParts = false;
		/** Where the headers end and the lists of where the rows are begin. */
		int64 HeadersEnd = 0;
	};

	/** Reads the headers of an OpenEXR file held in memory, whole or just its start. */
	bool ReadExrParts(TConstArrayView<uint8> Bytes, FExrFile& OutFile)
	{
		FRenderedFileBytes Reader(Bytes);
		if (Reader.ReadInt32LittleEndian() != 20000630)
		{
			return false;
		}
		OutFile = FExrFile();
		OutFile.bInParts = (Reader.ReadInt32LittleEndian() & 0x1000) != 0;

		while (!Reader.IsPastEnd())
		{
			// One part's header: attributes until an empty name.
			FExrPart Part;
			FString PartName;
			bool bAnyAttribute = false;
			while (!Reader.IsPastEnd())
			{
				const FString Name = Reader.ReadName();
				if (Name.IsEmpty())
				{
					break;
				}
				bAnyAttribute = true;
				const FString Type = Reader.ReadName();
				const int32 Size = Reader.ReadInt32LittleEndian();
				if (!Reader.Has(Size))
				{
					return false;
				}
				FRenderedFileBytes Value(Reader.Bytes.Slice(static_cast<int32>(Reader.Offset), Size));
				Reader.Offset += Size;

				if (Type == TEXT("chlist"))
				{
					while (Value.Has(1) && Value.Bytes[Value.Offset] != 0)
					{
						FExrChannel& Channel = Part.Channels.AddDefaulted_GetRef();
						Channel.Name = Value.ReadName();
						Channel.PixelType = Value.ReadInt32LittleEndian();
						// Linearity and sampling.
						Value.Offset += 12;
					}
				}
				else if (Type == TEXT("string"))
				{
					FString Text;
					for (const uint8 Character : Value.Bytes)
					{
						Text.AppendChar(static_cast<TCHAR>(Character));
					}
					OutFile.Text.Add(Name, Text);
					if (Name == TEXT("name"))
					{
						PartName = Text;
					}
				}
				else if (Type == TEXT("box2i") && Name == TEXT("dataWindow"))
				{
					Part.DataWindow.Min.X = Value.ReadInt32LittleEndian();
					Part.DataWindow.Min.Y = Value.ReadInt32LittleEndian();
					Part.DataWindow.Max.X = Value.ReadInt32LittleEndian();
					Part.DataWindow.Max.Y = Value.ReadInt32LittleEndian();
				}
				else if (Type == TEXT("compression") && Size == 1)
				{
					Part.Compression = Value.Bytes[0];
				}
			}
			if (!bAnyAttribute)
			{
				break;
			}

			for (FExrChannel& Channel : Part.Channels)
			{
				if (OutFile.bInParts && !PartName.IsEmpty() && !Channel.Name.Contains(TEXT(".")))
				{
					Channel.Name = PartName + TEXT(".") + Channel.Name;
				}
			}
			OutFile.Parts.Add(MoveTemp(Part));
			if (!OutFile.bInParts)
			{
				break;
			}
		}
		OutFile.HeadersEnd = Reader.Offset;
		return !OutFile.Parts.IsEmpty() && !Reader.IsPastEnd();
	}

	uint32 ReadUInt32BigEndian(FArchive& File)
	{
		uint8 Bytes[4] = {};
		File.Serialize(Bytes, 4);
		return (static_cast<uint32>(Bytes[0]) << 24) | (Bytes[1] << 16) | (Bytes[2] << 8) | Bytes[3];
	}

	uint32 MakeAtomType(const ANSICHAR* Name)
	{
		return (static_cast<uint32>(Name[0]) << 24) | (Name[1] << 16) | (Name[2] << 8) | Name[3];
	}

	/** The atoms directly inside a range of a QuickTime file: type, and where its content starts and ends. */
	struct FMovAtom
	{
		uint32 Type = 0;
		int64 Start = 0;
		int64 End = 0;
	};

	TArray<FMovAtom> ReadMovAtoms(FArchive& File, int64 Start, int64 End)
	{
		TArray<FMovAtom> Atoms;
		int64 Offset = Start;
		while (Offset + 8 <= End)
		{
			File.Seek(Offset);
			int64 Size = ReadUInt32BigEndian(File);
			FMovAtom Atom;
			Atom.Type = ReadUInt32BigEndian(File);
			Atom.Start = Offset + 8;
			if (Size == 1)
			{
				const uint64 High = ReadUInt32BigEndian(File);
				Size = static_cast<int64>((High << 32) | ReadUInt32BigEndian(File));
				Atom.Start = Offset + 16;
			}
			else if (Size == 0)
			{
				Size = End - Offset;
			}
			if (Size < 8 || Offset + Size > End)
			{
				break;
			}
			Atom.End = Offset + Size;
			Atoms.Add(Atom);
			Offset += Size;
		}
		return Atoms;
	}

	const FMovAtom* FindMovAtom(const TArray<FMovAtom>& Atoms, const ANSICHAR* Name)
	{
		return Atoms.FindByPredicate([Type = MakeAtomType(Name)](const FMovAtom& Atom) { return Atom.Type == Type; });
	}
}

bool UEcine::RenderedFiles::ReadExrHeader(const FString& Filename, FExrHeader& OutHeader)
{
	// Headers are small; the first megabyte holds them with room to spare.
	TArray<uint8> Bytes;
	const TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Filename));
	if (!File.IsValid())
	{
		return false;
	}
	Bytes.SetNumUninitialized(static_cast<int32>(FMath::Min<int64>(File->TotalSize(), 1024 * 1024)));
	File->Serialize(Bytes.GetData(), Bytes.Num());

	FExrFile Exr;
	if (!ReadExrParts(Bytes, Exr))
	{
		return false;
	}
	OutHeader = FExrHeader();
	OutHeader.Text = MoveTemp(Exr.Text);
	OutHeader.Size = Exr.Parts[0].DataWindow.Size() + FIntPoint(1, 1);
	OutHeader.PartCount = Exr.Parts.Num();
	for (const FExrPart& Part : Exr.Parts)
	{
		for (const FExrChannel& Channel : Part.Channels)
		{
			OutHeader.Channels.Add(Channel.Name);
		}
	}
	return true;
}

bool UEcine::RenderedFiles::ReadExrLayer(const FString& Filename, const FString& Layer, FImage& OutPicture)
{
	TArray<uint8> Bytes;
	FExrFile Exr;
	if (!FFileHelper::LoadFileToArray(Bytes, *Filename) || !ReadExrParts(Bytes, Exr))
	{
		return false;
	}

	// Where each part's blocks of rows are listed, one list after the other, after the headers.
	int64 OffsetTable = Exr.HeadersEnd;
	for (int32 PartIndex = 0; PartIndex < Exr.Parts.Num(); ++PartIndex)
	{
		const FExrPart& Part = Exr.Parts[PartIndex];
		const FIntPoint Size = Part.DataWindow.Size() + FIntPoint(1, 1);
		const int32 RowsInBlock = Part.Compression == ExrCompressionZip ? 16 : 1;
		const int32 BlockCount = FMath::DivideAndRoundUp(Size.Y, RowsInBlock);

		// The layer's channels in this part, by the letter they end with: where each sits in a row, and its kind of number.
		TMap<FString, TPair<int32, int32>> LayerChannels;
		int32 RowBytes = 0;
		for (const FExrChannel& Channel : Part.Channels)
		{
			int32 LastDot = INDEX_NONE;
			const bool bHasLayer = Channel.Name.FindLastChar(TEXT('.'), LastDot);
			if ((bHasLayer ? Channel.Name.Left(LastDot) : FString()) == Layer)
			{
				LayerChannels.Add(bHasLayer ? Channel.Name.Mid(LastDot + 1) : Channel.Name, TPair<int32, int32>(RowBytes, Channel.PixelType));
			}
			RowBytes += Size.X * (Channel.PixelType == ExrPixelHalf ? 2 : 4);
		}
		if (LayerChannels.IsEmpty())
		{
			OffsetTable += 8ll * BlockCount;
			continue;
		}
		if (Part.Compression != ExrCompressionNone && Part.Compression != ExrCompressionZipOneRow && Part.Compression != ExrCompressionZip)
		{
			return false;
		}

		OutPicture.Init(Size.X, Size.Y, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		const TArrayView64<FLinearColor> Pixels = OutPicture.AsRGBA32F();
		for (FLinearColor& Pixel : Pixels)
		{
			Pixel = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
		}

		TArray<uint8> Unpacked;
		TArray<uint8> Block;
		for (int32 BlockIndex = 0; BlockIndex < BlockCount; ++BlockIndex)
		{
			FRenderedFileBytes Reader(Bytes);
			Reader.Offset = OffsetTable + 8ll * BlockIndex;
			const uint32 OffsetLow = static_cast<uint32>(Reader.ReadInt32LittleEndian());
			const uint32 OffsetHigh = static_cast<uint32>(Reader.ReadInt32LittleEndian());
			Reader.Offset = (static_cast<int64>(OffsetHigh) << 32) | OffsetLow;
			if (Exr.bInParts && Reader.ReadInt32LittleEndian() != PartIndex)
			{
				return false;
			}
			const int32 FirstRow = Reader.ReadInt32LittleEndian() - Part.DataWindow.Min.Y;
			const int32 PackedSize = Reader.ReadInt32LittleEndian();
			const int32 RowCount = FMath::Min(RowsInBlock, Size.Y - FirstRow);
			if (Reader.IsPastEnd() || !Reader.Has(PackedSize) || FirstRow < 0 || RowCount <= 0)
			{
				return false;
			}
			const uint8* Packed = Bytes.GetData() + Reader.Offset;

			// A block that packing did not make smaller is stored as it is.
			Block.SetNumUninitialized(RowBytes * RowCount);
			if (Part.Compression == ExrCompressionNone || PackedSize == Block.Num())
			{
				FMemory::Memcpy(Block.GetData(), Packed, Block.Num());
			}
			else
			{
				Unpacked.SetNumUninitialized(Block.Num());
				if (!FCompression::UncompressMemory(NAME_Zlib, Unpacked.GetData(), Unpacked.Num(), Packed, PackedSize))
				{
					return false;
				}
				// Each byte was stored as its difference from the one before, and the block as
				// its even bytes followed by its odd ones.
				for (int32 Index = 1; Index < Unpacked.Num(); ++Index)
				{
					Unpacked[Index] = static_cast<uint8>(Unpacked[Index - 1] + Unpacked[Index] - 128);
				}
				const int32 OddStart = (Unpacked.Num() + 1) / 2;
				for (int32 Index = 0; Index < Unpacked.Num(); ++Index)
				{
					Block[Index] = (Index % 2 == 0) ? Unpacked[Index / 2] : Unpacked[OddStart + Index / 2];
				}
			}

			for (const TPair<FString, TPair<int32, int32>>& Channel : LayerChannels)
			{
				const int32 Component = Channel.Key == TEXT("R") ? 0 : Channel.Key == TEXT("G") ? 1 : Channel.Key == TEXT("B") ? 2 : Channel.Key == TEXT("A") ? 3 : INDEX_NONE;
				if (Component == INDEX_NONE)
				{
					continue;
				}
				for (int32 Row = 0; Row < RowCount; ++Row)
				{
					const uint8* Values = Block.GetData() + static_cast<int64>(Row) * RowBytes + Channel.Value.Key;
					for (int32 Column = 0; Column < Size.X; ++Column)
					{
						float Value = 0.0f;
						if (Channel.Value.Value == ExrPixelHalf)
						{
							FFloat16 Half;
							Half.Encoded = static_cast<uint16>(Values[Column * 2] | (Values[Column * 2 + 1] << 8));
							Value = Half.GetFloat();
						}
						else
						{
							const uint32 Bits = Values[Column * 4] | (Values[Column * 4 + 1] << 8) | (Values[Column * 4 + 2] << 16) | (static_cast<uint32>(Values[Column * 4 + 3]) << 24);
							if (Channel.Value.Value == ExrPixelFloat)
							{
								FMemory::Memcpy(&Value, &Bits, sizeof(float));
							}
							else
							{
								Value = static_cast<float>(Bits);
							}
						}
						Pixels[static_cast<int64>(FirstRow + Row) * Size.X + Column].Component(Component) = Value;
					}
				}
			}
		}
		return true;
	}
	return false;
}

TArray<FString> UEcine::RenderedFiles::GetExrLayers(const FExrHeader& Header)
{
	TArray<FString> Layers;
	for (const FString& Channel : Header.Channels)
	{
		int32 LastDot = INDEX_NONE;
		Layers.AddUnique(Channel.FindLastChar(TEXT('.'), LastDot) ? Channel.Left(LastDot) : FString());
	}
	return Layers;
}

int32 UEcine::RenderedFiles::CountMovFrames(const FString& Filename)
{
	const TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Filename));
	if (!File.IsValid())
	{
		return -1;
	}

	const TArray<FMovAtom> Top = ReadMovAtoms(*File, 0, File->TotalSize());
	const FMovAtom* Movie = FindMovAtom(Top, "moov");
	if (Movie == nullptr)
	{
		return -1;
	}
	for (const FMovAtom& Track : ReadMovAtoms(*File, Movie->Start, Movie->End))
	{
		if (Track.Type != MakeAtomType("trak"))
		{
			continue;
		}
		const FMovAtom* Media = FindMovAtom(ReadMovAtoms(*File, Track.Start, Track.End), "mdia");
		const FMovAtom* MediaInfo = Media != nullptr ? FindMovAtom(ReadMovAtoms(*File, Media->Start, Media->End), "minf") : nullptr;
		if (MediaInfo == nullptr)
		{
			continue;
		}
		// The video track is the one with a video media header.
		const TArray<FMovAtom> InfoAtoms = ReadMovAtoms(*File, MediaInfo->Start, MediaInfo->End);
		const FMovAtom* SampleTable = FindMovAtom(InfoAtoms, "stbl");
		if (FindMovAtom(InfoAtoms, "vmhd") == nullptr || SampleTable == nullptr)
		{
			continue;
		}
		if (const FMovAtom* SampleSizes = FindMovAtom(ReadMovAtoms(*File, SampleTable->Start, SampleTable->End), "stsz"))
		{
			// Version and flags, the size all samples share, then how many samples there are.
			File->Seek(SampleSizes->Start + 8);
			return static_cast<int32>(ReadUInt32BigEndian(*File));
		}
	}
	return -1;
}

void UEcine::RenderedFiles::ExpectRenderLogNoise(FAutomationTestBase& Test)
{
	Test.AddExpectedError(TEXT("Cannot resolve version number without a valid evaluated graph"), EAutomationExpectedErrorFlags::Contains, 0);
	Test.AddExpectedError(TEXT("Script call stack"), EAutomationExpectedErrorFlags::Contains, 0);
}

#endif
