#include "GoblinRenderer.h"
#include "GoblinRay.h"
#include "GoblinColor.h"
#include "GoblinCamera.h"
#include "GoblinFilm.h"
#include "GoblinUtils.h"

namespace Goblin {

    Renderer::Renderer(const RenderSetting& setting):
        mLightSampleIndexes(NULL), mBSDFSampleIndexes(NULL),
        mPickLightSampleIndexes(NULL),
        mSamples(NULL), mSampler(NULL), mPowerDistribution(NULL), 
        mSetting(setting) {
    }

    Renderer::~Renderer() {
        if(mLightSampleIndexes) {
            delete [] mLightSampleIndexes;
            mLightSampleIndexes = NULL;
        }
        if(mBSDFSampleIndexes) {
            delete [] mBSDFSampleIndexes;
            mBSDFSampleIndexes = NULL;
        }
        if(mPickLightSampleIndexes) {
            delete [] mPickLightSampleIndexes;
            mPickLightSampleIndexes = NULL;
        }
        if(mSamples) {
            delete [] mSamples;
            mSamples = NULL;
        }
        if(mSampler) {
            delete mSampler;
            mSampler = NULL;
        }
        if(mPowerDistribution) {
            delete mPowerDistribution;
            mPowerDistribution = NULL;
        }
    }

    void Renderer::render(const ScenePtr& scene) {
        const CameraPtr camera = scene->getCamera();
        Film* film = camera->getFilm();
        int xStart, xEnd, yStart, yEnd;
        film->getSampleRange(&xStart, &xEnd, &yStart, &yEnd);
        if(mSampler != NULL) {
            delete mSampler;
        }
        if(mSamples != NULL) {
            delete [] mSamples;
        }
        int samplePerPixel = mSetting.samplePerPixel;
        mSampler = new Sampler(xStart, xEnd, yStart, yEnd, 
            samplePerPixel);
        // bluh....api dependency on the above sampler
        // TODO wragle out the on APIdependency  
        querySampleQuota(scene, mSampler);
        int batchAmount = mSampler->maxSamplesPerRequest();
        mSamples = mSampler->allocateSampleBuffer(batchAmount);
        // temp progress reporter so that waiting can be not that boring...
        // TODO make this something more elegant
        unsigned long int accumulatedBuffer = 0;
        unsigned long int accumulatedSamples = 0;
        unsigned long int maxTotalSamples = mSampler->maxTotalSamples();
        unsigned long int reportStep = maxTotalSamples / 100;
        string backspace = "\b\b\b\b\b\b\b\b\b\b\b\b\b";

        int sampleNum = 0;
        while((sampleNum = mSampler->requestSamples(mSamples)) > 0) {
            for(int i = 0; i < sampleNum; ++i) {
                Ray ray;
                float w = camera->generateRay(mSamples[i], &ray);
                Color L = w * Li(scene, ray, mSamples[i]);
                film->addSample(mSamples[i], L);
            }

            // print out progress
            accumulatedBuffer += sampleNum;
            if(accumulatedBuffer > reportStep) {
                accumulatedSamples += accumulatedBuffer;
                accumulatedBuffer = 0;
                std::cout << backspace;
                std::cout << "progress %";
                std::cout << 100 * accumulatedSamples / maxTotalSamples;
                std::cout.flush();
            }
        }
        std::cout << backspace;
        film->writeImage();
    }

    Color Renderer::singleSampleLd(const ScenePtr& scene, const Ray& ray,
        float epsilon, const Intersection& intersection,
        const Sample& sample, 
        const LightSample& lightSample,
        const BSDFSample& bsdfSample,
        float pickLightSample,
        BSDFType type) const {

        const vector<Light*>& lights = scene->getLights();
        float pdf;
        int lightIndex = 
            mPowerDistribution->sampleDiscrete(pickLightSample, &pdf);
        const Light* light = lights[lightIndex];
        Color Ld = estimateLd(scene, ray, epsilon, intersection,
            light, lightSample, bsdfSample, type) / pdf;
        return Ld;
    }

    Color Renderer::multiSampleLd(const ScenePtr& scene, const Ray& ray,
        float epsilon, const Intersection& intersection,
        const Sample& sample, 
        LightSampleIndex* lightSampleIndexes,
        BSDFSampleIndex* bsdfSampleIndexes,
        BSDFType type) const {
        Color totalLd = Color::Black;
        const vector<Light*>& lights = scene->getLights();
        for(size_t i = 0; i < lights.size(); ++i) {
            Color Ld = Color::Black;
            uint32_t samplesNum = lightSampleIndexes[i].samplesNum;
            for(size_t n = 0; n < samplesNum; ++n) {
                const Light* light = lights[i];
                LightSample ls;
                BSDFSample bs;
                if(lightSampleIndexes != NULL && bsdfSampleIndexes != NULL) {
                    ls = LightSample(sample, lightSampleIndexes[i], n);
                    bs = BSDFSample(sample, bsdfSampleIndexes[i], n);
                }
                Ld +=  estimateLd(scene, ray, epsilon, intersection,
                    light, ls, bs, type);
            }
            Ld /= static_cast<float>(samplesNum);
            totalLd += Ld;
        }
        return totalLd;
    }

