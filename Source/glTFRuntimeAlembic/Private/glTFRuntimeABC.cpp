// Copyright 2025 - Roberto De Ioris

#include "glTFRuntimeABC.h"

namespace glTFRuntimeAlembic
{
	TSharedPtr<IOgawaNode> ParseOgawaBlob(const TArrayView64<uint8>& Blob)
	{
		const uint64 BlobSize = static_cast<uint64>(Blob.Num());

		// header + group offset
		if (BlobSize < 16)
		{
			return nullptr;
		}

		// check magic
		if (FMemory::Memcmp(Blob.GetData(), "Ogawa", 5))
		{
			return nullptr;
		}

		const uint64* RootOffset = reinterpret_cast<const uint64*>(Blob.GetData() + 8);

		if (*RootOffset >= BlobSize)
		{
			return nullptr;
		}

		return IOgawaNode::ReadHeader(Blob, *RootOffset);
	}

	TSharedPtr<IOgawaNode> IOgawaNode::ReadHeader(const TArrayView64<uint8>& Blob, uint64 Offset)
	{
		const uint64 BlobSize = static_cast<uint64>(Blob.Num());

		// group?
		if ((Offset >> 63) == 0)
		{
			if (Offset == 0)
			{
				return MakeShared<FOgawaGroup>();
			}

			if (Offset + 8 > BlobSize)
			{
				return nullptr;
			}

			const uint64* GroupsNum = reinterpret_cast<const uint64*>(Blob.GetData() + Offset);

			if (Offset + 8 + (*GroupsNum * 8) > BlobSize)
			{
				return nullptr;
			}

			const uint64* GroupsOffsets = reinterpret_cast<const uint64*>(Blob.GetData() + Offset + 8);

			TArray<TSharedRef<IOgawaNode>> Children;

			for (uint64 GroupIndex = 0; GroupIndex < *GroupsNum; GroupIndex++)
			{
				if ((GroupsOffsets[GroupIndex] & 0x7FFFFFFFFFFFFFFF) >= BlobSize)
				{
					return nullptr;
				}

				TSharedPtr<IOgawaNode> Child = IOgawaNode::ReadHeader(Blob, GroupsOffsets[GroupIndex]);
				if (!Child)
				{
					return nullptr;
				}

				Children.Add(Child.ToSharedRef());
			}

			TSharedRef<FOgawaGroup> OgawaGroup = MakeShared<FOgawaGroup>();
			OgawaGroup->Children = Children;

			return OgawaGroup;
		}
		else
		{
			Offset &= 0x7FFFFFFFFFFFFFFF;
			if (Offset == 0)
			{
				return MakeShared<FOgawaData>();
			}

			if (Offset + 8 > BlobSize)
			{
				return nullptr;
			}
			const uint64* DataSize = reinterpret_cast<const uint64*>(Blob.GetData() + Offset);

			if (Offset + 8 + *DataSize > BlobSize)
			{
				return nullptr;
			}

			TSharedRef<FOgawaData> OgawaData = MakeShared<FOgawaData>();
			OgawaData->Data = TArrayView64<uint8>(Blob.GetData() + Offset + 8, *DataSize);

			return OgawaData;
		}
	}

