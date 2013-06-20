#include "ContainerSystem.h"

#include "AnchorSystem.h"
#include "TransformationSystem.h"

#include <glm/glm.hpp>

#include <limits>

INSTANCE_IMPL(ContainerSystem);

ContainerSystem::ContainerSystem() : ComponentSystemImpl<ContainerComponent>("Container") {
    /* nothing saved */
    ContainerComponent cc;
    componentSerializer.add(new Property<bool>("enable", OFFSET(enable, cc)));
    componentSerializer.add(new Property<bool>("include_children", OFFSET(includeChildren, cc)));
}

static void updateMinMax(float& minX, float& minY, float& maxX, float& maxY, TransformationComponent* tc) {
    if (tc->size == glm::vec2(0.0f))
        return;
    minX = glm::min(minX, tc->position.x - tc->size.x * 0.5f);
    minY = glm::min(minY, tc->position.y - tc->size.y * 0.5f);
    maxX = glm::max(maxX, tc->position.x + tc->size.x * 0.5f);
    maxY = glm::max(maxY, tc->position.y + tc->size.y * 0.5f);
}

void ContainerSystem::DoUpdate(float) {
    if (components.empty())
        return;

    bool atLeastOneEnabled = false;
    FOR_EACH_COMPONENT(Container, bc)
        atLeastOneEnabled |= (bc->enable && !bc->entities.empty());
        if (atLeastOneEnabled)
            break;
    }

    if (!atLeastOneEnabled)
        return;

    const std::vector<Entity> allEntities(theTransformationSystem.RetrieveAllEntityWithComponent());

    FOR_EACH_ENTITY_COMPONENT(Container, a, bc)
        if (!bc->enable || bc->entities.empty())
            continue;

        //~ TODO
        float minX = std::numeric_limits<float>().max(), minY = std::numeric_limits<float>().max();
        float maxX = std::numeric_limits<float>().min(), maxY = std::numeric_limits<float>().min();
        for(auto jt : bc->entities) {
            TransformationComponent* tc = TRANSFORM(jt);
            updateMinMax(minX, minY, maxX, maxY, tc);

            if (bc->includeChildren) {
                theAnchorSystem.forEachECDo([jt, &minX, &minY, &maxX, &maxY] (Entity e, AnchorComponent *ac) -> void {
                    if (ac->parent == jt)
                        updateMinMax(minX, minY, maxX, maxY, TRANSFORM(e));
                });
            }
        }

        TransformationComponent* tc = TRANSFORM(a);
        tc->position = glm::vec2((minX + maxX) * 0.5, (minY + maxY) * 0.5);
        tc->size = glm::vec2(maxX - minX, maxY - minY);
    }
}
