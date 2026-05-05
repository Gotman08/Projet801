// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/StaticMesh.h"
#include "TileMappingDataAsset.generated.h"

/**
 * @brief Mesh variants for one tile id.
 *
 * The dungeon generator picks one of these four meshes for each cell
 * based on the cell's role in the local neighbourhood:
 *   - `FloorMesh`, placed for any cell whose tile id is in the
 *     "walkable" group, regardless of neighbours.
 *   - `WallMesh`, placed at the edge between a walkable cell and a
 *     non-walkable neighbour, rotated so the wall faces inward.
 *   - `CornerMesh` (optional), substituted for a wall when two
 *     orthogonal walls meet on the same cell. If null, the wall mesh
 *     is reused.
 *   - `DoorMesh`, replaces the wall when the neighbour is itself a
 *     door tile (creating a passage between rooms).
 *
 * Setting `bIsSolid = true` marks this tile as a wall-like obstacle
 * for neighbour computations, it's what triggers wall placement on
 * adjacent walkable cells.
 */
USTRUCT(BlueprintType)
struct WFCDUNGEON_API FTileVariants
{
	GENERATED_BODY()

	/** Human-readable name (for editor inspection only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	FString TileName;

	/** Whether this tile is a solid obstacle (wall, rock, ...) or
	 *  walkable ground (floor, sand, grass). Drives wall placement on
	 *  the neighbouring walkable cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	bool bIsSolid = false;

	/** Whether this tile is a door (replaces the wall variant on the
	 *  walkable cell adjacent to it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	bool bIsDoor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	TObjectPtr<UStaticMesh> FloorMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	TObjectPtr<UStaticMesh> WallMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	TObjectPtr<UStaticMesh> CornerMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	TObjectPtr<UStaticMesh> DoorMesh = nullptr;

	/**
	 * Optional mesh variants. When non-empty, one entry is picked at
	 * random per spawn (uniform distribution) instead of using the
	 * single-mesh fields above. This breaks the visual repetition that
	 * appears on big dungeons (every wall identical, every floor tile
	 * identical, ...). The single-mesh field is used as a fallback when
	 * the corresponding array is empty, so existing DataAssets keep
	 * working untouched.
	 *
	 * Tip: include the original mesh in the array (or leave the array
	 * empty) if you want it to remain in the rotation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Variants")
	TArray<TObjectPtr<UStaticMesh>> FloorMeshVariants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Variants")
	TArray<TObjectPtr<UStaticMesh>> WallMeshVariants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Variants")
	TArray<TObjectPtr<UStaticMesh>> CornerMeshVariants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Variants")
	TArray<TObjectPtr<UStaticMesh>> DoorMeshVariants;

	/** Optional yaw correction applied to every spawned mesh of this
	 *  variant. Use it to fix exporter conventions (Blender Z-up vs
	 *  UE Z-up, +Y forward vs +X forward, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	FRotator OffsetRotation = FRotator::ZeroRotator;

	/** Optional XYZ scale per variant, in case some assets ship at
	 *  different units than the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	FVector ScaleOverride = FVector(1.f, 1.f, 1.f);
};

/**
 * @brief Maps tile ids (as produced by the WFC C++ generator) to
 *        the user's UE5 static meshes.
 *
 * Created via `Add → Miscellaneous → Data Asset → TileMappingDataAsset`
 * in the editor. The user fills `TileIdToVariants` with one entry per
 * tile id used by their sample dungeon, then references the asset
 * from an `ADungeonGenerator` actor.
 *
 * The plugin ships a default asset (`/WFCDungeon/Default_TileMapping`)
 * that uses the engine's primitive meshes (Cube, Plane) so the user
 * can verify the workflow before plugging in their own art.
 */
UCLASS(BlueprintType)
class WFCDUNGEON_API UTileMappingDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Per-tile-id mesh assignments. Keys are the integer tile ids
	 *  emitted by the WFC generator (0=floor, 1=wall, ... by default).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC")
	TMap<int32, FTileVariants> TileIdToVariants;

	/**
	 * @brief Look up a variant by tile id, returning nullptr if absent.
	 *
	 * Tolerant on purpose: a missing tile id only logs a warning so
	 * partial or experimental DataAssets remain usable while the user
	 * builds them up incrementally.
	 */
	const FTileVariants* Find(int32 TileId) const
	{
		return TileIdToVariants.Find(TileId);
	}
};
