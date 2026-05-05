// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TileMappingDataAsset.h"
#include "DungeonGenerator.generated.h"

class UStaticMeshComponent;
class USceneComponent;

/**
 * @brief Actor that materialises a WFC-generated dungeon in the level.
 *
 * Drag this Actor into the world, set:
 *   - `JsonPath` to the `dungeon.json` produced by the C++
 *     `wfc_dungeon` CLI;
 *   - `TileMapping` to a `UTileMappingDataAsset` that resolves each
 *     tile id to a static mesh.
 *
 * Then click the "Generate From JSON" button in the Details panel
 * (`UFUNCTION(CallInEditor)`). The actor parses the JSON, iterates
 * the cells, and adds one or more `UStaticMeshComponent` per cell
 * according to the tile id and its cardinal neighbours:
 *
 *   - **Floor** : every walkable tile gets a floor mesh on the cell.
 *   - **Walls** : a wall mesh is added on each cardinal side that
 *     borders a solid neighbour, rotated so the wall faces the cell.
 *   - **Doors** : the wall on a side facing a door tile is replaced
 *     by the door mesh.
 *
 * Click "Clear Generated" to remove every spawned component before
 * regenerating from a different JSON.
 *
 * The actor stores spawned components as child components of itself
 * (rather than spawning separate Actors) so the whole dungeon moves /
 * rotates / scales cleanly with the parent transform.
 */
UCLASS(Blueprintable, ClassGroup = (WFC),
	meta = (BlueprintSpawnableComponent))
class WFCDUNGEON_API ADungeonGenerator : public AActor
{
	GENERATED_BODY()

public:
	ADungeonGenerator();

