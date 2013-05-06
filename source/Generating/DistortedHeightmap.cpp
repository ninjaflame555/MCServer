
// DistortedHeightmap.cpp

// Implements the cDistortedHeightmap class representing the height and composition generator capable of overhangs

#include "Globals.h"

#include "DistortedHeightmap.h"
#include "../OSSupport/File.h"
#include "../../iniFile/iniFile.h"





const cDistortedHeightmap::sGenParam cDistortedHeightmap::m_GenParam[biNumBiomes] =
{
	/* Biome              |    AmpX | AmpZ */
	/* biOcean            */ { 1.5f,  1.5f},
	/* biPlains           */ { 0.5f,  0.5f},
	/* biDesert           */ { 0.5f,  0.5f},
	/* biExtremeHills     */ {16.0f, 16.0f},
	/* biForest           */ { 3.0f,  3.0f},
	/* biTaiga            */ { 1.5f,  1.5f},
	
	/* biSwampland        */ { 0.0f,  0.0f},
	/* biRiver            */ { 0.0f,  0.0f},
	/* biNether           */ { 0.0f,  0.0f},  // Unused, but must be here due to indexing
	/* biSky              */ { 0.0f,  0.0f},  // Unused, but must be here due to indexing
	/* biFrozenOcean      */ { 0.0f,  0.0f},
	/* biFrozenRiver      */ { 0.0f,  0.0f},
	/* biIcePlains        */ { 0.0f,  0.0f},
	/* biIceMountains     */ { 8.0f,  8.0f},
	/* biMushroomIsland   */ { 4.0f,  4.0f},
	/* biMushroomShore    */ { 0.0f,  0.0f},
	/* biBeach            */ { 0.0f,  0.0f},
	/* biDesertHills      */ { 5.0f,  5.0f},
	/* biForestHills      */ { 6.0f,  6.0f},
	/* biTaigaHills       */ { 8.0f,  8.0f},
	/* biExtremeHillsEdge */ { 7.0f,  7.0f},
	/* biJungle           */ { 0.0f,  0.0f},
	/* biJungleHills      */ { 8.0f,  8.0f},
} ;





cDistortedHeightmap::cDistortedHeightmap(int a_Seed, cBiomeGen & a_BiomeGen) :
	m_Noise1(a_Seed + 1000),
	m_Noise2(a_Seed + 2000),
	m_Noise3(a_Seed + 3000),
	m_Noise4(a_Seed + 4000),
	m_Noise5(a_Seed + 5000),
	m_NoiseArrayX(m_NoiseArray),
	m_NoiseArrayZ(m_NoiseArray + 17 * 17 * 32),
	m_BiomeGen(a_BiomeGen),
	m_HeightGen(new cHeiGenBiomal(a_Seed, a_BiomeGen), 64)
{
}





void cDistortedHeightmap::Initialize(cIniFile & a_IniFile)
{
	// Read the params from the INI file:
	m_SeaLevel   =                 a_IniFile.GetValueSetI("Generator", "DistortedHeightmapSeaLevel",   62);
	m_FrequencyX = (NOISE_DATATYPE)a_IniFile.GetValueSetF("Generator", "DistortedHeightmapFrequencyX", 10);
	m_FrequencyY = (NOISE_DATATYPE)a_IniFile.GetValueSetF("Generator", "DistortedHeightmapFrequencyY", 10);
	m_FrequencyZ = (NOISE_DATATYPE)a_IniFile.GetValueSetF("Generator", "DistortedHeightmapFrequencyZ", 10);
}





void cDistortedHeightmap::PrepareState(int a_ChunkX, int a_ChunkZ)
{
	if ((m_CurChunkX == a_ChunkX) && (m_CurChunkZ == a_ChunkZ))
	{
		return;
	}
	m_CurChunkX = a_ChunkX;
	m_CurChunkZ = a_ChunkZ;
	
	m_HeightGen.GenHeightMap(a_ChunkX, a_ChunkZ, m_CurChunkHeights);
	GenerateNoiseArray(m_NoiseArrayX, m_Noise1, m_Noise2, m_Noise3);
	UpdateDistortAmps();
}





