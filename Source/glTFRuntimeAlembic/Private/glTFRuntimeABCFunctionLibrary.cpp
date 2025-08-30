// Copyright 2025 - Roberto De Ioris

#include "glTFRuntimeABCFunctionLibrary.h"
#include "CompGeom/PolygonTriangulation.h"
#include "GroomAsset.h"
#include "GroomBuilder.h"

bool UglTFRuntimeABCFunctionLibrary::LoadAlembicObjectAsRuntimeLOD(UglTFRuntimeAsset* Asset, const FString& ObjectPath, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& StaticMeshMaterialsConfig)
{
	TSharedPtr<glTFRuntimeAlembic::FObject> Root = glTFRuntimeAlembic::ParseArchive(Asset->GetParser()->GetBlob());
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

	FglTFRuntimePrimitive Primitive;

	TSharedPtr<glTFRuntimeAlembic::FArrayProperty> PositionsProperty = Object->FindArrayProperty(".geom/P");
	if (!PositionsProperty)
	{
		return false;
	}

	if (!PositionsProperty->Get(0, Primitive.Positions))
	{
		return false;
	}

	TSharedPtr<glTFRuntimeAlembic::FArrayProperty> FaceIndicesProperty = Object->FindArrayProperty(".geom/.faceIndices");
	if (!FaceIndicesProperty)
	{
		return false;
	}

#if 0
	if (!FaceIndicesProperty->Get(0, Primitive.Indices))
	{
		return false;
	}

	PolygonTriangulation::TriangulateSimplePolygon()
#endif

	return true;
}

UGroomAsset* UglTFRuntimeABCFunctionLibrary::LoadGroomFromAlembicObject(UglTFRuntimeAsset* Asset, const FString& ObjectPath)
{
	TSharedPtr<glTFRuntimeAlembic::FObject> Root = glTFRuntimeAlembic::ParseArchive(Asset->GetParser()->GetBlob());
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