#pragma once

#include <glm/glm.hpp>

#include "System.h"


struct TransformationComponent {
	TransformationComponent(): position(glm::vec2(0.0f, 0.0f)), size(1.0f, 1.0f), rotation(0), z(0), parent(0) { }

	glm::vec2 position, worldPosition, size;
	float rotation, worldRotation;//radians
	float z, worldZ;

	Entity parent;
};

#define theTransformationSystem TransformationSystem::GetInstance()
#define TRANSFORM(e) theTransformationSystem.Get(e)

UPDATABLE_SYSTEM(Transformation)

public:
	enum PositionReference {
		NW, N, NE,
		W , C, E ,
		SW, S, SE
	};
	static void setPosition(TransformationComponent* tc, const glm::vec2& p, PositionReference ref=C);

#if SAC_DEBUG
	void preDeletionCheck(Entity e) {
		FOR_EACH_COMPONENT(Transformation, bc)
			if (bc->parent == e) {
				LOGE("deleting an entity which is parent ! (Entity " << e << "/" << theEntityManager.entityName(e) << ')')
			}
		}
	}
#endif
};
