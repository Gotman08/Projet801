// SPDX-License-Identifier: MIT
#include "DungeonGenerator.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogWFCDungeonActor, Log, All);

ADungeonGenerator::ADungeonGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;
}

void ADungeonGenerator::BeginPlay()
{
	Super::BeginPlay();
}

/**
 * Looks at the configured `JsonPath` and tries the obvious resolution
 * candidates: as-is (absolute path), relative to ProjectDir, relative
 * to ProjectContent. Returns the first existing one or empty.
 */
FString ADungeonGenerator::ResolveJsonPath() const
{
	const FString Raw = JsonPath.FilePath;
	if (Raw.IsEmpty())
	{
		return FString();
	}

	// 1. As-is — handles absolute paths and paths the user typed
	//    relative to the editor's working dir.
	if (FPaths::FileExists(Raw))
	{
		return Raw;
	}

	// 2. Project root (FPaths::ProjectDir() ends with a slash).
	const FString ViaProject = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), Raw));
	if (FPaths::FileExists(ViaProject))
	{
		return ViaProject;
	}

	// 3. Project Content folder (common drop-zone for data files).
	const FString ViaContent = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectContentDir(), Raw));
	if (FPaths::FileExists(ViaContent))
	{
		return ViaContent;
	}

	UE_LOG(LogWFCDungeonActor, Warning,
		TEXT("Could not resolve JsonPath '%s' (tried as-is, ProjectDir, ProjectContentDir)."),
		*Raw);
	return FString();
}

void ADungeonGenerator::ClearGenerated()
{
	for (TObjectPtr<UStaticMeshComponent>& Comp : SpawnedComponents)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	SpawnedComponents.Reset();
}

/**
 * Loads the JSON, validates the schema we expect from `wfc_dungeon`,
 * then walks the cell array spawning one or more meshes per cell.
 *
 * The expected JSON layout (see `apps/wfc_dungeon.cpp`):
 *   {
 *     "version": 1,
 *     "metadata": { ... },
 *     "grid": { "rows": R, "cols": C,
 *               "cell_size_cm": F, "wall_height_cm": F },
 *     "tile_alphabet": [ { "id": I, "name": "..." }, ... ],
 *     "cells": [ { "r": R, "c": C, "tile_id": I,
 *                  "neighbors": { "n": I, "s": I, "e": I, "w": I } }, ... ]
 *   }
 */
void ADungeonGenerator::GenerateFromJson()
{
	ClearGenerated();

	if (!TileMapping)
	{
		UE_LOG(LogWFCDungeonActor, Warning,
			TEXT("TileMapping is not set on '%s' — assign a UTileMappingDataAsset before generating."),
			*GetName());
		return;
	}

	const FString ResolvedPath = ResolveJsonPath();
	if (ResolvedPath.IsEmpty())
	{
		return;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ResolvedPath))
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("Failed to read JSON file: %s"), *ResolvedPath);
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("Malformed JSON in %s"), *ResolvedPath);
		return;
	}

	const TSharedPtr<FJsonObject>* GridObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("grid"), GridObj) || !GridObj || !(*GridObj))
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("JSON has no 'grid' object — was it produced by wfc_dungeon?"));
		return;
	}

	double CellSizeCm = 200.0;
	double WallHeightCm = 300.0;
	(*GridObj)->TryGetNumberField(TEXT("cell_size_cm"), CellSizeCm);
	(*GridObj)->TryGetNumberField(TEXT("wall_height_cm"), WallHeightCm);
	const float CellCm = static_cast<float>(CellSizeCm) * CellSizeMultiplier;
	const float WallCm = static_cast<float>(WallHeightCm) * CellSizeMultiplier;

	const TArray<TSharedPtr<FJsonValue>>* CellsArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("cells"), CellsArray) || !CellsArray)
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("JSON has no 'cells' array."));
		return;
	}

	int32 SpawnedCount = 0;
	for (const TSharedPtr<FJsonValue>& Val : *CellsArray)
	{
		const TSharedPtr<FJsonObject>* CellObj = nullptr;
		if (!Val.IsValid() || !Val->TryGetObject(CellObj) || !CellObj || !(*CellObj))
			continue;

		int32 r = 0, c = 0, TileId = 0;
		(*CellObj)->TryGetNumberField(TEXT("r"), r);
		(*CellObj)->TryGetNumberField(TEXT("c"), c);
		(*CellObj)->TryGetNumberField(TEXT("tile_id"), TileId);

		int32 Neighbors[4] = { -1, -1, -1, -1 };  // N S E W
		const TSharedPtr<FJsonObject>* NbObj = nullptr;
		if ((*CellObj)->TryGetObjectField(TEXT("neighbors"), NbObj) && NbObj && (*NbObj))
		{
			(*NbObj)->TryGetNumberField(TEXT("n"), Neighbors[0]);
			(*NbObj)->TryGetNumberField(TEXT("s"), Neighbors[1]);
			(*NbObj)->TryGetNumberField(TEXT("e"), Neighbors[2]);
			(*NbObj)->TryGetNumberField(TEXT("w"), Neighbors[3]);
		}

		SpawnCell(r, c, TileId, Neighbors, CellCm, WallCm);
		++SpawnedCount;
	}

	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("Dungeon generated: %d cells from %s"),
		SpawnedCount, *ResolvedPath);
}