	/** Path to the JSON file emitted by `wfc_dungeon`. Either an
	 *  absolute path or a path relative to the project's root
	 *  directory (`FPaths::ProjectDir()`). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Input",
		meta = (RelativeToGameDir))
	FFilePath JsonPath;

	/** Mesh assignments. The DataAsset must contain at least every
	 *  tile id present in the JSON, otherwise the corresponding cells
	 *  are skipped (with a warning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Input")
	TObjectPtr<UTileMappingDataAsset> TileMapping = nullptr;

	/** Multiplier applied to the cell size embedded in the JSON. Use
	 *  it to scale the whole dungeon without re-running the generator
	 *  (e.g. shrink to 0.5× for a top-down preview). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Layout",
		meta = (ClampMin = "0.01"))
	float CellSizeMultiplier = 1.f;

	/** When true, walls and doors are spawned. Disable to inspect
	 *  only the floor layout (useful when debugging asset alignment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Layout")
	bool bSpawnWalls = true;

	/** When true, an invisible default floor mesh is forced on cells
	 *  whose tile id has no `FloorMesh` set, to preserve continuity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Layout")
	bool bFillFloorGaps = false;

	/** When true, every walkable cell on the dungeon perimeter spawns
	 *  a wall on the side that would face the empty space outside the
	 *  grid (out-of-bounds neighbour). Disable to keep an open border,
	 *  for instance when the dungeon sits inside a larger world the
	 *  player can roam through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Layout")
	bool bWallOnBorders = true;

	// ---------- Gameplay actors (mini-game scaffold) ----------
	// Optional: assign Blueprint classes here and the Generate step will
	// drop them into the level on top of the dungeon meshes. All actors
	// spawned this way are tracked so ClearGenerated() also destroys
	// them, keeping the level clean across iterations.

	/** Class scattered on random walkable cells across the whole map
	 *  (NOT just the four corners, despite the legacy name). Typical
	 *  use: `BP_ShooterNPCSpawner` that pumps enemies into the dungeon.
	 *  Count is controlled by `NumNpcSpawners`, placement is guaranteed
	 *  to land on walkable cells and never overlaps with pickups or
	 *  player starts. Leave null to disable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Gameplay")
	TSubclassOf<AActor> CornerSpawnerClass;

	/** Spawned at random walkable cells. Typical use:
	 *  `BP_ShooterPickup` (ammo / health / weapon crates). The number
	 *  of pickups is controlled by `NumRandomPickups`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Gameplay")
	TSubclassOf<AActor> RandomPickupClass;

	/** Spawned in a 2x2 cluster at the centre of the dungeon. Typical
	 *  use: `APlayerStart` (engine-builtin) or a custom subclass. The
	 *  count is fixed at 4, matching a 4-player co-op start area. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Gameplay")
	TSubclassOf<AActor> PlayerStartClass;

	/** How many `CornerSpawnerClass` (NPC spawners) to scatter on
	 *  walkable cells. Clamped to the number of walkable cells
	 *  remaining after PlayerStarts have been placed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Gameplay",
		meta = (ClampMin = "0"))
	int32 NumNpcSpawners = 4;

	/** How many `RandomPickupClass` instances to scatter on walkable
	 *  cells. Cells are picked uniformly without replacement, and
	 *  exclude cells already used by NPC spawners and player starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Gameplay",
		meta = (ClampMin = "0"))
	int32 NumRandomPickups = 8;

	/** Vertical offset in cm applied to NPC spawners and pickups so
	 *  they sit on the floor surface rather than floating above it.
	 *  Default 0 places the actor's local origin at the cell centre's
	 *  Z, which is correct for BP actors whose pivot is at their feet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Gameplay")
	float GameplayActorZOffsetCm = 0.f;

	/** Vertical offset in cm applied to PlayerStart actors specifically.
	 *  APlayerStart's collision capsule extends ~88 cm below its origin,
	 *  so a Z of 0 makes the capsule clip below the floor and the player
	 *  spawns under the map. Default 100 lifts the origin clear of the
	 *  floor mesh and keeps the capsule fully above ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Gameplay")
	float PlayerStartZOffsetCm = 100.f;

	/** RNG seed used to pick the random pickup positions. Same seed
	 *  reproduces the same scatter, change it to reroll. Independent
	 *  from the WFC seed so you can keep the dungeon layout and only
	 *  shuffle the pickups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Gameplay")
	int32 GameplayRandomSeed = 1;

	// ---------- One-click random regeneration ----------
	// `Generate Random` spawns the wfc_dungeon binary as a subprocess
	// with the parameters below, writes a temp JSON to Saved/WFCTemp,
	// then chains into the regular GenerateFromJson pipeline. No manual
	// JSON management required.

	/** Absolute path to the `wfc_dungeon.exe` binary. Built by CMake
	 *  on the C++ side (see `apps/wfc_dungeon.cpp` in the parent repo).
	 *  If left empty, `Generate Random` falls back to the existing
	 *  JsonPath behaviour (no fresh generation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Random",
		meta = (FilePathFilter = "exe"))
	FFilePath WFCBinaryPath;

	/** Absolute path to the sample `.txt` consumed by the WFC solver
	 *  (e.g. `samples/rooms.txt`). Required when `WFCBinaryPath` is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Random",
		meta = (FilePathFilter = "txt"))
	FFilePath SamplePath;

	/** Output grid height passed to wfc_dungeon as `--rows`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Random",
		meta = (ClampMin = "1"))
	int32 OutputRows = 24;

	/** Output grid width passed to wfc_dungeon as `--cols`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Random",
		meta = (ClampMin = "1"))
	int32 OutputCols = 24;

	/** Tile window size passed to wfc_dungeon as `-N`. 2 captures pair
	 *  adjacencies, 3 captures L-corners (richer but slower). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Random",
		meta = (ClampMin = "2", ClampMax = "5"))
	int32 TileN = 2;

	/** Connectivity retry budget passed as `--connectivity-attempts`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Random",
		meta = (ClampMin = "1"))
	int32 ConnectivityAttempts = 5;

	/** WFC RNG seed. Ignored when `bRandomizeSeedOnGenerate` is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Random")
	int32 WFCSeed = 42;

	/** When true, `Generate Random` rolls a fresh seed (current time
	 *  in seconds) on every click so the dungeon is always different. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Random")
	bool bRandomizeSeedOnGenerate = true;

	// ---------- NavMesh bounds ----------
	// Drops an `ANavMeshBoundsVolume` covering the dungeon at the end
	// of every Generate so AI controllers (e.g. enemies pumped by the
	// corner spawners) can pathfind on the freshly placed floor.

	/** When true, an `ANavMeshBoundsVolume` is spawned at the dungeon
	 *  centre, scaled to cover the whole grid, and the navigation
	 *  system is poked to rebuild. The volume is tracked alongside
	 *  the gameplay actors so `Clear Generated` removes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|NavMesh")
	bool bSpawnNavMeshBounds = true;

	/** Vertical span of the spawned NavMeshBoundsVolume in cm. Must be
	 *  taller than the wall height so doorways and elevated cells are
	 *  inside the volume, otherwise the navmesh stops at floor level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|NavMesh",
		meta = (ClampMin = "100.0"))
	float NavMeshHeightCm = 1000.f;

	/** Read JsonPath, parse it, and instantiate the dungeon. Existing
	 *  generated components are cleared first. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "WFC")
	void GenerateFromJson();

	/**
	 * @brief One-click full regeneration: runs the wfc_dungeon binary
	 *        on the configured sample with a fresh seed, then chains
	 *        into GenerateFromJson on the temporary output.
	 *
	 * Falls back to GenerateFromJson directly when WFCBinaryPath is
	 * empty, so users without the C++ binary can still iterate on the
	 * UE5 side using a pre-generated JSON.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "WFC")
	void GenerateRandom();

	/** Remove every component previously created by GenerateFromJson. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "WFC")
	void ClearGenerated();

protected:
	virtual void BeginPlay() override;

private:
	/** Root scene component so the actor has a transform handle. */
	UPROPERTY(VisibleAnywhere, Category = "WFC|Internal")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Tracking list of every component this actor created. Cleared
	 *  on regeneration so old meshes don't leak. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SpawnedComponents;

