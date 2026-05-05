// SPDX-License-Identifier: MIT
#include "DungeonGenerator.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Math/RandomStream.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Components/BrushComponent.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"

#if WITH_EDITOR
#include "Builders/CubeBuilder.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWFCDungeonActor, Log, All);

namespace
{
	/**
	 * Pick a mesh from `Variants` if it has at least one non-null entry;
	 * otherwise return `Fallback`. Used to honour the per-tile multi-mesh
	 * arrays (FloorMeshVariants / WallMeshVariants / ...) while keeping
	 * the original single-mesh fields working for legacy DataAssets.
	 *
	 * Null entries inside `Variants` are skipped so users can leave gaps
	 * while authoring without crashing the spawner.
	 */
	UStaticMesh* PickMeshOrFallback(
		const TArray<TObjectPtr<UStaticMesh>>& Variants,
		UStaticMesh* Fallback)
	{
		// Count non-null entries first so the random index lands on a
		// real mesh, avoids re-rolling on holes in the array.
		int32 NonNull = 0;
		for (const TObjectPtr<UStaticMesh>& M : Variants)
		{
			if (M) ++NonNull;
		}
		if (NonNull == 0)
		{
			return Fallback;
		}

		const int32 PickIdx = FMath::RandRange(0, NonNull - 1);
		int32 Seen = 0;
		for (const TObjectPtr<UStaticMesh>& M : Variants)
		{
			if (!M) continue;
			if (Seen == PickIdx) return M.Get();
			++Seen;
		}
		// Unreachable, guarded by NonNull > 0 above.
		return Fallback;
	}
}

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

	// 1. As-is, handles absolute paths and paths the user typed
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

	// Destroy every gameplay actor spawned by the previous generation
	// so iterating on the layout doesn't accumulate orphans in the level.
	for (TObjectPtr<AActor>& Actor : SpawnedActors)
	{
		if (Actor && IsValid(Actor.Get()))
		{
			Actor->Destroy();
		}
	}
	SpawnedActors.Reset();
	WalkableCellCentres.Reset();
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

	// Reset all diagnostics counters before this run. They are read in
	// SpawnCell (warn-once dedupe + per-mesh-type counts) and dumped in
	// the summary line at the end.
	DiagTileIdCounts.Reset();
	DiagSpawnedFloors = 0;
	DiagSpawnedWalls = 0;
	DiagSpawnedDoors = 0;
	DiagWarnedUnknownTileId.Reset();
	DiagWarnedNoFloorMesh.Reset();
	DiagWarnedNoWallMesh.Reset();
	DiagWarnedNoDoorMesh.Reset();

	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("[WFC] GenerateFromJson on '%s', JsonPath='%s' TileMapping='%s' "
		     "CellSizeMultiplier=%.3f bSpawnWalls=%s bFillFloorGaps=%s"),
		*GetName(),
		*JsonPath.FilePath,
		TileMapping ? *TileMapping->GetName() : TEXT("<none>"),
		CellSizeMultiplier,
		bSpawnWalls ? TEXT("true") : TEXT("false"),
		bFillFloorGaps ? TEXT("true") : TEXT("false"));

	if (!TileMapping)
	{
		UE_LOG(LogWFCDungeonActor, Warning,
			TEXT("[WFC] TileMapping is not set on '%s', assign a UTileMappingDataAsset before generating."),
			*GetName());
		return;
	}

	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("[WFC] DataAsset has %d tile-id entries: %s"),
		TileMapping->TileIdToVariants.Num(),
		*FString::JoinBy(TileMapping->TileIdToVariants, TEXT(", "),
			[](const TPair<int32, FTileVariants>& Kv) {
				// Format per entry: id{flags:Solid/Door, meshes:F/W/D}.
				// '1' means the flag is set or the mesh slot has a value;
				// '0' means cleared / null. Variants arrays are summarised
				// by their length (e.g. F=1[+3] = single mesh + 3 variants).
				return FString::Printf(
					TEXT("%d{flags=S%d/D%d, meshes=F%d[+%d]/W%d[+%d]/D%d[+%d]}"),
					Kv.Key,
					Kv.Value.bIsSolid ? 1 : 0,
					Kv.Value.bIsDoor ? 1 : 0,
					Kv.Value.FloorMesh != nullptr ? 1 : 0, Kv.Value.FloorMeshVariants.Num(),
					Kv.Value.WallMesh  != nullptr ? 1 : 0, Kv.Value.WallMeshVariants.Num(),
					Kv.Value.DoorMesh  != nullptr ? 1 : 0, Kv.Value.DoorMeshVariants.Num());
			}));

	const FString ResolvedPath = ResolveJsonPath();
	if (ResolvedPath.IsEmpty())
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC] Could not resolve JsonPath '%s' (tried as-is, ProjectDir, ProjectContentDir)."),
			*JsonPath.FilePath);
		return;
	}
	UE_LOG(LogWFCDungeonActor, Log, TEXT("[WFC] Resolved JSON path: %s"), *ResolvedPath);

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
			TEXT("JSON has no 'grid' object, was it produced by wfc_dungeon?"));
		return;
	}

	int32 JsonVersion = 1;
	Root->TryGetNumberField(TEXT("version"), JsonVersion);

	int32 GridRows = 0, GridCols = 0;
	double CellSizeCm = 200.0;
	double WallHeightCm = 300.0;
	(*GridObj)->TryGetNumberField(TEXT("rows"), GridRows);
	(*GridObj)->TryGetNumberField(TEXT("cols"), GridCols);
	(*GridObj)->TryGetNumberField(TEXT("cell_size_cm"), CellSizeCm);
	(*GridObj)->TryGetNumberField(TEXT("wall_height_cm"), WallHeightCm);
	const float CellCm = static_cast<float>(CellSizeCm) * CellSizeMultiplier;
	const float WallCm = static_cast<float>(WallHeightCm) * CellSizeMultiplier;

	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("[WFC] JSON version=%d, grid=%dx%d cell_size_cm=%.1f wall_height_cm=%.1f"),
		JsonVersion, GridRows, GridCols, CellSizeCm, WallHeightCm);

	// Lambda: parse the `cells: [...]` array and spawn every cell.
	// Single-floor only, no Z stacking.
	int32 SpawnedCount = 0;
	auto SpawnCellsArray =
		[&](const TArray<TSharedPtr<FJsonValue>>& CellsArr)
	{
		for (const TSharedPtr<FJsonValue>& Val : CellsArr)
		{
			const TSharedPtr<FJsonObject>* CellObj = nullptr;
			if (!Val.IsValid() || !Val->TryGetObject(CellObj) || !CellObj || !(*CellObj))
				continue;

			int32 r = 0, c = 0, TileId = 0;
			(*CellObj)->TryGetNumberField(TEXT("r"), r);
			(*CellObj)->TryGetNumberField(TEXT("c"), c);
			(*CellObj)->TryGetNumberField(TEXT("tile_id"), TileId);

			// Per-tile-id histogram: lets us tell apart "JSON had no
			// walls" from "plugin failed to spawn the walls it had".
			DiagTileIdCounts.FindOrAdd(TileId)++;

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
	};

	// Lambda: dump the diagnostic counters as a single human-readable
	// log block. Called once on every successful Generate call so the
	// log contains everything needed to triage "no walls" / "no floor"
	// reports without re-reading the JSON.
	auto LogSummary = [&]()
	{
		// Sort tile id histogram for deterministic output.
		TArray<int32> SortedIds;
		DiagTileIdCounts.GetKeys(SortedIds);
		SortedIds.Sort();
		FString Hist;
		for (int32 Id : SortedIds)
		{
			Hist += FString::Printf(TEXT(" %d=%d"), Id, DiagTileIdCounts[Id]);
		}

		UE_LOG(LogWFCDungeonActor, Log,
			TEXT("[WFC] === Generation summary ==="));
		UE_LOG(LogWFCDungeonActor, Log,
			TEXT("[WFC] Source: %s"), *ResolvedPath);
		UE_LOG(LogWFCDungeonActor, Log,
			TEXT("[WFC] Cells parsed: %d"), SpawnedCount);
		UE_LOG(LogWFCDungeonActor, Log,
			TEXT("[WFC] Tile id histogram (id=count):%s"), *Hist);
		UE_LOG(LogWFCDungeonActor, Log,
			TEXT("[WFC] Meshes spawned, Floors=%d Walls=%d Doors=%d (Total=%d components)"),
			DiagSpawnedFloors, DiagSpawnedWalls, DiagSpawnedDoors,
			SpawnedComponents.Num());

		// Re-emit the dedupe sets as a hint about why some categories
		// might be 0, these warnings already fired the first time
		// they hit, but consolidating them at the end makes the issue
		// obvious in a screenshot of the log tail.
		if (DiagWarnedNoFloorMesh.Num() > 0)
			UE_LOG(LogWFCDungeonActor, Warning,
				TEXT("[WFC] Walkable tile ids missing FloorMesh: %d entries"),
				DiagWarnedNoFloorMesh.Num());
		if (DiagWarnedNoWallMesh.Num() > 0)
			UE_LOG(LogWFCDungeonActor, Warning,
				TEXT("[WFC] Walkable tile ids missing WallMesh "
				     "(wall mesh lives on the FLOOR entry of the DataAsset, not the wall entry): %d entries"),
				DiagWarnedNoWallMesh.Num());
		if (DiagWarnedNoDoorMesh.Num() > 0)
			UE_LOG(LogWFCDungeonActor, Warning,
				TEXT("[WFC] Walkable tile ids missing DoorMesh: %d entries"),
				DiagWarnedNoDoorMesh.Num());
		if (DiagWarnedUnknownTileId.Num() > 0)
			UE_LOG(LogWFCDungeonActor, Warning,
				TEXT("[WFC] Tile ids in JSON with no DataAsset entry: %d distinct ids"),
				DiagWarnedUnknownTileId.Num());
	};

	// Single-floor only: expect `cells:` at the root.
	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("[WFC] Format detected: SINGLE-FLOOR, looking for 'cells' at root"));
	const TArray<TSharedPtr<FJsonValue>>* CellsArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("cells"), CellsArray) || !CellsArray)
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC] JSON has no 'cells' array, invalid format."));
		return;
	}
	SpawnCellsArray(*CellsArray);

	// Drop the gameplay actors on top of the dungeon (corner spawners,
	// random pickups, player starts). All three slots are optional and
	// silently skipped when the corresponding TSubclassOf is null.
	SpawnGameplayActors(GridRows, GridCols, CellCm);

	// Add the navmesh bounds volume so AI can pathfind on the dungeon.
	SpawnNavMeshBounds(GridRows, GridCols, CellCm);

	LogSummary();
}

