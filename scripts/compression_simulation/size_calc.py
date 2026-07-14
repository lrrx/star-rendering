from math import ceil, log

levels = [1,2,4,8,12,16,20,24,32, #64x64 voxel world, with outer bound at 32
          40,48,
          64]#skipped in iteration, only required for forward lookup
#thats why we can only assume from 16 onward

#double relativeScreenSpaceArea = avgMin / pixel_world_space_width(binIdx * 100 + extraDist);
#            double starMergeFactor = 1.0  * std::pow((1.0/relativeScreenSpaceArea), 3.0);

total_stars = 10**9 #1 billion stars
total_voxel_count = 64**3
stars_per_voxel = total_stars / total_voxel_count
compute_entire_LOD = False
print(stars_per_voxel)

####tip: calculate actual memory throughput (per frame) for reading stars and writing them back
##### does it exceed VRAM speed of given GPU?

total_megabytes_required = 0

for i in range(len(levels) - 1):
    l = levels[i]
    l_next = levels[i+1]

    voxel_width_in_pixels = ceil(1920 / (2 * l))
    voxel_count_at_level = (l_next)**3 - (l)**3
    if(compute_entire_LOD): voxel_count_at_level = total_voxel_count

    bits_required = 3 * ceil(log(voxel_width_in_pixels, 2));
    bits_required /= 1 #assumed with z ordering + delta encoding or huffman coding

    stars_at_level = int(voxel_count_at_level * stars_per_voxel)
    bytes_at_level = bits_required * stars_at_level / 8
    megabytes_at_level = ceil(bytes_at_level / (1024**2))
    total_megabytes_required += megabytes_at_level
    print(i,
          l,
          voxel_width_in_pixels,
          voxel_count_at_level,
          str(round(stars_at_level / 1000000))+"m",
          str(round(bits_required, 2))+"b",
          str(megabytes_at_level) + "MB",
    sep="\t")
print()
print(str(total_megabytes_required) + "MB")

#i know i can have about 300-400 million stars in 16 ms with 64x64 px tile-local caching in the classic voxel compute renderer
# but can we go faster if we write out in a compressed format (e.g. two stars per r32f) or pre-blend even before shader local memory, in just a local variable? <-> z-ordering <-> locality in screenspace

#so how would the limits be if we need less memory throughput for writing the stars to screen?
#how does fetch compare to write? -> measure

#TODO: take into account 1D, 2D and 3D distribution of stars
#TODO: estimate delta encoding bits based on star density with a function
