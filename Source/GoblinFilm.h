#ifndef GOBLIN_FILM_H
#define GOBLIN_FILM_H

#include "GoblinColor.h"
#include <string>
namespace Goblin {
    class Film {
    public:
        Film(int xRes, int yRes, const float crop[4], 
            const std::string& filename);
        ~Film(); 

        int getXResolution();
        int getYResolution();

        //TODO replace x y with a Sample struct
        void addSample(int x, int y, const Color& L);
        void writeImage();

    private:
        int mXRes, mYRes;
        int mXStart, mYStart, mXEnd, mYEnd, mXCount, mYCount;
        float mCrop[4];
        std::string mFilename;
        class Pixel {
        public:
            Pixel(): color(0.0f, 0.0f, 0.0f, 1.0f), weight(0.0f) {}
            Color color;
            float weight;
            float pad[3];
        };
        Pixel* pixels;
    };

    inline int Film::getXResolution() { return mXRes; }
    inline int Film::getYResolution() { return mYRes; }
}

#endif //GOBLIN_FILM_H