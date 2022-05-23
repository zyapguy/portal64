#ifndef __LEVEL_DEFINITION_H__
#define __LEVEL_DEFINITION_H__

#include "../physics/world.h"
#include "../scene/portal_surface.h"
#include "../math/boxs16.h"
#include "../math/box3d.h"
#include "../math/range.h"

struct StaticContentElement {
    Gfx* displayList;
    u8 materialIndex;
};

struct BoundingSphere {
    short x, y, z;
    short radius;
};

enum TriggerCutsceneIndex {
    TRIGGER_START,
};

enum CutsceneStepType {
    CutsceneStepTypeNoop,
    CutsceneStepTypePlaySound,
    CutsceneStepTypeStartSound,
    CutsceneStepTypeDelay,
};

struct CutsceneStep {
    enum CutsceneStepType type;

    union {
        struct {
            u16 soundId;
            u8 volume;
            u8 pitch;
        } playSound;
        float delay;
        int noop;
    };
};

struct Cutscene {
    struct CutsceneStep* steps;
    u16 stepCount;
};

struct Trigger {
    struct Cutscene cutscene;
    struct Box3D box;
};

struct Location {
    struct Transform transform;
    short roomIndex;
};

struct LevelDefinition {
    struct CollisionObject* collisionQuads;
    struct StaticContentElement *staticContent;
    struct Rangeu16 *roomStaticMapping;
    struct BoundingBoxs16* staticBoundingBoxes;
    struct PortalSurface* portalSurfaces;
    // maps index of a collisionQuads to indices in portalSurfaces
    struct PortalSurfaceMapping* portalSurfaceMapping;
    struct Trigger* triggers;
    struct Location* locations;
    struct World world;
    short collisionQuadCount;
    short staticContentCount;
    short portalSurfaceCount;
    short triggerCount;
    short locationCount;
    short startLocation;
};

#endif