/**
 * Place the mesh stack for one cell in the local space of the actor.
 *
 * Convention (matches the C++ generator):
 *   - X axis = column index x cell size (right)
 *   - Y axis = row index x cell size (forward)
 *   - Floor mesh is centred on the cell.
 *   - Walls are placed at the cell's edge that faces a solid neighbour,
 *     rotated so the wall normal points into the cell. The yaw values
 *     map cardinal directions to the offsets:
 *       N -> -90 (wall on the negative-Y edge)
 *       S -> +90
 *       E ->   0
 *       W -> 180
 *     A door neighbour replaces the wall mesh with the door variant.
 *     An out-of-bounds neighbour spawns a wall when bWallOnBorders is on.
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
		// Unknown tile id, warn once per id so a 32x32 dungeon does
		// not flood the log.
		if (!DiagWarnedUnknownTileId.Contains(TileId))
		{
			DiagWarnedUnknownTileId.Add(TileId);
			UE_LOG(LogWFCDungeonActor, Warning,
				TEXT("[WFC] tile_id=%d has no entry in DataAsset, cells with this id are skipped."),
				TileId);
		}
		return;
	}

	const FVector CellCentre(c * CellCm, r * CellCm, 0.f);

	// Floor placement: any tile that is not solid is treated as
	// walkable and gets a floor (or the gap-filler if requested).
	if (!Variant->bIsSolid)
	{
		// Record the cell centre regardless of whether a floor mesh
		// was actually placed, so the gameplay scatter step can still
		// drop pickups onto a missing-mesh tile (it's still walkable).
		WalkableCellCentres.Add(CellCentre);

		UStaticMesh* Mesh = PickMeshOrFallback(
			Variant->FloorMeshVariants, Variant->FloorMesh);
		if (Mesh)
		{
			AddMesh(Mesh, CellCentre, 0.f, *Variant);
			++DiagSpawnedFloors;
		}
		else
		{
			if (!DiagWarnedNoFloorMesh.Contains(TileId))
			{
				DiagWarnedNoFloorMesh.Add(TileId);
				UE_LOG(LogWFCDungeonActor, Warning,
					TEXT("[WFC] Walkable tile_id=%d has no FloorMesh "
					     "or FloorMeshVariants, floor will be invisible."), TileId);
			}
			if (bFillFloorGaps)
			{
				// No-op if the user has not provided a fallback. The
				// flag is here mainly as a hook for future extensions.
			}
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
			// Out-of-bounds neighbour: when bWallOnBorders is true the
			// dungeon is sealed by spawning a regular wall on this edge,
			// otherwise the border stays open (useful when the dungeon
			// sits inside a larger walkable world).
			if (bWallOnBorders)
			{
				UStaticMesh* Mesh = PickMeshOrFallback(
					Variant->WallMeshVariants, Variant->WallMesh);
				if (Mesh)
				{
					AddMesh(Mesh, Edge.Offset, Edge.Yaw, *Variant);
					++DiagSpawnedWalls;
				}
				else if (!DiagWarnedNoWallMesh.Contains(TileId))
				{
					DiagWarnedNoWallMesh.Add(TileId);
					UE_LOG(LogWFCDungeonActor, Warning,
						TEXT("[WFC] Walkable tile_id=%d has no WallMesh, "
						     "border walls (bWallOnBorders=true) will not spawn for this tile."),
						TileId);
				}
			}
			continue;
		}

		const FTileVariants* NbVariant = TileMapping->Find(NbId);
		if (!NbVariant) continue;

		if (NbVariant->bIsDoor)
		{
			UStaticMesh* Mesh = PickMeshOrFallback(
				Variant->DoorMeshVariants, Variant->DoorMesh);
			if (Mesh)
			{
				AddMesh(Mesh, Edge.Offset, Edge.Yaw, *Variant);
				++DiagSpawnedDoors;
			}
			else if (!DiagWarnedNoDoorMesh.Contains(TileId))
			{
				DiagWarnedNoDoorMesh.Add(TileId);
				UE_LOG(LogWFCDungeonActor, Warning,
					TEXT("[WFC] Walkable tile_id=%d has no DoorMesh on its DataAsset entry "
					     ", doors towards neighbouring door tiles will not spawn."), TileId);
			}
		}
		else if (NbVariant->bIsSolid)
		{
			UStaticMesh* Mesh = PickMeshOrFallback(
				Variant->WallMeshVariants, Variant->WallMesh);
			if (Mesh)
			{
				AddMesh(Mesh, Edge.Offset, Edge.Yaw, *Variant);
				++DiagSpawnedWalls;
			}
			else if (!DiagWarnedNoWallMesh.Contains(TileId))
			{
				DiagWarnedNoWallMesh.Add(TileId);
				UE_LOG(LogWFCDungeonActor, Warning,
					TEXT("[WFC] Walkable tile_id=%d has no WallMesh on its DataAsset entry "
					     ", walls between this cell and solid neighbours will not spawn. "
					     "Reminder: WallMesh lives on the FLOOR entry (the walkable cell), "
					     "not on the wall entry."), TileId);
			}
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

/**
 * Drops gameplay actors (NPC spawners, pickups, player starts) on top
 * of the freshly spawned dungeon meshes. All three classes are
 * optional, the routine silently skips any slot left null.
 *
 * Coordinate convention matches the mesh spawn pass:
 *   - X axis grows with the column index
 *   - Y axis grows with the row index
 *   - The dungeon's local origin is the actor transform, every spawn
 *     converts a local offset to world space via the actor transform
 *     so the whole rig moves with the parent actor.
 */
