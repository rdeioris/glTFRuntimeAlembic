// Copyright 2025 - Roberto De Ioris

#include "glTFRuntimeABCFunctionLibrary.h"
#include "GroomAsset.h"
#include "GroomBuilder.h"

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

	TSharedPtr<FObject> FObject::BuildObject(const TSharedPtr<FObject>& Parent, const FString& Name, const TMap<FString, FString>& Metadata, const TSharedPtr<FOgawaGroup>& Group)
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
				TSharedPtr<IProperty> NewProperty = IProperty::BuildProperty(PropertyHeaders.ToSharedRef(), PropertyHeadersOffset, Properties->Children[PropertyIndex]);
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

			if ((ChildIndex + 2) >= Group->Children.Num())
			{
				return nullptr;
			}

			TSharedPtr<FOgawaGroup> ChildGroup = Group->GetGroup(1 + ChildIndex);
			if (!ChildGroup)
			{
				return nullptr;
			}

			TSharedPtr<FObject> NewChild = BuildObject(NewObject, ChildName, {}, ChildGroup);
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
		return FObject::BuildObject(nullptr, "ABC", {}, RootGroup->GetGroup(2));
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

	TSharedPtr<const FObject> FObject::Find(const FString& Path) const
	{
		TSharedPtr<const FObject> CurrentObject = AsShared();

		// absolute?
		if (Path.StartsWith("/"))
		{
			while (CurrentObject->Parent)
			{
				CurrentObject = CurrentObject->Parent;
			}
		}

		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"));

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

	TSharedPtr<IProperty> IProperty::BuildProperty(const TSharedRef<FOgawaData>& Headers, uint64& Offset, const TSharedPtr<IOgawaNode>& PropertyNode)
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

		// TODO (Metadata)
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

		// TODO support inline metadata
		if (MetadataIndex == 0xFF)
		{
			return nullptr;
		}

		// CompoundProperty
		if (PropertyType == 0)
		{
			TSharedPtr<FOgawaGroup> PropertyGroup = PropertyNode->Group();
			if (!PropertyGroup)
			{
				return nullptr;
			}

			TSharedRef<FCompoundProperty> CompoundProperty = MakeShared<FCompoundProperty>(Name, TMap<FString, FString>{});

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
					TSharedPtr<IProperty> NewProperty = BuildProperty(PropertyHeaders.ToSharedRef(), PropertyHeadersOffset, PropertyGroup->Children[PropertyIndex]);
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
}

bool UglTFRuntimeABCFunctionLibrary::LoadAlembicObjectAsRuntimeLOD(UglTFRuntimeAsset* Asset, const FString& ObjectPath, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& StaticMeshMaterialsConfig)
{
	TSharedPtr<glTFRuntimeAlembic::IOgawaNode> OgawaNode = glTFRuntimeAlembic::ParseOgawaBlob(Asset->GetParser()->GetBlob());
	if (!OgawaNode)
	{
		return false;
	}

	TSharedPtr<glTFRuntimeAlembic::FOgawaGroup> Group = OgawaNode->Group();
	if (!Group)
	{
		return false;
	}

	TSharedPtr<glTFRuntimeAlembic::FObject> Root = glTFRuntimeAlembic::ParseArchive(Group.ToSharedRef());
	if (!Root)
	{
		return false;
	}

	TSharedPtr<const glTFRuntimeAlembic::FObject> Object = Root->Find(ObjectPath);
	if (!Object)
	{
		return false;
	}

	TSharedPtr<glTFRuntimeAlembic::IProperty> GeomPProperty = Object->FindProperty(".geom/P");
	if (!GeomPProperty)
	{
		return false;
	}

	TSharedRef<glTFRuntimeAlembic::FArrayProperty> GeomPArrayProperty = StaticCastSharedRef<glTFRuntimeAlembic::FArrayProperty>(GeomPProperty.ToSharedRef());

	TArray<FVector> Vertices;
	//if (!GeomPArrayProperty->GetSample(0, Vertices))
	{
		return false;
	}

	return true;
}

UGroomAsset* UglTFRuntimeABCFunctionLibrary::LoadGroomFromAlembicObject(UglTFRuntimeAsset* Asset, const FString& ObjectPath)
{
	TSharedPtr<glTFRuntimeAlembic::IOgawaNode> OgawaNode = glTFRuntimeAlembic::ParseOgawaBlob(Asset->GetParser()->GetBlob());
	if (!OgawaNode)
	{
		return nullptr;
	}

	TSharedPtr<glTFRuntimeAlembic::FOgawaGroup> Group = OgawaNode->Group();
	if (!Group)
	{
		return nullptr;
	}

	TSharedPtr<glTFRuntimeAlembic::FObject> Root = glTFRuntimeAlembic::ParseArchive(Group.ToSharedRef());
	if (!Root)
	{
		return nullptr;
	}

	TSharedPtr<const glTFRuntimeAlembic::FObject> Object = Root->Find(ObjectPath);
	if (!Object)
	{
		return nullptr;
	}

	TSharedPtr<glTFRuntimeAlembic::FArrayProperty> NumVerticesProperty = Object->FindArrayProperty(".geom/nVertices");
	if (!NumVerticesProperty)
	{
		return nullptr;
	}

	TSharedPtr<glTFRuntimeAlembic::FArrayProperty> PositionsProperty = Object->FindArrayProperty(".geom/P");
	if (!PositionsProperty)
	{
		return nullptr;
	}

	uint32 TrueSampleIndex = 0;
	if (!NumVerticesProperty->GetSampleTrueIndex(0, TrueSampleIndex))
	{
		return nullptr;
	}

	const uint64 NumStrands = NumVerticesProperty->Num(TrueSampleIndex);

	if (NumStrands == 0)
	{
		return nullptr;
	}

	FHairDescription HairDescription;

	uint64 TotalVertexIndex = 0;

	for (uint64 StrandIndex = 0; StrandIndex < NumStrands; StrandIndex++)
	{
		FStrandID StrandID = HairDescription.AddStrand();

		uint32 NumVertices = 0;
		if (!NumVerticesProperty->Get(TrueSampleIndex, StrandIndex, 0, NumVertices))
		{
			return nullptr;
		}

		SetHairStrandAttribute(HairDescription, StrandID, HairAttribute::Strand::VertexCount, static_cast<int32>(NumVertices));

		for (uint32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
		{
			FVector Position;
			if (!PositionsProperty->Get(TrueSampleIndex, TotalVertexIndex + VertexIndex, Position))
			{
				return nullptr;
			}

			FVertexID VertexID = HairDescription.AddVertex();
			SetHairVertexAttribute(HairDescription, VertexID, HairAttribute::Vertex::Position, FVector3f(Asset->GetParser()->TransformPosition(Position)));
		}

		TStrandAttributesRef<float> WidthStrandAttributeRef = HairDescription.StrandAttributes().GetAttributesRef<float>(HairAttribute::Strand::Width);
		if (!WidthStrandAttributeRef.IsValid())
		{
			HairDescription.StrandAttributes().RegisterAttribute<float>(HairAttribute::Strand::Width);
			WidthStrandAttributeRef = HairDescription.StrandAttributes().GetAttributesRef<float>(HairAttribute::Strand::Width);
		}

		WidthStrandAttributeRef[StrandID] = 0.01f;

		TotalVertexIndex += NumVertices;
	}

	UGroomAsset* GroomAsset = NewObject<UGroomAsset>(GetTransientPackage(), NAME_None, RF_Public);
	GroomAsset->SetNumGroup(1);

	TArray<FHairGroupsLOD>& InHairGroupsLOD = GroomAsset->GetHairGroupsLOD();
	InHairGroupsLOD[0].LODs[0].GeometryType = EGroomGeometryType::Strands;

	TArray<FHairGroupsInterpolation>& InHairGroupsInterpolation = GroomAsset->GetHairGroupsInterpolation();
	InHairGroupsInterpolation[0].InterpolationSettings.bUseUniqueGuide = true;

	TArray<FHairGroupPlatformData>& OutHairGroupsData = GroomAsset->GetHairGroupsPlatformData();

	GroomAsset->GetHairGroupsPhysics()[0].SolverSettings.EnableSimulation = true;

	OutHairGroupsData[0].Cards.LODs.SetNum(1);
	OutHairGroupsData[0].Meshes.LODs.SetNum(1);

	GroomAsset->UpdateHairGroupsInfo();
#if WITH_EDITOR
	GroomAsset->UpdateCachedSettings();
#endif

	TArray<FHairGroupInfoWithVisibility>& OutHairGroupsInfo = GroomAsset->GetHairGroupsInfo();

	FHairDescriptionGroups HairDescriptionGroups;
	FGroomBuilder::BuildHairDescriptionGroups(HairDescription, HairDescriptionGroups);

	const int32 GroupIndex = 0;

	const FHairDescriptionGroup& HairGroup = HairDescriptionGroups.HairGroups[GroupIndex];
	check(GroupIndex <= HairDescriptionGroups.HairGroups.Num());
	check(GroupIndex == HairGroup.Info.GroupIndex);

	FHairStrandsDatas StrandsData;
	FHairStrandsDatas GuidesData;
	FGroomBuilder::BuildData(HairGroup, InHairGroupsInterpolation[GroupIndex], OutHairGroupsInfo[GroupIndex], StrandsData, GuidesData, true/*bAllowCurveReordering*/, true/*bApplyDecimation*/, GroomAsset->IsDeformationEnable(GroupIndex));

	FGroomBuilder::BuildBulkData(HairGroup.Info, GuidesData, OutHairGroupsData[GroupIndex].Guides.BulkData, false /*bAllowCompression*/);
	FGroomBuilder::BuildBulkData(HairGroup.Info, StrandsData, OutHairGroupsData[GroupIndex].Strands.BulkData, true /*bAllowCompression*/);

	// If there is no simulation or no global interpolation on that group there is no need for builder the interpolation data
	if (GroomAsset->NeedsInterpolationData(GroupIndex))
	{
		FHairStrandsInterpolationDatas InterpolationData;
		FGroomBuilder::BuildInterplationData(HairGroup.Info, StrandsData, GuidesData, InHairGroupsInterpolation[GroupIndex].InterpolationSettings, InterpolationData);
		FGroomBuilder::BuildInterplationBulkData(GuidesData, InterpolationData, OutHairGroupsData[GroupIndex].Strands.InterpolationBulkData);
	}
	else
	{
		OutHairGroupsData[GroupIndex].Strands.InterpolationBulkData.Reset();
	}

	FGroomBuilder::BuildClusterBulkData(StrandsData, HairDescriptionGroups.Bounds.SphereRadius, InHairGroupsLOD[GroupIndex], OutHairGroupsData[GroupIndex].Strands.ClusterBulkData);

	// Curve prototype
	if (IsRenderCurveEnabled())
	{
		FHairStrandsDatas StrandsDataNoShuffled;
		FHairStrandsDatas GuidesDataNoShuffled;
		FGroomBuilder::BuildData(HairGroup, InHairGroupsInterpolation[GroupIndex], OutHairGroupsInfo[GroupIndex], StrandsDataNoShuffled, GuidesDataNoShuffled, false /*bAllowCurveReordering*/);
		FGroomBuilder::BuildRenderCurveResourceBulkData(StrandsDataNoShuffled, OutHairGroupsData[GroupIndex].Strands.CurveResourceData);
	}

#if WITH_EDITOR
	GroomAsset->CommitHairDescription(MoveTemp(HairDescription), UGroomAsset::EHairDescriptionType::Source);
	GroomAsset->CacheDerivedDatas();
#endif

	GroomAsset->InitResources();

	return GroomAsset;
}