// Copyright 2025 - Roberto De Ioris

#pragma once

#include "CoreMinimal.h"
#include "Async/ParallelFor.h"

UENUM()
enum class EglTFRuntimeAlembicPODType : uint8
{
	Boolean = 0,
	Uint8,
	Int8,
	Uint16,
	Int16,
	Uint32,
	Int32,
	Uint64,
	Int64,
	Float16,
	Float32,
	Float64,
	String,
	WString,

	NumTypes,

	Unknown = 127
};

UENUM()
enum class EglTFRuntimeAlembicXformOpType : uint8
{
	Scale = 0,
	Translate = 1,
	Rotate = 2,
	Matrix = 3,
	RotateX = 4,
	RotateY = 5,
	RotateZ = 6
};

namespace glTFRuntimeAlembic
{
	struct GLTFRUNTIMEALEMBIC_API IOgawaNode : public TSharedFromThis<IOgawaNode>
	{
		IOgawaNode(const bool bInIsData) : bIsData(bInIsData)
		{

		}

		TSharedPtr<struct FOgawaGroup> Group()
		{
			if (bIsData)
			{
				return nullptr;
			}

			return StaticCastSharedPtr<FOgawaGroup>(AsShared().ToSharedPtr());
		}

		TSharedPtr<struct FOgawaData> Data()
		{
			if (!bIsData)
			{
				return nullptr;
			}

			return StaticCastSharedPtr<FOgawaData>(AsShared().ToSharedPtr());
		}

		const bool bIsData;

		static TSharedPtr<IOgawaNode> ReadHeader(const TArrayView64<uint8>& Blob, uint64 Offset);
	};

	struct GLTFRUNTIMEALEMBIC_API FOgawaGroup : public IOgawaNode
	{
		FOgawaGroup() : IOgawaNode(false) {}
		FOgawaGroup(const FOgawaGroup& Other) = delete;
		FOgawaGroup& operator=(const FOgawaGroup& Other) = delete;

		TArray<TSharedRef<IOgawaNode>> Children;

		TSharedPtr<FOgawaGroup> GetGroup(const uint64 Index)
		{
			if (!Children.IsValidIndex(Index))
			{
				return nullptr;
			}

			return Children[Index]->Group();
		}

		TSharedPtr<FOgawaData> GetData(const uint64 Index)
		{
			if (!Children.IsValidIndex(Index))
			{
				return nullptr;
			}

			return Children[Index]->Data();
		}
	};

	struct GLTFRUNTIMEALEMBIC_API FOgawaData : public IOgawaNode
	{
		FOgawaData() : IOgawaNode(true) {}
		FOgawaData(const FOgawaData& Other) = delete;
		FOgawaData& operator=(const FOgawaData& Other) = delete;

		TArrayView64<uint8> Data;

		uint64 Num() const
		{
			return Data.Num();
		}

		template<typename T>
		T* Read(const uint64 Offset) const
		{
			if (Offset + sizeof(T) > Num())
			{
				return nullptr;
			}

			return reinterpret_cast<T*>(Data.GetData() + Offset);
		}

		TArrayView64<uint8> View(const uint64 Offset, const uint64 Size) const
		{
			if (Offset + Size > Num())
			{
				return TArrayView64<uint8>();
			}

			return TArrayView64<uint8>(Data.GetData() + Offset, Size);
		}

		bool ReadUTF8(const uint64 Offset, const uint32 Size, FString& OutString) const
		{
			if (Offset + Size > Num())
			{
				return false;
			}

			FUTF8ToTCHAR Converter(reinterpret_cast<const char*>(Data.GetData() + Offset), Size);

			OutString = FString(Converter.Length(), Converter.Get());

			return true;
		}
	};

	GLTFRUNTIMEALEMBIC_API TSharedPtr<IOgawaNode> ParseOgawaBlob(const TArrayView64<uint8>& Blob);

	struct GLTFRUNTIMEALEMBIC_API IProperty : public TSharedFromThis<IProperty>
	{
		IProperty(const FString& InName, const TMap<FString, FString>& InMetadata, const bool bInIsCompound) : Name(InName), Metadata(InMetadata), bIsCompound(bInIsCompound)
		{
		}