void ADungeonGenerator::SpawnGameplayActors(int32 Rows, int32 Cols, float CellCm)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogWFCDungeonActor, Warning,
			TEXT("[WFC|Gameplay] No UWorld, gameplay actors skipped."));
		return;
	}
	if (Rows <= 0 || Cols <= 0)
	{
		UE_LOG(LogWFCDungeonActor, Warning,
			TEXT("[WFC|Gameplay] Grid dimensions missing in JSON (rows=%d cols=%d), "
			     "gameplay actors skipped."), Rows, Cols);
		return;
	}

	const FTransform& T = GetActorTransform();
	int32 SpawnedCorners = 0, SpawnedPickups = 0, SpawnedStarts = 0;

	// NPC spawners and pickups share one offset (typically 0). Player
	// starts use their own larger offset because APlayerStart's capsule
	// extends below its origin and would clip through the floor.
	const FVector NpcPickupZ(0.f, 0.f, GameplayActorZOffsetCm);
	const FVector PlayerStartZ(0.f, 0.f, PlayerStartZOffsetCm);

	auto SpawnAt = [&](TSubclassOf<AActor> Class, const FVector& LocalPos) -> AActor*
	{
		if (!*Class) return nullptr;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = this;
		AActor* Spawned = World->SpawnActor<AActor>(
			*Class, T.TransformPosition(LocalPos), FRotator::ZeroRotator, Params);
		if (Spawned) SpawnedActors.Add(Spawned);
		return Spawned;
	};

	// Tracks which walkable cell indices have already been claimed by
	// a previous gameplay slot. PlayerStarts go first, then NPC
	// spawners, then pickups, so each tier picks from a strictly
	// smaller pool with no overlap between categories.
	TSet<int32> UsedCellIndices;

	// --- 1. PlayerStarts at the dungeon centre -------------------------
	// Find the 4 walkable cells closest to the geometric centre and put
	// one PlayerStart on each. Reserves these cells so NPC spawners
	// and pickups never land on top of the spawn area.
	if (PlayerStartClass && WalkableCellCentres.Num() > 0)
	{
		const FVector Centre(
			0.5f * static_cast<float>(Cols - 1) * CellCm,
			0.5f * static_cast<float>(Rows - 1) * CellCm,
			0.f);

		TArray<int32> SortedIdx;
		SortedIdx.Reserve(WalkableCellCentres.Num());
		for (int32 i = 0; i < WalkableCellCentres.Num(); ++i) SortedIdx.Add(i);
		SortedIdx.Sort([&](int32 A, int32 B)
		{
			return FVector::DistSquared(WalkableCellCentres[A], Centre)
			     < FVector::DistSquared(WalkableCellCentres[B], Centre);
		});

		const int32 NumStarts = FMath::Min(4, SortedIdx.Num());
		for (int32 i = 0; i < NumStarts; ++i)
		{
			const int32 CellIdx = SortedIdx[i];
			const FVector& Cell = WalkableCellCentres[CellIdx];
			if (SpawnAt(PlayerStartClass, Cell + PlayerStartZ))
			{
				UsedCellIndices.Add(CellIdx);
				++SpawnedStarts;
			}
		}
	}

	// Helper: return up to `Count` walkable cell indices not yet in
	// `Used`, picked uniformly at random without replacement via
	// Fisher-Yates partial shuffle. Mutates `Used` so successive
	// callers see the previously chosen indices as taken.
	auto PickFreeWalkable = [&](int32 Count, FRandomStream& Rng,
	                            TSet<int32>& Used) -> TArray<int32>
	{
		TArray<int32> Available;
		Available.Reserve(WalkableCellCentres.Num());
		for (int32 i = 0; i < WalkableCellCentres.Num(); ++i)
		{
			if (!Used.Contains(i)) Available.Add(i);
		}
		const int32 Want = FMath::Min(Count, Available.Num());
		for (int32 i = 0; i < Want; ++i)
		{
			const int32 j = Rng.RandRange(i, Available.Num() - 1);
			Available.Swap(i, j);
			Used.Add(Available[i]);
		}
		Available.SetNum(Want);
		return Available;
	};

	FRandomStream Rng(GameplayRandomSeed);

	// --- 2. NPC spawners scattered on walkable cells -------------------
	// Replaces the legacy "one at each grid corner" behaviour: NPC
	// spawners now land on N random walkable cells anywhere in the
	// dungeon, guaranteeing each one is on a real floor tile.
	if (CornerSpawnerClass && NumNpcSpawners > 0)
	{
		const TArray<int32> NpcCells = PickFreeWalkable(
			NumNpcSpawners, Rng, UsedCellIndices);
		for (int32 Idx : NpcCells)
		{
			const FVector& Cell = WalkableCellCentres[Idx];
			if (SpawnAt(CornerSpawnerClass, Cell + NpcPickupZ)) ++SpawnedCorners;
		}
	}

	// --- 3. Random pickups on walkable cells ---------------------------
	// Picks from the remaining pool only, so a pickup never spawns on
	// top of an NPC spawner or PlayerStart.
	if (RandomPickupClass && NumRandomPickups > 0)
	{
		const TArray<int32> PickupCells = PickFreeWalkable(
			NumRandomPickups, Rng, UsedCellIndices);
		for (int32 Idx : PickupCells)
		{
			const FVector& Cell = WalkableCellCentres[Idx];
			if (SpawnAt(RandomPickupClass, Cell + NpcPickupZ)) ++SpawnedPickups;
		}
	}

	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("[WFC|Gameplay] Spawned: corners=%d pickups=%d player_starts=%d "
		     "(walkable cells available=%d)"),
		SpawnedCorners, SpawnedPickups, SpawnedStarts,
		WalkableCellCentres.Num());
}