void cDistortedHeightmap::GenerateNoiseArray(NOISE_DATATYPE * a_NoiseArray, cNoise & a_Noise1, cNoise & a_Noise2, cNoise & a_Noise3)
{
	// Parameters:
	const int INTERPOL_X = 8;
	const int INTERPOL_Y = 4;
	const int INTERPOL_Z = 8;
	
	ASSERT((NOISE_SIZE_Y % INTERPOL_Y) == 1);  // Interpolation needs to set the 0-th and the last element

	int idx = 0;
	for (int y = 0; y < NOISE_SIZE_Y; y += INTERPOL_Y)
	{
		NOISE_DATATYPE NoiseY = ((NOISE_DATATYPE)y) / m_FrequencyY;
		NOISE_DATATYPE * CurFloor = &(a_NoiseArray[y * 17 * 17]);
		for (int z = 0; z < 17; z += INTERPOL_Z)
		{
			NOISE_DATATYPE NoiseZ = ((NOISE_DATATYPE)(m_CurChunkZ * cChunkDef::Width + z)) / m_FrequencyZ;
			for (int x = 0; x < 17; x += INTERPOL_X)
			{
				NOISE_DATATYPE NoiseX = ((NOISE_DATATYPE)(m_CurChunkX * cChunkDef::Width + x)) / m_FrequencyX;
				CurFloor[x + 17 * z] = 
					a_Noise1.CubicNoise3D(NoiseX, NoiseY, NoiseZ) * (NOISE_DATATYPE)0.5 +
					a_Noise2.CubicNoise3D(NoiseX / 2, NoiseY / 2, NoiseZ / 2) +
					a_Noise3.CubicNoise3D(NoiseX / 4, NoiseY / 4, NoiseZ / 4) * 2;
			}
		}
		// Linear-interpolate this XZ floor:
		ArrayLinearInterpolate2D(CurFloor, 17, 17, INTERPOL_X, INTERPOL_Z);
	}  // for y
	
	// Finish the 3D linear interpolation by interpolating between each XZ-floors on the Y axis
	for (int y = 1; y < NOISE_SIZE_Y - 1; y++)
	{
		if ((y % INTERPOL_Y) == 0)
		{
			// This is the interpolation source floor, already calculated
			continue;
		}
		int LoFloorY = (y / INTERPOL_Y) * INTERPOL_Y;
		int HiFloorY = LoFloorY + INTERPOL_Y;
		NOISE_DATATYPE * LoFloor  = &(a_NoiseArray[LoFloorY * 17 * 17]);
		NOISE_DATATYPE * HiFloor  = &(a_NoiseArray[HiFloorY * 17 * 17]);
		NOISE_DATATYPE * CurFloor = &(a_NoiseArray[y * 17 * 17]);
		NOISE_DATATYPE Ratio = ((NOISE_DATATYPE)(y % INTERPOL_Y)) / INTERPOL_Y;
		int idx = 0;
		for (int z = 0; z < cChunkDef::Width; z++)
		{
			for (int x = 0; x < cChunkDef::Width; x++)
			{
				CurFloor[idx] = LoFloor[idx] + (HiFloor[idx] - LoFloor[idx]) * Ratio;
				idx += 1;
			}  // for x
			idx += 1;  // Skipping one X column
		}  // for z
	}  // for y
}





void cDistortedHeightmap::GenHeightMap(int a_ChunkX, int a_ChunkZ, cChunkDef::HeightMap & a_HeightMap)
{
	PrepareState(a_ChunkX, a_ChunkZ);
	for (int z = 0; z < cChunkDef::Width; z++)
	{
		for (int x = 0; x < cChunkDef::Width; x++)
		{
			int NoiseArrayIdx = x + 17 * z;
			cChunkDef::SetHeight(a_HeightMap, x, z, m_SeaLevel - 1);
			for (int y = cChunkDef::Height - 1; y > m_SeaLevel - 1; y--)
			{
				int HeightMapHeight = GetValue(NoiseArrayIdx + 17 * 17 * y, x, z);
				if (y < HeightMapHeight)
				{
					cChunkDef::SetHeight(a_HeightMap, x, z, y);
					break;
				}
			}  // for y
		}  // for x
	}  // for z
}





void cDistortedHeightmap::ComposeTerrain(cChunkDesc & a_ChunkDesc)
{
	PrepareState(a_ChunkDesc.GetChunkX(), a_ChunkDesc.GetChunkZ());
	a_ChunkDesc.FillBlocks(E_BLOCK_AIR, 0);
	for (int z = 0; z < cChunkDef::Width; z++)
	{
		for (int x = 0; x < cChunkDef::Width; x++)
		{
			int NoiseArrayIdx = x + 17 * z;
			int LastAir = a_ChunkDesc.GetHeight(x, z) + 1;
			bool HasHadWater = false;
			for (int y = LastAir; y > 0; y--)
			{
				int HeightMapHeight = GetValue(NoiseArrayIdx + 17 * 17 * y, x, z);

				if (y >= HeightMapHeight)
				{
					// "air" part
					LastAir = y;
					if (y < m_SeaLevel)
					{
						a_ChunkDesc.SetBlockType(x, y, z, E_BLOCK_STATIONARY_WATER);
						HasHadWater = true;
					}
					continue;
				}
				// "ground" part:
				if (LastAir - y > 4)
				{
					a_ChunkDesc.SetBlockType(x, y, z, E_BLOCK_STONE);
					continue;
				}
				if (HasHadWater)
				{
					a_ChunkDesc.SetBlockType(x, y, z, E_BLOCK_SAND);
				}
				else
				{
					a_ChunkDesc.SetBlockType(x, y, z, (LastAir == y + 1) ? E_BLOCK_GRASS : E_BLOCK_DIRT);
				}
			}  // for y
			a_ChunkDesc.SetBlockType(x, 0, z, E_BLOCK_BEDROCK);
		}  // for x
	}  // for z
}





