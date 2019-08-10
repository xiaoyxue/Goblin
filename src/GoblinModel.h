#ifndef GOBLIN_MODEL_H
#define GOBLIN_MODEL_H
#include "GoblinPrimitive.h"
#include "GoblinLight.h"

namespace Goblin {
class Model : public Primitive {
public:
    bool intersectable() const;

    bool intersect(const Ray& ray, IntersectFilter f) const;

    bool intersect(const Ray& ray, float* epsilon, 
        Intersection* intersection, IntersectFilter f) const;

    bool isCameraLens() const;

    BBox getAABB() const;

    const MaterialPtr& getMaterial() const;

    const AreaLight* getAreaLight() const;

    void refine(PrimitiveList& refinedPrimitives) const;

    void collectRenderList(RenderList& rList,
        const Matrix4& m = Matrix4::Identity) const ;

    static void clearRefinedModels();

    Model(const Geometry* geometry, const MaterialPtr& material,
        const AreaLight* areaLight = nullptr, bool isCameraLens = false);
    // For chunk allocation refined models, 
    // allocate first then set geometry
    Model(): mGeometry(nullptr), mAreaLight(nullptr), mIsCameraLens(false) {}
    void init(const Geometry* geometry, const MaterialPtr& material,
        const AreaLight* areaLight);
private:
    const Geometry* mGeometry;
    MaterialPtr mMaterial;
    const AreaLight* mAreaLight;
    bool mIsCameraLens;
    // used to keep the refined models generated by refine method
    static std::vector<Model*> refinedModels;
friend class ModelPrimitiveCreator;
};

inline bool Model::intersectable() const { 
    return mGeometry->intersectable(); 
}

inline bool Model::isCameraLens() const {
    return mIsCameraLens;
}

inline const MaterialPtr& Model::getMaterial() const {
    return mMaterial;
}

inline const AreaLight* Model::getAreaLight() const {
    return mAreaLight;
}

inline void Model::init(const Geometry* geometry, 
    const MaterialPtr& material, const AreaLight* areaLight) {
    mGeometry = geometry;
    mMaterial = material;
    mAreaLight = areaLight;
}

inline void Model::clearRefinedModels() {
    for (size_t i = 0; i < refinedModels.size(); ++i) {
        delete [] refinedModels[i];
        refinedModels[i] = nullptr;
    }
}
    
class ParamSet;
class SceneCache;

Primitive* createModel(const ParamSet& params,
	const SceneCache& sceneCache);

}

#endif // GOBLIN_MODEL_H