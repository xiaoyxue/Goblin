#include "GoblinImageIO.h"
#include "GoblinLight.h"
#include "GoblinRay.h"
#include "GoblinSampler.h"
#include "GoblinScene.h"

#include <sstream>

namespace Goblin {

LightSampleIndex::LightSampleIndex(SampleQuota* sampleQuota,
    int requestNum) {
    SampleIndex oneDIndex = sampleQuota->requestOneDQuota(requestNum);
    SampleIndex twoDIndex = sampleQuota->requestTwoDQuota(requestNum);
    // theoretically this two should be the same...
    // this is just a paranoid double check
    samplesNum = std::min(oneDIndex.sampleNum, twoDIndex.sampleNum);
    componentIndex = oneDIndex.offset;
    geometryIndex = twoDIndex.offset;
}

LightSample::LightSample(const RNG& rng) {
    uComponent = rng.randomFloat();
    uGeometry[0] = rng.randomFloat();
    uGeometry[1] = rng.randomFloat();
}

LightSample::LightSample(const Sample& sample,
    const LightSampleIndex& index, uint32_t n) {
    uComponent = sample.u1D[index.componentIndex][n];
    uGeometry[0] = sample.u2D[index.geometryIndex][2 * n];
    uGeometry[1] = sample.u2D[index.geometryIndex][2 * n + 1];
}

BSSRDFSampleIndex::BSSRDFSampleIndex(SampleQuota* sampleQuota,
    int requestNum):lsIndex(LightSampleIndex(sampleQuota, requestNum)) {
    pickLightIndex = sampleQuota->requestOneDQuota(requestNum).offset;
    pickAxisIndex = sampleQuota->requestOneDQuota(requestNum).offset;
    discSampleIndex = sampleQuota->requestTwoDQuota(requestNum).offset;
    singleScatterIndex = sampleQuota->requestOneDQuota(requestNum).offset;
    samplesNum = lsIndex.samplesNum;
}

BSSRDFSample::BSSRDFSample(const RNG& rng):
    uPickLight(rng.randomFloat()),
    uPickAxis(rng.randomFloat()),
    ls(LightSample(rng)),
    uSingleScatter(rng.randomFloat()) {
    uDisc[0] = rng.randomFloat();
    uDisc[1] = rng.randomFloat();
}

BSSRDFSample::BSSRDFSample(const Sample& sample,
    const BSSRDFSampleIndex& index, uint32_t n):
    ls(LightSample(sample, index.lsIndex, n)) {
    uPickLight = sample.u1D[index.pickLightIndex][n];
    uPickAxis = sample.u1D[index.pickAxisIndex][n];
    uDisc[0] = sample.u2D[index.discSampleIndex][2 * n];
    uDisc[1] = sample.u2D[index.discSampleIndex][2 * n + 1];
    uSingleScatter = sample.u1D[index.singleScatterIndex][n];
}

size_t Light::nextLightId = 0;

Light::Light(): mLightId(nextLightId++) {}

void Light::setOrientation(const Vector3& dir) {
    Vector3 xAxis, yAxis;
    const Vector3& zAxis = dir;
    coordinateAxises(zAxis, &xAxis, &yAxis);
    Matrix3 rotation(
        xAxis[0], yAxis[0], zAxis[0],
        xAxis[1], yAxis[1], zAxis[1],
        xAxis[2], yAxis[2], zAxis[2]);
    mToWorld.setOrientation(Quaternion(rotation));
}

PointLight::PointLight(const Color& I, const Vector3& P):
mIntensity(I) {
    mToWorld.setPosition(P);
    mParams.setInt("type", Point);
    mParams.setVector3("intensity",
		Vector3(mIntensity.r, mIntensity.g, mIntensity.b));
    mParams.setVector3("position", P);
}

Color PointLight::sampleL(const Vector3& p, float epsilon,
    const LightSample& lightSample,
    Vector3* wi, float* pdf, Ray* shadowRay) const {
    Vector3 dir = mToWorld.getPosition() - p;
    *wi = normalize(dir);
    *pdf = 1.0f;
    shadowRay->o = p;
    shadowRay->d = *wi;
    shadowRay->mint = epsilon;
    float squaredDistance = squaredLength(dir);
    shadowRay->maxt = sqrt(squaredDistance) - epsilon;
    return mIntensity / squaredDistance;
}

Vector3 PointLight::samplePosition(const ScenePtr& scene,
    const LightSample& ls, Vector3* surfaceNormal,
    float* pdfArea) const {
    // there is only one possible position for point light
    *surfaceNormal = Vector3::Zero;
    *pdfArea = 1.0f;
    return mToWorld.getPosition();
}

Vector3 PointLight::sampleDirection(const Vector3& surfaceNormal,
    float u1, float u2, float* pdfW) const {
    *pdfW = uniformSpherePdf();
    return uniformSampleSphere(u1, u2);
}

float PointLight::pdfPosition(const ScenePtr& scene,
    const Vector3& p) const {
    return 0.0f;
}

float PointLight::pdfDirection(const Vector3& p, const Vector3& n,
    const Vector3& wo) const {
    return uniformSpherePdf();
}

Color PointLight::eval(const Vector3& p, const Vector3& n,
    const Vector3& wo) const {
    // TODO assert p == mToWorld.getPosition()?
    return mIntensity;
}

Color PointLight::power(const Scene& scene) const {
    return 4.0f * PI * mIntensity;
}

DirectionalLight::DirectionalLight(const Color& R, const Vector3& D):
mRadiance(R) {
    setOrientation(D);
    mParams.setInt("type", Directional);
    mParams.setVector3("radiance",
		Vector3(mRadiance.r, mRadiance.g, mRadiance.b));
    mParams.setVector3("direction", D);
}

Color DirectionalLight::sampleL(const Vector3& p, float epsilon,
    const LightSample& lightSample,
    Vector3* wi, float* pdf, Ray* shadowRay) const {
    *wi = -getDirection();
    *pdf = 1.0f;
    shadowRay->o = p;
    shadowRay->d = *wi;
    shadowRay->mint = epsilon;
    return mRadiance;
}

// approximation of sample directional light by sampling among
// the world bounding sphere, first sample a point from disk
// with world radius that perpendicular to light direction,
// then offset it back world radius distance as ray origin
// ray dir is simply light dir
Vector3 DirectionalLight::samplePosition(const ScenePtr& scene,
    const LightSample& ls, Vector3* surfaceNormal, float* pdfArea) const {
    Vector3 worldCenter;
    float worldRadius;
    scene->getBoundingSphere(&worldCenter, &worldRadius);
    Vector3 xAxis, yAxis;
    Vector3 zAxis = getDirection();
    coordinateAxises(zAxis, &xAxis, &yAxis);
    Vector2 diskXY = uniformSampleDisk(ls.uGeometry[0], ls.uGeometry[1]);
    Vector3 worldDiskSample = worldCenter +
        worldRadius * (diskXY.x *xAxis +diskXY.y *yAxis);
    *surfaceNormal = Vector3::Zero;
    *pdfArea = 1.0f / (PI * worldRadius * worldRadius);
    return worldDiskSample - zAxis * worldRadius;
}

Vector3 DirectionalLight::sampleDirection(const Vector3& surfaceNormal,
    float u1, float u2, float* pdfW) const {
    *pdfW = 1.0f;
    return getDirection();
}

float DirectionalLight::pdfPosition(const ScenePtr& scene,
    const Vector3& p) const {
    Vector3 worldCenter;
    float worldRadius;
    scene->getBoundingSphere(&worldCenter, &worldRadius);
    return 1.0f / (PI * worldRadius * worldRadius);
}

float DirectionalLight::pdfDirection(const Vector3& p,
    const Vector3& n, const Vector3& wo) const {
    return 0.0f;
}

Color DirectionalLight::eval(const Vector3& p, const Vector3& n,
    const Vector3& wo) const {
    float cosTheta = dot(wo, getDirection());
    bool isParallel = fabs(cosTheta - 1.0f) < 1e-5f;
    return isParallel ? mRadiance : Color::Black;
}

Color DirectionalLight::power(const Scene& scene) const {
    Vector3 center;
    float radius;
    scene.getBoundingSphere(&center, &radius);
    // well...... we can't make it infinitely big, so use bounding
    // sphere for a rough approximation
    return radius * radius * PI * mRadiance;
}

SpotLight::SpotLight(const Color& intensity, const Vector3& position,
    const Vector3& dir, float cosThetaMax, float cosFalloffStart):
    mIntensity(intensity), mCosThetaMax(cosThetaMax),
    mCosFalloffStart(cosFalloffStart) {
    mToWorld.setPosition(position);
    Vector3 direction = normalize(dir);
    setOrientation(direction);
    mParams.setInt("type", Spot);
    mParams.setVector3("intensity",
		Vector3(intensity.r, intensity.g, intensity.b));
    mParams.setVector3("direction", direction);
}

Color SpotLight::sampleL(const Vector3& p, float epsilon,
    const LightSample& lightSample,
    Vector3* wi, float* pdf, Ray* shadowRay) const {
    Vector3 dir = mToWorld.getPosition() - p;
    *wi = normalize(dir);
    *pdf = 1.0f;
    shadowRay->o = p;
    shadowRay->d = *wi;
    shadowRay->mint = epsilon;
    float squaredDistance = squaredLength(dir);
    shadowRay->maxt = sqrt(squaredDistance) - epsilon;
    return falloff(-(*wi)) * mIntensity / squaredDistance;
}

Vector3 SpotLight::samplePosition(const ScenePtr& scene,
    const LightSample& ls, Vector3* surfaceNormal, float* pdfArea) const {
    *surfaceNormal = Vector3::Zero;
    *pdfArea = 1.0f;
    return mToWorld.getPosition();
}

Vector3 SpotLight::sampleDirection(const Vector3& surfaceNormal,
    float u1, float u2, float* pdfW) const {
    Vector3 dLocal = uniformSampleCone(u1, u2, mCosThetaMax);
    *pdfW = uniformConePdf(mCosThetaMax);
    return mToWorld.onVector(dLocal);
}

float SpotLight::pdfPosition(const ScenePtr& scene,
    const Vector3& p) const {
    return 0.0f;
}

float SpotLight::pdfDirection(const Vector3& p,
    const Vector3& n, const Vector3& wo) const {
    return uniformConePdf(mCosThetaMax);
}

Color SpotLight::eval(const Vector3& p, const Vector3& n,
    const Vector3& wo) const {
    // TODO assert p == mToWorld.getPosition() ?
    return falloff(wo) * mIntensity;
}

Color SpotLight::power(const Scene& scene) const {
    // integrate the solid angle =
	// integrate sinTheta over 0->thetaMax over 0->2PI =
	// 2PI * (1 - cosThetaMax)
    return mIntensity * TWO_PI *
        (1.0f - 0.5f * (mCosThetaMax + mCosFalloffStart));
}

float SpotLight::falloff(const Vector3& w) const {
    float cosTheta = dot(w, mToWorld.onVector(Vector3::UnitZ));
    if (cosTheta < mCosThetaMax) {
        return 0.0f;
    }
    if (cosTheta > mCosFalloffStart) {
        return 1.0f;
    }
    float delta = (cosTheta - mCosThetaMax) / (mCosFalloffStart - mCosThetaMax);
    return delta * delta * delta * delta;
}

GeometrySet::GeometrySet(const Geometry* geometry):
    mSumArea(0.0f), mAreaDistribution(nullptr) {
    if (geometry->intersectable()) {
        mGeometries.push_back(geometry);
    } else {
        geometry->refine(mGeometries);
    }
    mSumArea = 0.0f;
    mGeometriesArea.resize(mGeometries.size());
    for (size_t i = 0; i < mGeometries.size(); ++i) {
        float area = mGeometries[i]->area();
        mGeometriesArea[i] = area;
        mSumArea += area;
    }
    mAreaDistribution = new CDF1D(mGeometriesArea);
}

GeometrySet::~GeometrySet() {
    if (mAreaDistribution != nullptr ) {
        delete mAreaDistribution;
        mAreaDistribution = nullptr;
    }
}

Vector3 GeometrySet::sample(const Vector3& p,
    const LightSample& lightSample,
    Vector3* normal) const {
    // pick up a geometry to sample based on area distribution
    float uComp = lightSample.uComponent;
    int geoIndex = mAreaDistribution->sampleDiscrete(uComp);
    // sample out ps from picked up geometry surface
    float u1 = lightSample.uGeometry[0];
    float u2 = lightSample.uGeometry[1];
    Vector3 ps = mGeometries[geoIndex]->sample(p, u1, u2, normal);
    return ps;
}

Vector3 GeometrySet::sample(const LightSample& lightSample,
    Vector3* normal) const {
    float uComp = lightSample.uComponent;
    int geoIndex = mAreaDistribution->sampleDiscrete(uComp);
    float u1 = lightSample.uGeometry[0];
    float u2 = lightSample.uGeometry[1];
    Vector3 ps = mGeometries[geoIndex]->sample(u1, u2, normal);
    return ps;
}

float GeometrySet::pdf(const Vector3& p, const Vector3& wi) const {
    float pdf = 0.0f;
    for (size_t i = 0; i < mGeometries.size(); ++i) {
        pdf += mGeometriesArea[i] * mGeometries[i]->pdf(p, wi);
    }
    pdf /= mSumArea;
    return pdf;
}


AreaLight::AreaLight(const Color& Le, const Geometry* geometry,
    const Transform& toWorld, uint32_t samplesNum): mLe(Le),
    mSamplesNum(samplesNum) {
    mToWorld = toWorld;
    // only uniform scaling support right now
    const Vector3& scale = mToWorld.getScale();
    if (!isEqual(scale.x, scale.y) ||
        !isEqual(scale.y, scale.z) ||
        !isEqual(scale.z, scale.x)) {
		std::cerr << "Area light only support uniform scaling. "
            "Non uniform scaling result to undefined behavior. " << std::endl;
    }
    mGeometrySet = new GeometrySet(geometry);
}

AreaLight::~AreaLight() {
    if (mGeometrySet != nullptr) {
        delete mGeometrySet;
        mGeometrySet = nullptr;
    }
}

Color AreaLight::L(const Vector3& ps, const Vector3& ns,
    const Vector3& w) const {
    return dot(ns, w) > 0.0f ? mLe : Color::Black;
}

Color AreaLight::sampleL(const Vector3& p, float epsilon,
    const LightSample& lightSample,
    Vector3* wi, float* pdf, Ray* shadowRay) const {
    // transform world space p to local space since all GeometrySet methods
    // are in local space
    Vector3 pLocal = mToWorld.invertPoint(p);
    Vector3 nsLocal;
    Vector3 psLocal = mGeometrySet->sample(pLocal, lightSample, &nsLocal);
    Vector3 wiLocal = normalize(psLocal - pLocal);
    *pdf = mGeometrySet->pdf(pLocal, wiLocal);
    // transform
    Vector3 ps = mToWorld.onPoint(psLocal);
    Vector3 ns = normalize(mToWorld.onNormal(nsLocal));
    *wi = normalize(ps - p);

    shadowRay->o = p;
    shadowRay->d = *wi;
    shadowRay->mint = epsilon;
    shadowRay->maxt = length(ps - p) - epsilon;

    return L(ps, ns, -*wi);
}

Vector3 AreaLight::samplePosition(const ScenePtr& scene,
    const LightSample& ls, Vector3* surfaceNormal,
    float* pdfArea) const {
    Vector3 worldScale = mToWorld.getScale();
    float worldArea = mGeometrySet->area() *
        (worldScale.x * worldScale.y +
        worldScale.y * worldScale.z +
        worldScale.z * worldScale.x) / 3.0f;
    *pdfArea = 1.0f / worldArea;
    Vector3 nLocal;
    Vector3 pLocal = mGeometrySet->sample(ls, &nLocal);
    *surfaceNormal = normalize(mToWorld.onNormal(nLocal));
    return mToWorld.onPoint(pLocal);
}

Vector3 AreaLight::sampleDirection(const Vector3& surfaceNormal,
    float u1, float u2, float* pdfW) const {
    Vector3 localDir = cosineSampleHemisphere(u1, u2);
    float cosTheta = localDir.z;
    Vector3 right, up;
    coordinateAxises(surfaceNormal, &right, &up);
    Matrix3 surfaceToWorld(
        right.x, up.x, surfaceNormal.x,
        right.y, up.y, surfaceNormal.y,
        right.z, up.z, surfaceNormal.z);
    *pdfW = cosTheta * INV_PI;
    return surfaceToWorld * localDir;
}

float AreaLight::pdfPosition(const ScenePtr& scene,
    const Vector3& p) const {
    Vector3 worldScale = mToWorld.getScale();
    // based on the assumption that we are using uniform scaling
    float worldArea = mGeometrySet->area() *
        (worldScale.x * worldScale.y);
    return 1.0f / worldArea;
}

float AreaLight::pdfDirection(const Vector3& p, const Vector3& n,
    const Vector3& wo) const {
    float cosTheta = dot(wo, n);
    return cosTheta > 0.0f ? cosTheta * INV_PI : 0.0f;
}

Color AreaLight::eval(const Vector3& p, const Vector3& n,
    const Vector3& wo) const {
    // only front face of geometry emit radiance
    return dot(n, wo) > 0.0f ? mLe : Color::Black;
}

Color AreaLight::power(const Scene& scene) const {
    Vector3 worldScale = mToWorld.getScale();
    // based on the assumption that we are using uniform scaling
    float worldArea = mGeometrySet->area() *
        (worldScale.x * worldScale.y);
    // if any random angle output mLe on the area light surface,
    // we can think of the input radience with perpendular angle
    // per unit area mLe * PI (similar to how we get lambert bsdf)
    return mLe * PI * worldArea;
}

float AreaLight::pdf(const Vector3& p, const Vector3& wi) const {
    Vector3 pLocal = mToWorld.invertPoint(p);
    Vector3 wiLocal = mToWorld.invertVector(wi);
    return mGeometrySet->pdf(pLocal, wiLocal);
}


ImageBasedLight::ImageBasedLight(const std::string& radianceMap,
    const Color& filter, const Quaternion& orientation,
    uint32_t samplesNum):
    mRadiance(nullptr), mDistribution(nullptr),
    mSamplesNum(samplesNum), mSampleMIPLevel(0) {
    // Make default orientation facing the center of
    // environment map since spherical coordinate is z-up
    mToWorld.rotateX(-0.5f * PI);
    mToWorld.rotateY(-0.5f * PI);
    mToWorld.setOrientation(orientation * mToWorld.getOrientation());
    int width, height;
    Color* buffer = loadImage(radianceMap, &width, &height);
    if (buffer == nullptr) {
        std::cerr << "errror loading image " << radianceMap << std::endl;
        width = 1;
        height = 1;
        buffer = new Color[1];
        buffer[0] = Color::Magenta;
    }
    for (int i = 0; i < width * height; ++i) {
        buffer[i] *= filter;
    }

    mRadiance = new MIPMap<Color>(buffer, width, height);
    int maxLevel = mRadiance->getLevelsNum() - 1;
    mAverageRadiance = mRadiance->lookup(maxLevel, 0.0f, 0.0f);

    int buildDistLevel = std::max(0, maxLevel - 8);
    const ImageBuffer<Color>* distBuffer =
        mRadiance->getImageBuffer(buildDistLevel);
    Color* distImage = distBuffer->image;
    int distWidth = distBuffer->width;
    int distHeight = distBuffer->height;
    float* dist = new float[distWidth * distHeight];
    for (int i = 0; i < distHeight; ++i) {
        float sinTheta = sin(((float)i + 0.5f) / (float)distHeight * PI);
        for (int j = 0; j < distWidth; ++j) {
            int index = i * distWidth + j;
            dist[index] = distImage[index].luminance() * sinTheta;
        }
    }
    mDistribution = new CDF2D(dist, distWidth, distHeight);
    delete [] dist;
}

ImageBasedLight::~ImageBasedLight() {
    if (mRadiance != nullptr) {
        delete mRadiance;
        mRadiance = nullptr;
    }
    if (mDistribution != nullptr) {
        delete mDistribution;
        mDistribution = nullptr;
    }
}

Color ImageBasedLight::Le(const Ray& ray) const {
    const Vector3& w = mToWorld.invertVector(ray.d);
    float theta = sphericalTheta(w);
    float phi = sphericalPhi(w);
    float s = phi * INV_TWOPI;
    float t = theta * INV_PI;
    return mRadiance->lookup(mSampleMIPLevel, s, t);
}

Color ImageBasedLight::sampleL(const Vector3& p, float epsilon,
    const LightSample& lightSample,
    Vector3* wi, float* pdf, Ray* shadowRay) const {
    float pdfST;
    Vector2 st = mDistribution->sampleContinuous(
        lightSample.uGeometry[0], lightSample.uGeometry[1], &pdfST);
    float theta = st[1] * PI;
    float phi = st[0] * TWO_PI;
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);
    float cosPhi = cos(phi);
    float sinPhi = sin(phi);
    Vector3 wLocal(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
    *wi = mToWorld.onVector(wLocal);

    if (sinTheta == 0.0f) {
        *pdf = 0.0f;
    }
    *pdf = pdfST / (TWO_PI * PI * sinTheta);

    shadowRay->o = p;
    shadowRay->d = *wi;
    shadowRay->mint = epsilon;

    return mRadiance->lookup(mSampleMIPLevel, st[0], st[1]);
}

Vector3 ImageBasedLight::samplePosition(const ScenePtr& scene,
    const LightSample& ls, Vector3* surfaceNormal, float* pdfArea) const {
    Vector3 worldCenter;
    float worldRadius;
    scene->getBoundingSphere(&worldCenter, &worldRadius);
    Vector3 sphereSample =
        uniformSampleSphere(ls.uGeometry[0], ls.uGeometry[1]);
    *surfaceNormal = -sphereSample;
    // intend to omit radius factor from pdfAdrea since the distance is
    // supposed to be inifinite far away
    *pdfArea = 1.0f / (4.0f * PI);
    return worldCenter + worldRadius * sphereSample;
}

Vector3 ImageBasedLight::sampleDirection(const Vector3& surfaceNormal,
    float u1, float u2, float* pdfW) const {
    // TODO don't really think this is actually correct, revisit later
    Vector3 localDir = cosineSampleHemisphere(u1, u2);
    float cosTheta = localDir.z;
    Vector3 right, up;
    coordinateAxises(surfaceNormal, &right, &up);
    Matrix3 surfaceToWorld(
        right.x, up.x, surfaceNormal.x,
        right.y, up.y, surfaceNormal.y,
        right.z, up.z, surfaceNormal.z);
    *pdfW = cosTheta * INV_PI;
    return surfaceToWorld * localDir;
}

float ImageBasedLight::pdfPosition(const ScenePtr& scene,
    const Vector3& p) const {
    // intend to omit radius factor from pdfAdrea since the distance is
    // supposed to be inifinite far away
    return 1.0f / (4.0f * PI);
}

float ImageBasedLight::pdfDirection(const Vector3& p, const Vector3&n,
    const Vector3& wo) const {
    // TODO don't really think this is actually correct, revisit later
    float cosTheta = dot(n, wo);
    return cosTheta > 0.0f ? cosTheta * INV_PI : 0.0f;
}

Color ImageBasedLight::eval(const Vector3& p, const Vector3& n,
    const Vector3& wo) const {
    const Vector3& w = mToWorld.invertVector(-wo);
    float theta = sphericalTheta(w);
    float phi = sphericalPhi(w);
    float s = phi * INV_TWOPI;
    float t = theta * INV_PI;
    return mRadiance->lookup(mSampleMIPLevel, s, t);
}

Color ImageBasedLight::power(const Scene& scene) const {
    Vector3 center;
    float radius;
    scene.getBoundingSphere(&center, &radius);
    // raough power estimation, assume radiance in world sphere
    // diffuse distribution
    return mAverageRadiance * PI * (4.0f * PI * radius * radius);
}

float ImageBasedLight::pdf(const Vector3& p, const Vector3& wi) const {
    Vector3 wiLocal = mToWorld.invertVector(wi);
    float theta = sphericalTheta(wiLocal);
    float sinTheta = sin(theta);
    if (sinTheta == 0.0f) {
        return 0.0f;
    }
    float phi = sphericalPhi(wiLocal);
    float pdf = mDistribution->pdf(phi * INV_TWOPI, theta * INV_PI) /
        (TWO_PI * PI * sinTheta);
    return pdf;
}

    
Light* createPointLight(const ParamSet& params,
	const SceneCache& sceneCache) {
    Vector3 intensity = params.getVector3("intensity");
    Vector3 position = params.getVector3("position");
    return new PointLight(
		Color(intensity[0], intensity[1], intensity[2]), position);
}

Light* createDirectionalLight(const ParamSet& params,
	const SceneCache& sceneCache) {
    Vector3 radiance = params.getVector3("radiance");
    Vector3 direction = params.getVector3("direction");
    return new DirectionalLight(
		Color(radiance[0], radiance[1], radiance[2]), direction);
}

Light* createSpotLight(const ParamSet& params,
	const SceneCache& sceneCache) {
    Vector3 intensity = params.getVector3("intensity");
    Vector3 position = params.getVector3("position");
    Vector3 direction;
    if (params.hasVector3("target")) {
        direction = normalize(params.getVector3("target") - position);
    } else {
        direction= params.getVector3("direction");
    }
    float cosThetaMax = cos(radians(params.getFloat("theta_max")));
    float cosFalloffStart = cos(radians(params.getFloat("falloff_start")));
    return new SpotLight(
		Color(intensity[0], intensity[1], intensity[2]),
		position, direction, cosThetaMax, cosFalloffStart);
}

Light* createAreaLight(const ParamSet& params,
	const SceneCache& sceneCache) {
	Vector3 radiance = params.getVector3("radiance");
	std::string geoName = params.getString("geometry");
    const Geometry* geometry = sceneCache.getGeometry(geoName);
    // TODO: this cause a problem that we can't run time modify
    // the transform for area light since it's not tied in between
    // instance in scene and the transform in area light itself..
    // need to find a way to improve this part
    Transform toWorld = getTransform(params);
    int sampleNum = params.getInt("sample_num", 1);
    return new AreaLight(
		Color(radiance[0], radiance[1], radiance[2]),
		geometry, toWorld, sampleNum);
}

Light* createImageBasedLight(const ParamSet& params,
	const SceneCache& sceneCache) {
	std::string filename = params.getString("file");
	std::string filePath = sceneCache.resolvePath(filename);
    Vector3 filter = params.getVector3("filter");
    Quaternion orientation = getQuaternion(params);
    int sampleNum = params.getInt("sample_num", 1);
    return new ImageBasedLight(filePath,
		Color(filter[0], filter[1], filter[2]),
		orientation, sampleNum);
}

}