		FString Name;
		TMap<FString, FString> Metadata;

		const bool bIsCompound;

		static TSharedPtr<IProperty> BuildProperty(const TSharedRef<FOgawaData>& Headers, uint64& Offset, const TSharedPtr<IOgawaNode>& PropertyNode, const TArray<TArrayView64<uint8>>& IndexedMetadata);
	};

	struct GLTFRUNTIMEALEMBIC_API FCompoundProperty : public IProperty
	{
		FCompoundProperty() = delete;
		FCompoundProperty(const FCompoundProperty& Other) = delete;
		FCompoundProperty& operator=(const FCompoundProperty& Other) = delete;

		FCompoundProperty(const FString& InName, const TMap<FString, FString>& InMetadata) : IProperty(InName, InMetadata, true) {}

		TArray<TSharedRef<IProperty>> Children;

		TArray<FString> GetChildrenNames() const;

		TSharedPtr<IProperty> GetChild(const int32 ChildIndex) const;
		TSharedPtr<IProperty> GetChild(const FString ChildName) const;
	};

	struct GLTFRUNTIMEALEMBIC_API FScalarProperty : public IProperty
	{
		FScalarProperty() = delete;
		FScalarProperty(const FScalarProperty& Other) = delete;
		FScalarProperty& operator=(const FScalarProperty& Other) = delete;

		FScalarProperty(const FString& InName,
			const EglTFRuntimeAlembicPODType InPODType,
			const uint8 InExtent,
			const TMap<FString, FString>& InMetadata,
			const TSharedRef<FOgawaGroup> InGroup,
			const uint32 InNextSampleIndex,
			const uint32 InFirstChangedIndex,
			const uint32 InLastChangedIndex,
			const float InTimeSampling) :

			IProperty(InName, InMetadata, false),
			Group(InGroup),
			PODType(InPODType),
			Extent(InExtent),
			NextSampleIndex(InNextSampleIndex),
			FirstChangedIndex(InFirstChangedIndex),
			LastChangedIndex(InLastChangedIndex),
			TimeSampling(InTimeSampling)
		{
			bIsArray = false;
			if (!GetPODSize(PODSize))
			{
				PODSize = 0;
			}
		}

		TSharedRef<FOgawaGroup> Group;
		bool bIsArray;

		bool GetSampleTrueIndex(const uint32 Index, uint32& TrueIndex) const
		{
			if (Index >= NextSampleIndex)
			{
				return false;
			}

			TrueIndex = Index - FirstChangedIndex + 1;

			if (Index < FirstChangedIndex || (FirstChangedIndex == 0 && LastChangedIndex == 0))
			{
				TrueIndex = 0;
			}
			else if (Index >= LastChangedIndex)
			{
				TrueIndex = LastChangedIndex - FirstChangedIndex + 1;
			}

			return true;
		}

		template<typename T, typename U>
		bool ReadAndCast(const TSharedPtr<FOgawaData> Data, const uint64 Offset, U& Value)
		{
			// skip initial hash
			T* ValuePtr = Data->Read<T>(16 + Offset);
			if (!ValuePtr)
			{
				return false;
			}
			Value = *ValuePtr;
			return true;
		}

