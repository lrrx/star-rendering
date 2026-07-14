## 27.04.2026
### Done
- start this journal
- clean up git repo a bit, restructure directories etc.
- rename git repo to stars

### TODO
- [x] create c++/compute shader benchmark for atomic write throughput limits

### Plan
- 14:00 - 15:00: output image compute buffer speed measurements
- short break
- 15:00 - 15:30: figure out math for coord downsampling merge likelyhood, implement voxel-pixel-width downsampling merge approach -> measurements tomorrow
- break
- 15:30 - 16:00 implement z-order
-16:00 - 16:30 clean up build system

### Open Problems
- how can we enhance memory write data compression (r32f writing), how can we get down precision here?
- how can i simulate the required precision for looking at the different voxel cells of stars in non-uniform distribution (e.g. 3D Gauss Disk), at which density do which effects get better or worse from which viewing angle (star merging, SM local tile blending, coordinate precision read overhead etc.)

### Ideas:
- Schuetz et. al.: Software Rasterization of 2 Billion Points in Real Time -> Voxelize / dynamic at roughly same points per voxel in screen space at all times (dynamic LOD up down using octree or other BVH), this would be nice for warp occupancy

### Claude:
**Done today:**
- Built a throughput test: one thread per pixel, single `imageAtomicAdd` to an 1920xTEX_SCALE by 1080xTEX_SCALE r32f texture, no contention (TEX_SCALE in 1,4,8,16, higher seemed slightly better, which goes against what claude said about fitting the texture in L2)
- Result: ~51 G atomic ops/sec (~412 GB/s effective) → near the RTX 4070's 504 GB/s VRAM ceiling (ca. 200mil atomic writes per 4ms)
- Conclusion: bandwidth-bound, not atomic-unit-bound

**Still to try:**
- Shrink texture to ≤16 MB (e.g. 2048×2048) so it fits in the 36 MB L2 — expect 2–4× on the no-contention case (<- not true from my test, test again to be safe)
- Add a worst-case contention shader (many threads hammering few pixels) to measure the serialization floor
- Test shared-memory atomic accumulation with a final flush per tile — expect 3–10× over current in a realistic rasterizer once binning costs are accounted for

### Conclusion / Learnings
- repo renamed and restructured
- atomic write testing deeper than imagined, gotta continue tomorrow
- also had to do some other orga stuff
- with the atomic writes with tile local accumulation first, it should technically be possible to draw a LOT of tile-local stars on the further away tiles
- i considered before that i should split up tiles near to the camera further to do better compression (and more precise culling as a nice side effect), but maybe they shouldnt get too small either to have good SM-tile utilization. but then i would have to draw more stars per grid box? so whats the tradeoff here...
-> checking in python shows that with static size voxels this would be too big.


```python
$ python size_calc.py
3814.697265625
0       1       960     7       0m      15.0b   1MB
1       2       480     56      0m      13.5b   1MB
2       4       240     448     2m      12.0b   3MB
3       8       120     1216    5m      10.5b   6MB
4       12      80      2368    9m      10.5b   12MB
5       16      60      3904    15m     9.0b    16MB
6       20      48      5824    22m     9.0b    24MB
7       24      40      18944   72m     9.0b    78MB
8       32      30      31232   119m    7.5b    107MB
9       40      24      46592   178m    7.5b    159MB
10      48      20      151552  578m    7.5b    517MB

>>> 151552 * 64 * 64 / 10**6
620.756992
```
- this means, we want roughly the same amount of voxels and premerged star points on the screen at all times. voxels should neither be too big nor too small to get both low coordinate memory requirements and also good SM-Tile locality

## 28.04.2026
### New Idea! Fully Tile-Based Renderer
- use chunks to easily bin groups of stars to 2d-screentiles (voxels are called chunks now)
- using 64x64 tiles, we'd get (for 1920x1080) 30x17 2d-screentiles (2d local tile buckets)
- we'd have to do a prepass to figure out which chunks belong to which 2d-screentile
- the remaining chunks/stars would have to be split up or sorted star-by-star into the 2d-screentiles. if they're not so many, just render them directly in an additional pass
- binning the original stars (without chunkung) would probably be way to expensive (see NVIDIA article on prefix sum, 10^7 list in a few ms on CUDA. thats way to slow for this case)

