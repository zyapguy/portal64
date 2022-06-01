#include "epa.h"

#include "../util/assert.h"
#include "../util/memory.h"
#include "../math/plane.h"
#include "../math/mathf.h"

#define MAX_ITERATIONS  10

#define MAX_SIMPLEX_POINTS      (4 + MAX_ITERATIONS)
#define MAX_SIMPLEX_TRIANGLES   (4 + MAX_ITERATIONS * 2)

#define NEXT_FACE(index)        ((index) == 2 ? 0 : (index) + 1)

union SimplexTriangleIndexData {
    struct {
        unsigned char indices[3];
        unsigned char adjacentFaces[3];
        // the index of the point oppositve to the 
        // in the cooresponding adjacent face
        unsigned char oppositePoints[3];
    };
    int alignment;
};

struct SimplexTriangle {
    union SimplexTriangleIndexData indexData;
    float distanceToOrigin;
    struct Vector3 normal;
};

struct ExpandingSimplex {
    struct Vector3 points[MAX_SIMPLEX_POINTS];
    struct Vector3 aPoints[MAX_SIMPLEX_POINTS];
    unsigned pointCount;
    struct SimplexTriangle triangles[MAX_SIMPLEX_TRIANGLES];
    unsigned triangleCount;
    unsigned char triangleHeap[MAX_SIMPLEX_TRIANGLES];
};


#define GET_PARENT_INDEX(heapIndex) (((heapIndex) - 1) >> 1)
#define GET_CHILD_INDEX(heapIndex, childHeapIndex)  (((heapIndex) << 1) + 1 + (childHeapIndex))
#define EXPANDING_SIMPLEX_GET_DISTANCE(simplex, triangleIndex)  ((simplex)->triangles[triangleIndex].distanceToOrigin)

int validateHeap(struct ExpandingSimplex* simplex) {
    for (int i = 1; i < simplex->triangleCount; ++i) {
        int parentIndex = GET_PARENT_INDEX(i);

        if (simplex->triangles[simplex->triangleHeap[i]].distanceToOrigin < simplex->triangles[simplex->triangleHeap[parentIndex]].distanceToOrigin) {
            return 0;
        }
    }

    return 1;
}

void expandingSimplexAddPoint(struct ExpandingSimplex* simplex, struct Vector3* aPoint, struct Vector3* pointDiff) {
    int result = simplex->pointCount;
    simplex->aPoints[result] = *aPoint;
    simplex->points[result] = *pointDiff;
    ++simplex->pointCount; 
}

int expandingSimplexSiftDownHeap(struct ExpandingSimplex* simplex, int heapIndex) {
    int parentHeapIndex = GET_PARENT_INDEX(heapIndex);
    float currentDistance = EXPANDING_SIMPLEX_GET_DISTANCE(simplex, simplex->triangleHeap[heapIndex]);

    while (heapIndex > 0) {
        // already heaped
        if (currentDistance >= EXPANDING_SIMPLEX_GET_DISTANCE(simplex, simplex->triangleHeap[parentHeapIndex])) {
            break;
        }

        // swap the parent with the current node
        int tmp = simplex->triangleHeap[heapIndex];
        simplex->triangleHeap[heapIndex] = simplex->triangleHeap[parentHeapIndex];
        simplex->triangleHeap[parentHeapIndex] = tmp;

        // move up to the parent
        heapIndex = parentHeapIndex;
        parentHeapIndex = GET_PARENT_INDEX(heapIndex);
    }

    return heapIndex;
}

int expandingSimplexSiftUpHeap(struct ExpandingSimplex* simplex, int heapIndex) {
    float currentDistance = EXPANDING_SIMPLEX_GET_DISTANCE(simplex, simplex->triangleHeap[heapIndex]);

    while (heapIndex < simplex->triangleCount) {
        int swapWithChild = -1;

        int childHeapIndex = GET_CHILD_INDEX(heapIndex, 0);

        // reached the end of the heap
        if (childHeapIndex >= simplex->triangleCount) {
            break;
        }

        float childDistance = EXPANDING_SIMPLEX_GET_DISTANCE(simplex, simplex->triangleHeap[childHeapIndex]);

        // check that we don't run off the end of the heap
        if (childDistance < currentDistance) {
            swapWithChild = childHeapIndex;
        }

        float otherChildDistance = EXPANDING_SIMPLEX_GET_DISTANCE(simplex, simplex->triangleHeap[childHeapIndex + 1]);

        // grab the smallest child
        if (childHeapIndex + 1 < simplex->triangleCount && otherChildDistance < currentDistance && otherChildDistance < childDistance) {
            swapWithChild = childHeapIndex + 1;
        }

        if (swapWithChild == -1) {
            // no child out of order
            break;
        }

        // swap child with the current node
        int tmp = simplex->triangleHeap[heapIndex];
        simplex->triangleHeap[heapIndex] = simplex->triangleHeap[swapWithChild];
        simplex->triangleHeap[swapWithChild] = tmp;

        heapIndex = swapWithChild;
    }

    return heapIndex;
}