	/** Tracking list of full actors spawned on top of the dungeon
	 *  (NPC spawners at corners, random pickups, player starts).
	 *  ClearGenerated() destroys every entry so iterating on the
	 *  layout doesn't leave orphaned actors in the level. */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedActors;

	/** World-space (relative to the actor) centre point of every
	 *  walkable cell encountered during the most recent Generate
	 *  call. Used by SpawnGameplayActors to scatter pickups on real
	 *  walkable ground rather than into walls. */
	TArray<FVector> WalkableCellCentres;

	// --- Diagnostics state (reset at the start of GenerateFromJson) ---
	// All of this is purely for logging. The non-UPROPERTY containers
	// are fine here because the data is regenerated from scratch on
	// every Generate call, so there is nothing to serialise.

	/** Per-tile counters of how many times each tile_id appeared in
	 *  the JSON cells array. Helps the user distinguish "the JSON has
	 *  no walls" from "the plugin failed to spawn walls". */
	TMap<int32, int32> DiagTileIdCounts;

	/** Per-call counts of meshes actually instantiated, broken down
	 *  by their semantic role. The summary line at the end of
	 *  GenerateFromJson reports each one so a quick read tells you
	 *  whether floors/walls/doors/stairs all spawned in expected
	 *  proportions. */
	int32 DiagSpawnedFloors = 0;
	int32 DiagSpawnedWalls = 0;
	int32 DiagSpawnedDoors = 0;

	/** Tile ids we have already warned about, deduplicated by category
	 *  so a 32x32 dungeon does not spam the log with the same warning
	 *  thousands of times. */
	TSet<int32> DiagWarnedUnknownTileId;
	TSet<int32> DiagWarnedNoFloorMesh;
	TSet<int32> DiagWarnedNoWallMesh;
	TSet<int32> DiagWarnedNoDoorMesh;

	/**
	 * @brief Resolve the absolute path of the JSON, looking first at
	 *        `JsonPath` as-is, then relative to ProjectDir.
	 */
	FString ResolveJsonPath() const;

	/**
	 * @brief Spawn the meshes for one cell.
	 *
	 * @param r, c               Cell coordinates inside the grid.
	 * @param TileId             Tile id of this cell.
	 * @param NeighborTileIds    Cardinal neighbours: index 0=N, 1=S,
	 *                           2=E, 3=W. -1 means out-of-bounds.
	 * @param CellCm             Final cell size in cm (already scaled).
	 * @param WallHeightCm       Wall height in cm.
	 */
	void SpawnCell(int32 r, int32 c,
	               int32 TileId,
	               const int32 NeighborTileIds[4],
	               float CellCm,
	               float WallHeightCm);

	/**
	 * @brief Add a single static mesh component at a relative location
	 *        and yaw, applying the variant's offset rotation and scale.
	 */
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh,
	                              const FVector& RelativeLocation,
	                              float YawDegrees,
	                              const FTileVariants& Variant);

	/**
	 * @brief Spawn the configured gameplay actors (corner spawners,
	 *        random pickups, player starts) on top of the dungeon.
	 *
	 * @param Rows, Cols  Grid dimensions parsed from the JSON.
	 * @param CellCm      Cell size in cm, after CellSizeMultiplier.
	 *
	 * Skips silently any of the three slots that are not configured
	 * (`TSubclassOf` left null), and clamps `NumRandomPickups` to the
	 * number of walkable cells actually collected during the spawn pass.
	 */
	void SpawnGameplayActors(int32 Rows, int32 Cols, float CellCm);

	/**
	 * @brief Drop a single `ANavMeshBoundsVolume` covering the whole
	 *        dungeon footprint, then poke the navigation system so the
	 *        navmesh rebuilds asynchronously.
	 *
	 * The volume is added to `SpawnedActors` so the next `ClearGenerated`
	 * destroys it together with the meshes and gameplay actors. No-op
	 * when `bSpawnNavMeshBounds` is false or the world is missing.
	 */
	void SpawnNavMeshBounds(int32 Rows, int32 Cols, float CellCm);
};
