#ifndef DESCENT_HPP 
#define DESCENT_HPP

#include <stdint.h>
#include <windows.h>
#include "vec2.hpp"
#include "worker.hpp"

#define DIB_WIDTH 640 
#define DIB_HEIGHT 480

#define TEX_LENGTH 16 

#define TILE_WIDTH 20
#define TILE_HEIGHT 20 

#define TILE_WIDTH 20
#define TILE_HEIGHT 20

#define SPR_CAP 256

enum GameButtons {
    BT_LEFT = 0,
    BT_UP = 1 ,
    BT_RIGHT = 2,
    BT_DOWN = 3
};

#define COUNTOF_BT 4

struct Sprite {
    Vec2 Pos;
    int32_t Texture;
};

struct GameState {
    /*OtherRendering*/
    uint32_t Pixels[DIB_HEIGHT][DIB_WIDTH] = {};
    uint8_t TileMap[TILE_HEIGHT][TILE_WIDTH] = {};
    uint32_t TexData[SPR_CAP][TEX_LENGTH][TEX_LENGTH] = {};

    /*Sprite*/
    uint32_t SpriteCount = 0;
    Sprite Sprites[SPR_CAP] = {};

    float ZBuffer[DIB_WIDTH] = {};
    float SpriteSquareDis[SPR_CAP] = {};

    /*Camera*/
    Vec2 Pos = {}; 
    Vec2 Dir = {}; 
    Vec2 Plane = {}; 

    /*Other*/
    uint32_t Buttons[COUNTOF_BT] = {};

    float FrameDelta = 0.0F;
    float TotalTime = 0.0F;

    Worker Workers[4];

    GameState();
    void Update();
};

#endif