void expandingSimplexFixHeap(struct ExpandingSimplex* simplex, int heapIndex) {
    int nextHeapIndex = expandingSimplexSiftUpHeap(simplex, heapIndex);

    if (nextHeapIndex != heapIndex) {
        return;
    }

    expandingSimplexSiftDownHeap(simplex, nextHeapIndex);
}

int expandingSimplexFindHeapIndex(struct ExpandingSimplex* simplex, int value) {
    for (int i = 0; i < simplex->triangleCount; ++i) {
        if (simplex->triangleHeap[i] == value) {
            return i;
        }
    }

    return 0;
}

void expandingSimplexTriangleInitNormal(struct ExpandingSimplex* simplex, struct SimplexTriangle* triangle) {
    struct Vector3 edgeB;
    vector3Sub(&simplex->points[triangle->indexData.indices[1]], &simplex->points[triangle->indexData.indices[0]], &edgeB);
    struct Vector3 edgeC;
    vector3Sub(&simplex->points[triangle->indexData.indices[2]], &simplex->points[triangle->indexData.indices[0]], &edgeC);

    vector3Cross(&edgeB, &edgeC, &triangle->normal);
}

int expandingSimplexTriangleCheckEdge(struct ExpandingSimplex* simplex, struct SimplexTriangle* triangle, int index) {
    struct Vector3* pointA = &simplex->points[triangle->indexData.indices[index]];

    struct Vector3 edge;
    vector3Sub(&simplex->points[triangle->indexData.indices[NEXT_FACE(index)]], pointA, &edge);
    struct Vector3 toOrigin;
    vector3Negate(pointA, &toOrigin);

    struct Vector3 crossCheck;
    vector3Cross(&edge, &toOrigin, &crossCheck);

    // check if origin is off to the side of edge
    if (vector3Dot(&crossCheck, &triangle->normal) >= 0.0f) {
        return 0;
    }

    float edgeLerp = vector3Dot(&toOrigin, &edge);
    float edgeMagSqrd = vector3MagSqrd(&edge);

    if (edgeLerp < 0.0f) {
        edgeLerp = 0.0f;
    } else if (edgeLerp > edgeMagSqrd) {
        edgeLerp = 1.0f;
    } else {
        edgeLerp /= edgeMagSqrd;
    }

    struct Vector3 nearestPoint;
    vector3AddScaled(pointA, &edge, edgeLerp, &nearestPoint);

    triangle->distanceToOrigin = sqrtf(vector3MagSqrd(&nearestPoint));

    return 1;
}

void expandingSimplexTriangleDetermineDistance(struct ExpandingSimplex* simplex, struct SimplexTriangle* triangle) {
    vector3Normalize(&triangle->normal, &triangle->normal);

    for (int i = 0; i < 3; ++i) {
        if (expandingSimplexTriangleCheckEdge(simplex, triangle, i)) {
            return;
        }
    }
    
    triangle->distanceToOrigin = vector3Dot(&triangle->normal, &simplex->points[triangle->indexData.indices[0]]);
}

