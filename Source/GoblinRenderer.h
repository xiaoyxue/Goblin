#ifndef GOBLIN_RENDERER_H
#define GOBLIN_RENDERER_H

#include "GoblinMaterial.h"
#include "GoblinScene.h"
#include "GoblinSampler.h"

namespace Goblin {
    class Sample;
    class Color;
    class Ray;
    class Sampler;
    class ParamSet;
    struct BSDFSample;
    struct LightSample;
    struct BSDFSampleIndex;
    struct LightSampleIndex;

    struct RenderSetting {
        RenderSetting(): 
            samplePerPixel(1), maxRayDepth(5) {}
        int samplePerPixel;
        int maxRayDepth;
    };

    class Renderer {
    public:
        Renderer(const RenderSetting& setting);
        ~Renderer();
        void render(const ScenePtr& scene);
    private:
        Color Li(const ScenePtr& scene, const Ray& ray, 
            const Sample& sample) const;
        Color directLighting(const ScenePtr& scene, const Ray& ray,
            float epsilon, const Intersection& intersection, 
            const Sample& sample, BSDFType type = BSDFAll) const;
        Color estimateLd(const ScenePtr& scene, const Ray& ray,
            float epsilon, const Intersection& intersection, 
            const Light* light, const LightSample& ls,
            const BSDFSample& bs, BSDFType type = BSDFAll) const;
        Color specularReflect(const ScenePtr& scene, const Ray& ray, 
            float epsilon, const Intersection& intersection,
            const Sample& sample) const;
        Color specularRefract(const ScenePtr& scene, const Ray& ray, 
            float epsilon, const Intersection& intersection,
            const Sample& sample) const;
        void querySampleQuota(const ScenePtr& scene, Sampler* sampler);

    private:
        LightSampleIndex* mLightSampleIndexes;
        BSDFSampleIndex* mBSDFSampleIndexes;
        Sample* mSamples;
        Sampler* mSampler;
        RenderSetting mSetting;
    };
}

#endif //GOBLIN_RENDERER_H