/**
 * Spawns a single `ANavMeshBoundsVolume` that covers the whole grid
 * footprint and has enough vertical span to include every wall and
 * doorway. The default brush of an `ANavMeshBoundsVolume` is a
 * 200x200x200 cube, so we scale it by `(SizeXYZ / 200)` to grow it
 * to the dungeon dimensions.
 *
 * After spawning we notify the navigation system so the navmesh
 * rebuilds without the user having to nudge the volume in the editor.
 */
void ADungeonGenerator::SpawnNavMeshBounds(int32 Rows, int32 Cols, float CellCm)
{
	if (!bSpawnNavMeshBounds) return;
	UWorld* World = GetWorld();
	if (!World) return;
	if (Rows <= 0 || Cols <= 0)
	{
		UE_LOG(LogWFCDungeonActor, Warning,
			TEXT("[WFC|NavMesh] Invalid grid (%dx%d), volume skipped."), Rows, Cols);
		return;
	}

	// Footprint of the dungeon in world units (post CellSizeMultiplier).
	// We add one cell of padding on each axis so the boundary walls
	// are comfortably inside the volume.
	const float SizeX = (static_cast<float>(Cols) + 1.f) * CellCm;
	const float SizeY = (static_cast<float>(Rows) + 1.f) * CellCm;
	const float SizeZ = FMath::Max(NavMeshHeightCm, 100.f);

	// Centre of the grid in actor-local space, raised so the volume's
	// pivot (its centre) lands halfway up the requested height.
	const FVector LocalCentre(
		0.5f * static_cast<float>(Cols - 1) * CellCm,
		0.5f * static_cast<float>(Rows - 1) * CellCm,
		0.5f * SizeZ);
	const FTransform& T = GetActorTransform();
	const FVector WorldCentre = T.TransformPosition(LocalCentre);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;

	ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(
		ANavMeshBoundsVolume::StaticClass(),
		WorldCentre, FRotator::ZeroRotator, Params);
	if (!Volume)
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC|NavMesh] Failed to spawn ANavMeshBoundsVolume."));
		return;
	}