void expandingSimplexRotateEdge(struct ExpandingSimplex* simplex, struct SimplexTriangle* triangleA, int triangleAIndex, int heapIndex) {
    // new triangles are setup so the edge to rotate is the first edge
    int triangleBIndex = triangleA->indexData.adjacentFaces[0];

    struct SimplexTriangle* triangleB = &simplex->triangles[triangleBIndex];

    int relativeIndex0 = triangleA->indexData.oppositePoints[0];
    int relativeIndex1 = NEXT_FACE(relativeIndex0);
    int relativeIndex2 = NEXT_FACE(relativeIndex1);

    triangleA->indexData.adjacentFaces[0] = triangleB->indexData.adjacentFaces[relativeIndex2];
    triangleB->indexData.adjacentFaces[relativeIndex1] = triangleA->indexData.adjacentFaces[1];
    triangleA->indexData.adjacentFaces[1] = triangleBIndex;
    triangleB->indexData.adjacentFaces[relativeIndex2] = triangleAIndex;

    triangleA->indexData.indices[1] = triangleB->indexData.indices[relativeIndex0];
    triangleB->indexData.indices[relativeIndex2] = triangleA->indexData.indices[2];

    triangleA->indexData.oppositePoints[0] = triangleB->indexData.oppositePoints[relativeIndex2];
    triangleB->indexData.oppositePoints[relativeIndex1] = triangleA->indexData.oppositePoints[1];
    triangleA->indexData.oppositePoints[1] = relativeIndex1;
    triangleB->indexData.oppositePoints[relativeIndex2] = 0;

    expandingSimplexTriangleInitNormal(simplex, triangleA);
    expandingSimplexTriangleDetermineDistance(simplex, triangleA);
    expandingSimplexFixHeap(simplex, heapIndex);

    expandingSimplexTriangleInitNormal(simplex, triangleB);
    expandingSimplexTriangleDetermineDistance(simplex, triangleB);
    expandingSimplexFixHeap(simplex, expandingSimplexFindHeapIndex(simplex, triangleBIndex));
}

void expandingSimplexTriangleCheckRotate(struct ExpandingSimplex* simplex, int triangleIndex, int heapIndex) {
    struct SimplexTriangle* triangle = &simplex->triangles[triangleIndex];
    struct SimplexTriangle* adjacent = &simplex->triangles[triangle->indexData.adjacentFaces[0]];
    struct Vector3* oppositePoint = &simplex->points[adjacent->indexData.indices[triangle->indexData.oppositePoints[0]]];

    struct Vector3* firstPoint = &simplex->points[triangle->indexData.indices[0]];

    struct Vector3 offset;
    vector3Sub(oppositePoint, firstPoint, &offset);

    if (vector3Dot(&offset, &triangle->normal) > 0.0f) {
        expandingSimplexRotateEdge(simplex, triangle, triangleIndex, heapIndex);
    } else {
        expandingSimplexTriangleDetermineDistance(simplex, triangle);
        expandingSimplexFixHeap(simplex, heapIndex);
    }
}

void expandingSimplexTriangleInit(struct ExpandingSimplex* simplex, union SimplexTriangleIndexData* indexData, struct SimplexTriangle* triangle) {
    triangle->indexData = *indexData;
    expandingSimplexTriangleInitNormal(simplex, triangle);
}

void expandingSimplexAddTriangle(struct ExpandingSimplex* simplex, union SimplexTriangleIndexData* data) {
    if (simplex->triangleCount == MAX_SIMPLEX_TRIANGLES) {
        return;
    }

    int result = simplex->triangleCount;
    expandingSimplexTriangleInit(simplex, data, &simplex->triangles[result]);
    expandingSimplexTriangleDetermineDistance(simplex, &simplex->triangles[result]);

    simplex->triangleHeap[result] = result;
    ++simplex->triangleCount;
    expandingSimplexSiftDownHeap(simplex, result);
}

struct SimplexTriangle* expandingSimplexClosestFace(struct ExpandingSimplex* simplex) {
    return &simplex->triangles[simplex->triangleHeap[0]];
}

union SimplexTriangleIndexData gInitialSimplexIndexData[] = {
    {{{0, 1, 2}, {3, 1, 2}, {2, 2, 2}}},
    {{{2, 1, 3}, {0, 3, 2}, {0, 1, 0}}},
    {{{0, 2, 3}, {0, 1, 3}, {1, 1, 0}}},
    {{{1, 0, 3}, {0, 2, 1}, {2, 1, 0}}},
};

void expandingSimplexInit(struct ExpandingSimplex* expandingSimplex, struct Simplex* simplex) {
    __assert(simplex->nPoints == 4);

    expandingSimplex->triangleCount = 0;
    expandingSimplex->pointCount = 0;

    for (int i = 0; i < 4; ++i) {
        expandingSimplexAddPoint(expandingSimplex, &simplex->objectAPoint[i], &simplex->points[i]);
    }

    for (int i = 0; i < 4; ++i) {
        expandingSimplexAddTriangle(expandingSimplex, &gInitialSimplexIndexData[i]);
    }
}