/**
 * Place the mesh stack for one cell in the local space of the actor.
 *
 * Convention (matches the C++ generator):
 *   - X axis = column index × cell size (right)
 *   - Y axis = row index × cell size (forward)
 *   - Floor mesh is centred on the cell.
 *   - Walls are placed at the cell's edge that faces a solid neighbour,
 *     rotated so the wall normal points into the cell. The yaw values
 *     map cardinal directions to the offsets:
 *       N → -90° (wall on the negative-Y edge)
 *       S → +90°
 *       E →   0°
 *       W → 180°
 *     A door neighbour replaces the wall mesh with the door variant.
 */
void ADungeonGenerator::SpawnCell(int32 r, int32 c,
                                  int32 TileId,
                                  const int32 NeighborTileIds[4],
                                  float CellCm,
                                  float WallHeightCm)
{
	const FTileVariants* Variant = TileMapping->Find(TileId);
	if (!Variant)
	{
		// Unknown tile id — skip silently. We could log once per id
		// but that gets very chatty on big dungeons.
		return;
	}

	const FVector CellCentre(c * CellCm, r * CellCm, 0.f);

	// Floor placement: any tile that is not solid is treated as
	// walkable and gets a floor (or the gap-filler if requested).
	if (!Variant->bIsSolid)
	{
		UStaticMesh* Mesh = Variant->FloorMesh;
		if (Mesh)
		{
			AddMesh(Mesh, CellCentre, 0.f, *Variant);
		}
		else if (bFillFloorGaps)
		{
			// No-op if the user has not provided a fallback. The
			// flag is here mainly as a hook for future extensions.
		}
	}

	if (!bSpawnWalls)
	{
		return;
	}

	// Wall placement: when our cell is walkable but a cardinal
	// neighbour is solid, drop a wall on that edge (or a door if the
	// neighbour is a door tile).
	if (Variant->bIsSolid)
	{
		return;
	}

	const float HalfCell = CellCm * 0.5f;
	struct FEdge { int32 NbIndex; FVector Offset; float Yaw; };
	const FEdge Edges[4] = {
		{ 0, FVector(c * CellCm,             r * CellCm - HalfCell, 0.f), -90.f }, // N
		{ 1, FVector(c * CellCm,             r * CellCm + HalfCell, 0.f),  90.f }, // S
		{ 2, FVector(c * CellCm + HalfCell,  r * CellCm,            0.f),   0.f }, // E
		{ 3, FVector(c * CellCm - HalfCell,  r * CellCm,            0.f), 180.f }, // W
	};

	for (const FEdge& Edge : Edges)
	{
		const int32 NbId = NeighborTileIds[Edge.NbIndex];
		if (NbId < 0)
		{
			// Out-of-bounds neighbour: optional perimeter wall. We
			// keep it open to simplify navmesh rebakes; users who
			// want a hard border can wrap their dungeon in walls.
			continue;
		}

		const FTileVariants* NbVariant = TileMapping->Find(NbId);
		if (!NbVariant) continue;

		if (NbVariant->bIsDoor && Variant->DoorMesh)
		{
			AddMesh(Variant->DoorMesh, Edge.Offset, Edge.Yaw, *Variant);
		}
		else if (NbVariant->bIsSolid && Variant->WallMesh)
		{
			AddMesh(Variant->WallMesh, Edge.Offset, Edge.Yaw, *Variant);
		}
	}

	(void)WallHeightCm; // currently used only by user-side asset scaling
}

UStaticMeshComponent* ADungeonGenerator::AddMesh(UStaticMesh* Mesh,
                                                 const FVector& RelativeLocation,
                                                 float YawDegrees,
                                                 const FTileVariants& Variant)
{
	if (!Mesh) return nullptr;

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(SceneRoot);
	Comp->RegisterComponent();
	Comp->SetStaticMesh(Mesh);

	FRotator Rot(0.f, YawDegrees, 0.f);
	Rot += Variant.OffsetRotation;
	Comp->SetRelativeLocationAndRotation(RelativeLocation, Rot);
	Comp->SetRelativeScale3D(Variant.ScaleOverride);

	SpawnedComponents.Add(Comp);
	return Comp;
}