	TSharedPtr<FObject> FObject::BuildObject(const TSharedPtr<FObject>& Parent, const FString& Name, const TMap<FString, FString>& Metadata, const TSharedPtr<FOgawaGroup>& Group, const TArray<TArrayView64<uint8>>& IndexedMetadata)
	{
		if (!Group || Group->Children.Num() < 1)
		{
			return nullptr;
		}

		TSharedPtr<FOgawaGroup> Properties = Group->GetGroup(0);
		if (!Properties)
		{
			return nullptr;
		}

		TSharedRef<FObject> NewObject = MakeShared<FObject>(Parent, Name, Metadata);

		NewObject->Properties = MakeShared<FCompoundProperty>("", TMap<FString, FString>{});

		if (Properties->Children.Num() > 0)
		{
			TSharedPtr<FOgawaData> PropertyHeaders = Properties->Children.Last()->Data();
			if (!PropertyHeaders)
			{
				return nullptr;
			}

			uint64 PropertyHeadersOffset = 0;
			uint64 PropertyIndex = 0;
			while (PropertyHeadersOffset < PropertyHeaders->Num())
			{
				if (PropertyIndex >= Properties->Children.Num() - 1)
				{
					return nullptr;
				}
				TSharedPtr<IProperty> NewProperty = IProperty::BuildProperty(PropertyHeaders.ToSharedRef(), PropertyHeadersOffset, Properties->Children[PropertyIndex], IndexedMetadata);
				if (!NewProperty)
				{
					return nullptr;
				}
				PropertyIndex++;

				NewObject->Properties->Children.Add(NewProperty.ToSharedRef());
			}
		}

		if (Group->Children.Num() < 2)
		{
			return NewObject;
		}

		TSharedPtr<FOgawaData> ObjectHeaders = Group->Children.Last()->Data();
		if (!ObjectHeaders)
		{
			return nullptr;
		}

		// skip hash
		if (ObjectHeaders->Num() < 32)
		{
			return nullptr;
		}

		const uint32 ObjectHeadersSize = ObjectHeaders->Num() - 32;

		uint64 Offset = 0;
		uint64 ChildIndex = 0;
		while (Offset < ObjectHeadersSize)
		{
			uint32* ChildNameSize = ObjectHeaders->Read<uint32>(Offset);
			if (!ChildNameSize)
			{
				return nullptr;
			}

			Offset += sizeof(uint32);

			FString ChildName;
			if (!ObjectHeaders->ReadUTF8(Offset, *ChildNameSize, ChildName))
			{
				return nullptr;
			}

			Offset += *ChildNameSize;

			uint8* ChildMetadataIndexOrSize = ObjectHeaders->Read<uint8>(Offset);
			if (!ChildMetadataIndexOrSize)
			{
				return nullptr;
			}

			Offset += sizeof(uint8);

			TMap<FString, FString> ChildMetadata;

			// inline metadata?
			if (*ChildMetadataIndexOrSize == 0xFF)
			{
				uint32* ChildMetadataSize = ObjectHeaders->Read<uint32>(Offset);
				if (!ChildMetadataSize)
				{
					return nullptr;
				}

				Offset += sizeof(uint32);

				ChildMetadata = DataToMetadata(ObjectHeaders->View(Offset, *ChildMetadataSize));

				Offset += *ChildMetadataSize;
			}
			else
			{
				if (!IndexedMetadata.IsValidIndex(*ChildMetadataIndexOrSize))
				{
					return nullptr;
				}

				ChildMetadata = DataToMetadata(IndexedMetadata[*ChildMetadataIndexOrSize]);
			}

			if ((ChildIndex + 2) >= Group->Children.Num())
			{
				return nullptr;
			}

			TSharedPtr<FOgawaGroup> ChildGroup = Group->GetGroup(1 + ChildIndex);
			if (!ChildGroup)
			{
				return nullptr;
			}

			TSharedPtr<FObject> NewChild = BuildObject(NewObject, ChildName, ChildMetadata, ChildGroup, IndexedMetadata);
			if (!NewChild)
			{
				return nullptr;
			}

			NewObject->Children.Add(NewChild.ToSharedRef());
			ChildIndex++;
		}

		return NewObject;
	}

	TSharedPtr<FObject> ParseArchive(const TSharedRef<FOgawaGroup> RootGroup)
	{
		TMap<FString, FString> FileMetadata;
		// retrieve file metadata
		TSharedPtr<FOgawaData> FileMetadataData = RootGroup->GetData(3);
		if (FileMetadataData)
		{
			FileMetadata = DataToMetadata(FileMetadataData->Data);
		}

		// retrieve indexed metadata
		TArray<TArrayView64<uint8>> IndexedMetadata;
		// first item is always empty
		IndexedMetadata.AddDefaulted();
		TSharedPtr<FOgawaData> IndexedMetadataData = RootGroup->GetData(5);
		if (IndexedMetadataData)
		{
			uint64 Offset = 0;
			while (Offset < IndexedMetadataData->Num())
			{
				uint8* MetadataSize = IndexedMetadataData->Read<uint8>(Offset);
				if (!MetadataSize)
				{
					return nullptr;
				}

				Offset += sizeof(uint8);

				IndexedMetadata.Add(IndexedMetadataData->View(Offset, *MetadataSize));

				Offset += *MetadataSize;
			}
		}

		return FObject::BuildObject(nullptr, "ABC", FileMetadata, RootGroup->GetGroup(2), IndexedMetadata);
	}

	TSharedPtr<FObject> ParseArchive(const TArrayView64<uint8>& Blob)
	{
		TSharedPtr<IOgawaNode> OgawaNode = ParseOgawaBlob(Blob);
		if (!OgawaNode)
		{
			return nullptr;
		}

		TSharedPtr<FOgawaGroup> RootGroup = OgawaNode->Group();
		if (!RootGroup)
		{
			return nullptr;
		}

		return ParseArchive(RootGroup.ToSharedRef());
	}

