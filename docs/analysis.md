## GPUs
- run on different GPUs, compare e.g. NVIDIA uint vs. float atomic
- use atomicImageAdd (NVIDIA)
- compare imageStore/atomicImageAdd (with low contention should be similar)
- test AMD vs. NVIDIA (amd i think has float atomic add only, uint, but maybe we dont even need atomic if tiles well separated)
- create comparison table