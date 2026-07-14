#include <vector>
#include <queue>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

struct BBox {
    glm::highp_f64vec3 min;
    glm::highp_f64vec3 max;

    bool contains(glm::highp_f64vec3 p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }

    double distSq(glm::highp_f64vec3 p) const {
        double dx = std::max({0.0, min.x - p.x, p.x - max.x});
        double dy = std::max({0.0, min.y - p.y, p.y - max.y});
        double dz = std::max({0.0, min.z - p.z, p.z - max.z});
        return dx * dx + dy * dy + dz * dz;
    }
};

struct OctreeNode {
    BBox bounds;
    OctreeNode* children[8] = {nullptr};
    std::vector<size_t> starIndices;
    bool isLeaf = true;

    OctreeNode(BBox b) : bounds(b) {}
    ~OctreeNode() {
        for (auto child : children) delete child;
    }
};

class Octree {
public:
    OctreeNode* root;
    
    Octree(const std::vector<Star>& stars) {
        // Calculate global bounds
        BBox globalBounds{{1e18, 1e18, 1e18}, {-1e18, -1e18, -1e18}};
        for (const auto& s : stars) {
            globalBounds.min = glm::min(globalBounds.min, s.mPosition);
            globalBounds.max = glm::max(globalBounds.max, s.mPosition);
        }
        // Expand bounds slightly to avoid precision edge cases
        globalBounds.min -= 1.0; globalBounds.max += 1.0;

        root = new OctreeNode(globalBounds);
        for (size_t i = 0; i < stars.size(); ++i) {
            insert(root, i, stars);
        }
    }

    ~Octree() { delete root; }

private:
    void insert(OctreeNode* node, size_t idx, const std::vector<Star>& stars) {
        if (node->isLeaf) {
            if (node->starIndices.size() < 32) { // Leaf capacity
                node->starIndices.push_back(idx);
                return;
            }
            // Split leaf into children
            node->isLeaf = false;
            BBox b = node->bounds;
            glm::highp_f64vec3 mid = (b.min + b.max) * 0.5;

            for (int i = 0; i < 8; ++i) {
                BBox childBox;
                childBox.min.x = (i & 1) ? mid.x : b.min.x;
                childBox.max.x = (i & 1) ? b.max.x : mid.x;
                childBox.min.y = (i & 2) ? mid.y : b.min.y;
                childBox.max.y = (i & 2) ? b.max.y : mid.y;
                childBox.min.z = (i & 4) ? mid.z : b.min.z;
                childBox.max.z = (i & 4) ? b.max.z : mid.z;
                node->children[i] = new OctreeNode(childBox);
            }

            for (size_t existingIdx : node->starIndices) {
                insertIntoChildren(node, existingIdx, stars);
            }
            node->starIndices.clear();
        }
        insertIntoChildren(node, idx, stars);
    }

    void insertIntoChildren(OctreeNode* node, size_t idx, const std::vector<Star>& stars) {
        for (int i = 0; i < 8; ++i) {
            if (node->children[i]->bounds.contains(stars[idx].mPosition)) {
                insert(node->children[i], idx, stars);
                return;
            }
        }
    }
};