#if WITH_EDITOR
	// Physically rebuild the brush polygons to the requested size.
	// UCubeBuilder.Build() rebuilds the brush in place, which actually
	// changes the volume's collision and visualisation bounds, unlike
	// SetActorScale3D which only stretches what is already there.
	if (UCubeBuilder* Cube = NewObject<UCubeBuilder>(Volume))
	{
		Cube->X = SizeX;
		Cube->Y = SizeY;
		Cube->Z = SizeZ;
		Cube->Build(World, Volume);
		Volume->ReregisterAllComponents();
	}
#else
	// Runtime build fall-back: actor scaling. Less precise (the
	// underlying brush mesh is not rebuilt) but still updates the
	// navigation bounds correctly for cooked games.
	Volume->SetActorScale3D(FVector(SizeX / 200.f, SizeY / 200.f, SizeZ / 200.f));
#endif

	SpawnedActors.Add(Volume);

	// Tell the navigation system that bounds changed so the nav data
	// is rebuilt asynchronously. Without this call the volume is
	// visible in the editor but the navmesh stays empty until the
	// user moves the volume by hand.
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
	{
		NavSys->OnNavigationBoundsUpdated(Volume);
	}

	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("[WFC|NavMesh] Spawned bounds volume at (%.0f,%.0f,%.0f) "
		     "size=(%.0f,%.0f,%.0f) cm covering grid %dx%d"),
		WorldCentre.X, WorldCentre.Y, WorldCentre.Z,
		SizeX, SizeY, SizeZ, Rows, Cols);
}