### TODO (DONE)
[x] finish atomic write measurements, simulate SM-Tile writing in simple 2d pixel writes to get realistic upper bound: Result: (1.07 billion writes in 2ms)
[x] figure out math for coord downsampling merge likelyhood
    - https://en.wikipedia.org/wiki/Balls_into_bins_problem?utm_source=copilot.com
    - `bin_distribution_problem.md`

[x] implement voxel-pixel-width downsampling merge approach (implemenet voxel-pixel-width coordinate precision downsampling), with fixed chunk pixel width for now
[x] use this to "merge" stars, rather than geometric area stuff (simpler, same effect)
[x] implement z-order and delta encoding for stars

### Conclusion
- implemented in c++ first parts of the new tile based rendering approach (mostly just coordinate encoding/packing etc.)
- looked into distribution/binning math
- finished measurements

### Knowledge
- NVIDIA RTX4070 has 46 Streaming Multiprocessor cores

## 29.04.2026
### TODO
- figure out layout for GPU structs / data structures for delta encoded, batched, chunked stars
- implement this


- adjust voxelization math (`size_calc.py`) for the conclusion / learnings from yesterday + math from`bin_distribution_problem.md`
- add z-order and delta encoding savings to (`size_calc.py`)
- subpixel precision for star position encoding to be able to do 4px-distributed antialiasing per star, or maybe some other solution for star encoding

[ ] **clean up build system:** create cleaner structure in build system to build different binaries as build targets using defines or smth (main exe, star caching, star export, exported gaia data visualization, atomic write throughput limit tester etc.)

- RAII Abstraction for Compute Shader or Vert-Frag Shader Programs
- think through dynamic voxel size (roughly unchanging voxel-screen-size) math/architecture, adjust (`size_calc.py`) when this is done.

## 30.04.2026
- run 2d static tiles test -> 133m points in 0.97 ms


## 04.05.2026
### Goals
- implement coalesced delta encoded stars <- not finished yewt

### Done
- half-done: coalesced delta-encoded stars

### Not Done
- do math on non-uniform star distribution (for LOD / star merging)
- start culling implementation

## 05.05.2026

## Done
- coalesced delta-encoded stars -> tested: perfectly uniform pattern on random points, perfect circles on (cos(i), sin(i))
- measurements on merge-culling stars and frustum culling -> camera angular visible portion of full view field at 90 deg with 16:9 is only like 9-10% !
- python scripts to calculate visible chunks (early frustum culling work)
- calculating how many chunks would fall on 2d-screentile borders or vs just fit right in the screentile
```python
phi = math.tan(math.radians(90 / 30))
>>> math.ceil(2/ phi) - math.ceil(1/phi)
19
>>> 19 * 1 + 19 * 4 + 19 * 16  + 19 * 64 + 19 * 256 + 14 * 1024
20815
```

```
$ python voxel_opt.py
             visible    inside  border
N =   32-> (0.098,      0.005,  0.995)  gpu=       9 us          = (      3224,         16,           3208)
N =   64-> (0.096,      0.073,  0.927)  gpu=      16 us          = (     25168,       1838,          23330)
N =  128-> (0.095,      0.347,  0.653)  gpu=      57 us          = (    198944,      69000,         129944)
N =  256-> (0.094,      0.615,  0.385)  gpu=     391 us          = (   1582144,     972990,         609154)
N =  512-> (0.094,      0.792,  0.208)  gpu=    3039 us          = (  12619904,    9990748,        2629156)
N = 1024-> (0.094,      0.892,  0.108)  gpu=   26047 us          = ( 100811008,   89894416,       10916592)
N = 2048-> (0.094,      0.945,  0.055)  gpu=  189298 us          = ( 805896704,  761409092,       44487612)
N = 4096-> (0.031,      0.916,  0.084)  gpu= 1342050 us          = (2149843968, 1970239260,      179604708)
```
-> at small voxel grid sizes, it truly would be a problem that too many voxels (relatively) just cross the tile borders
-> 2048 would be ideal, but this would need heavy optimization
-> IDEA IMPORTANT: maybe i need a data structure, where each leaf actually contains the same number of points? because i want load balancing! and maybe i could weigh it with expected screenspace size