	TArray<FString> FObject::GetChildrenNames() const
	{
		TArray<FString> Names;

		for (const TSharedRef<FObject>& Child : Children)
		{
			Names.Add(Child->Name);
		}

		return Names;
	}

	TSharedPtr<FObject> FObject::GetChild(const int32 ChildIndex) const
	{
		if (!Children.IsValidIndex(ChildIndex))
		{
			return nullptr;
		}

		return Children[ChildIndex];
	}

	TSharedPtr<FObject> FObject::GetChild(const FString ChildName) const
	{
		for (const TSharedRef<FObject>& Child : Children)
		{
			if (Child->Name == ChildName)
			{
				return Child;
			}
		}

		return nullptr;
	}

	TSharedPtr<const FObject> FObject::Find(const FString& InPath) const
	{
		TSharedPtr<const FObject> CurrentObject = AsShared();

		// absolute?
		if (InPath.StartsWith("/"))
		{
			while (CurrentObject->Parent)
			{
				CurrentObject = CurrentObject->Parent;
			}
		}

		TArray<FString> Parts;
		InPath.ParseIntoArray(Parts, TEXT("/"));

		for (const FString& Part : Parts)
		{
			CurrentObject = CurrentObject->GetChild(Part);
			if (!CurrentObject)
			{
				return nullptr;
			}
		}

		return CurrentObject;
	}

	TSharedPtr<IProperty> FObject::FindProperty(const FString& PropertyPath) const
	{
		TSharedPtr<FCompoundProperty> CurrentCompoundProperty = Properties;

		TSharedPtr<IProperty> CurrentProperty = nullptr;

		TArray<FString> Parts;
		PropertyPath.ParseIntoArray(Parts, TEXT("/"));

		for (int32 PartIndex = 0; PartIndex < Parts.Num(); PartIndex++)
		{
			const FString& Part = Parts[PartIndex];
			CurrentProperty = CurrentCompoundProperty->GetChild(Part);
			if (!CurrentProperty)
			{
				return nullptr;
			}

			if (!CurrentProperty->bIsCompound && PartIndex < Parts.Num() - 1)
			{
				return nullptr;
			}

			CurrentCompoundProperty = StaticCastSharedPtr<FCompoundProperty>(CurrentProperty);
		}

		return CurrentProperty;
	}