int cDistortedHeightmap::GetHeightmapAt(NOISE_DATATYPE a_X, NOISE_DATATYPE a_Z)
{
	int ChunkX = (int)floor(a_X / (NOISE_DATATYPE)16);
	int ChunkZ = (int)floor(a_Z / (NOISE_DATATYPE)16);
	int RelX = (int)(a_X - (NOISE_DATATYPE)ChunkX * cChunkDef::Width);
	int RelZ = (int)(a_Z - (NOISE_DATATYPE)ChunkZ * cChunkDef::Width);

	// If we're withing the same chunk, return the pre-cached heightmap:
	if ((ChunkX == m_CurChunkX) && (ChunkZ == m_CurChunkZ))
	{
		return cChunkDef::GetHeight(m_CurChunkHeights, RelX, RelZ);
	}
	cChunkDef::HeightMap Heightmap;
	m_HeightGen.GenHeightMap(ChunkX, ChunkZ, Heightmap);
	return cChunkDef::GetHeight(Heightmap, RelX, RelZ);
}





int cDistortedHeightmap::GetValue(int a_NoiseArrayIdx, int a_RelX, int a_RelZ)
{
	int AmpIdx = a_RelX + 17 * a_RelZ;
	NOISE_DATATYPE DistX = m_NoiseArrayX[a_NoiseArrayIdx] * m_DistortAmpX[AmpIdx];
	NOISE_DATATYPE DistZ = m_NoiseArrayZ[a_NoiseArrayIdx] * m_DistortAmpZ[AmpIdx];
	DistX += (NOISE_DATATYPE)(m_CurChunkX * cChunkDef::Width + a_RelX);
	DistZ += (NOISE_DATATYPE)(m_CurChunkZ * cChunkDef::Width + a_RelZ);
	return GetHeightmapAt(DistX, DistZ);
}





void cDistortedHeightmap::UpdateDistortAmps(void)
{
	BiomeNeighbors Biomes;
	for (int z = -1; z <= 1; z++)
	{
		for (int x = -1; x <= 1; x++)
		{
			m_BiomeGen.GenBiomes(m_CurChunkX + x, m_CurChunkZ + z, Biomes[x + 1][z + 1]);
		}  // for x
	}  // for z

	// Linearly interpolate 4x4 blocks of Amps:
	const int STEPZ = 4;  // Must be a divisor of 16
	const int STEPX = 4;  // Must be a divisor of 16
	for (int z = 0; z < 17; z += STEPZ)
	{
		for (int x = 0; x < 17; x += STEPX)
		{
			GetDistortAmpsAt(Biomes, x, z, m_DistortAmpX[x + 17 * z], m_DistortAmpZ[x + 17 * z]);
		}
	}
	ArrayLinearInterpolate2D(m_DistortAmpX, 17, 17, STEPX, STEPZ);
	ArrayLinearInterpolate2D(m_DistortAmpZ, 17, 17, STEPX, STEPZ);
}	





void cDistortedHeightmap::GetDistortAmpsAt(BiomeNeighbors & a_Neighbors, int a_RelX, int a_RelZ, NOISE_DATATYPE & a_DistortAmpX, NOISE_DATATYPE & a_DistortAmpZ)
{
	// Sum up how many biomes of each type there are in the neighborhood:
	int BiomeCounts[biNumBiomes];
	memset(BiomeCounts, 0, sizeof(BiomeCounts));
	int Sum = 0;
	for (int z = -8; z <= 8; z++)
	{
		int FinalZ = a_RelZ + z + cChunkDef::Width;
		int IdxZ = FinalZ / cChunkDef::Width;
		int ModZ = FinalZ % cChunkDef::Width;
		int WeightZ = 9 - abs(z);
		for (int x = -8; x <= 8; x++)
		{
			int FinalX = a_RelX + x + cChunkDef::Width;
			int IdxX = FinalX / cChunkDef::Width;
			int ModX = FinalX % cChunkDef::Width;
			EMCSBiome Biome = cChunkDef::GetBiome(a_Neighbors[IdxX][IdxZ], ModX, ModZ);
			if ((Biome < 0) || (Biome >= ARRAYCOUNT(BiomeCounts)))
			{
				continue;
			}
			int WeightX = 9 - abs(x);
			BiomeCounts[Biome] += WeightX + WeightZ;
			Sum += WeightX + WeightZ;
		}  // for x
	}  // for z

	if (Sum <= 0)
	{
		// No known biome around? Weird. Return a bogus value:
		ASSERT(!"cHeiGenBiomal: Biome sum failed, no known biome around");
		a_DistortAmpX = 16;
		a_DistortAmpZ = 16;
	}

	// For each biome type that has a nonzero count, calc its amps and add it:
	NOISE_DATATYPE AmpX = 0;
	NOISE_DATATYPE AmpZ = 0;
	for (int i = 0; i < ARRAYCOUNT(BiomeCounts); i++)
	{
		AmpX += BiomeCounts[i] * m_GenParam[i].m_DistortAmpX;
		AmpZ += BiomeCounts[i] * m_GenParam[i].m_DistortAmpZ;
	}
	a_DistortAmpX = AmpX / Sum;
	a_DistortAmpZ = AmpZ / Sum;
}