void expandingSimplexExpand(struct ExpandingSimplex* expandingSimplex, int newPointIndex) {
    if (newPointIndex == -1) {
        return;
    }

    struct SimplexTriangle* faceToRemove = expandingSimplexClosestFace(expandingSimplex);

    union SimplexTriangleIndexData existing = faceToRemove->indexData;

    unsigned char triangleIndices[3];
    triangleIndices[0] = expandingSimplex->triangleHeap[0];
    triangleIndices[1] = expandingSimplex->triangleCount;
    triangleIndices[2] = expandingSimplex->triangleCount + 1;

    // first connect all the adjacent face information
    for (int i = 0; i < 3; ++i) {
        union SimplexTriangleIndexData next;
        int nextFace = NEXT_FACE(i);
        int nextNextFace = NEXT_FACE(nextFace);
        next.indices[0] = existing.indices[i];
        next.indices[1] = existing.indices[nextFace];
        next.indices[2] = newPointIndex;

        next.adjacentFaces[0] = existing.adjacentFaces[i];
        next.adjacentFaces[1] = triangleIndices[nextFace];
        next.adjacentFaces[2] = triangleIndices[nextNextFace];

        next.oppositePoints[0] = existing.oppositePoints[i];
        next.oppositePoints[1] = 1;
        next.oppositePoints[2] = 0;

        // update back reference to new triangle
        struct SimplexTriangle* otherTriangle = &expandingSimplex->triangles[existing.adjacentFaces[i]];
        int backReferenceIndex = NEXT_FACE(existing.oppositePoints[i]);
        otherTriangle->indexData.adjacentFaces[backReferenceIndex] = triangleIndices[i];
        otherTriangle->indexData.oppositePoints[backReferenceIndex] = 2;

        expandingSimplexTriangleInit(expandingSimplex, &next, &expandingSimplex->triangles[triangleIndices[i]]);
    }

    // then check for edge rotation
    for (int i = 0; i < 3; ++i) {
        int triangleIndex = triangleIndices[i];

        if (i != 0) {
            expandingSimplex->triangleHeap[triangleIndex] = triangleIndex;
            ++expandingSimplex->triangleCount;
        }

        expandingSimplexTriangleCheckRotate(expandingSimplex, triangleIndex, i == 0 ? 0 : triangleIndex);
    }
}

void epaCalculateContact(struct ExpandingSimplex* simplex, struct SimplexTriangle* closestFace, struct EpaResult* result) {
    struct Vector3 baryCoords;

    struct Vector3 planePos;
    vector3Scale(&closestFace->normal, &planePos, closestFace->distanceToOrigin);

    calculateBarycentricCoords(
        &simplex->points[closestFace->indexData.indices[0]],
        &simplex->points[closestFace->indexData.indices[1]],
        &simplex->points[closestFace->indexData.indices[2]],
        &planePos,
        &baryCoords
    );

    evaluateBarycentricCoords(
        &simplex->aPoints[closestFace->indexData.indices[0]],
        &simplex->aPoints[closestFace->indexData.indices[1]],
        &simplex->aPoints[closestFace->indexData.indices[2]],
        &baryCoords,
        &result->contactA
    );

    vector3AddScaled(&result->contactA, &result->normal, -result->penetration, &result->contactB);
}

void epaSolve(struct Simplex* startingSimplex, void* objectA, MinkowsiSum objectASum, void* objectB, MinkowsiSum objectBSum, struct EpaResult* result) {
    struct ExpandingSimplex* simplex = stackMalloc(sizeof(struct ExpandingSimplex));
    expandingSimplexInit(simplex, startingSimplex);
    struct SimplexTriangle* closestFace = NULL;
    float projection = 0.0f;

    for (int i = 0; i < MAX_ITERATIONS; ++i) {
        struct Vector3 reverseNormal;

        closestFace = expandingSimplexClosestFace(simplex);

        int nextIndex = simplex->pointCount;

        struct Vector3* aPoint = &simplex->aPoints[nextIndex];
        struct Vector3 bPoint;

        objectASum(objectA, &closestFace->normal, aPoint);
        vector3Negate(&closestFace->normal, &reverseNormal);
        objectBSum(objectB, &reverseNormal, &bPoint);

        vector3Sub(aPoint, &bPoint, &simplex->points[nextIndex]);

        projection = vector3Dot(&simplex->points[nextIndex], &closestFace->normal);

        if ((projection - closestFace->distanceToOrigin) < 0.00000001f) {
            break;
        }

        ++simplex->pointCount;
        expandingSimplexExpand(simplex, nextIndex);
    }

    if (closestFace) {
        result->normal = closestFace->normal;
        result->penetration = projection;
        epaCalculateContact(simplex, closestFace, result);
    }

    stackMallocFree(simplex);
}