- compression factor at different voxel screenspace sizes (measured from actual c++ code e.g. how many 0 deltas were counted). the number of stars per voxel was 512 * 127 ( = 65024). aligns perfectly with statistical formula prediction when comparing
```
px      factor (how many stars remain relatively)
20      0.123
24      0.211
30      0.378
40      0.628
```
-> binary dumped shader to identify performance issues (was not the actual issue), you can read NVIDIA assembly from `cat shader.bin` interesting
-> memory access pattern of uint vs uvec4 (4 vs. 16 bytes) at first considered to be bottleneck, but changing this showed actually slightly worse performance in nsight

-> frametimes on stars per per chunk (= threads * rows in batch)
```
                time    time
thr.  batches   frame   per star
512 * 15        0.1125  0.146484
512 * 64        0.2605  0.079498
512 * 127       0.4100  0.063054
512 * 255       0.7120  0.054534
```
- we see that as batches become longer, some kind of constant overhead decreases. (i would assume data pulling for new chunk, SM reinitialization, but this would need to be bisected in more detail if info required)

## 06.05.2026
- split up task into subtasks more
- basic frustum culling and 3d projection

### Done:
- wrote down all the calculations and research from yesterday
- mockup in blender for 2d-screentiles vs. 3d chunks intersection

## 07.05.2026

### Done:
- more tests with blender / tile-frustum vs. chunk intersection
- analyse density distributions from dr3 pdf graphs
- updated vis.cpp (`data/gaiadr3/visualization`) to calculate bounding boxes of different quantiles of most dense star region
```
$ ./OpenGLProject.exe ../../../compacted_counts_sorted.txt --max-distance 999999 --top-n 2000000000 --rebuild --agregate-grid 128
Parsing text file...
Processed 20054394 lines. Kept 20054393 voxels; dropped 0.
Saved cache to ../../../compacted_counts_sorted.txt.bin.
--- HUD Histograms Data Reference ---
Histogram 1 (Left  - colors)   100% line = 19004004 points
Histogram 2 (Right - distance) 100% line = 3.40302e+08 sum voxel count
-------------------------------------
Visualizing 20054393 points.

=== Oriented bounding boxes (PCA-aligned) ===
frac   voxels     stars in box  dimensions (kpc-units)      volume        center
20%    1          340302336     0.00 x 0.00 x 0.00          0.00          (0.0, 0.0, 0.0)
30%    6          412875092     2.20 x 1.50 x 0.00          0.00          (-1.0, 0.0, -0.5)
40%    36         539155486     7.24 x 6.26 x 1.88          85.00         (-1.3, -0.4, -1.6)
50%    129        673423211     11.45 x 10.91 x 2.67        333.48        (-1.4, -0.6, -2.1)
60%    369        807995975     15.70 x 15.81 x 3.89        964.12        (-2.1, -0.5, -2.0)
70%    1026       942576616     23.54 x 21.97 x 7.67        3966.88       (-3.1, -0.1, -2.4)
80%    3320       1077198664    56775.47 x 30.66 x 16.82    29270978.00   (16377.2, 16380.0, 16377.8)
90%    19411      1211815720    56792.79 x 58.18 x 38.77    128105208.00  (16370.4, 16379.6, 16370.1)
==============================================
```

- looked at GAIA Hertzsprung-Russel Diagramm (how can we encode packed lumi/brightness)
- began implementing actual star renderer based on current state of tilebased renderer
- simplified star loading/caching code (stateless now)

## 08.05.2026
- finish bare bones gpu-driven-rendering implementation, test in 2d
- how to parallelize draw list generation well?
- rethink ChunkMeta / TileDrawList / Row-Batches to make it efficient?
- if done, move on to 3d projection <-> this requires screentile-frustum vs. chunk intersection math for draw list generation
- IDEA: maybe just send ray in direction, walk chunk-size stepwise, check surrounding chunks (simple + cheap implementation)
- BUT: how to make sure that one chunk is assigned only to one tile always (geometric determinism)

## 11.05.2026
- Implemented Lumi-Temp Output + Compositing Pass using interleaved Texture (2x Screen size, with alternating Lumi/Temp r32f texels)
- lumi-temp support in shared memory tile
- debug overlay, new glsl font
- 4x AA weighted pixel splatting
- show per-screentile workload in number of stars

## 12.05.2026
- worst-case measurement of how many stars per screentile we can do in 1ms -> result ca. 60k - 70k -> 33 million stars in 1ms for current impl
- remove multi-binning per tile, we need a flat, evenly distributable list
- draw list clear pass
- make drawlist generation per-chunk instead of per-screentile
- split split per-tile draw list into 8 layers, launch z-dimension workgroup for each
- gpu-side profiling counters (stars, chunks, tiles drawn per frame)