		template<typename T>
		bool ReadPOD(const TSharedPtr<FOgawaData> Data, const uint64 Offset, T& Value)
		{
			switch (PODType)
			{
			case EglTFRuntimeAlembicPODType::Boolean:
				return ReadAndCast<uint8>(Data, Offset, Value);
			case EglTFRuntimeAlembicPODType::Float16:
			{
				return ReadAndCast<FFloat16>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Float32:
			{
				return ReadAndCast<float>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Float64:
			{
				return ReadAndCast<double>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Uint8:
			{
				return ReadAndCast<uint8>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Int8:
			{
				return ReadAndCast<int8>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Uint16:
			{
				return ReadAndCast<uint16>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Int16:
			{
				return ReadAndCast<int16>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Uint32:
			{
				return ReadAndCast<uint32>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Int32:
			{
				return ReadAndCast<int32>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Uint64:
			{
				return ReadAndCast<uint64>(Data, Offset, Value);
			}
			case EglTFRuntimeAlembicPODType::Int64:
			{
				return ReadAndCast<int64>(Data, Offset, Value);
			}
			default:
				break;
			}

			return false;
		}

		bool GetPODSize(uint64& Size)
		{
			switch (PODType)
			{
			case EglTFRuntimeAlembicPODType::Boolean:
				Size = sizeof(uint8);
				return true;
			case EglTFRuntimeAlembicPODType::Float16:
			{
				Size = sizeof(FFloat16);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Float32:
			{
				Size = sizeof(float);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Float64:
			{
				Size = sizeof(double);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Uint8:
			{
				Size = sizeof(uint8);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Int8:
			{
				Size = sizeof(int8);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Uint16:
			{
				Size = sizeof(uint16);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Int16:
			{
				Size = sizeof(int16);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Uint32:
			{
				Size = sizeof(uint32);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Int32:
			{
				Size = sizeof(int32);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Uint64:
			{
				Size = sizeof(uint64);
				return true;
			}
			case EglTFRuntimeAlembicPODType::Int64:
			{
				Size = sizeof(int64);
				return true;
			}
			default:
				break;
			}
			return false;
		}

		template<typename T>
		bool Get(const uint32 TrueSampleIndex, const uint64 ExtentIndex, T& Value)
		{
			if (PODSize == 0 || ExtentIndex >= Extent)
			{
				return false;
			}

			const TSharedPtr<FOgawaData> Data = Group->GetData(TrueSampleIndex);
			if (!Data)
			{
				return false;
			}

			return ReadPOD(Data, PODSize * ExtentIndex, Value);
		}

		template<typename T, uint8 N>
		bool Get(const uint32 TrueSampleIndex, T(&Values)[N])
		{
			if (PODSize == 0 || N > Extent)
			{
				return false;
			}

			const TSharedPtr<FOgawaData> Data = Group->GetData(TrueSampleIndex);
			if (!Data)
			{
				return false;
			}

			for (uint8 ExtentIndex = 0; ExtentIndex < N; ExtentIndex++)
			{
				if (!ReadPOD(Data, PODSize * ExtentIndex, Values[ExtentIndex]))
				{
					return false;
				}
			}

			return true;
		}

		bool Get(const uint32 TrueSampleIndex, FMatrix& Matrix)
		{
			if (PODSize == 0)
			{
				return false;
			}

			Matrix = FMatrix::Identity;

			uint8 NumRows = 0;
			uint8 NumCols = 0;

			if (Extent == 16)
			{
				NumRows = 4;
				NumCols = 4;
			}
			else if (Extent == 9)
			{
				NumRows = 3;
				NumCols = 3;
			}

			const TSharedPtr<FOgawaData> Data = Group->GetData(TrueSampleIndex);
			if (!Data)
			{
				return false;
			}

			for (uint8 Row = 0; Row < NumRows; Row++)
			{
				for (uint8 Col = 0; Col < NumCols; Col++)
				{
					if (!ReadPOD(Data, (Row * NumCols + Col) * PODSize, Matrix.M[Row][Col]))
					{
						return false;
					}
				}
			}

			return true;
		}

		const EglTFRuntimeAlembicPODType PODType;
		const uint8 Extent;

		const uint32 NextSampleIndex;
		const uint32 FirstChangedIndex;
		const uint32 LastChangedIndex;
		const float TimeSampling;

		uint64 PODSize = 0;
	};

	struct GLTFRUNTIMEALEMBIC_API FArrayProperty : public FScalarProperty
	{
		FArrayProperty() = delete;
		FArrayProperty(const FArrayProperty& Other) = delete;
		FArrayProperty& operator=(const FArrayProperty& Other) = delete;

		FArrayProperty(const FString& InName,
			const EglTFRuntimeAlembicPODType InPODType,
			const uint8 InExtent,
			const TMap<FString, FString>& InMetadata,
			const TSharedRef<FOgawaGroup> InGroup,
			const uint32 InNextSampleIndex,
			const uint32 InFirstChangedIndex,
			const uint32 InLastChangedIndex,
			const float InTimeSampling) : FScalarProperty(InName, InPODType, InExtent, InMetadata, InGroup, InNextSampleIndex, InFirstChangedIndex, InLastChangedIndex, InTimeSampling)
		{
			bIsArray = true;
		}

		uint64 Num(const uint32 TrueSampleIndex) const
		{
			TArray<uint64> Dims;
			if (!GetDims(TrueSampleIndex, Dims))
			{
				return 0;
			}

			return Algo::Accumulate(Dims, 1, [](const uint64 A, const uint64 B) { return A * B; });
		}

		bool GetDims(const uint32 TrueSampleIndex, TArray<uint64>& Dims) const
		{
			if (PODSize == 0)
			{
				return false;
			}

			const uint32 DimsIndex = TrueSampleIndex * 2 + 1;

			const TSharedPtr<FOgawaData> DimsData = Group->GetData(DimsIndex);
			if (!DimsData)
			{
				return false;
			}

			// retrieve the size
			if (DimsData->Num() == 0)
			{
				const TSharedPtr<FOgawaData> Data = Group->GetData(DimsIndex - 1);
				if (!Data)
				{
					return false;
				}
				Dims.SetNum(1, EAllowShrinking::No);
				Dims[0] = 0;
				if (Data->Num() >= 16)
				{
					Dims[0] = (Data->Num() - 16) / (PODSize * Extent);
				}
				return true;
			}

			Dims.Empty();
			uint64 Offset = 0;
			while (Offset < DimsData->Num())
			{
				uint64* Dim = DimsData->Read<uint64>(Offset);
				if (!Dim)
				{
					return false;
				}
				Dims.Add(*Dim);
				Offset += sizeof(uint64);
			}

			return true;
		}

		template<typename T>
		bool Get(const uint32 TrueSampleIndex, const uint64 ArrayIndex, const uint64 ExtentIndex, T& Value)
		{
			if (PODSize == 0 || ExtentIndex >= Extent)
			{
				return false;
			}

			const TSharedPtr<FOgawaData> Data = Group->GetData(TrueSampleIndex * 2);
			if (!Data)
			{
				return false;
			}

			return ReadPOD(Data, (ArrayIndex * PODSize * Extent) + (PODSize * ExtentIndex), Value);
		}

		template<typename T, uint8 N>
		bool Get(const uint32 TrueSampleIndex, const uint64 ArrayIndex, T(&Values)[N])
		{
			if (PODSize == 0 || N > Extent)
			{
				return false;
			}

			const TSharedPtr<FOgawaData> Data = Group->GetData(TrueSampleIndex * 2);
			if (!Data)
			{
				return false;
			}

			for (uint8 ExtentIndex = 0; ExtentIndex < N; ExtentIndex++)
			{
				if (!ReadPOD(Data, (ArrayIndex * PODSize * Extent) + (PODSize * ExtentIndex), Values[ExtentIndex]))
				{
					return false;
				}
			}

			return true;
		}

		bool Get(const uint32 TrueSampleIndex, const uint64 ArrayIndex, FVector3f& Value)
		{
			float Floats[3];
			if (!Get(TrueSampleIndex, ArrayIndex, Floats))
			{
				return false;
			}
			Value = FVector3f(Floats[0], Floats[1], Floats[2]);
			return true;
		}

		bool Get(const uint32 TrueSampleIndex, const uint64 ArrayIndex, FVector3d& Value)
		{
			double Doubles[3];
			if (!Get(TrueSampleIndex, ArrayIndex, Doubles))
			{
				return false;
			}
			Value = FVector3d(Doubles[0], Doubles[1], Doubles[2]);
			return true;
		}

		template<typename T>
		bool Get(const uint32 TrueSampleIndex, TArray<T>& Values)
		{
			if (PODSize == 0 || Extent < 3)
			{
				return false;
			}

			const uint64 NumElements = Num(TrueSampleIndex);

			Values.SetNum(NumElements, EAllowShrinking::No);

			const TSharedPtr<FOgawaData> Data = Group->GetData(TrueSampleIndex * 2);
			if (!Data)
			{
				return false;
			}

			for (uint64 ArrayIndex = 0; ArrayIndex < NumElements; ArrayIndex++)
			{
				if (!ReadPOD(Data, (ArrayIndex * PODSize * Extent), Values[ArrayIndex].X))
				{
					return false;
				}

				if (!ReadPOD(Data, (ArrayIndex * PODSize * Extent) + PODSize, Values[ArrayIndex].Y))
				{
					return false;
				}

				if (!ReadPOD(Data, (ArrayIndex * PODSize * Extent) + (PODSize * 2), Values[ArrayIndex].Z))
				{
					return false;
				}
			}

			return true;
		}
	};

	struct GLTFRUNTIMEALEMBIC_API FObject : public TSharedFromThis<FObject>
	{
		FObject() = delete;
		FObject(const FObject& Other) = delete;
		FObject& operator=(const FObject& Other) = delete;

		FObject(const TSharedPtr<FObject>& InParent, const FString& InName, const TMap<FString, FString>& InMetadata) : Parent(InParent), Name(InName), Metadata(InMetadata)
		{
			if (InParent)
			{
				if (InParent->Path == "/")
				{
					Path = "/" + Name;
				}
				else
				{
					Path = InParent->Path + "/" + Name;
				}
			}
			else
			{
				Path = "/";
			}
		}

		TSharedPtr<FObject> Parent;
		FString Name;
		FString Path;
		TMap<FString, FString> Metadata;

		TSharedPtr<FCompoundProperty> Properties = nullptr;

		TArray<TSharedRef<FObject>> Children;

		TArray<FString> GetChildrenNames() const;

		TSharedPtr<FObject> GetChild(const int32 ChildIndex) const;
		TSharedPtr<FObject> GetChild(const FString ChildName) const;

		TSharedPtr<IProperty> GetProperty(const int32 PropertyIndex) const;
		TSharedPtr<IProperty> GetProperty(const FString& PropertyName) const;

		TSharedPtr<FScalarProperty> GetScalarProperty(const int32 PropertyIndex) const;
		TSharedPtr<FScalarProperty> GetScalarProperty(const FString& PropertyName) const;

		TSharedPtr<FArrayProperty> GetArrayProperty(const int32 PropertyIndex) const;
		TSharedPtr<FArrayProperty> GetArrayProperty(const FString& PropertyName) const;

		TSharedPtr<FCompoundProperty> GetCompoundProperty(const int32 PropertyIndex) const;
		TSharedPtr<FCompoundProperty> GetCompoundProperty(const FString& PropertyName) const;

		TSharedPtr<const FObject> Find(const FString& ObjectPath) const;

		static TSharedPtr<FObject> BuildObject(const TSharedPtr<FObject>& Parent, const FString& Name, const TMap<FString, FString>& Metadata, const TSharedPtr<FOgawaGroup>& Group, const TArray<TArrayView64<uint8>>& IndexedMetadata);

		TSharedPtr<IProperty> FindProperty(const FString& PropertyPath) const;
		TSharedPtr<FArrayProperty> FindArrayProperty(const FString& PropertyPath) const;
		TSharedPtr<FScalarProperty> FindScalarProperty(const FString& PropertyPath) const;

		FString GetSchema() const;
	};

	GLTFRUNTIMEALEMBIC_API TSharedPtr<FObject> ParseArchive(const TSharedRef<FOgawaGroup> Group);
	GLTFRUNTIMEALEMBIC_API TSharedPtr<FObject> ParseArchive(const TArrayView64<uint8>& Blob);
	GLTFRUNTIMEALEMBIC_API TMap<FString, FString> DataToMetadata(const TArrayView64<uint8>& Data);
	GLTFRUNTIMEALEMBIC_API bool BuildMatrix(const uint32 OpsTrueSampleIndex, const TSharedRef<FScalarProperty>& Ops, const uint32 ValsTrueSampleIndex, const TSharedRef<FScalarProperty>& Vals, FMatrix& Matrix);
}