	TSharedPtr<IProperty> IProperty::BuildProperty(const TSharedRef<FOgawaData>& Headers, uint64& Offset, const TSharedPtr<IOgawaNode>& PropertyNode, const TArray<TArrayView64<uint8>>& IndexedMetadata)
	{
		if (!PropertyNode)
		{
			return nullptr;
		}

		const uint32* Info = Headers->Read<uint32>(Offset);
		if (!Info)
		{
			return nullptr;
		}

		Offset += sizeof(uint32);

		const uint8 PropertyType = *Info & 0x3;
		const uint8 SizeHint = (*Info >> 2) & 0x3;

		auto SizeHintReader8 = [](const TSharedRef<FOgawaData>& Headers, uint64& Offset, uint32& Value) -> bool
			{
				const uint8* UInt8Value = Headers->Read<uint8>(Offset);
				if (!UInt8Value)
				{
					return false;
				}
				Value = *UInt8Value;
				Offset += sizeof(uint8);
				return true;
			};

		auto SizeHintReader16 = [](const TSharedRef<FOgawaData>& Headers, uint64& Offset, uint32& Value) -> bool
			{
				const uint16* UInt16Value = Headers->Read<uint16>(Offset);
				if (!UInt16Value)
				{
					return false;
				}
				Value = *UInt16Value;
				Offset += sizeof(uint16);
				return true;
			};

		auto SizeHintReader32 = [](const TSharedRef<FOgawaData>& Headers, uint64& Offset, uint32& Value) -> bool
			{
				const uint32* UInt32Value = Headers->Read<uint32>(Offset);
				if (!UInt32Value)
				{
					return false;
				}
				Value = *UInt32Value;
				Offset += sizeof(uint32);
				return true;
			};

		TFunction<bool(const TSharedRef<FOgawaData>& Headers, uint64& Offset, uint32& Value)> SizeHintReader = nullptr;

		uint8 MetadataIndex = 0;
		EglTFRuntimeAlembicPODType PODType = EglTFRuntimeAlembicPODType::Unknown;
		uint8 Extent = 0;

		uint32 NextSampleIndex = 0;
		uint32 FirstChangedIndex = 0;
		uint32 LastChangedIndex = 0;
		float TimeSampling = 0;

		if (SizeHint == 0)
		{
			SizeHintReader = SizeHintReader8;
		}
		else if (SizeHint == 1)
		{
			SizeHintReader = SizeHintReader16;
		}
		else if (SizeHint == 2)
		{
			SizeHintReader = SizeHintReader32;
		}
		else
		{
			return nullptr;
		}

		// scalar or array
		if (PropertyType != 0)
		{
			PODType = static_cast<EglTFRuntimeAlembicPODType>((*Info >> 4) & 0xF);
			if (PODType >= EglTFRuntimeAlembicPODType::NumTypes)
			{
				return nullptr;
			}

			const bool bHasTimeSamplingIndex = (*Info >> 8) & 1;
			const bool bHasFirstAndLastChangedIndex = (*Info >> 9) & 1;
			const bool Homogenous = (*Info >> 10) & 1;
			const bool bZeroFirstAndLastChangedIndex = (*Info >> 11) & 1;
			Extent = (*Info >> 12) & 0xFF;
			MetadataIndex = (*Info >> 20) & 0xFF;

			if (!SizeHintReader(Headers, Offset, NextSampleIndex))
			{
				return nullptr;
			}

			if (bHasFirstAndLastChangedIndex)
			{
				if (!SizeHintReader(Headers, Offset, FirstChangedIndex))
				{
					return nullptr;
				}

				if (!SizeHintReader(Headers, Offset, LastChangedIndex))
				{
					return nullptr;
				}
			}
			else if (bZeroFirstAndLastChangedIndex)
			{
				FirstChangedIndex = 0;
				LastChangedIndex = 0;
			}
			else
			{
				FirstChangedIndex = 1;
				LastChangedIndex = NextSampleIndex - 1;
			}

			// TODO manage time sampling
			if (bHasTimeSamplingIndex)
			{
				uint32 TimeSamplingIndex;
				if (!SizeHintReader(Headers, Offset, TimeSamplingIndex))
				{
					return nullptr;
				}
				TimeSampling = 0;
			}
			else
			{
				TimeSampling = 0;
			}
		}

		uint32 NameSize;
		if (!SizeHintReader(Headers, Offset, NameSize))
		{
			return nullptr;
		}

		FString Name;
		if (!Headers->ReadUTF8(Offset, NameSize, Name))
		{
			return nullptr;
		}

		Offset += NameSize;

		TMap<FString, FString> PropertyMetadata;
		// inline metadata
		if (MetadataIndex == 0xFF)
		{
			uint32 PropertyMetadataSize = 0;
			if (!SizeHintReader(Headers, Offset, PropertyMetadataSize))
			{
				return nullptr;
			}

			PropertyMetadata = DataToMetadata(Headers->View(Offset, PropertyMetadataSize));

			Offset += PropertyMetadataSize;
		}
		else
		{
			if (!IndexedMetadata.IsValidIndex(MetadataIndex))
			{
				return nullptr;
			}

			PropertyMetadata = DataToMetadata(IndexedMetadata[MetadataIndex]);
		}

		// CompoundProperty
		if (PropertyType == 0)
		{
			TSharedPtr<FOgawaGroup> PropertyGroup = PropertyNode->Group();
			if (!PropertyGroup)
			{
				return nullptr;
			}

			TSharedRef<FCompoundProperty> CompoundProperty = MakeShared<FCompoundProperty>(Name, PropertyMetadata);

			if (PropertyGroup->Children.Num() > 0)
			{
				TSharedPtr<FOgawaData> PropertyHeaders = PropertyGroup->Children.Last()->Data();
				if (!PropertyHeaders)
				{
					return nullptr;
				}

				uint64 PropertyHeadersOffset = 0;
				uint64 PropertyIndex = 0;
				while (PropertyHeadersOffset < PropertyHeaders->Num())
				{
					if (PropertyIndex >= PropertyGroup->Children.Num() - 1)
					{
						return nullptr;
					}
					TSharedPtr<IProperty> NewProperty = BuildProperty(PropertyHeaders.ToSharedRef(), PropertyHeadersOffset, PropertyGroup->Children[PropertyIndex], IndexedMetadata);
					if (!NewProperty)
					{
						return nullptr;
					}
					PropertyIndex++;

					CompoundProperty->Children.Add(NewProperty.ToSharedRef());
				}
			}

			return CompoundProperty;
		}
		else if (PropertyType == 1) // ScalarProperty
		{
			TSharedPtr<FOgawaGroup> PropertyGroup = PropertyNode->Group();
			if (!PropertyGroup)
			{
				return nullptr;
			}

			return MakeShared<FScalarProperty>(Name, PODType, Extent, TMap<FString, FString>{}, PropertyGroup.ToSharedRef(), NextSampleIndex, FirstChangedIndex, LastChangedIndex, TimeSampling);
		}
		else // ArrayProperty, we cover both 2 and 3 here, as 3 means "scalar like"
		{
			TSharedPtr<FOgawaGroup> PropertyGroup = PropertyNode->Group();
			if (!PropertyGroup)
			{
				return nullptr;
			}

			return MakeShared<FArrayProperty>(Name, PODType, Extent, TMap<FString, FString>{}, PropertyGroup.ToSharedRef(), NextSampleIndex, FirstChangedIndex, LastChangedIndex, TimeSampling);
		}
	}

