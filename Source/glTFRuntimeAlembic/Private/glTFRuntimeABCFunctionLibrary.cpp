// Copyright 2025 - Roberto De Ioris

#include "glTFRuntimeABCFunctionLibrary.h"
#include "CompGeom/PolygonTriangulation.h"
#include "GroomAsset.h"
#include "GroomBuilder.h"

bool UglTFRuntimeABCFunctionLibrary::LoadAlembicObjectAsRuntimeLOD(UglTFRuntimeAsset* Asset, const FString& ObjectPath, const int32 SampleIndex, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& StaticMeshMaterialsConfig)
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

	FglTFRuntimePrimitive Primitive;

	TSharedPtr<glTFRuntimeAlembic::FArrayProperty> PositionsProperty = Object->FindArrayProperty(".geom/P");
	if (!PositionsProperty)
	{
		return false;
	}

	uint32 PositionsPropertyTrueSampleIndex;
	if (!PositionsProperty->GetSampleTrueIndex(SampleIndex, PositionsPropertyTrueSampleIndex))
	{
		return false;
	}

	if (!PositionsProperty->Get(PositionsPropertyTrueSampleIndex, Primitive.Positions))
	{
		return false;
	}

	for (FVector& Position : Primitive.Positions)
	{
		Position = Asset->GetParser()->TransformPosition(Position);
	}

	TSharedPtr<glTFRuntimeAlembic::FArrayProperty> FaceIndicesProperty = Object->FindArrayProperty(".geom/.faceIndices");
	if (!FaceIndicesProperty)
	{
		return false;
	}

	uint32 FaceIndicesPropertyTrueSampleIndex;
	if (!FaceIndicesProperty->GetSampleTrueIndex(SampleIndex, FaceIndicesPropertyTrueSampleIndex))
	{
		return false;
	}

	TSharedPtr<glTFRuntimeAlembic::FArrayProperty> FaceCountsProperty = Object->FindArrayProperty(".geom/.faceCounts");
	if (!FaceCountsProperty)
	{
		return false;
	}

	uint32 FaceCountsPropertyTrueSampleIndex;
	if (!FaceCountsProperty->GetSampleTrueIndex(SampleIndex, FaceCountsPropertyTrueSampleIndex))
	{
		return false;
	}

	TArray<FVector> Normals;
	TSharedPtr<glTFRuntimeAlembic::FArrayProperty> NormalsProperty = Object->FindArrayProperty(".geom/N");
	if (NormalsProperty)
	{
		uint32 NormalsPropertyTrueSampleIndex;
		if (!NormalsProperty->GetSampleTrueIndex(SampleIndex, NormalsPropertyTrueSampleIndex))
		{
			return false;
		}

		if (!NormalsProperty->Get(NormalsPropertyTrueSampleIndex, Normals))
		{
			return false;
		}
	}

	const uint32 NumFaces = FaceCountsProperty->Num(FaceCountsPropertyTrueSampleIndex);
	uint32 TotalFaceIndices = 0;
	for (uint32 FaceIndex = 0; FaceIndex < NumFaces; FaceIndex++)
	{
		uint32 NumVertices;
		if (!FaceCountsProperty->Get(FaceCountsPropertyTrueSampleIndex, FaceIndex, 0, NumVertices))
		{
			return false;
		}

		TArray<uint32> PositionIndexMap;
		TArray<uint32> VertexIndexIndexMap;
		TArray<FVector> PolygonPositions;

		for (uint32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
		{
			uint32 PositionIndex;
			if (!FaceIndicesProperty->Get(FaceIndicesPropertyTrueSampleIndex, TotalFaceIndices + VertexIndex, 0, PositionIndex))
			{
				return false;
			}

			if (!Primitive.Positions.IsValidIndex(PositionIndex))
			{
				return false;
			}

			PolygonPositions.Add(Primitive.Positions[PositionIndex]);
			PositionIndexMap.Add(PositionIndex);
			VertexIndexIndexMap.Add(TotalFaceIndices + VertexIndex);
		}

		TArray<UE::Geometry::FIndex3i> Triangles;
		PolygonTriangulation::TriangulateSimplePolygon(PolygonPositions, Triangles);
		for (uint32 TriangleIndex = 0; TriangleIndex < static_cast<uint32>(Triangles.Num()); TriangleIndex++)
		{
			Primitive.Indices.Add(PositionIndexMap[Triangles[TriangleIndex].A]);
			Primitive.Indices.Add(PositionIndexMap[Triangles[TriangleIndex].B]);
			Primitive.Indices.Add(PositionIndexMap[Triangles[TriangleIndex].C]);
			if (Normals.Num() > 0)
			{
				Primitive.Normals.Add(Asset->GetParser()->TransformVector(Normals[VertexIndexIndexMap[Triangles[TriangleIndex].A]]));
				Primitive.Normals.Add(Asset->GetParser()->TransformVector(Normals[VertexIndexIndexMap[Triangles[TriangleIndex].B]]));
				Primitive.Normals.Add(Asset->GetParser()->TransformVector(Normals[VertexIndexIndexMap[Triangles[TriangleIndex].C]]));
				Primitive.Tangents.AddDefaulted();
				Primitive.Tangents.AddDefaulted();
				Primitive.Tangents.AddDefaulted();
			}
		}

		TotalFaceIndices += NumVertices;
	}

	RuntimeLOD.Primitives.Add(MoveTemp(Primitive));

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