/**
 * Run the wfc_dungeon CLI as a subprocess with a fresh seed and pipe
 * the JSON it writes into the regular GenerateFromJson pipeline. The
 * temporary JSON lives under `<ProjectDir>/Saved/WFCTemp/` so the
 * editor can hot-reload it and the file is excluded from packaging.
 *
 * Falls back to a plain GenerateFromJson() when WFCBinaryPath is
 * empty: the user can still iterate on the UE5 side with a manually
 * authored JSON, no binary required.
 */
void ADungeonGenerator::GenerateRandom()
{
	// 1. Roll a seed when requested. Mixed with the millisecond
	//    component of the wall clock so two clicks within the same
	//    second still produce different layouts.
	if (bRandomizeSeedOnGenerate)
	{
		const FDateTime Now = FDateTime::UtcNow();
		WFCSeed = static_cast<int32>(Now.GetTicks() & 0x7FFFFFFF);
	}

	// 2. Without a configured binary path, fall back on the existing
	//    JSON-driven flow. Still useful for users iterating on a hand
	//    crafted JSON without the C++ generator installed.
	if (WFCBinaryPath.FilePath.IsEmpty())
	{
		UE_LOG(LogWFCDungeonActor, Warning,
			TEXT("[WFC|Random] WFCBinaryPath is empty, falling back to "
			     "GenerateFromJson on the existing JsonPath."));
		GenerateFromJson();
		return;
	}

	if (SamplePath.FilePath.IsEmpty())
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC|Random] SamplePath is required when WFCBinaryPath is set."));
		return;
	}

	// 3. Resolve binary + sample to absolute paths so the working
	//    directory of the spawned process never matters.
	const FString BinaryAbs = FPaths::ConvertRelativePathToFull(WFCBinaryPath.FilePath);
	const FString SampleAbs = FPaths::ConvertRelativePathToFull(SamplePath.FilePath);

	IPlatformFile& FileMgr = FPlatformFileManager::Get().GetPlatformFile();
	if (!FileMgr.FileExists(*BinaryAbs))
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC|Random] wfc_dungeon binary not found: %s"), *BinaryAbs);
		return;
	}
	if (!FileMgr.FileExists(*SampleAbs))
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC|Random] sample not found: %s"), *SampleAbs);
		return;
	}

	// 4. Build a fresh temp output path under Saved/. Includes the
	//    seed so multiple runs leave a trail you can re-load.
	const FString TempDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WFCTemp"));
	FileMgr.CreateDirectoryTree(*TempDir);
	const FString TempJson = FPaths::Combine(
		TempDir, FString::Printf(TEXT("dungeon_seed%d.json"), WFCSeed));

	// 5. Compose the CLI invocation. Quoted paths handle spaces in
	//    user directories ("C:/Users/Some Name/...").
	const FString Args = FString::Printf(
		TEXT("\"%s\" --rows %d --cols %d -N %d --seed %d --connectivity-attempts %d -o \"%s\""),
		*SampleAbs,
		OutputRows, OutputCols, TileN, WFCSeed, ConnectivityAttempts,
		*TempJson);

	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("[WFC|Random] Running: \"%s\" %s"), *BinaryAbs, *Args);

	// 6. CreateProc: hidden window, no input pipe, blocking on a
	//    short polling loop so we know the JSON is ready before we
	//    chain into GenerateFromJson.
	int32 ReturnCode = -1;
	FProcHandle Proc = FPlatformProcess::CreateProc(
		*BinaryAbs, *Args,
		/*bLaunchDetached=*/false,
		/*bLaunchHidden=*/true,
		/*bLaunchReallyHidden=*/true,
		nullptr, 0, nullptr, nullptr);
	if (!Proc.IsValid())
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC|Random] Failed to spawn wfc_dungeon."));
		return;
	}

	FPlatformProcess::WaitForProc(Proc);
	FPlatformProcess::GetProcReturnCode(Proc, &ReturnCode);
	FPlatformProcess::CloseProc(Proc);

	// Exit codes match wfc_dungeon: 0 = success+connected,
	// 2 = WFC failure, 3 = success but disconnected after retries.
	if (ReturnCode == 2)
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC|Random] wfc_dungeon failed (exit=2, WFC contradiction). "
			     "Try a different seed or sample."));
		return;
	}
	if (ReturnCode == 3)
	{
		UE_LOG(LogWFCDungeonActor, Warning,
			TEXT("[WFC|Random] wfc_dungeon ran but the dungeon is disconnected "
			     "(exit=3). Spawning anyway, expect isolated rooms."));
	}
	else if (ReturnCode != 0)
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC|Random] wfc_dungeon returned %d, aborting."), ReturnCode);
		return;
	}

	if (!FileMgr.FileExists(*TempJson))
	{
		UE_LOG(LogWFCDungeonActor, Error,
			TEXT("[WFC|Random] Expected output JSON not found: %s"), *TempJson);
		return;
	}

	// 7. Point the JsonPath UPROPERTY at the freshly generated file
	//    and reuse the existing parsing + spawn pipeline.
	JsonPath.FilePath = TempJson;
	UE_LOG(LogWFCDungeonActor, Log,
		TEXT("[WFC|Random] Generation OK (seed=%d, exit=%d), loading %s"),
		WFCSeed, ReturnCode, *TempJson);
	GenerateFromJson();
}