	TSharedPtr<IProperty> FObject::GetProperty(const int32 PropertyIndex) const
	{
		if (!Properties->Children.IsValidIndex(PropertyIndex))
		{
			return nullptr;
		}

		return Properties->Children[PropertyIndex];
	}

	TSharedPtr<IProperty> FObject::GetProperty(const FString& PropertyName) const
	{
		for (const TSharedRef<IProperty>& Property : Properties->Children)
		{
			if (Property->Name == PropertyName)
			{
				return Property;
			}
		}

		return nullptr;
	}

	TSharedPtr<FArrayProperty> FObject::GetArrayProperty(const int32 PropertyIndex) const
	{
		TSharedPtr<IProperty> Property = GetProperty(PropertyIndex);
		if (!Property)
		{
			return nullptr;
		}

		if (Property->bIsCompound)
		{
			return nullptr;
		}

		TSharedPtr<FScalarProperty> ScalarProperty = StaticCastSharedPtr<FScalarProperty>(Property);
		if (!ScalarProperty->bIsArray)
		{
			return nullptr;
		}

		return StaticCastSharedPtr<FArrayProperty>(ScalarProperty);
	}

	TSharedPtr<FArrayProperty> FObject::GetArrayProperty(const FString& PropertyName) const
	{
		TSharedPtr<IProperty> Property = GetProperty(PropertyName);
		if (!Property)
		{
			return nullptr;
		}

		if (Property->bIsCompound)
		{
			return nullptr;
		}

		TSharedPtr<FScalarProperty> ScalarProperty = StaticCastSharedPtr<FScalarProperty>(Property);
		if (!ScalarProperty->bIsArray)
		{
			return nullptr;
		}

		return StaticCastSharedPtr<FArrayProperty>(ScalarProperty);
	}

	TSharedPtr<FArrayProperty> FObject::FindArrayProperty(const FString& PropertyPath) const
	{
		TSharedPtr<IProperty> Property = FindProperty(PropertyPath);
		if (!Property)
		{
			return nullptr;
		}

		if (Property->bIsCompound)
		{
			return nullptr;
		}

		TSharedPtr<FScalarProperty> ScalarProperty = StaticCastSharedPtr<FScalarProperty>(Property);
		if (!ScalarProperty->bIsArray)
		{
			return nullptr;
		}

		return StaticCastSharedPtr<FArrayProperty>(ScalarProperty);
	}

	TSharedPtr<IProperty> FCompoundProperty::GetChild(const int32 ChildIndex) const
	{
		if (!Children.IsValidIndex(ChildIndex))
		{
			return nullptr;
		}

		return Children[ChildIndex];
	}

	TSharedPtr<IProperty> FCompoundProperty::GetChild(const FString ChildName) const
	{
		for (const TSharedRef<IProperty>& Child : Children)
		{
			if (Child->Name == ChildName)
			{
				return Child;
			}
		}

		return nullptr;
	}

	TMap<FString, FString> DataToMetadata(const TArrayView64<uint8>& Data)
	{
		TArray<FString> Items;
		TArray<uint8> CurrentItem;
		for (int64 Index = 0; Index < Data.Num(); Index++)
		{
			if (Data[Index] == ';')
			{
				CurrentItem.Add(0);
				Items.Add(UTF8_TO_TCHAR(CurrentItem.GetData()));
				CurrentItem.Empty();
			}
			else
			{
				CurrentItem.Add(Data[Index]);
			}
		}

		if (CurrentItem.Num() > 0)
		{
			CurrentItem.Add(0);
			Items.Add(UTF8_TO_TCHAR(CurrentItem.GetData()));
		}

		TMap<FString, FString> Metadata;

		for (const FString& Item : Items)
		{
			TArray<FString> KeyValue;
			Item.ParseIntoArray(KeyValue, TEXT("="), false);
			if (KeyValue.Num() >= 2)
			{
				Metadata.Add(KeyValue[0], KeyValue[1]);
			}
		}

		return Metadata;
	}
}