### TODO:
- draw list generation: proper AABB intersection test instead of just chunk center projection

## 14.05.2026
- load gaia dataset
- switch off delta encoding for now (for implementation simplicity)
- disable z-workgroup layering (didnt really improve performance, since we still get the same number of starss)

## 18.05.2026
- setup linux env

## 19.05.2026
- dynamic job list generation, well compacted on gpu (workload distribution)
- render big gaia subset (~65m) for first time without substantial visual errors
- maybe static dispatch stream compaction / prefix sum would beat dynamic draw list generation of now, but it gives great performance boost already if heavy tiles can be split up well

### TODO: -split up by row count, not by chunk count (this should give big gains)

## 22.05.2026
- began cosmoscout integration
- still need to figure out solution for sharing code between cosmoscout and this codebase (maybe git submodule or shared library) -> git would be nice for merging to release branch or smth
- loadbalancing optimizations -> balance by rows, second pass / (and perhaps even datastructure) for really sparse spatial regions, and later third for really heavy chunks
- we want roughly constant number of launched workgroups independent of view angle

## 26.05.2026
- integrate changes made during cosmoscout experimentation
- library vs. host architecture split (cmake-based c++ library for star rendering)
- move back to chunk-center based culling (since aabb-frustum was glitchy, flickery near grid planes and near origin)
    - with some extra work, we might be able to get proper culling from this alone (add extra bins around screen + full-resolution bins for near chunks)
    - we need to generate 2-3 draw lists anyways (default, near, far/sparse stars)
- discovered performance spike when looking down from top, if merely rotating by 90 degrees (horizontal vs. vertical rotation):
    - the performance tanks along one axis. very likely related to pixel contention in writePixel from how stars are laid out on screen
    - experiment with morton code to counteract this
    - more load balancing needed

- but first: fully bridge library to cosmoscout so that running version will always also run in cosmoscout with no extra work required
(for this, we have to fix matrix/camera position precision)
- fix: allow different chunk sizes (we dont even need chunking for about 2mil low density area stars), meaning that we can make chunks much smaller.
- then: split near vs. far chunks -> stream near chunks in full-precision, keep lod chunks on gpu

## 27.05.2026
- finished library + host split ("full bridge" from above)
- integrated with cosmoscout + cosmoscout ui
- started working on drawlist chunk overlay in debug ui, need to do splitting tomorrow
- reenable keyboard input in StarRenderer + newstar.hpp interface
- fix matrix precision problems by using vistas gauss-jordan matrix inversion and just passing it though
- in my custom host rendering code, i simply glm::invert on double precision mat4, works as well

### Approach for dealing with half-full rows + very sparse regions
- have naive rendering for low density stars
- cap stars per chunk at % BATCH_WIDTH == 0(can only be n*BATCH_WIDTH) -> the rest of stars per batch get put into low density chunks, a second pass is done for those
- add culling for the larger chunks too
- technically, i could do everything in one drawlist + raster pass, by just encoding the chunk size into the so far unused 8 highest bits of chunkID
- or we do 2-3 separate passes (may be better for static workload distribution, since we can somewhat anticipate how heavy jobs will)

## 28.05.2026
- move away from tiled accumulation rendering for now (gave actually worse performance in current state).
- simplify jobs for better workloading, this means we cant have tiling anymore but we should get much better load balacing
    - 1 job = range of rows + chunk anchor
### TODO
- check if we might want to switch to r64i lumi*temp,temp accumulation (SSBO <-> tiling, broader GPU support, ImgBuf <-> no tiling, tighter GPU support, but also free texture locality (z-order))
- re-enabled aabb-frustum intersection test for drawlist generation, looks clean in host so far, have to test in cosmoscout
- discovered issue with clipping star magnitudes on star-merge + wrong math for non-linear brightness addition/temp-brightness weighing
```c
#define USE_OLD_STAR_MERGING
minMag:0
maxMag:1.89624e+07

//out of range values fixed here
//#define USE_OLD_STAR_MERGING
minMag:-1.4533
maxMag:15
```
- this issue is exemplified when using tighter quantisation, as more stars get merged (happened to be the case in 256-step morton vs. 1024 2-10-10-10 local coordinate packing)