    Color Renderer::estimateLd(const ScenePtr& scene, const Ray& ray,
        float epsilon, const Intersection& intersection, const Light* light, 
        const LightSample& ls, const BSDFSample& bs, BSDFType type) const {

        Color Ld = Color::Black;
        const MaterialPtr& material = 
            intersection.primitive->getMaterial();
        const Fragment& fragment = intersection.fragment;
        Vector3 wo = -ray.d;
        Vector3 wi;
        Vector3 p = fragment.getPosition();
        Vector3 n = fragment.getNormal();
        float lightPdf, bsdfPdf;
        Ray shadowRay;
        // MIS for lighting part
        Color L = light->sampleL(p, epsilon, ls, &wi, &lightPdf, &shadowRay);
        if(L != Color::Black && lightPdf > 0.0f) {
            Color f = material->bsdf(fragment, wo, wi);
            if(f != Color::Black && !scene->intersect(shadowRay)) {
                // we don't do MIS for delta distribution light
                // since there is only one sample need for it
                if(light->isDelta()) {
                    return f * L * absdot(n, wi) / lightPdf;
                } else {
                    bsdfPdf = material->pdf(fragment, wo, wi);
                    float lWeight = powerHeuristic(1, lightPdf, 1, bsdfPdf);
                    Ld += f * L * absdot(n, wi) * lWeight / lightPdf;
                }
            }
        }

        // MIS for bsdf part
        BSDFType sampledType;
        Color f = material->sampleBSDF(fragment, wo, bs, 
            &wi, &bsdfPdf, type, &sampledType);
        if(f != Color::Black && bsdfPdf > 0.0f) {
            // calculate the misWeight if it's not a specular material
            // otherwise we should got 0 Ld from light sample earlier,
            // and count on this part for all the Ld contribution
            float fWeight = 1.0f;
            if(!(sampledType & BSDFSpecular)) {
                lightPdf = light->pdf(p, wi);
                if(lightPdf == 0.0f) {
                    return Ld;
                }
                fWeight = powerHeuristic(1, bsdfPdf, 1, lightPdf);
            }
            Intersection lightIntersect;
            float lightEpsilon;
            Ray r(fragment.getPosition(), wi, epsilon);
            if(scene->intersect(r, &lightEpsilon, &lightIntersect)) {
                if(lightIntersect.primitive->getAreaLight() == light) {
                    Color Li = lightIntersect.Le(-wi);
                    if(Li != Color::Black) {
                        Ld += f * Li * absdot(wi, n) * fWeight / bsdfPdf;
                    }
                }
            }
        }

        return Ld;
    }

    Color Renderer::specularReflect(const ScenePtr& scene, const Ray& ray, 
        float epsilon, const Intersection& intersection,
        const Sample& sample) const {
        Color L(Color::Black);
        const Vector3& n = intersection.fragment.getNormal();
        const Vector3& p = intersection.fragment.getPosition();
        const MaterialPtr& material = 
            intersection.primitive->getMaterial();
        Vector3 wo = -ray.d;
        Vector3 wi;
        float pdf;
        // fill in a random BSDFSample for api request, specular actually
        // don't need to do any monte carlo sampling(only one possible out dir)
        Color f = material->sampleBSDF(intersection.fragment, 
            wo, BSDFSample(), &wi, &pdf, 
            BSDFType(BSDFSpecular | BSDFReflection));
        if(f != Color::Black && absdot(wi, n) != 0.0f) {
            Ray reflectiveRay(p, wi, epsilon);
            reflectiveRay.depth = ray.depth + 1;
            Color Lr = Li(scene, reflectiveRay, sample);
            L += f * Lr * absdot(wi, n) / pdf;
        }
        return L;
    }

    Color Renderer::specularRefract(const ScenePtr& scene, const Ray& ray, 
        float epsilon, const Intersection& intersection,
        const Sample& sample) const {
        Color L(Color::Black);
        const Vector3& n = intersection.fragment.getNormal();
        const Vector3& p = intersection.fragment.getPosition();
        const MaterialPtr& material = 
            intersection.primitive->getMaterial();
        Vector3 wo = -ray.d;
        Vector3 wi;
        float pdf;
        // fill in a random BSDFSample for api request, specular actually
        // don't need to do any monte carlo sampling(only one possible out dir)
        Color f = material->sampleBSDF(intersection.fragment, 
            wo, BSDFSample(), &wi, &pdf, 
            BSDFType(BSDFSpecular | BSDFTransmission));
        if(f != Color::Black && absdot(wi, n) != 0.0f) {
            Ray refractiveRay(p, wi, epsilon);
            refractiveRay.depth = ray.depth + 1;
            Color Lr = Li(scene, refractiveRay, sample);
            L += f * Lr * absdot(wi, n) / pdf;
        }
        return L;
    }
}