## 29.05.2026
- higher precision morton encoding
- glm::dmat for matrix inversion (prevent conditioning issues)
- shader and c++ code cleanup (remove commented out code etc.)
- reach near-perfect alignment with existing cosmoscout renderer (only near stars are off)

## 01.06.2026
- refactoring
- restructure/cleanup code in StarRenderer, separation of concerns
- prepare architecture for adding different types of LOD chunks (regular, close/high-detail, sparse)
- texture wrapper class
- extract debug overlay generation code

## 02.06.2026
- implement different chunk classes for workload balancing + hotkeys
- add overlapping chunks
- add new debugoverlay layer that visualizes some data about generated draw list of frame ('B' hotkey)

## 03.06.2026
- lanes for tile flush workload balancing

## 04.06.2026
- improve performance counters + metrics (memory vs. time vs. stars)
- experiment with different chunk sizes (current seems to be pretty good, also changing chunk size now just shifts which chunk type stars fall into)
- improve LDS tile memory access pattern
- TODO: read on memory bank conflicts: https://rocm.docs.amd.com/projects/composable_kernel/en/develop/conceptual/ck_tile/hardware/lds_bank_conflicts.html
- bump gpu thread count to 512, huge 30% perf. boost compared to 256 (ca. 145us per million stars now on camera view '8')
- higher thread counts give diminishing returns and might (i think) have less hardware support
### DONE: TODO: there must be some race condition in tip pointer draw list generation parallel algorithm (Flickering issue)
- because using the 'B' hotkey overlay i found that tiles that have more than MAX_TILE_CHUNKS (e.g. more than one job) are the ones "flickering"
- i figured this out by going to the edge of the dense chunks (other chunks turned off) and slowly moving camera sideways towards center

## TODO: figure out why many stars in center-most chunk that are very close to origin or on origin (error,singularity?) that cause lag
- for now i just filter them out, but idk if something is off with my data preprocessing perhaps

## TODO: new high-res (f32 vec3 star coords) chunks near-center, or if going for full-on approach near-camera
## TODO: verify wether flickering in CosmoScout-VR was only from parallelization issue or actually also a numeric error issue perhaps with frustum binning math
## TODO: re-implement star merging with new chunking system, at least for dense regions, but preferrably everywhere
## TODO: bug with pixels of bottom most pixel row landing at top screen edge (maybe from lane splitting idk?)


## 05.06.2026
- fix racy behaviour causing flickering issue
- further improvements on debug overlay
- refactoring

## 08.06.2026

- architectural changes for new QuantizedChunk aux LOD streaming

## 09.06.2026

- screenspace geometry logic for detecting when switching to high precision LOD
- finish LOD impl

## 10.06.2026

- ImageDiffs and ghost/varying stars hunting

## 11.06.2026

- found issue in threshold logic in composition pass (srBlit vs. quad.frag), where in original srBlit we skip on both 0, even on temp = 0.0, whereas i was only skipping on lumi = 0.0
- start working on presentation


# TODO
- load cosmoscout-given stars instead of my own, but apply galactic rotation during gpu upload for better chunk alignment
- unrotate in shader
- simulate 3d gauss disk distribution (to approximate gaia)
- filter out low-star-count chunks to separate pass
- filter out far away stars to separate pass
- push test to limits, work on compressing delta encoding down (no huffmann/probabilistic coding yet)
- figure out batch size / work distribution inside SM (when do i get warp stalls, how to split up voxels in star best)
- test non-uniform distribution, look at claude code chat for hot-loop SM utilization shader code, where we just pull available work packets in while loop
- on-GPU LOD generation (raw star upload, then generate LOD Data on-GPU and leave it there)

-smaller tiles, smaller chunks
-same-position merging
-multiple z-layers in local tile / framebuffer, merge-accumulate
-increase star density
-buildGaiaCache in history
-fix missing gaia tiles
-shader recompile on enable/disable debug overlays
-support for defines (non-values just #DEFINE_ENABLE_XYZ) + galactic plane rotation
-near full-rotation tiles
-TODOs in code itself
-allow vieport resizing

## VISUAL VALIDATION TESTS
- edge case where "ghost pixel" might lie exactly on screentile border -> do pixel values get distributed and written correctly
- image diff in cosmoscout
- performance in CosmoScout